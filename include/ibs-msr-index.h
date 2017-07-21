/*
 * Copyright (C) 2015-2017 Advanced Micro Devices, Inc.
 *
 * This file contains the MSR numbers, bits and masks for AMD IBS data
 * This file is distributed under the BSD license described in
 * include/LICENSE.bsd
 * Alternatively, this file may be distributed under the terms of the
 * Linux kernel's version of the GPLv2. See include/LICENSE.gpl
 *
 *
 * Macros defined here represent register numbers and bit masks associated with
 * IBS. The numbers (and names) are derived from the various AMD BKDG manuals.
 *
 * Because various families of processors support different sets of IBS
 * IBS data, support for a limited family of processors is marked in comments.
 * 
 * Decoder for processor names:
 * No marking: Supported since Family 10h
 * LN: Llano, Family 12h
 * BD: Bulldozer, Family 15h Models 00h-0Fh
 * TN: Trinity, Family 15h Models 10h-1Fh
 * KV: Kaveri, Family 15h Models 30h-3Fh
 * CZ: Carrizo, Family 15h Models 60h-6Fh
 * ST: Stoney Ridge, Family 15h Models 70h-7Fh
 * BT: Bobcat, Family 16h Models 00h-0Fh
 * JG: Jaguar, Family 16h Models 30h-3Fh
 * ZN: Zen, Family 17h Models 00h-0Fh
 * 
 */

#ifndef IBS_MSR_INDEX_H
#define IBS_MSR_INDEX_H

/*
 * Bits and masks by register
 */

#define MSR_IBS_FETCH_CTL	0xc0011030
#define 	IBS_RAND_EN		(1ULL<<57)
#define 	IBS_L2_TLB_MISS		(1ULL<<56)
#define 	IBS_L1_TLB_MISS		(1ULL<<55)
#define 	IBS_L1_TLB_PG_SZ	(3ULL<<53)
#define 	IBS_PHY_ADDR_VALID	(1ULL<<52)
#define 	IBS_IC_MISS		(1ULL<<51)
#define 	IBS_FETCH_COMP		(1ULL<<50)
#define 	IBS_FETCH_VAL		(1ULL<<49)
#define 	IBS_FETCH_EN		(1ULL<<48)
#define 	IBS_FETCH_LAT		(0xffffULL<<32)
#define 	IBS_FETCH_CNT		(0xffffULL<<16)
#define 	IBS_FETCH_MAX_CNT	0xffffULL

#define MSR_IBS_FETCH_LIN_AD	0xc0011031
#define 	IBS_FETCH_LIN_AD	(~0ULL)

#define MSR_IBS_FETCH_PHYS_AD	0xc0011032
#define 	IBS_FETCH_PHYS_AD	(~0ULL)

#define MSR_IBS_OP_CTL		0xc0011033
#define 	IBS_OP_CUR_CNT		(0x7ffffffULL<<32) /* LN+ */
#define		IBS_OP_CUR_CNT_OLD	(0XfffffULL<<32) /* Family 10h only */
/* Alternate mask excludes bits that are randomized by software */
#define		IBS_OP_CUR_CNT_23	(0x7fffff0ULL<<32)
#define 	IBS_OP_MAX_CNT		(0x7f0ffffULL) /* LN+ */
#define		IBS_OP_MAX_CNT_OLD	(0xffffULL) /* Family 10h only */
#define 	IBS_OP_CNT_CTL		(1ULL<<19)
#define 	IBS_OP_VAL		(1ULL<<18)
#define 	IBS_OP_EN		(1ULL<<17)

#define MSR_IBS_OP_RIP		0xc0011034
#define 	IBS_OP_RIP		(~0ULL)

#define MSR_IBS_OP_DATA		0xc0011035
#define 	IBS_OP_MICROCODE	(1ULL<<40) /* KV+, BT+ */
#define 	IBS_OP_BRN_FUSE		(1ULL<<39) /* KV+, BT+ */
#define 	IBS_RIP_INVALID		(1ULL<<38) /* LN+ */
#define 	IBS_OP_BRN_RET		(1ULL<<37)
#define 	IBS_OP_BRN_MISP		(1ULL<<36)
#define 	IBS_OP_BRN_TAKEN	(1ULL<<35)
#define 	IBS_OP_RETURN		(1ULL<<34)
#define		IBS_OP_MISP_RETURN	(1ULL<<33) /* Fam. 10h, LN, BD only */
#define		IBS_OP_BRN_RESYNC	(1ULL<<32) /* Fam. 10h, LN, BD only */
#define 	IBS_TAG_TO_RET_CTR	(0xffffULL<<16)
#define 	IBS_COMP_TO_RET_CTR	0xffffULL

