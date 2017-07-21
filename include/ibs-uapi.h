/*
 * User API header for interfacing with the Linux kernel driver in the AMD
 * Research IBS Toolkit.
 *
 * Copyright (C) 2015-2017 Advanced Micro Devices, Inc.
 *
 * This file is distributed under the BSD license described in
 * include/LICENSE.bsd
 * Alternatively, this file may be distributed under the terms of the 
 * Linux kernel's version of the GPLv2. See include/LICENSE.gpl
 *
 *
 * This file contains the user interface to the driver portion of the AMD
 * Research IBS Toolkit. The driver and its code are available in the upper-
 * level 'driver' directory.
 * This file includes:
 * (1) definitions of structs that represent records read from the driver
 *     and passed to user-level applications.
 * (2) definition and documentation of ioctl commands that may be issued
 *     to the driver from user-space applications
 *
 */

#ifndef IBS_UAPI_H
#define IBS_UAPI_H

#if !defined(__KERNEL__) && !defined(MODULE)
#include <sys/ioctl.h>
#include <stdint.h>

// In the future, if we make modifications to the ibs_op_t or ibs_fetch_t
// data structures, raw dumps of these values may be impossible to read.
// As such, we need to version this data structure so that parsing
// applications can read old IBS dumps.
#define IBS_OP_STRUCT_VERSION 1
#define IBS_FETCH_STRUCT_VERSION 1

/* The following unions can be used to pull out specific values from inside of
   an IBS sample. */
typedef union {
    uint64_t val;
    struct {
        uint16_t ibs_op_max_cnt     : 16;
        uint16_t reserved_1          : 1;
        uint16_t ibs_op_en           : 1;
        uint16_t ibs_op_val          : 1;
        uint16_t ibs_op_cnt_ctl      : 1;
        uint16_t ibs_op_max_cnt_upper: 7;
        uint16_t reserved_2          : 5;
        uint32_t ibs_op_cur_cnt     : 27;
        uint32_t reserved_3          : 5;
    } reg;
} ibs_op_ctl_t;

typedef union {
    uint64_t val;
    struct {
        uint16_t ibs_comp_to_ret_ctr;
        uint16_t ibs_tag_to_ret_ctr;
        uint8_t ibs_op_brn_resync   : 1; /* Fam. 10h, LN, BD only */
        uint8_t ibs_op_misp_return  : 1; /* Fam. 10h, LN, BD only */
        uint8_t ibs_op_return       : 1;
        uint8_t ibs_op_brn_taken    : 1;
        uint8_t ibs_op_brn_misp     : 1;
        uint8_t ibs_op_brn_ret      : 1;
        uint8_t ibs_rip_invalid     : 1;
        uint8_t ibs_op_brn_fuse     : 1; /* KV+, BT+ */
        uint8_t ibs_op_microcode    : 1; /* KV+, BT+ */
        uint32_t reserved           : 23;
    } reg;
} ibs_op_data1_t;

typedef union {
    uint64_t val;
    struct {
        uint8_t  ibs_nb_req_src          : 3;
        uint8_t  reserved_1              : 1;
        uint8_t  ibs_nb_req_dst_node     : 1; /* Not valid in BT, JG */
        uint8_t  ibs_nb_req_cache_hit_st : 1; /* Not valid in BT, JG */
        uint64_t reserved_2              : 58;
    } reg;
} ibs_op_data2_t;

typedef union {
    uint64_t val;
    struct {
        uint8_t ibs_ld_op                    : 1;
        uint8_t ibs_st_op                    : 1;
        uint8_t ibs_dc_l1_tlb_miss           : 1;
        uint8_t ibs_dc_l2_tlb_miss           : 1;
        uint8_t ibs_dc_l1_tlb_hit_2m         : 1;
        uint8_t ibs_dc_l1_tlb_hit_1g         : 1;
        uint8_t ibs_dc_l2_tlb_hit_2m         : 1;
        uint8_t ibs_dc_miss                  : 1;
        uint8_t ibs_dc_miss_acc              : 1;
        uint8_t ibs_dc_ld_bank_con           : 1; /* Fam. 10h, LN, BD only */
        uint8_t ibs_dc_st_bank_con           : 1; /* Fam. 10h, LN only */
        uint8_t ibs_dc_st_to_ld_fwd          : 1; /* Fam. 10h, LN, BD, BT+ */
        uint8_t ibs_dc_st_to_ld_can          : 1; /* Fam. 10h, LN, BD only */
        uint8_t ibs_dc_wc_mem_acc            : 1;
        uint8_t ibs_dc_uc_mem_acc            : 1;
        uint8_t ibs_dc_locked_op             : 1;
        uint16_t ibs_dc_no_mab_alloc         : 1; /* Fam. 10h-TN:
                                                    IBS DC MAB hit */
        uint16_t ibs_lin_addr_valid          : 1;
        uint16_t ibs_phy_addr_valid          : 1;
        uint16_t ibs_dc_l2_tlb_hit_1g        : 1;
        uint16_t ibs_l2_miss                 : 1; /* KV+, BT+ */
        uint16_t ibs_sw_pf                   : 1; /* KV+, BT+ */
        uint16_t ibs_op_mem_width            : 4; /* KV+, BT+ */
        uint16_t ibs_op_dc_miss_open_mem_reqs: 6; /* KV+, BT+ */
        uint16_t ibs_dc_miss_lat;
        uint16_t ibs_tlb_refill_lat; /* KV+, BT+ */
    } reg;
} ibs_op_data3_t;

