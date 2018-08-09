/*
 * Linux kernel driver for the AMD Research IBS Toolkit
 *
 * Copyright (C) 2015-2018 Advanced Micro Devices, Inc.
 *
 * This driver is available under the Linux kernel's version of the GPLv2.
 * See driver/LICENSE for more licensing details.
 *
 * This file contains the logic for user-level control of the driver through
 * ioctl() commands sent to the device.
 * The ioctl() options are described in the comments of ibs-uapi.h
 */
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <linux/wait.h>
#include <linux/delay.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)
#include <linux/atomic.h>
#else
#include <asm/atomic.h>
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,13,0)
#include <uapi/asm-generic/ioctls.h>
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30)
#include <asm-generic/ioctls.h>
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)
#include <asm/ioctls.h>
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
#include <asm-x86/ioctls.h>
#else
#include <asm-x86_64/ioctls.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
#undef atomic_long_xchg
#define atomic_long_xchg(v, new) (atomic_xchg((atomic64_t *)(v), (new)))
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37) && !defined(pr_warn)
#define pr_warn(fmt, ...) printk(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#endif

#include "ibs-fops.h"
#include "ibs-msr-index.h"
#include "ibs-structs.h"
#include "ibs-uapi.h"
#include "ibs-utils.h"
#include "ibs-workarounds.h"

/* Declarations of all the devices on a per-cpu basis.
 * Real declaration is in ibs-core.c */
extern void *pcpu_op_dev;
extern void *pcpu_fetch_dev;

static inline void enable_ibs_op_on_cpu(struct ibs_dev *dev,
		const int cpu, const u64 op_ctl)
{
	if (dev->workaround_fam17h_zn)
		start_fam17h_zn_dyn_workaround(cpu);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,13,0)
	wrmsrl_on_cpu(cpu, MSR_IBS_OP_CTL, op_ctl);
#else
	wrmsr_on_cpu(cpu, MSR_IBS_OP_CTL, (u32)((u64)(op_ctl)),
			(u32)((u64)(op_ctl) >> 32));
#endif
}

/* Disabling IBS op sampling takes a little bit of extra work.
 * It is possible that an IBS op has been sampled, meaning that
 * IbsOpVal is set. However, the NMI has not yet arrived from
 * the APIC. As such, we will (in the very near future) enter
 * the NMI handler.
 * If we just fully zero out the IBS_OP_CTL register, then that
 * NMI handler will see that IbsOpVal is zero and think that this
 * NMI was not caused by an op sample. It would pass the interrupt
 * on down the NMI chain, which could eventually lead to a system
 * reboot (if, for instance, there is a watchdog timer enabled).
 * We can't just read IbsOpVal, zero out IbsOpEn, and then save IbsOpVal
 * back into the register; the NMI could arrive between the zeroing of
 * the register and the resetting of IbsOpVal. There is no way to do an
 * atomic read-modify-write to an MSR.
 * Our solution is thus to force IbsOpVal to true, but zero out all the
 * other bits. We wait for a microsecond (giving the APIC time to poke
 * this core), then fully disable IBS_OP_CTL. This prevents the dangling
 * IbsOpVal from inadvertently eating any real NMIs targetted at this
 * core (except during this microsecond-long window, where we are spin
 * looping and thus hopefully not producing any real work that would
 * cause an NMI). */
static void disable_ibs_op(void *info)
{
	wrmsrl(MSR_IBS_OP_CTL, IBS_OP_VAL);
	udelay(1);
	wrmsrl(MSR_IBS_OP_CTL, 0ULL);
}

void disable_ibs_op_on_cpu(struct ibs_dev *dev, const int cpu)
{
	if (dev->workaround_fam10h_err_420)
		do_fam10h_workaround_420(cpu);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	smp_call_function_single(cpu, disable_ibs_op, NULL, 1);
#else
	smp_call_function_single(cpu, disable_ibs_op, NULL, 1, 1);
#endif
	if (dev->workaround_fam17h_zn)
		stop_fam17h_zn_dyn_workaround(cpu);
}

static inline void enable_ibs_fetch_on_cpu(struct ibs_dev *dev,
		const int cpu, const u64 fetch_ctl)
{
	if (dev->workaround_fam17h_zn)
		start_fam17h_zn_dyn_workaround(cpu);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,13,0)
	wrmsrl_on_cpu(cpu, MSR_IBS_FETCH_CTL, fetch_ctl);
#else
	wrmsr_on_cpu(cpu, MSR_IBS_FETCH_CTL, (u32)((u64)(fetch_ctl)),
			(u32)((u64)(fetch_ctl) >> 32));
#endif
}

