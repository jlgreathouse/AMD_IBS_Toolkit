/*
 * Linux kernel driver for the AMD Research IBS Toolkit
 *
 * Copyright (C) 2015-2017 Advanced Micro Devices, Inc.
 *
 * This driver is available under the Linux kernel's version of the GPLv2.
 * See driver/LICENSE for more licensing details.
 *
 * 
 * This file contains the core of the code for an IBS driver. User programs
 * interface with this driver using device file system nodes at
 * /dev/cpu/<cpuid>/ibs/op and /dev/cpu/<cpuid>/ibs/fetch, where <cpuid>
 * represents an integer ID of a core in the system. For details about the user
 * interface, see the code in this file, ibs-structs.h, and ibs-uapi.h.
 */
#include <asm/nmi.h>
#include <linux/cpu.h>
/* Older versions of Linux need device.h, so keep this */
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
#include <linux/kdebug.h>
#else
#include <asm-x86_64/kdebug.h>
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
#include <linux/irq_work.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)
#include <asm/apic.h>
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0)
#include <linux/cpuhotplug.h>
#endif

#include "ibs-fops.h"
#include "ibs-interrupt.h"
#include "ibs-msr-index.h"
#include "ibs-structs.h"
#include "ibs-uapi.h"
#include "ibs-utils.h"
#include "ibs-workarounds.h"

#define IBS_BUFFER_SIZE	(PAGE_SIZE << 8)
#define IBS_OP_BUFFER_SIZE	IBS_BUFFER_SIZE
#define IBS_FETCH_BUFFER_SIZE	IBS_BUFFER_SIZE

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37) && !defined(pr_warn)
#define pr_warn(fmt, ...) printk(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#endif

#if !defined(APIC_EILVTn) && !defined(APIC_EILVT0)
#define APIC_EILVT0 0x500
#define APIC_EILVT_MSG_NMI 0x4
#endif

#if !defined (CPU_UP_CANCELED_FROZEN)
#define CPU_UP_CANCELED_FROZEN (CPU_UP_CANCELED | CPU_TASKS_FROZEN)
#endif

void *pcpu_op_dev;
void *pcpu_fetch_dev;
static struct class *ibs_class;
static int ibs_major;

static int ibs_fetch_supported = 0;
static int ibs_op_supported = 0;
static int ibs_brn_trgt_supported = 0;
static int ibs_op_cnt_ext_supported = 0;
static int ibs_rip_invalid_chk_supported = 0;
static int ibs_op_brn_fuse_supported = 0;
static int ibs_fetch_ctl_extd_supported = 0;
static int ibs_op_data4_supported = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0)
static int ibs_hotplug_notifier;
#endif

/* Family 10h Erratum #420: Instruction-Based Sampling Engine May Generate
 * Interrupt that Cannot Be Cleared */
static int workaround_fam10h_err_420 = 0;
/* Family 15h Models 00h-1Fh Erratum 718: the processor only sets but never
 * clears MSR C001_1037[3], [6], and [19]. */
static int workaround_fam15h_err_718 = 0;
/* Family 17h Model 07h processors do not necessarily enable IBS by default.
 * They require setting some bits in each core to run IBS.
 * This can be done with a BIOS setting on many boards, but we run the same
 * settings in this driver to increase compatibility */
static int workaround_fam17h_m01h = 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)
static struct notifier_block handle_ibs_nmi_notifier =
{
	.notifier_call 	= handle_ibs_nmi,
	.next		= NULL,
	.priority	= INT_MAX
};
#endif

static const struct file_operations ibs_fops = {
	.open =			ibs_open,
	.owner =		THIS_MODULE,
	.poll =			ibs_poll,
	.read =			ibs_read,
	.release =		ibs_release,
	.unlocked_ioctl =	ibs_ioctl,
};

static void init_ibs_dev(struct ibs_dev *dev, int cpu)
{
	mutex_init(&dev->read_lock);
	init_waitqueue_head(&dev->readq);
	init_waitqueue_head(&dev->pollq);
	dev->cpu = cpu;
	atomic_set(&dev->in_use, 0);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	init_irq_work(&dev->bottom_half, &handle_ibs_work);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,9,0)
	dev->bottom_half.flags = IRQ_WORK_LAZY;
