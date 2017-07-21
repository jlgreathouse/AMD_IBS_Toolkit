/*
 * Linux kernel driver for the AMD Research IBS Toolkit
 *
 * Copyright (C) 2015-2017 Advanced Micro Devices, Inc.
 *
 *  This driver is available under the Linux kernel's version of the GPLv2.
 * See driver/LICENSE for more licensing details.
 *
 * These functions are useful across various files in the IBS driver, so they
 * are included in this general utilities file.
 */
#ifndef IBS_UTILS_H
#define IBS_UTILS_H

#include <linux/types.h>

#include "ibs-structs.h"

 /* IBS flavors as ints */
#define IBS_OP		0
#define IBS_FETCH	1

/* Convert (flavor, cpu) to minor device number and back */
#define IBS_MINOR(flavor, cpu)  ((cpu << 1) | flavor)
#define IBS_CPU(minor)      (minor >> 1)
#define IBS_FLAVOR(minor)   (minor & 1)

/* Remove all entries in the current IBS sample buffer for the target device */
int reset_ibs_buffer(struct ibs_dev *dev);

/* Create the buffer that will store IBS samples for this target device */
int setup_ibs_buffer(struct ibs_dev *dev, u64 size);

/* Free any allocations done after you're finished with a sample buffer in
 * the target device. */
int free_ibs_buffer(struct ibs_dev *dev);

/**
 * scatter_bits() - "scatter" a quantity over a certain positions
 * @qty: data to scatter, packed densely into the low bits
 * @fmt: scatter pattern
 *
 * Return: bitmask with quantity represented in specified bits
 */
u64 scatter_bits(u64 qty, u64 fmt);

/**
 * gather_bits() - "gather" bits from certain positions
 * @reg: data "register"
 * @fmt: mask of bit positions from which to gather
 *
 * Return: quantity represented by the specified bits
 */
u64 gather_bits(u64 reg, u64 fmt);

#endif	/* IBS_UTILS_H */