void disable_ibs_fetch_on_cpu(struct ibs_dev *dev, const int cpu)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,13,0)
	wrmsrl_on_cpu(cpu, MSR_IBS_FETCH_CTL, 0ULL);
#else
	wrmsr_on_cpu(cpu, MSR_IBS_FETCH_CTL, 0UL, 0UL);
#endif
	if (dev->workaround_fam17h_zn)
		stop_fam17h_zn_dyn_workaround(cpu);
}

static void set_ibs_defaults(struct ibs_dev *dev)
{
	atomic_long_set(&dev->poll_threshold, 1);
	if (dev->flavor == IBS_OP)
	{
		if (dev->ibs_op_cnt_ext_supported)
		{
			dev->ctl = (scatter_bits(0, IBS_OP_CUR_CNT_23) |
					scatter_bits(0x4000, IBS_OP_MAX_CNT) |
					IBS_OP_CNT_CTL);
		}
		else
		{
			dev->ctl = (scatter_bits(0, IBS_OP_CUR_CNT_OLD) |
					scatter_bits(0x4000, IBS_OP_MAX_CNT_OLD) |
					IBS_OP_CNT_CTL);
		}
	}
	else	/* dev->flavor == IBS_FETCH */
		dev->ctl = (IBS_RAND_EN |
				scatter_bits(0, IBS_FETCH_CNT) |
				scatter_bits(0x1000, IBS_FETCH_MAX_CNT));
}

int ibs_open(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	struct ibs_dev *dev;

	if (IBS_FLAVOR(minor) == IBS_OP)
		dev = per_cpu_ptr(pcpu_op_dev, IBS_CPU(minor));
	else	/* IBS_FLAVOR(minor) == IBS_FETCH */
		dev = per_cpu_ptr(pcpu_fetch_dev, IBS_CPU(minor));

	if (atomic_cmpxchg(&dev->in_use, 0, 1) != 0)
		return -EBUSY;

	file->private_data = dev;

	mutex_lock(&dev->ctl_lock);
	set_ibs_defaults(dev);
	reset_ibs_buffer(dev);
	mutex_unlock(&dev->ctl_lock);

	return 0;
}

int ibs_release(struct inode *inode, struct file *file)
{
	struct ibs_dev *dev = file->private_data;

	mutex_lock(&dev->ctl_lock);

	if (dev->flavor == IBS_OP)
		disable_ibs_op_on_cpu(dev, dev->cpu);
	else /* dev->flavor == IBS_FETCH */
		disable_ibs_fetch_on_cpu(dev, dev->cpu);

	set_ibs_defaults(dev);
	reset_ibs_buffer(dev);

	atomic_set(&dev->in_use, 0);

	mutex_unlock(&dev->ctl_lock);

	return 0;
}

static ssize_t do_ibs_read(struct ibs_dev *dev, char __user *buf, size_t count)
{
	long rd = atomic_long_read(&dev->rd);
	long wr = atomic_long_read(&dev->wr);
	long entries = atomic_long_read(&dev->entries);
	void *rd_ptr = dev->buf + rd * dev->entry_size;
	long entries_read = 0;

	/* Read this much: */
	count = min(count, (size_t)(entries * dev->entry_size));
	if (count == 0)
		return 0;

	if (rd < wr) {	/* Buffer has not wrapped */
		if (copy_to_user(buf, rd_ptr, count))
			return -EFAULT;
	} else {	/* Buffer has wrapped */

		/* First, read up to end of buffer */
		size_t bytes_to_end = (dev->capacity - rd) * dev->entry_size;
		size_t bytes_to_read = min(count, bytes_to_end);
		if (copy_to_user(buf, rd_ptr, bytes_to_read))
			return -EFAULT;

		/* If necessary, complete the read at buffer start */
		if (count > bytes_to_end) {
			buf += bytes_to_end;
			rd_ptr = dev->buf;
			bytes_to_read = min(count - bytes_to_end,
					(size_t)(wr * dev->entry_size));
			if (copy_to_user(buf, rd_ptr, bytes_to_read))
				return -EFAULT;
		}
	}
	entries_read = count / dev->entry_size;
	rd = (rd + entries_read) % dev->capacity;
	atomic_long_set(&dev->rd, rd);
	atomic_long_sub(entries_read, &dev->entries);
	return count;
}

