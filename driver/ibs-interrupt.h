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
#ifndef IBS_INTERRUPT_H
#define IBS_INTERRUPT_H

#include <asm/nmi.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
#include <linux/irq_work.h>
/* If possible, we want to wake up clients that are poll()ing on reads in
 * a bottom-half work queue. That's what this does. */
void handle_ibs_work(struct irq_work *w);
#endif

/* This is the actual interrupt handler, safe for running in NMI context,
 * that reads the IBS values and dumps them into memory. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
int handle_ibs_nmi(unsigned int cmd, struct pt_regs *regs);
#else /* Kernel version is below 3.2.0 */
int handle_ibs_nmi(struct notifier_block *self, unsigned long cmd,
				void *data);
#endif

#endif /* IBS_INTERRUPT_H */
