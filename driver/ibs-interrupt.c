/*
 * Linux kernel driver for the AMD Research IBS Toolkit
 *
 * Copyright (C) 2015-2017 Advanced Micro Devices, Inc.
 *
 * This driver is available under the Linux kernel's version of the GPLv2.
 * See driver/LICENSE for more licensing details.
 *
 * This is the NMI/interrupt handling code that is called whenever IBS samples
 * are taken in the hardware.
 */
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
#include <linux/kdebug.h>
#else
#include <asm-x86_64/kdebug.h>
#endif
#include <linux/sched.h>

#include "ibs-msr-index.h"
#include "ibs-interrupt.h"
#include "ibs-structs.h"

extern void *pcpu_op_dev;
extern void *pcpu_fetch_dev;

static inline void wake_up_queues(struct ibs_dev *dev)
{
	wake_up(&dev->readq);
	if (atomic_long_read(&dev->entries) >=
		atomic_long_read(&dev->poll_threshold))
	{
		wake_up(&dev->pollq);
	}
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
void handle_ibs_work(struct irq_work *w)
{
	struct ibs_dev *dev = container_of(w, struct ibs_dev, bottom_half);
	wake_up_queues(dev);
}
#endif

/**
 * lfsr_random - 16-bit Linear Feedback Shift Register (LFSR)
 *
 * LFSR from Paul Drongowski
 */
static inline unsigned int lfsr_random(void)
{
	static unsigned int lfsr_value = 0xF00D;
	unsigned int bit;

	/* Compute next bit to shift in */
	bit = ((lfsr_value >> 0) ^
		(lfsr_value >> 2) ^
		(lfsr_value >> 3) ^
		(lfsr_value >> 5)) & 0x0001;

	/* Advance to next register value */
	lfsr_value = (lfsr_value >> 1) | (bit << 15);

	return lfsr_value;
}

/*
 * randomize_op_ctl - perform the pre-reenable randomization of op_ctl bits
 */
static inline u64 randomize_op_ctl(u64 op_ctl)
{
	u64 random_bits = lfsr_random() & 0xf;	/* Get 4 random bits */
	return ((random_bits << 32) | (op_ctl & ~(0xfULL << 32)));
}

static inline void enable_ibs_op(const u64 op_ctl)
{
	/* No need for Fam 17h workaround here:
	 * This function is only called when IBS is "enabled" in the driver
	 * but is paused during the NMI handling of the IBS interrupt. */
	wrmsrl(MSR_IBS_OP_CTL, op_ctl);
}

static inline void enable_ibs_fetch(const u64 fetch_ctl)
{
	/* No need for the major Fam. 17h workaround here:
	 * This function is only called when IBS is "enabled" in the driver
	 * but is paused during the NMI handling of the IBS interrupt. */

	/* The definition of IbsFetchVal in Families 15h and 17h is somewhat
	 * opaque. It is described as read-only but it must be reset in order
	 * to allow the fetch counter to start counting. On 17h, especially,
	 * this means that we must actually zero out the whole register before
	 * we can turn on fetch sampling. We do this on all cores to simplify
	 * the control logic. */
	u64 zero = 0LL;
	wrmsrl(MSR_IBS_FETCH_CTL, zero);
	wrmsrl(MSR_IBS_FETCH_CTL, fetch_ctl);
}

/**
 * collect_op_data - fill fields of ibs_op specific to op flavor
 */
static inline void collect_op_data(struct ibs_dev *dev, struct ibs_op *sample)
{
	rdmsrl(MSR_IBS_OP_CTL, sample->op_ctl);
	rdmsrl(MSR_IBS_OP_RIP, sample->op_rip);
	rdmsrl(MSR_IBS_OP_DATA, sample->op_data);
	rdmsrl(MSR_IBS_OP_DATA2, sample->op_data2);
	rdmsrl(MSR_IBS_OP_DATA3, sample->op_data3);
	if (dev->ibs_op_data4_supported)
		rdmsrl(MSR_IBS_OP_DATA4, sample->op_data4);
	rdmsrl(MSR_IBS_DC_LIN_AD, sample->dc_lin_ad);
	rdmsrl(MSR_IBS_DC_PHYS_AD, sample->dc_phys_ad);
	if (dev->ibs_brn_trgt_supported)
		rdmsrl(MSR_IBS_BR_TARGET, sample->br_target);
}

/**
 * collect_fetch_data - fill fields of ibs_fetch specific to fetch flavor
 */
static inline void collect_fetch_data(struct ibs_dev *dev, struct ibs_fetch *sample)
{
	rdmsrl(MSR_IBS_FETCH_CTL, sample->fetch_ctl);
	if (dev->ibs_fetch_ctl_extd_supported)
		rdmsrl(MSR_IBS_EXTD_CTL, sample->fetch_ctl_extd);
	rdmsrl(MSR_IBS_FETCH_LIN_AD, sample->fetch_lin_ad);
	rdmsrl(MSR_IBS_FETCH_PHYS_AD, sample->fetch_phys_ad);
}

/**
 * collect_common_data - fill fields common to both fetch and op flavors
 * @sample:	ptr to either struct ibs_op or struct ibs_fetch
 */
#define collect_common_data(sample) \
	do { \
		rdtscll(sample->tsc); \
		asm ("movq %%cr3, %%rax\n\t" \
		     "movq %%rax, %0" \
		     : "=m"(sample->cr3) \
		     : /* no input */ \
		     : "%rax" \
		); \
		sample->tid = current->pid; \
		sample->pid = current->tgid; \
		sample->cpu = smp_processor_id(); \
		sample->kern_mode = !user_mode(regs); \
	} while (0)

static inline void handle_ibs_op_event(struct pt_regs *regs)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33)
	struct ibs_dev *dev = this_cpu_ptr(pcpu_op_dev);
