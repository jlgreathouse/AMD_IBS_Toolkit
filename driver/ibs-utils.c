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
#include <linux/mutex.h>
#include <linux/vmalloc.h>
#include <asm/errno.h>

#include "ibs-utils.h"

int reset_ibs_buffer(struct ibs_dev *dev)
{
	if (dev == NULL)
		return -EACCES;
	atomic_long_set(&dev->wr, 0);
	atomic_long_set(&dev->rd, 0);
	atomic_long_set(&dev->entries, 0);
	atomic_long_set(&dev->lost, 0);
	return 0;
}

int setup_ibs_buffer(struct ibs_dev *dev, u64 size)
{
	void *tmp;
	if (dev == NULL || size == 0)
		return -EACCES;

	tmp = vmalloc(size);
	if (!tmp)
		return -ENOMEM;

	dev->buf = tmp;
	dev->size = size;
	dev->capacity = size / dev->entry_size;

	reset_ibs_buffer(dev);

	return 0;
}
int free_ibs_buffer(struct ibs_dev *dev)
{
	if (dev == NULL)
		return -EACCES;
	vfree(dev->buf);
	return 0;
}

u64 scatter_bits(u64 qty, u64 fmt)
{
	u64 reg = 0;
	u64 qty_pos = 1;
	u64 fmt_pos = 1;
	u8 i;
	for (i = 0; i < 64; i++) {
		if (fmt & fmt_pos) {
			if (qty & qty_pos) {
				reg |= fmt_pos;
			}
			qty_pos <<= 1;
		}
		fmt_pos <<= 1;
	}
	return reg;
}

u64 gather_bits(u64 reg, u64 fmt)
{
	u64 qty = 0;
	u64 qty_pos = 1;
	u64 fmt_pos = 1;
	u8 i;
	for (i = 0; i < 64; i++) {
		if (fmt & fmt_pos) {
			if (reg & fmt_pos) {
				qty |= qty_pos;
			}
			qty_pos <<= 1;
		}
		fmt_pos <<= 1;
	}
	return qty;
}
