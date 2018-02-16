/*
 * Linux kernel driver for the AMD Research IBS Toolkit
 *
 * Copyright (C) 2015-2017 Advanced Micro Devices, Inc.
 *
 * This driver is available under the Linux kernel's version of the GPLv2.
 * See driver/LICENSE for more licensing details.
 * 
 * This file contains a series of workarounds for various generations of AMD
 * CPUs. This prevents the main driver code from being polluted by details of
 * the main IBS driver.
 */
/* This needs to come first because some old kernels need the u32/u64 defs.
 * However, places like msr.h also use them. So put this at the top. */
#include <linux/types.h>

#include <asm/msr.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/percpu.h>
#include <linux/version.h>

#include "ibs-structs.h"
#include "ibs-uapi.h"
#include "ibs-msr-index.h"

#ifndef topology_sibling_cpumask
#define topology_sibling_cpumask(cpu) (per_cpu(cpu_sibling_map, cpu))
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
struct msr_info {
	u32 msr_no;
	union {
		struct {
			u32 l;
			u32 h;
		};
		u64 q;
	} reg;
	int err;
};
#endif

#define FAM17H_MSR_WA_1 0xc0011020
#define FAM17H_MSR_WA_1_BITS 0x40000000000000ULL
#define FAM17H_MSR_WA_2 0xc0011029
#define FAM17H_MSR_WA_2_BITS 0x80000ULL
#define FAM17H_MSR_WA_3 0xc0010296
#define FAM17H_MSR_WA_3_BITS 0x404040ULL
#define CPUID_EXT_FEATURES 0xc0011005
/* Storage for old MSR values that are changed when enabling IBS
 * on Family 17h Model 01h cores. We assume this IBS driver is the
 * only thing to change them, and that they are the same per core.
 * As such, we only have one value for the whole system so that
 * we can know what to set them back to. */
static u64 fam17h_old_1 = 0;
static u64 fam17h_old_2 = 0;
static u64 fam17h_old_3 = 0;
/* Need to keep track of whether Op, Fetch, or both are on
 * so that when we are doing the MSR workaround, we only turn it off when
 * both devices are disabled */
static int* pcpu_num_devices_enabled;
static spinlock_t * pcpu_workaround_lock;
static int workarounds_started = 0;

/* The following function exists because there are some WRMSR commands that
 * need to do a heavyweight pipeline flush to prevent any hardware problems.
 * We use a WBINVD to flush the pipeline and the caches. */
static void custom_wrmsr_flush(void *info)
{
	struct msr_info *rv = info;
	asm volatile("wbinvd");
	rv->err = wrmsr_safe((rv->msr_no), (rv->reg.l), (rv->reg.h));
}

/* Different kernels had different ways of performing a 64-bit rdmsr and
 * wrmsr commands on target CPUs.
 * To prevent us from having ifdefs all over the place, these functions
 * will do the same thing on various kernels. */
static inline u64 custom_rdmsrl_on_cpu(unsigned int cpu, u32 msr_no)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,13,0)
	u64 ret_val;
	rdmsrl_on_cpu(cpu, msr_no, &ret_val);
	return ret_val;
#else
	u32 lo, hi;
	rdmsr_on_cpu(cpu, msr_no, &lo, &hi);
	return (u64)lo | ((u64)hi << 32ULL);
#endif
}

static inline void custom_wrmsrl_on_cpu(unsigned int cpu, u32 msr_no, u64 val)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,13,0)
	wrmsrl_on_cpu(cpu, msr_no, val);
#else
	u32 lo, hi;
	lo = val & 0xffffffff;
	hi = val >> 32;
	wrmsr_on_cpu(cpu, msr_no, lo, hi);
#endif
}

/* When performing the workarounds for Family 17h Model 01h, we want to
 * store off the default values of a series of registers so we can
 * restore the bits we will change after we are done. */
static void init_fam17h_m01h_workaround(void)
{
	rdmsrl(FAM17H_MSR_WA_1, fam17h_old_1);
	rdmsrl(FAM17H_MSR_WA_2, fam17h_old_2);
	rdmsrl(FAM17H_MSR_WA_3, fam17h_old_3);
}

int init_workaround_structs(void)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;
	if (workarounds_started)
		return 0;

	if (c->x86_vendor == X86_VENDOR_AMD && c->x86 == 0x17 &&
			c->x86_model == 0x1)
	{
		init_fam17h_m01h_workaround();
	} 
	pcpu_num_devices_enabled = alloc_percpu(int);
	if (!pcpu_num_devices_enabled)
		return -1;
	pcpu_workaround_lock = alloc_percpu(spinlock_t);
	if (!pcpu_workaround_lock) {
		free_percpu(pcpu_num_devices_enabled);
		return -1;
	}
	workarounds_started = 1;
	return 0;
}

void free_workaround_structs(void)
{
	if (workarounds_started)
	{
		free_percpu(pcpu_num_devices_enabled);
		free_percpu(pcpu_workaround_lock);
	}
}