ssize_t ibs_read(struct file *file, char __user *buf, size_t count,
			loff_t *fpos)
{
	struct ibs_dev *dev = file->private_data;
	ssize_t retval;

	if (count < dev->entry_size || count > dev->size)
		return -EINVAL;
	/* Make count a multiple of the entry size */
	count -= count % dev->entry_size;

	/*
	 * Assuming we are the sole reader, we will rarely spin on this lock.
	 */
	mutex_lock(&dev->read_lock);
	while (!atomic_long_read(&dev->entries)) {	/* No data */
		mutex_unlock(&dev->read_lock);

		/* If IBS is disabled, return nothing */
		mutex_lock(&dev->ctl_lock);
		if ((dev->flavor == IBS_OP && !(dev->ctl & IBS_OP_EN)) ||
		(dev->flavor == IBS_FETCH && !(dev->ctl & IBS_FETCH_EN))) {
			mutex_unlock(&dev->ctl_lock);
			return 0;
		}
		mutex_unlock(&dev->ctl_lock);
	
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(dev->readq,
					(atomic_long_read(&dev->rd) !=
					atomic_long_read(&dev->wr))))
			return -ERESTARTSYS;
		mutex_lock(&dev->read_lock);
	}
	retval = do_ibs_read(dev, buf, count);
	mutex_unlock(&dev->read_lock);
	return retval;
}

unsigned int ibs_poll(struct file *file, poll_table *wait)
{
	struct ibs_dev *dev = file->private_data;

	/* Update the poll table in case nobody has data */
	poll_wait(file, &dev->pollq, wait);

	mutex_lock(&dev->read_lock);
	if (atomic_long_read(&dev->entries) >=
		atomic_long_read(&dev->poll_threshold)) {
		mutex_unlock(&dev->read_lock);
		return POLLIN | POLLRDNORM;	/* There is enough data */
	}
	mutex_unlock(&dev->read_lock);

	/* Check whether IBS is disabled */
	mutex_lock(&dev->ctl_lock);
	if ((dev->flavor == IBS_OP && !(dev->ctl & IBS_OP_EN)) ||
	(dev->flavor == IBS_FETCH && !(dev->ctl & IBS_FETCH_EN))) {
		mutex_unlock(&dev->ctl_lock);
		return POLLHUP;
	}
	mutex_unlock(&dev->ctl_lock);
	return 0;
}