typedef union {
    uint64_t val;
    struct {
        uint8_t ibs_op_ld_resync: 1;
        uint64_t reserved       : 63;
    } reg;
} ibs_op_data4_t; /* CZ, ST only */

typedef union {
    uint64_t val;
    struct {
        uint64_t ibs_dc_phys_addr   : 48;
        uint64_t reserved           : 16;
    } reg;
} ibs_op_dc_phys_addr_t;

// User-space structs that define how we talk to the IBS driver
typedef struct ibs_op {
        ibs_op_ctl_t            op_ctl;
        uint64_t                op_rip;
        ibs_op_data1_t          op_data;
        ibs_op_data2_t          op_data2;
        ibs_op_data3_t          op_data3;
        ibs_op_data4_t          op_data4;
        uint64_t                dc_lin_ad;
        ibs_op_dc_phys_addr_t   dc_phys_ad;
        uint64_t                br_target;
        uint64_t                tsc;
        uint64_t                cr3;
        int                     tid;
        int                     pid;
        int                     cpu;
        int                     kern_mode;
} ibs_op_t;
typedef ibs_op_t ibs_op_v1_t;

typedef union {
    uint64_t val;
    struct {
        uint16_t ibs_fetch_max_cnt;
        uint16_t ibs_fetch_cnt;
        uint16_t ibs_fetch_lat;
        uint8_t ibs_fetch_en        : 1;
        uint8_t ibs_fetch_val       : 1;
        uint8_t ibs_fetch_comp      : 1;
        uint8_t ibs_ic_miss         : 1;
        uint8_t ibs_phy_addr_valid  : 1;
        uint8_t ibs_l1_tlb_pg_sz    : 2;
        uint8_t ibs_l1_tlb_miss     : 1;
        uint8_t ibs_l2_tlb_miss     : 1;
        uint8_t ibs_rand_en         : 1;
        uint8_t ibs_fetch_l2_miss   : 1; /* CZ+ */
        uint8_t reserved            : 5;
    } reg;
} ibs_fetch_ctl_t;

typedef union {
    uint64_t val;
    struct {
        uint64_t ibs_fetch_phy_addr : 48;
        uint64_t reserved           : 16;
    } reg;
} ibs_fetch_phys_addr;

typedef union {
    uint64_t val;
    struct {
        uint16_t ibs_itlb_refill_lat;
        uint64_t reserved               : 48;
    } reg;
} ibs_fetch_extd_ctl; /* CZ+ */

typedef struct ibs_fetch {
        ibs_fetch_ctl_t     fetch_ctl;
        ibs_fetch_extd_ctl  fetch_ctl_extd;
        uint64_t            fetch_lin_ad;
        ibs_fetch_phys_addr fetch_phys_ad;
        uint64_t            tsc;
        uint64_t            cr3;
        int                 tid;
        int                 pid;
        int                 cpu;
        int                 kern_mode;
} ibs_fetch_t;
typedef ibs_fetch_t ibs_fetch_v1_t;
#endif