#endif
#endif
	dev->ibs_fetch_supported = ibs_fetch_supported;
	dev->ibs_op_supported = ibs_op_supported;
	dev->ibs_brn_trgt_supported = ibs_brn_trgt_supported;
	dev->ibs_op_cnt_ext_supported = ibs_op_cnt_ext_supported;
	dev->ibs_rip_invalid_chk_supported = ibs_rip_invalid_chk_supported;
	dev->ibs_op_brn_fuse_supported = ibs_op_brn_fuse_supported;
	dev->ibs_fetch_ctl_extd_supported = ibs_fetch_ctl_extd_supported;
	dev->ibs_op_data4_supported = ibs_op_data4_supported;
	dev->workaround_fam10h_err_420 = workaround_fam10h_err_420;
	dev->workaround_fam15h_err_718 = workaround_fam15h_err_718;
	dev->workaround_fam17h_m01h = workaround_fam17h_m01h;
}

static void init_ibs_op_dev(struct ibs_dev *dev, int cpu)
{
	init_ibs_dev(dev, cpu);
	dev->flavor = IBS_OP;
	dev->entry_size = sizeof(struct ibs_op);
	mutex_init(&dev->ctl_lock);
}

static void init_ibs_fetch_dev(struct ibs_dev *dev, int cpu)
{
	init_ibs_dev(dev, cpu);
	dev->flavor = IBS_FETCH;
	dev->entry_size = sizeof(struct ibs_fetch);
	mutex_init(&dev->ctl_lock);
}

static void ibs_setup_lvt(void *nothing)
{
	u64 ibs_control;
	u8 offset;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)
	unsigned long reg;
	unsigned int v;
#endif
	preempt_disable();
	rdmsrl(MSR_IBS_CONTROL, ibs_control);
	if (!(ibs_control & IBS_LVT_OFFSET_VAL))
		goto fail;
	offset = ibs_control & IBS_LVT_OFFSET;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	if(!setup_APIC_eilvt(offset, 0, APIC_EILVT_MSG_NMI, 0))
		goto out;
#else
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,30)
	reg = (offset << 4) + APIC_EILVTn(0);
#else
	reg = (offset << 4) + APIC_EILVT0;
#endif
	v = (APIC_EILVT_MSG_NMI << 8);
	apic_write(reg, v);
	goto out;
#endif
fail:
	pr_warn("IBS APIC setup fail on cpu %d\n", smp_processor_id());
out:
	preempt_enable();
	return;
}

static int ibs_device_create(int flavor, int cpu)
{
	struct device *dev;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
	dev = device_create(ibs_class, NULL,
			MKDEV(ibs_major, IBS_MINOR(flavor, cpu)), NULL,
			"ibs_%s%u", flavor == IBS_OP ? "op" : "fetch", cpu);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
	dev = device_create(ibs_class, NULL,
			MKDEV(ibs_major, IBS_MINOR(flavor, cpu)),
			"ibs_%u_%s",
			cpu, flavor == IBS_OP ? "op" : "fetch");
#else
	dev = device_create(ibs_class, NULL,
			MKDEV(ibs_major, IBS_MINOR(flavor, cpu)), NULL,
			"cpu/%u/ibs/%s",
			cpu, flavor == IBS_OP ? "op" : "fetch");
#endif
	return IS_ERR(dev) ? PTR_ERR(dev) : 0;
}

static void ibs_device_destroy(int flavor, int cpu)
{
	device_destroy(ibs_class, MKDEV(ibs_major, IBS_MINOR(flavor, cpu)));
}

/* When we're about to bring a CPU online, we need to create the fetch and op
   devices for it. We create these when onlining and remove them when offlining
   because this driver may be removed while the CPU is down, and we don't want
   to lose any of the data structures or workaround state while we can't
   talk to the CPU */
static int ibs_prepare_up(unsigned int cpu)
{
	int err = 0;
	pr_info("IBS: Creating IBS devices on core %u\n", cpu);
	err = ibs_device_create(IBS_OP, cpu);
	if (err)
		return err;
	err = ibs_device_create(IBS_FETCH, cpu);
	if (err)
		ibs_device_destroy(IBS_OP, cpu);
	return err;
}

/* Once the CPU is actually online and we can talk to it/run code on it,
   set up its LVT and do any core-specific workarounds that change CPU state */
static int ibs_online_up(unsigned int cpu)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0)
	ibs_prepare_up(cpu);
#endif
	pr_info("IBS: Bringing up IBS on core %u\n", cpu);
	ibs_setup_lvt(NULL);
	if(workaround_fam17h_m01h)
		start_fam17h_m01h_static_workaround(cpu);
	return 0;
}