#define MSR_IBS_OP_DATA2	0xc0011036
#define 	NB_IBS_REQ_CACHE_HIT_ST	(1ULL<<5) /* Not valid in BT, JG */
#define 	NB_IBS_REQ_DST_NODE	(1ULL<<4) /* Not valid in BT, JG */
#define 	NB_IBS_REQ_SRC		7ULL

#define MSR_IBS_OP_DATA3	0xc0011037
#define 	IBS_TLB_REFILL_LAT	(0xffffULL<<48) /* KV+, BT+ */
#define 	IBS_DC_MISS_LAT		(0xffffULL<<32)
#define 	IBS_OP_DC_MISS_OPEN_MEM_REQS	(0x3fULL<<26) /* KV+, BT+ */
#define 	IBS_OP_MEM_WIDTH	(0xfULL<<22) /* KV+, BT+ */
#define 	IBS_SW_PF		(1ULL<<21) /* KV+, BT+ */
#define 	IBS_L2_MISS		(1ULL<<20) /* KV+, BT+ */
#define 	IBS_DC_L2_TLB_HIT_1G	(1ULL<<19)
#define 	IBS_DC_PHY_ADDR_VALID	(1ULL<<18)
#define 	IBS_DC_LIN_ADDR_VALID	(1ULL<<17)
#define 	DC_MISS_NO_MAB_ALLOC	(1ULL<<16) /* KV+, BT+ only */
#define 	IBS_DC_MAB_HIT		(1ULL<<16) /* Fam. 10h-TN,  */
#define 	IBS_DC_LOCKED_OP	(1ULL<<15)
#define 	IBS_DC_UC_MEM_ACC	(1ULL<<14)
#define 	IBS_DC_WC_MEM_ACC	(1ULL<<13)
#define		IBS_DC_ST_TO_LD_CAN	(1ULL<<12) /* Fam. 10h only */
#define		IBS_DC_ST_TO_LD_FWD	(1ULL<<11) /* Fam. 10h only */
#define		IBS_DC_ST_BNK_CON	(1ULL<<10) /* Fam. 10h only */
#define		IBS_DC_LD_BNK_CON	(1ULL<<9) /* Fam. 10h only */
#define 	IBS_DC_MIS_ACC		(1ULL<<8)
#define 	IBS_DC_MISS		(1ULL<<7)
#define 	IBS_DC_L2_TLB_HIT_2M	(1ULL<<6)
#define 	IBS_DC_L1_TLB_HIT_1G	(1ULL<<5)
#define 	IBS_DC_L1_TLB_HIT_2M	(1ULL<<4)
#define 	IBS_DC_L2_TLB_MISS	(1ULL<<3)
#define 	IBS_DC_L1_TLB_MISS	(1ULL<<2)
#define 	IBS_ST_OP		(1ULL<<1)
#define 	IBS_LD_OP		1ULL

#define MSR_IBS_DC_LIN_AD	0xc0011038
#define 	IBS_DC_LIN_AD		(~0ULL)

#define MSR_IBS_DC_PHYS_AD	0xc0011039
#define 	IBS_DC_PHYS_AD		0xffffffffffffULL

#define MSR_IBS_CONTROL		0xc001103a
#define 	IBS_LVT_OFFSET_VAL	(1ULL<<8)
#define 	IBS_LVT_OFFSET		0xfULL

#define MSR_IBS_BR_TARGET	0xc001103b /* LN+ */
#define 	IBS_BR_TARGET		(~0ULL) /* LN+ */

#define MSR_IBS_EXTD_CTL	0xc001103c /* CZ+ */
#define		IBS_ITLB_REFILL_LAT	(0xffff) /* CZ+ */

#define MSR_IBS_OP_DATA4	0xc001103d /* CZ, ST only */
#define		IBS_OP_LD_RESYNC	1ULL /* CZ, ST only */

#endif	/* IBS_MSR_INDEX_H */