#else
	struct ibs_dev *dev = per_cpu_ptr(pcpu_op_dev, smp_processor_id());
#endif
	unsigned int old_wr = atomic_long_read(&dev->wr);
	unsigned int new_wr = (old_wr + 1) % dev->capacity;
	struct ibs_op *sample;

	if (new_wr == atomic_long_read(&dev->rd)) {	/* Full buffer */
		atomic_long_inc(&dev->lost);
		goto out;
	}
	sample = (struct ibs_op *)(dev->buf + (old_wr * dev->entry_size));

	collect_op_data(dev, sample);
	collect_common_data(sample);

	atomic_long_set(&dev->wr, new_wr);
	atomic_long_inc(&dev->entries);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	irq_work_queue(&dev->bottom_half);
#else
	/* Add more work directly into the NMI handler, but in older kernels, we
	 * didn't have access to IRQ work queues. */
	wake_up_queues(dev);
#endif

out:
	dev->ctl = randomize_op_ctl(dev->ctl);
	if (dev->workaround_fam15h_err_718)
		wrmsrl(MSR_IBS_OP_DATA3, 0ULL);
	enable_ibs_op(dev->ctl);
}

static inline void handle_ibs_fetch_event(struct pt_regs *regs)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33)
	struct ibs_dev *dev = this_cpu_ptr(pcpu_fetch_dev);
#else
	struct ibs_dev *dev = per_cpu_ptr(pcpu_fetch_dev, smp_processor_id());
#endif
	unsigned int old_wr = atomic_long_read(&dev->wr);
	unsigned int new_wr = (old_wr + 1) % dev->capacity;
	struct ibs_fetch *sample;

	if (new_wr == atomic_long_read(&dev->rd)) {	/* Full buffer */
		atomic_long_inc(&dev->lost);
		goto out;
	}
	sample = (struct ibs_fetch *)(dev->buf + (old_wr * dev->entry_size));

	collect_fetch_data(dev, sample);
	collect_common_data(sample);

	atomic_long_set(&dev->wr, new_wr);
	atomic_long_inc(&dev->entries);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	irq_work_queue(&dev->bottom_half);
#else
	/* Add more work directly into the NMI handler, but in older kernels, we
	 * didn't have access to IRQ work queues. */
	wake_up_queues(dev);
#endif

out:
	enable_ibs_fetch(dev->ctl);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
static inline int handle_ibs_event(struct pt_regs *regs)
{
	u64 tmp;
	int retval = NMI_DONE;

	/* Check for op sample */
	rdmsrl(MSR_IBS_OP_CTL, tmp);
	if (tmp & IBS_OP_VAL) {
		handle_ibs_op_event(regs);
		retval += NMI_HANDLED;
	}

	/* Check for fetch sample */
	rdmsrl(MSR_IBS_FETCH_CTL, tmp);
	if (tmp & IBS_FETCH_VAL) {
		handle_ibs_fetch_event(regs);
		retval += NMI_HANDLED;
	}

	/* Return immediately if both checks fail */
	return retval;
}

int handle_ibs_nmi(unsigned int cmd, struct pt_regs *regs)
{
	return handle_ibs_event(regs);
}
#else /* Kernel version is below 3.2.0 */

#define NMI_DONE	NOTIFY_OK
#define NMI_HANDLED	NOTIFY_STOP

static inline int handle_ibs_event(struct pt_regs *regs)
{
	u64 tmp;
	int retval = 0;

	/* Check for op sample */
	rdmsrl(MSR_IBS_OP_CTL, tmp);
	if (tmp & IBS_OP_VAL) {
		handle_ibs_op_event(regs);
		retval++;
	}

	/* Check for fetch sample only if no op samples were avilable.
	 * We choose to have only one NMI succeed on these older kernels,
	 * becuse otherwise the queued up NMI work spits out angry messages
	 * to dmesg about unhandled NMIs. */
	rdmsrl(MSR_IBS_FETCH_CTL, tmp);
	if (retval == 0 && (tmp & IBS_FETCH_VAL)) {
		handle_ibs_fetch_event(regs);
		retval++;
	}

	/* If either check succeeds, let's assume we were the source of the NMI  */
	return retval;
}

int handle_ibs_nmi(struct notifier_block *self, unsigned long cmd,
				void *data)
{
	struct die_args *args = (struct die_args *)data;

	if (cmd != DIE_NMI)
		return NOTIFY_OK;

	/* Ignore memory / I/O errors */
	if (args->err & 0xc0)
		return NOTIFY_OK;

	if (handle_ibs_event(args->regs) > 0)
		return NOTIFY_STOP;
	else
		return NOTIFY_OK;
}
#endif