void init_workaround_initialize(void)
{
	unsigned int cpu;
	if (!workarounds_started)
		return;
	for_each_possible_cpu(cpu) {
		spinlock_t *workaround_lock;
		int *num_devs = per_cpu_ptr(pcpu_num_devices_enabled, cpu);
		*num_devs = 0;
		workaround_lock = per_cpu_ptr(pcpu_workaround_lock, cpu);
		spin_lock_init(workaround_lock);
	}
}

/* Enabling IBS on Family 17h Model 01h processors requires unsetting some bits
 * in various MSRs so long as any IBS samples can flow through the pipeline.
 * This function reads those MSRs out, sets a global view of the default state
 * of those bits, and unsets them on the local core.
 * This must be called before writing the enable bit into IBS_OP_CTL or
 * IBS_FETCH_CTL. */
static void enable_fam17h_m01h_dyn_workaround(const int cpu)
{
	__u64 set_bits;
	__u64 op_ctl, fetch_ctl;
	__u64 cur1, cur3;
	unsigned int cpu_to_use;
	/* Check if both op and fetch are off */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30)
	/* for_each_cpu() was in 2.6.28, but not topology_sibilng_cpumask() */
	for_each_cpu(cpu_to_use, topology_sibling_cpumask(cpu))
#else
	for_each_cpu_mask(cpu_to_use, topology_core_siblings(cpu))
#endif
	{
		op_ctl = custom_rdmsrl_on_cpu(cpu_to_use, MSR_IBS_OP_CTL);
		fetch_ctl = custom_rdmsrl_on_cpu(cpu_to_use, MSR_IBS_FETCH_CTL);
		/* Workaround already enabled if any other IBS is enabled
		 * on this physical core. Skip the workaround. */
		if ((op_ctl & IBS_OP_EN) || (fetch_ctl & IBS_FETCH_EN))
			return;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30)
	cpu_to_use = cpumask_first(topology_sibling_cpumask(cpu));
#else
	cpu_to_use = first_cpu(topology_core_siblings(cpu));
#endif
	cur1 = custom_rdmsrl_on_cpu(cpu_to_use, FAM17H_MSR_WA_1);
	cur3 = custom_rdmsrl_on_cpu(cpu_to_use, FAM17H_MSR_WA_3);
	/* Set some bits on workaround MSR 1 then write back. */
	set_bits = cur1 | FAM17H_MSR_WA_1_BITS;
	custom_wrmsrl_on_cpu(cpu_to_use, FAM17H_MSR_WA_1, set_bits);
	/* Unset some bits on workaround MSR 3 then write back */
	set_bits = cur3 & ~FAM17H_MSR_WA_3_BITS;
	custom_wrmsrl_on_cpu(cpu_to_use, FAM17H_MSR_WA_3, set_bits);
}

static void disable_fam17h_m01h_dyn_workaround(const int cpu)
{
	__u64 op_ctl, fetch_ctl;
	__u64 cur1, cur3, set_bits;
	unsigned int cpu_to_use;
	/* Check if both op and fetch are off */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30)
	for_each_cpu(cpu_to_use, topology_sibling_cpumask(cpu))
#else
	for_each_cpu_mask(cpu_to_use, topology_core_siblings(cpu))
#endif
	{
		op_ctl = custom_rdmsrl_on_cpu(cpu_to_use, MSR_IBS_OP_CTL);
		fetch_ctl = custom_rdmsrl_on_cpu(cpu_to_use, MSR_IBS_FETCH_CTL);
		/* Can't turn off the workaround while any IBS stuff is on */
		if ((op_ctl & IBS_OP_EN) || (fetch_ctl & IBS_FETCH_EN))
			return;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30)
	cpu_to_use = cpumask_first(topology_sibling_cpumask(cpu));
#else
	cpu_to_use = first_cpu(topology_core_siblings(cpu));
#endif
	/* First time we're enabling IBS on this physical core; set the bits */
	/* Read current values */
	cur1 = custom_rdmsrl_on_cpu(cpu_to_use, FAM17H_MSR_WA_1);
	cur3 = custom_rdmsrl_on_cpu(cpu_to_use, FAM17H_MSR_WA_3);
	/* Unset some bits on workaround MSR 1 (if they were originally
	 * unset) and then write the new register back */
	set_bits = cur1 & ~FAM17H_MSR_WA_1_BITS;
	set_bits |= fam17h_old_1;
	custom_wrmsrl_on_cpu(cpu_to_use, FAM17H_MSR_WA_1, set_bits);
	/* Set some bits on workaround MSR 3 (if they were originally
	 * set) and then write the new register back */
	set_bits = cur3 | FAM17H_MSR_WA_3_BITS;
	set_bits |= fam17h_old_3;
	custom_wrmsrl_on_cpu(cpu_to_use, FAM17H_MSR_WA_3, set_bits);
}

