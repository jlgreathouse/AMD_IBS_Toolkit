/*
 * Linux kernel driver for the AMD Research IBS Toolkit
 *
 * Copyright (C) 2017-2018 Advanced Micro Devices, Inc.
 *
 * This driver is available under the Linux kernel's version of the GPLv2.
 * See driver/LICENSE for more licensing details.
 *
 * This file contains a series of workarounds for various generations of AMD
 * CPUs. This prevents the main driver code from being polluted by details of
 * the main IBS driver.
 */
#ifndef IBS_WORKAROUND_H
#define IBS_WORKAROUND_H

/* Call these two functions during the single-threaded driver initializatio
 * so that we can define, create, and initialize any data structures needed
 * for any of our workarounds.
 * We don't know which of these workarounds are needed yet (because that
 * requires information about which CPU we're running on, so this will
 * initialize all of the needed structures early on. */
/* init_workaround_structs() will return -1 if it could not create the required
 * data structures. */
int init_workaround_structs(void);
void free_workaround_structs(void);
void init_workaround_initialize(void);

/* Family 10h Erratum #420: Instruction-Based Sampling Engine May Generate
 * Interrupt that Cannot Be Cleared.
 * Workaround for Fam. 10h Erratum 420 is to first set IbsOpMaxCnt to
 * 0 without unsetting IbsOpEn. *Then* clearning IbsOpEn.
 * This function sets IbsOpMaxCnt to zero. */
void do_fam10h_workaround_420(const int cpu);

/* Family 17h processors with first-generation CPUs (previously code-named
 * "Zen") do not necessarily enable IBS by default.
 * They require setting some bits in each core to run IBS.
 * This can be done with a BIOS setting on many boards, but we run the same
 * settings in this driver to increase compatibility */
/* We need to have locks to prevent IBS Fetch and Op devices from smashing
 * on the same registers at the same time. This sets them up when creating
 * either device. The start function will enable the workaround if this is
 * the first device on this core. The stop function will disable the
 * workaround if this is the last device on this core. */
void start_fam17h_zn_dyn_workaround(const int cpu);
void stop_fam17h_zn_dyn_workaround(const int cpu);

/* The following functions should only be called when you are starting the
 * driver up and stopping the driver. These workarounds must be enabled
 * the entire time the driver runs when on a Family 17h processor with a
 * first-generation CPU (previously code-named "Zen").
 * You may also want to call the start function whenever bringing up
 * a new core that was down when starting the driver. */
void start_fam17h_zn_static_workaround(const int cpu);
void stop_fam17h_zn_static_workaround(const int cpu);

#endif	/* IBS_WORKAROUND_H */