/* CPU is actually offline at this point, so just destroy its data structs. */
static int ibs_down(unsigned int cpu)
{
	pr_info("IBS: Core %u is down\n", cpu);
	if (ibs_op_supported)
		ibs_device_destroy(IBS_OP, cpu);
	if (ibs_fetch_supported)
		ibs_device_destroy(IBS_FETCH, cpu);
	return 0;
}

/* When we're about to take a CPU offline and we won't be able to run any code
   on it. As such, we must remove any CPU state-changing workarounds that might
   be forgotten if this driver is removed while the core is down. Also remove
   any IBS interrupt-causing bits so that we don't get unhandled interrupts. */
static int ibs_prepare_down(unsigned int cpu)
{
	pr_info("IBS: Starting to take down core %u\n", cpu);
	disable_ibs_op_on_cpu(per_cpu_ptr(pcpu_op_dev, cpu), cpu);
	disable_ibs_fetch_on_cpu(per_cpu_ptr(pcpu_fetch_dev, cpu), cpu);
	if (workaround_fam17h_m01h)
		stop_fam17h_m01h_static_workaround(cpu);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0)
	ibs_down(cpu);
#endif
	return 0;
}

/* After 4.10.0, we directly call the above helper functions from the
   cpuhp_setup_state machine. No need to setup this generic callback */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,10,0)
static int ibs_class_cpu_callback(struct notifier_block *nfb,
				unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	int err = 0;

	switch (action) {
	case CPU_UP_PREPARE:
		err = ibs_prepare_up(cpu);
		break;
	case CPU_ONLINE:
		ibs_online_up(cpu);
		break;
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
	case CPU_DEAD:
		ibs_down(cpu);
		break;
	case CPU_DOWN_PREPARE:
		ibs_prepare_down(cpu);
		break;
	}
	return notifier_from_errno(err);
}

#ifdef __refdata
static struct notifier_block __refdata ibs_class_cpu_notifier = {
#else
static struct notifier_block ibs_class_cpu_notifier = {
#endif
	.notifier_call = ibs_class_cpu_callback,
};
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
static char *ibs_devnode(struct device *dev, 
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0)
umode_t *mode
#else
mode_t *mode
#endif
)
{
	int minor = MINOR(dev->devt);

	return kasprintf(GFP_KERNEL, "cpu/%u/ibs/%s", IBS_CPU(minor),
			IBS_FLAVOR(minor) == IBS_OP ? "op" : "fetch");
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
static int ibs_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	add_uevent_var(env, "DEVMODE=%#o", 0666);
	return 0;
}
#endif

static int check_for_ibs_support(void)
{
	unsigned int feature_id;
	/* Must be on an AMD CPU */
	struct cpuinfo_x86 *c = &boot_cpu_data;
	if (c->x86_vendor != X86_VENDOR_AMD)
	{
		pr_err("Unable to start IBS driver.\n");
		pr_err("This is not an AMD processor.\n");
		return -EINVAL;
	}

	/* IBS support is included in Family 10h, 12h, 14h, 15h, 16h, and 17h
	 * 11h and everything before 10h did not have it. */
	if (c->x86 < 0x10 || c->x86 == 0x11)
	{
		pr_err("Processor too old to support IBS.\n");
		return -EINVAL;
	}

	if (c->x86 == 0x10)
	{
		pr_info("IBS: Startup enabling workaround for "
			"Family 10h Errata 420\n");
		workaround_fam10h_err_420 = 1;
	}

	if (c->x86 == 0x15 && c->x86_model <= 0x1f)
	{
		pr_info("IBS: Startup enabling workaround for "
			"Family 15h Models 00h-1Fh Errata 718\n");
		workaround_fam15h_err_718 = 1;
	}

	feature_id = cpuid_ecx(0x80000001);
	/* Check bit 10 of CPUID_Fn8000_0001_ECX for IBS support */
	if (!(feature_id & (1 << 10)) && !workaround_fam17h_m01h)
	{
		if (c->x86 == 0x17 && c->x86_model == 0x1)
		{
			unsigned int cpu;
			pr_info("IBS: Startup enabling workaround for "
				"Family 17h Model 01h\n");
			workaround_fam17h_m01h = 1;
			for_each_online_cpu(cpu) {
				start_fam17h_m01h_static_workaround(cpu);
			}
		}
		else
		{
			pr_err("CPUID_Fn8000_0001 indicates no IBS support.\n");
			return -EINVAL;
		}
	}

	if (workaround_fam17h_m01h)
	{
		pr_info("IBS: This workaround may slow down your processor.\n");
		pr_info("IBS: Unload IBS driver to maximize performance.\n");
	}

	/* If we are here, time to check the IBS capability flags for
	 * what, if anything, is supported. */
	feature_id = cpuid_eax(0x8000001B);

	/* bit 0 is feature flags valid. If 0, die. */
	if (! (feature_id & 1))
	{
		pr_err("CPUID_Fn8000_001B indicates no IBS support.\n");
		return -EINVAL;
	}
	/* Now check support for Op or Fetch sampling. If neither, die. */
	ibs_fetch_supported = feature_id & (1 << 1);
	/* Op count is more complicated. We want all of its features in this
	 * driver, so or them all together */
	ibs_op_supported = feature_id & (1 << 2);
	ibs_op_supported |= feature_id & (1 << 3);
	ibs_op_supported |= feature_id & (1 << 4);
	if (!ibs_fetch_supported && !ibs_op_supported)
	{
		pr_err("CPUID_Fn800_001B says no Op _or_ Fetch support.\n");
		return -EINVAL;
	}

	/* Now to set all the other feature flags */
	ibs_brn_trgt_supported = feature_id & (1 << 5);
	ibs_op_cnt_ext_supported = feature_id & (1 << 6);
	ibs_rip_invalid_chk_supported = feature_id & (1 << 7);
	ibs_op_brn_fuse_supported = feature_id & (1 << 8);
	ibs_fetch_ctl_extd_supported = feature_id & (1 << 9);
	ibs_op_data4_supported = feature_id & (1 << 10);
	return 0;
}

