/*
 * Linux kernel driver for the AMD Research IBS Toolkit
 *
 * Copyright (C) 2015-2017 Advanced Micro Devices, Inc.
 *
 * This driver is available under the Linux kernel's version of the GPLv2.
 * See driver/LICENSE for more licensing details.
 *
 *
 * This file contains the structs used to communicate IBS samples from the
 * driver into user-space.
 */
#ifndef IBS_STRUCTS_H
#define IBS_STRUCTS_H

#include <linux/types.h>
#include <linux/version.h>
#include <linux/wait.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
#include <linux/irq_work.h>
#endif

struct ibs_op {
	__u64	op_ctl;
	__u64	op_rip;
	__u64	op_data;
	__u64	op_data2;
	__u64	op_data3;
	__u64	op_data4;
	__u64	dc_lin_ad;
	__u64	dc_phys_ad;
	__u64	br_target;
	__u64	tsc;
	__u64	cr3;
	pid_t	tid;
	pid_t	pid;
	int	cpu;
	int	kern_mode;
};

struct ibs_fetch {
	__u64	fetch_ctl;
	__u64	fetch_ctl_extd;
	__u64	fetch_lin_ad;
	__u64	fetch_phys_ad;
	__u64	tsc;
	__u64	cr3;
	pid_t	tid;
	pid_t	pid;
	int	cpu;
	int	kern_mode;
};

struct ibs_dev {
	char *buf;	/* buffer memory region */
	u64 size;	/* size of buffer memory region in bytes */
	u64 entry_size;	/* size of each entry in bytes */
	u64 capacity;	/* buffer capacity in entries */

	atomic_long_t wr;	/* write index (0 <= wr < capacity) */
	atomic_long_t rd;	/* read index */
	atomic_long_t entries;	/* buffer occupancy in entries */
	atomic_long_t lost;	/* dropped samples counter */
	struct mutex read_lock;	/* read lock */

	wait_queue_head_t readq;	/* wait queue for blocking read */
	wait_queue_head_t pollq;	/* dedicated wait queue for polling */
	atomic_long_t poll_threshold;	/* min size for poll to return */

	u64 ctl;	/* copy of op/fetch ctl MSR to store control options */
	struct mutex ctl_lock;	/* lock for device control options */

	int cpu;		/* this device's cpu id */
	int flavor;		/* IBS_FETCH or IBS_OP */
	atomic_t in_use;	/* nonzero when device is open */

	/* Information about what IBS stuff is supported on this CPU */
	int ibs_fetch_supported;
	int ibs_op_supported;
	int ibs_brn_trgt_supported;
	int ibs_op_cnt_ext_supported;
	int ibs_rip_invalid_chk_supported;
	int ibs_op_brn_fuse_supported;
	int ibs_fetch_ctl_extd_supported;
	int ibs_op_data4_supported;
	int workaround_fam10h_err_420;
	int workaround_fam15h_err_718;
	int workaround_fam17h_m01h;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	struct irq_work bottom_half;
#endif
};

#endif	/* IBS_STRUCTS_H */