/**
 * DOC: IBS ioctl commands
 *
 * ENABLE:        Activate IBS.
 *
 * DISABLE:       Deactivate IBS. You may still read buffered samples in the
 *                disabled state.
 *
 * SET_CUR_CNT:   Set the upper 23 bits of the 27-bit IBS op/cycle counter
 *                start value (the low 4 bits are randomized). Possible values
 *                satisfy 0 <= CUR_CNT < 2^23. (On IBS fetch devices, this
 *                command behaves like SET_CNT; see that ioctl for details.)
 *
 * SET_CNT:       Set the upper 16 bits of the 20-bit fetch counter (the low 4
 *                bits are randomized). Possible values satisfy 0<= CNT < 2^16
 *                *and* CNT <= MAX_CNT (see SET_MAX_CNT ioctl).  (On IBS op
 *                devices, this command behaves like SET_CUR_CNT; see that ioctl
 *                for details.)
 *
 *                This does nothing on Trinity (and earlier??) processors, on
 *                which the fetch counter always begins "at the maximum value"
 *                (see Erratum 719 in Revision Guide for AMD Family 15h Models
 *                10-1Fh Processors, Order #48931).
 *
 * GET_CUR_CNT:   Return the counter start value (*not* the current value).
 * GET_CNT:       Same as above.
 *
 * SET_MAX_CNT:   Valid inputs to this command are slightly different for fetch
 *                and op IBS flavors. When issued to an IBS *op* device, set the
 *                upper 23 bits of the 27-bit IBS op/cycle counter
 *                maximum value (the low 4 bits are always 0). Possible values
 *                satisfy 9 <= MAX_CNT < 2^23.
 *
 *                When issued to an IBS *fetch* device, set the upper 16 bits of
 *                the 20-bit fetch counter (the low 4 bits are always zero).
 *                Possible values satisfy 0<= MAX_CNT < 2^16 *and* CNT <= MAX_CNT
 *                (see SET_CNT ioctl).
 *
 * GET_MAX_CNT:   Return the counter maximum value.
 *
 * SET_CNT_CTL:   IBS op counter control - count ops or count cycles. Possible
 *                values are 0 to count cycles and 1 to count ops. Default 1.
 *                (Not meaningful for fetch devices.)
 *
 * GET_CNT_CTL:   Return the counter control value. (Not meaningful for fetch
 *                devices.)
 *
 * SET_RAND_EN:   IBS fetch randomization enable. Possible values are 0 to
 *                disable randomization (low 4 bits are set to 0h upon fetch
 *                enable), and 1 to enable. Default 1. (Not meaningful for op
 *                devices.)
 *
 * GET_RAND_EN:   Return the IBS fetch randomization enable value. (Not
 *                meaningful for op devices.)
 *
 * GET_LOST:      Return the number of IBS samples that were lost because the
 *                ring buffer used to store the samples was full, and the
 *                user has not read the values. Reading this resets the counter
 *                to zero (0).
 *
 * SET_POLL_SIZE: This sets the minimum number of samples (*not* bytes) that
 *                must be ready before a call to poll (select, poll, epoll) will
 *                return, indicating that the device is "ready". Defaults to 1.
 *                Note that you can still read when the device is not "ready".
 *
 *                This is designed to optimize read efficiency. By reading only
 *                when there is a substantial amount of data ready, fewer calls
 *                to read can collect the same amount of data from the device.
 *
 *                This value should be chosen with the buffer capacity in mind.
 *                Possible values satisfy 0 < POLL_SIZE < capacity (in number of
 *                entries); any other input sets errno to -EINVAL.
 *
 * GET_POLL_SIZE: Returns the current POLL_SIZE.
 *
 * SET_BUFFER_SIZE: Set the size of the IBS sample buffer in number of bytes.
 *                If the requested buffer size equals the existing buffer size,
 *                then the buffer is simply cleared; otherwise, the existing
 *                buffer is freed and a new one of requested size is allocated.
 *                (If this allocation fails, -ENOMEM is returned.) IBS must be
 *                disabled. The argument must be at least the size of one buffer
 *                entry (i.e. the size of one of the structs ibs_op or ibs_fetch
 *                defined above).
 *
 * GET_BUFFER_SIZE: Get the size of the IBS sample buffer in number of bytes.
 *
 * DEBUG_BUFFER: Print information about the buffers to the kernel log.
 *
 * RESET_BUFFER: Empty the sample buffer, throwing away existing data.
 *
 * FIONREAD:      Returns the number of samples that are immediately available to
 *                read. This will still work when the driver is disabled, since
 *                the buffers don't drain until they are fully read or IBS is
 *                re-enabled.
 */
#define IBS_ENABLE      0x0U
#define IBS_DISABLE     0x1U

#define SET_CUR_CNT     0x2U
#define GET_CUR_CNT     0x3U
#define SET_CNT         0x4U
#define GET_CNT         0x5U
#define SET_MAX_CNT     0x6U
#define GET_MAX_CNT     0x7U
#define SET_CNT_CTL     0x8U
#define GET_CNT_CTL     0x9U
#define SET_RAND_EN     0xAU
#define GET_RAND_EN     0xBU

#define SET_POLL_SIZE   0xCU
#define GET_POLL_SIZE   0xDU
#define SET_BUFFER_SIZE 0xEU
#define GET_BUFFER_SIZE 0xFU

#define RESET_BUFFER    0x10U

#define GET_LOST        0xEEU
#define DEBUG_BUFFER    0xEFU

#endif        /* IBS_UAPI_H */