static void destroy_ibs_class(void)
{
	class_destroy(ibs_class);
}

static void destroy_ibs_devices(void)
{
	unsigned int cpu;
	for_each_online_cpu(cpu) {
		if (ibs_op_supported)
			ibs_device_destroy(IBS_OP, cpu);
		if (ibs_fetch_supported)
			ibs_device_destroy(IBS_FETCH, cpu);
	}
}

static void destroy_ibs_hotplug(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0)
	get_online_cpus();
	cpuhp_remove_state(ibs_hotplug_notifier);
	put_online_cpus();
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,15,0)
	cpu_notifier_register_begin();
	__unregister_hotcpu_notifier(&ibs_class_cpu_notifier);
	cpu_notifier_register_done();
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)
	get_online_cpus();
	unregister_hotcpu_notifier(&ibs_class_cpu_notifier);
	put_online_cpus();
#else
	unregister_hotcpu_notifier(&ibs_class_cpu_notifier);
#endif
}

static void unregister_ibs_chrdev(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
	__unregister_chrdev(ibs_major, 0, NR_CPUS, "cpu/ibs");
#else
	unregister_chrdev(ibs_major, "cpu/ibs");
#endif
}

static void destroy_ibs_cpu_structs(void)
{
	unsigned int cpu;
	for_each_possible_cpu(cpu) {
		free_ibs_buffer(per_cpu_ptr(pcpu_fetch_dev, cpu));
		free_ibs_buffer(per_cpu_ptr(pcpu_op_dev, cpu));
		if (workaround_fam17h_m01h)
			stop_fam17h_m01h_static_workaround(cpu);
	}
	free_percpu(pcpu_fetch_dev);
	free_percpu(pcpu_op_dev);
	free_workaround_structs();
}