/* Grab lock and call into the dynamic workaround function */
void start_fam17h_m01h_dyn_workaround(const int cpu)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30)
	int cpu_to_use = cpumask_first(topology_sibling_cpumask(cpu));
#else
	int cpu_to_use = first_cpu(topology_core_siblings(cpu));
#endif
	spinlock_t *cpu_workaround_lock =
		per_cpu_ptr(pcpu_workaround_lock, cpu_to_use);
	spin_lock(cpu_workaround_lock);
	enable_fam17h_m01h_dyn_workaround(cpu);
	spin_unlock(cpu_workaround_lock);
}

/* Grab lock and call into the dynamic workaround stopper */
void stop_fam17h_m01h_dyn_workaround(const int cpu)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30)
	int cpu_to_use = cpumask_first(topology_sibling_cpumask(cpu));
#else
	int cpu_to_use = first_cpu(topology_core_siblings(cpu));
#endif
	spinlock_t *cpu_workaround_lock =
		per_cpu_ptr(pcpu_workaround_lock, cpu_to_use);
	spin_lock(cpu_workaround_lock);
	disable_fam17h_m01h_dyn_workaround(cpu);
	spin_unlock(cpu_workaround_lock);
}

void start_fam17h_m01h_static_workaround(const int cpu)
{
	struct msr_info this_msr;
	unsigned int cpu_to_use;
	u64 cur;

	if (!workarounds_started)
	{
		init_workaround_structs();
		init_workaround_initialize();
	}

	/* Turn on IBS in the CPUID chain. We want to do this per-thread
	 * because the MSR that overrides CPUID defaults is per-thread. */
	cur = custom_rdmsrl_on_cpu(cpu, CPUID_EXT_FEATURES);
	cur |= (1ULL << 42); /* Enable IBS in CPUID */
	custom_wrmsrl_on_cpu(cpu, CPUID_EXT_FEATURES, cur);

	/* However, our workarounds are per-core, so if we are in the
	 * non-main thread, just skip doing the workaround. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30)
	cpu_to_use = cpumask_first(topology_sibling_cpumask(cpu));
#else
	cpu_to_use = first_cpu(topology_core_siblings(cpu));
#endif
	if (cpu_to_use != cpu)
		return;

	/* We want to turn on some bits on each physical core when we enable
	 * the driver, or if that core comes up after we enable the driver. */
	this_msr.msr_no = FAM17H_MSR_WA_2;
	cur = custom_rdmsrl_on_cpu(cpu_to_use, FAM17H_MSR_WA_2);
	cur |= FAM17H_MSR_WA_2_BITS;
	this_msr.reg.q = cur;
	/* Use a custom wrmsr function for this so we can put in a
	 * pipeline flush. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	smp_call_function_single(cpu_to_use, custom_wrmsr_flush, &this_msr, 1);
#else
	smp_call_function_single(cpu_to_use, custom_wrmsr_flush, &this_msr, 1, 1);
#endif
}

void stop_fam17h_m01h_static_workaround(const int cpu)
{
	struct msr_info this_msr;
	unsigned int cpu_to_use;
	u64 cur;

	this_msr.msr_no = FAM17H_MSR_WA_2;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30)
	cpu_to_use = cpumask_first(topology_sibling_cpumask(cpu));
#else
	cpu_to_use = first_cpu(topology_core_siblings(cpu));
#endif
	/* Our static workarounds are per-core, not per-thread, so we
	 * only want to unset the workaround once per core. */
	if (cpu_to_use == cpu)
	{
		/* Turn off bits on each core when we disable the driver. */
		cur = custom_rdmsrl_on_cpu(cpu_to_use, FAM17H_MSR_WA_2);
		/* Unset the bits */
		cur = fam17h_old_2 | (cur & ~FAM17H_MSR_WA_2_BITS);
		this_msr.reg.q = cur;
		/* Use a custom wrmsr function for this so we can put in a
		 * pipeline flush. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
		smp_call_function_single(cpu_to_use, custom_wrmsr_flush,
				&this_msr, 1);
#else
                smp_call_function_single(cpu_to_use, custom_wrmsr_flush,
				&this_msr, 1, 1);
#endif
	}

	/* Turn off IBS in the CPUID chain. It's OK to do this without checking
	 * what it was when we started, because we wouldn't *be* in this
	 * workaround function if it was on when we started. */
	cur = custom_rdmsrl_on_cpu(cpu, CPUID_EXT_FEATURES);
	cur &= ~(1ULL << 42); /* Enable IBS in CPUID */
	custom_wrmsrl_on_cpu(cpu, CPUID_EXT_FEATURES, cur);
}

void do_fam10h_workaround_420(const int cpu)
{
	__u64 old_op_ctl;
	rdmsrl(MSR_IBS_OP_CTL, old_op_ctl);
	old_op_ctl = (old_op_ctl | IBS_OP_VAL) & (~ IBS_OP_MAX_CNT_OLD);
	custom_wrmsrl_on_cpu(cpu, MSR_IBS_OP_CTL, old_op_ctl);
}
