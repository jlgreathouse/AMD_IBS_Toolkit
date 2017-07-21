/*
 * Linux kernel driver for the AMD Research IBS Toolkit
 *
 * Copyright (C) 2017 Advanced Micro Devices, Inc.
 *
 * This driver is available under the Linux kernel's version of the GPLv2.
 * See driver/LICENSE for more licensing details.
 *
 * This file contains the logic for user-level control of the driver through
 * file-system commands such as ioctl() and read().
 * The ioctl() options are described in the comments of ibs-uapi.h
 */
#ifndef IBS_FOPS_H
#define IBS_FOPS_H

#include <linux/poll.h>

#include "ibs-structs.h"
/**
 * ibs_open - open a device node instance
 */
int ibs_open(struct inode *inode, struct file *file);

/**
 * ibs_poll - check device for read readiness
 *
 * Returns: (POLLIN | POLLRDNORM) if enough data is available for immediate
 * read ("enough" is determined by poll_threshold--see struct ibs_dev for
 * details); 0 if read would block to wait for data; or POLLHUP if there is not
 * enough data and IBS is disabled.
 */
unsigned int ibs_poll(struct file *file, poll_table *wait);

/**
 * ibs_read - read IBS samples from the device
 * @count:  number of bytes to read; value must be (1) at least the size of
 *      one entry and (2) no more than the total buffer size
 *
 * This function reads as many IBS observations as possible up to @count bytes.
 *
 * Returns: Number of bytes read, or negative error code
 */
ssize_t ibs_read(struct file *file, char __user *buf, size_t count,
            loff_t *fpos);

/**
 * ibs_release - disable IBS and clear the data buffer
 */
int ibs_release(struct inode *inode, struct file *file);

/**
 * ibs_ioctl() - perform an ioctl command
 *
 * The command that is called with a user sends an ioctl() to an IBS device.
 *
 * The details of the commands and what they do are contained in ibs-uapi.h
 */
long ibs_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

/**
 * disable_ibs_op_on_cpu()
 *
 * This forcibly disables IBS Op sampling on a target CPU.
 */
void disable_ibs_op_on_cpu(struct ibs_dev *dev, const int cpu);

/**
 * disable_ibs_fetch_on_cpu()
 *
 * This forcibly disables IBS Fetch sampling on a target CPU.
 */
void disable_ibs_fetch_on_cpu(struct ibs_dev *dev, const int cpu);
#endif	/* IBS_FOPS_H */