static __init int ibs_init(void)
{
	int err = 0;
	unsigned int cpu;

	err = check_for_ibs_support();
	if (err < 0)
		goto out;

	pr_info("IBS: Initializing IBS module\n");

	pcpu_op_dev = alloc_percpu(struct ibs_dev);
	if (!pcpu_op_dev)
		goto err_metadata;
	pcpu_fetch_dev = alloc_percpu(struct ibs_dev);
	if (!pcpu_fetch_dev) {
		free_percpu(pcpu_op_dev);
		goto err_metadata;
	}
	if (init_workaround_structs()) {
		free_percpu(pcpu_op_dev);
		free_percpu(pcpu_fetch_dev);
err_metadata:
		pr_err("Failed to allocate IBS device metadata; exiting\n");
		err = -ENOMEM;
		goto out;
	}

	for_each_possible_cpu(cpu) {
		init_ibs_op_dev(per_cpu_ptr(pcpu_op_dev, cpu), cpu);
		err = setup_ibs_buffer(per_cpu_ptr(pcpu_op_dev, cpu),
					IBS_OP_BUFFER_SIZE);
		if (err)
			goto err_buffers;
		init_ibs_fetch_dev(per_cpu_ptr(pcpu_fetch_dev, cpu), cpu);
		err = setup_ibs_buffer(per_cpu_ptr(pcpu_fetch_dev, cpu),
					IBS_FETCH_BUFFER_SIZE);
		if (err) {
err_buffers:
			pr_err("CPU %d failed to allocate IBS device buffer; "
				    "exiting\n", cpu);
			goto out_buffers;
		}
		init_workaround_initialize();
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
	ibs_major = __register_chrdev(0, 0, NR_CPUS, "cpu/ibs", &ibs_fops);
#else
	ibs_major = register_chrdev(0, "cpu/ibs", &ibs_fops);
#endif
	if (ibs_major < 0) {
		pr_err("Failed to get IBS device number; exiting\n");
		goto out_buffers;
	}

	ibs_class = class_create(THIS_MODULE, "ibs");
	if (IS_ERR(ibs_class)) {
		err = PTR_ERR(ibs_class);
		pr_err("Failed to create IBS class; exiting\n");
		goto out_chrdev;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
	ibs_class->devnode = ibs_devnode;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
	ibs_class->dev_uevent = ibs_uevent;
#endif

	/* Kernel versions older than 4.10 require some in-kernel locking
	   around registering the hotplug notifier, or they could eadlock.
	   See: https://patchwork.kernel.org/patch/3805711/ */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0)
	/* 4.10 says just to use get_online_cpus()s, see the Dec. 2016 version
	  https://www.kernel.org/doc/html/v4.11/core-api/cpu_hotplug.html */
	get_online_cpus();
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,15,0)
	/* 3.15 introduced cpu_notifier_register_begin() for this. */
	cpu_notifier_register_begin();
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)
	get_online_cpus();
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	on_each_cpu(ibs_setup_lvt, NULL, 1);
#else
	on_each_cpu(ibs_setup_lvt, NULL, 1, 1);
#endif
	for_each_online_cpu(cpu) {
		if (ibs_op_supported)
			err = ibs_device_create(IBS_OP, cpu);
		if (err != 0)
			goto out_class;
		if (ibs_fetch_supported)
			err = ibs_device_create(IBS_FETCH, cpu);
		if (err != 0)
			goto out_class;
	}

	/* After setting up the current CPUs' data structures, we register the
	   notifier for them. This changes depending on the kernel version */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0)
	/* We currently set up everything on CPUHP_AP_ONLINE_DYN because it's
	   likely that we are not the first device on this chain. Current
	   kernels have a bug where, if you are the first device on a DYN
	   notifier chain, your removal from that chain will fail. See:
	   https://lkml.org/lkml/2017/7/5/574 */
	ibs_hotplug_notifier = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
			"cpu/ibs:online", ibs_online_up, ibs_prepare_down);
	put_online_cpus();
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,15,0)
	__register_hotcpu_notifier(&ibs_class_cpu_notifier);
	cpu_notifier_register_done();
#else
	register_hotcpu_notifier(&ibs_class_cpu_notifier);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)
	put_online_cpus();
#endif
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
	err = register_nmi_handler(NMI_LOCAL, handle_ibs_nmi,
				NMI_FLAG_FIRST, "ibs_op");
#else
	err = register_die_notifier(&handle_ibs_nmi_notifier);
#endif
	if (err) {
		pr_err("Failed to register NMI handler; exiting\n");
		goto out_device;
	}

	goto out;

out_device:
	destroy_ibs_devices();
out_class:
	destroy_ibs_hotplug();
out_chrdev:
	unregister_ibs_chrdev();
out_buffers:
	destroy_ibs_cpu_structs();
out:
	return err;
}

static __exit void ibs_exit(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
	unregister_nmi_handler(NMI_LOCAL, "ibs_op");
#else
	unregister_die_notifier(&handle_ibs_nmi_notifier);
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,10,0)
	/* We only need to do this on older kernels -- the CPU hotplug destroyer
	   will call into the per-device destroy functions on newer kernels */
	destroy_ibs_devices();
#endif
	destroy_ibs_hotplug();
	unregister_ibs_chrdev();
	destroy_ibs_cpu_structs();
	destroy_ibs_class();

	pr_info("IBS: exited IBS module\n");
}

module_init(ibs_init);
module_exit(ibs_exit);

MODULE_LICENSE("GPL v2");