long ibs_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long retval = 0;
	struct ibs_dev *dev = file->private_data;
	int cpu = dev->cpu;

	/* Lock-free commands */
	switch (cmd) {
	case DEBUG_BUFFER:
		pr_info("cpu %d buffer: { wr = %lu; rd = %lu; entries = %lu; "
			"lost = %lu; capacity = %llu; entry_size = %llu; "
			"size = %llu; }\n",
			cpu,
			atomic_long_read(&dev->wr),
			atomic_long_read(&dev->rd),
			atomic_long_read(&dev->entries),
			atomic_long_read(&dev->lost),
			dev->capacity,
			dev->entry_size,
			dev->size);
		return 0;
	case GET_LOST:
		return atomic_long_xchg(&dev->lost, 0);
	case FIONREAD:
		return atomic_long_read(&dev->entries);
	}

	/* Commands that require the ctl_lock */
	mutex_lock(&dev->ctl_lock);
	/* For SET* commands, ensure IBS is disabled */
	if (cmd == SET_CUR_CNT || cmd == SET_CNT ||
		cmd == SET_MAX_CNT ||
		cmd == SET_CNT_CTL ||
		cmd == SET_RAND_EN ||
		cmd == SET_POLL_SIZE ||
		cmd == SET_BUFFER_SIZE ||
		cmd == RESET_BUFFER) {
			if ((dev->flavor == IBS_OP && dev->ctl & IBS_OP_EN) ||
			(dev->flavor == IBS_FETCH && dev->ctl & IBS_FETCH_EN)) {
				mutex_unlock(&dev->ctl_lock);
				return -EBUSY;
		}
	}
	switch (cmd) {
	case IBS_ENABLE:
		if (dev->flavor == IBS_OP) {
			dev->ctl |= IBS_OP_EN;
			enable_ibs_op_on_cpu(dev, cpu, dev->ctl);
		} else {	/* dev->flavor == IBS_FETCH */
			dev->ctl |= IBS_FETCH_EN;
			enable_ibs_fetch_on_cpu(dev, cpu, dev->ctl);
		}
		break;
	case IBS_DISABLE:
		if (dev->flavor == IBS_OP) {
			disable_ibs_op_on_cpu(dev, cpu);
			dev->ctl &= ~IBS_OP_EN;
		} else {	/* dev->flavor == IBS_FETCH */
			disable_ibs_fetch_on_cpu(dev, cpu);
			dev->ctl &= ~IBS_FETCH_EN;
		}
		break;
	case SET_CUR_CNT:
	case SET_CNT:
		if (dev->flavor == IBS_OP) {
			if (dev->ibs_op_cnt_ext_supported)
			{
				dev->ctl &= ~IBS_OP_CUR_CNT_23;
				dev->ctl |= scatter_bits(arg, IBS_OP_CUR_CNT_23);
			}
			else
			{
				dev->ctl &= ~IBS_OP_CUR_CNT_OLD;
				dev->ctl |= scatter_bits(arg, IBS_OP_CUR_CNT_OLD);
			}
		} else {	/* dev->flavor == IBS_FETCH */
			dev->ctl &= ~IBS_FETCH_CNT;
			dev->ctl |= scatter_bits(arg, IBS_FETCH_CNT);
		}
		break;
	case GET_CUR_CNT:
	case GET_CNT:
		if (dev->flavor == IBS_OP)
			if (dev->ibs_op_cnt_ext_supported)
				retval = gather_bits(dev->ctl, IBS_OP_CUR_CNT_23);
			else
				retval = gather_bits(dev->ctl, IBS_OP_CUR_CNT_OLD);
		else	/* dev->flavor == IBS_FETCH */
			retval = gather_bits(dev->ctl, IBS_FETCH_CNT);
		break;
	case SET_MAX_CNT:
		if (dev->flavor == IBS_OP) {
			if (dev->ibs_op_cnt_ext_supported)
			{
				dev->ctl &= ~IBS_OP_MAX_CNT;
				dev->ctl |= scatter_bits(arg, IBS_OP_MAX_CNT);
			}
			else
			{
				dev->ctl &= ~IBS_OP_MAX_CNT_OLD;
				dev->ctl |= scatter_bits(arg, IBS_OP_MAX_CNT_OLD);
			}
		} else {	/* dev->flavor == IBS_FETCH */
			dev->ctl &= ~IBS_FETCH_MAX_CNT;
			dev->ctl |= scatter_bits(arg, IBS_FETCH_MAX_CNT);
		}
		break;
	case GET_MAX_CNT:
		if (dev->flavor == IBS_OP)
			if (dev->ibs_op_cnt_ext_supported)
				retval = gather_bits(dev->ctl, IBS_OP_MAX_CNT);
			else
				retval = gather_bits(dev->ctl, IBS_OP_MAX_CNT_OLD);
		else	/* dev->flavor == IBS_FETCH */
			retval = gather_bits(dev->ctl, IBS_FETCH_MAX_CNT);
		break;
	case SET_CNT_CTL:
		if (dev->flavor != IBS_OP)
			retval = -EINVAL;
		else if (arg == 1)
			dev->ctl |= IBS_OP_CNT_CTL;
		else if (arg == 0)
			dev->ctl &= ~IBS_OP_CNT_CTL;
		else
			retval = -EINVAL;
		break;
	case GET_CNT_CTL:
		if (dev->flavor != IBS_OP)
			retval = -EINVAL;
		else
			retval = (dev->ctl & IBS_OP_CNT_CTL) ? 1 : 0;
		break;
	case SET_RAND_EN:
		if (dev->flavor != IBS_FETCH)
			retval = -EINVAL;
		else if (arg == 1)
			dev->ctl |= IBS_RAND_EN;
		else if (arg == 0)
			dev->ctl &= ~IBS_RAND_EN;
		else
			retval = -EINVAL;
		break;
	case GET_RAND_EN:
		if (dev->flavor != IBS_FETCH)
			retval = -EINVAL;
		else
			retval = (dev->ctl & IBS_RAND_EN) ? 1 : 0;
		break;
	case SET_POLL_SIZE:
		if (0 < arg && arg < dev->capacity)
			atomic_long_set(&dev->poll_threshold, arg);
		else
			retval = -EINVAL;
		break;
	case GET_POLL_SIZE:
		retval = atomic_long_read(&dev->poll_threshold);
		break;
	case SET_BUFFER_SIZE:
		/* Ensure requested buffer can hold at least one entry */
		if (arg < dev->entry_size) {
			retval = -EINVAL;
			break;
		}
		/* Do not re-allocate if there is no change */
		if (arg == dev->size) {
			reset_ibs_buffer(dev);
			break;
		}

		free_ibs_buffer(dev);
		retval = setup_ibs_buffer(dev, arg);
		if (retval)
			pr_warn("Failed to set IBS %s cpu %d buffer size to %ld; "
				"leaving buffer unchanged\n",
				dev->flavor == IBS_OP ? "op" : "fetch",
				dev->cpu, arg);
		break;
	case GET_BUFFER_SIZE:
		retval = dev->size;
		break;
	case RESET_BUFFER:
		reset_ibs_buffer(dev);
		break;
	default:	/* Command not recognized */
		retval = -ENOTTY;
		break;
	}
	mutex_unlock(&dev->ctl_lock);
	return retval;
}
