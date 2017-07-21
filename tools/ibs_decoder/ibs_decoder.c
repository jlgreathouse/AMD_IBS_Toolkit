/*
 * Copyright (C) 2015-2017 Advanced Micro Devices, Inc.
 *
 * This file is distributed under the BSD license described in tools/LICENSE
 *
 * This tool parses binary IBS op data from stdin to stdout in csv format.
 *
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>
#include "ibs-uapi.h"

static int fam15h_model01h_err717 = 0;
static int fam14h_err484 = 0;

static inline void print_u8(FILE *outf, uint8_t input) {
    fprintf(outf, "%" PRIu8 ",", input);
}

static inline void print_u16(FILE *outf, uint16_t input) {
    fprintf(outf, "%" PRIu16 ",", input);
}

static inline void print_u32(FILE *outf, uint32_t input) {
    fprintf(outf, "%" PRIu32 ",", input);
}

static inline void print_u64(FILE *outf, uint64_t input) {
    fprintf(outf, "%" PRIu64 ",", input);
}

static inline void print_x64(FILE *outf, uint64_t input) {
    fprintf(outf, "0x%" PRIx64 ",", input);
}

#define CHECK_ASPRINTF_RET(num_bytes) \
{ \
    if (num_bytes <= 0) \
    { \
        fprintf(stderr, "asprintf in %s:%d failed\n", __FILE__, __LINE__); \
            exit(-1); \
    }\
}
#define print_hdr(opf, fmt, ...) \
{ \
    char *header_line; \
    int num_bytes = asprintf(&header_line, fmt, __VA_ARGS__); \
    CHECK_ASPRINTF_RET(num_bytes); \
    fwrite(header_line, 1, num_bytes, opf); \
    free(header_line); \
}

FILE *op_in_fp = NULL;
FILE *op_out_fp = NULL;
FILE *fetch_in_fp = NULL;
FILE *fetch_out_fp = NULL;

void set_op_in_file(char *opt)
{
    op_in_fp = fopen(opt, "r");
    if (op_in_fp == NULL) {
        fprintf(stderr, "Cannot fopen Op Input File: %s\n", opt);
        fprintf(stderr, "    %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void set_op_out_file(char *opt)
{
    op_out_fp = fopen(opt, "w");
    if (op_out_fp == NULL) {
        fprintf(stderr, "Cannot fopen Op Output File: %s\n", opt);
        fprintf(stderr, "    %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void set_fetch_in_file(char *opt)
{
    fetch_in_fp = fopen(opt, "r");
    if (fetch_in_fp == NULL) {
        fprintf(stderr, "Cannot fopen Fetch Input File: %s\n", opt);
        fprintf(stderr, "    %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void set_fetch_out_file(char *opt)
{
    fetch_out_fp = fopen(opt, "w");
    if (fetch_out_fp == NULL) {
        fprintf(stderr, "Cannot fopen Fetch Output File: %s\n", opt);
        fprintf(stderr, "    %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void parse_args(int argc, char *argv[])
{
    static struct option longopts[] =
    {
        {"op_in_file", required_argument, NULL, 'i'},
        {"op_out_file", required_argument, NULL, 'o'},
        {"fetch_in_file", required_argument, NULL, 'f'},
        {"fetch_out_file", required_argument, NULL, 'g'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    char c;
    while ((c = getopt_long(argc, argv, "+hi:o:f:g:", longopts, NULL)) != -1)
    {
        switch (c) {
            fprintf(stderr, "Found: %s : %c\n", argv[optind], c);
            case 'h':
            case '?':
                fprintf(stderr, "This program parses IBS traces from the AMD Research IBS monitor.\n");
                fprintf(stderr, "It takes Op and Fetch traces (in separate files) and saves them to ");
                fprintf(stderr, "human-readable CSV files with appropriate headers.\n");
                fprintf(stderr, "This is done as a separate progrma to reduce the monitor's overhead.\n");
                fprintf(stderr, "Usage: ./ibs_decoer [-o op_input] [-f fetch_input]\n");
                fprintf(stderr, "--op_in_file (or -i):\n");
                fprintf(stderr, "       File with IBS op samples from the monitor program.\n");
                fprintf(stderr, "--op_out_file (or -o):\n");
                fprintf(stderr, "       CSV file to output decoded IBS op trace.\n");
                fprintf(stderr, "--fetch_in_file (or -f):\n");
                fprintf(stderr, "       File with IBS fetch samples from the monitor program.\n");
                fprintf(stderr, "--fetch_out_file (or -g):\n");
                fprintf(stderr, "       CSV file to output decoded IBS fetch trace.\n");
                fprintf(stderr, "If you skip either of the input arguments, that IBS sample type will be ignored.\n");
                fprintf(stderr, "You cannot skip the *_out_file argument when you have an input file.\n\n");
                exit(EXIT_SUCCESS);
            case 'i':
                set_op_in_file(optarg);
                break;
            case 'o':
                set_op_out_file(optarg);
                break;
            case 'f':
                set_fetch_in_file(optarg);
                break;
            case 'g':
                set_fetch_out_file(optarg);
                break;
            default:
                fprintf(stderr, "Found this bad argument: %s\n", argv[optind]);
                break;
        }
    }

    if (op_in_fp == NULL && fetch_in_fp == NULL)
    {
        fprintf(stderr, "\n\nWARNING. No input files given.\n\n");
    }
    if (op_in_fp != NULL && op_out_fp == NULL)
    {
        fprintf(stderr, "\n\nERROR. There is an Op input file, ");
        fprintf(stderr, "but no Op output file target.\n\n");
        exit(EXIT_FAILURE);
    }
    if (fetch_in_fp != NULL && fetch_out_fp == NULL)
    {
        fprintf(stderr, "\n\nERROR. There is a Fetch input file, ");
        fprintf(stderr, "but no Fetch output file target.\n\n");
        exit(EXIT_FAILURE);
    }
}

#define header_parse(string, target) \
if (!done_checking && !strncmp(line, string, sizeof(string)-1))\
{\
    char *token = strtok(line, split);\
    if (token != NULL)\
        token = strtok(NULL, split);\
    if (token != NULL)\
        *target = strtoul(token, NULL, 0);\
    done_checking = 1;\
}
void parse_op_in_header(uint32_t *family, uint32_t *model, int *brn_resync,
        int *misp_return, int *brn_target, int *op_cnt_ext,
        int *rip_invalid_chk, int *op_brn_fuse, int *ibs_op_data_4,
        int *microcode, int *ibs_op_data2_4_5, int *dc_ld_bnk_con,
        int *dc_st_bnk_con, int *dc_st_to_ld_fwd, int *dc_st_to_ld_can,
        int *ibs_data3_20_31_48_63)
{
    char line[256];
    memset(line, 0, sizeof(line));
    // Reading this line marks the end of the header
    while (strncmp(line, "=============================================",
        sizeof("=============================================")-1))
    {
        const char split[2] = ":";
        if (!fgets(line, 255, op_in_fp))
            break;
        if (!strncmp(line, "=============================================",
            sizeof("=============================================")-1))
            break;

        int done_checking = 0;
        header_parse("AMD Processor Family:", family);
        header_parse("AMD Processor Model:", model);
        header_parse("IbsOpBrnResync:", brn_resync);
        header_parse("IbsOpMispReturn:", misp_return);
        header_parse("BrnTrgt:", brn_target);
        header_parse("OpCntExt:", op_cnt_ext);
        header_parse("RipInvalidChk:", rip_invalid_chk);
        header_parse("OpBrnFuse:", op_brn_fuse);
        header_parse("IbsOpData4:", ibs_op_data_4);
        header_parse("Microcode:", microcode);
        header_parse("IBSOpData2_4_5:", ibs_op_data2_4_5);
        header_parse("IbsDcLdBnkCon:", dc_ld_bnk_con);
        header_parse("IbsDcStBnkCon:", dc_st_bnk_con);
        header_parse("IbsDcStToLdFwd:", dc_st_to_ld_fwd);
        header_parse("IbsDcStToLdCan:", dc_st_to_ld_can);
        header_parse("IbsData3_20_31_48_63:", ibs_data3_20_31_48_63);
    }

    if (*family == 0x15 && *model <= 0x1)
        fam15h_model01h_err717 = 1;
    if (*family == 0x14)
        fam14h_err484 = 1;

}

void parse_fetch_in_header(uint32_t *family, uint32_t *model,
        int *fetch_ctl_ext)
{
    char line[256];
    memset(line, 0, sizeof(line));
    // Reading this line marks the end of the header
    while (strncmp(line, "=============================================",
                sizeof("=============================================")-1))
    {
        const char split[2] = ":";
        if (!fgets(line, 255, fetch_in_fp))
            break;
        if (!strncmp(line, "=============================================",
                    sizeof("=============================================")-1))
            break;

        int done_checking = 0;
        header_parse("AMD Processor Family:", family);
        header_parse("AMD Processor Model:", model);
        header_parse("IbsFetchCtlExtd:", fetch_ctl_ext);
    }
}

static void output_common_header(FILE *outf)
{
    print_hdr(outf, "%s,%s,%s,%s,%s,", "TSC", "CPU_Number",
            "TID", "PID", "Kern_mode");
}

static void output_op_header(FILE *outf, uint32_t fam, uint32_t model,
        int brn_resync, int misp_return, int brn_trgt, int op_cnt_ext,
        int rip_invalid_chk, int op_brn_fuse, int ibs_op_data_4, int microcode,
        int ibs_op_data2_4_5, int dc_ld_bnk_con, int dc_st_bnk_con,
        int dc_st_to_ld_fwd, int dc_st_to_ld_can, int ibs_data3_20_31_48_63)
{
    // Basic things for each IBS sample. TSC when it was taken, CPU number,
    // PID, TID, and user/kernel mode.
    output_common_header(outf);

    // IbsOpRip / IBS_OP_RIP
    print_hdr(outf, "%s,", "IbsOpRip");

    // Data on sampling rate
    if (op_cnt_ext)
    {
        print_hdr(outf, "%s,", "IbsOpMaxCnt[26:0]");
    }
    else
    {
        print_hdr(outf, "%s,", "IbsOpMaxCnt[19:0]");
    }

    // IbsOpData / IBS_OP_DATA
    print_hdr(outf, "%s,%s,", "IbsCompToRetCtr", "IbsTagToRetCtr");
    if (brn_resync)
        print_hdr(outf, "%s,", "IbsOpBrnResync");
    if (misp_return)
        print_hdr(outf, "%s,", "IbsOpMispReturn");
    print_hdr(outf, "%s,%s,%s,%s,", "IbsOpReturn", "IbsOpBrnTaken", "IbsOpBrnMisp",
            "IbsOpBrnRet");
    if (rip_invalid_chk)
        print_hdr(outf, "%s,", "IbsRipInvalid");
    if (op_brn_fuse)
        print_hdr(outf, "%s,", "IbsOpBrnFuse");
    if (microcode)
        print_hdr(outf, "%s,", "IbsOpMicrocode");

    // IbsOpData2 / IBS_OP_DATA2
    if (fam < 0x17)
    {
        print_hdr(outf, "%s,", "NbIbsReqSrc");
    }
    else
    {
        print_hdr(outf, "%s,", "DataSrc");
    }

    if (ibs_op_data2_4_5)
    {
        if (fam < 0x17)
        {
            print_hdr(outf, "%s,%s,", "NbIbsReqDstNode", "NbIbsReqCacheHitSt");
        }
        else
        {
            print_hdr(outf, "%s,%s,", "RmtNode", "CacheHitSt");
        }
    }

    // IbsOpData3 / IBS_OP_DATA3
    print_hdr(outf, "%s,%s,%s,%s,%s,%s,%s,%s,%s,", "IbsLdOp", "IbsStOp",
            "IbsDcL1tlbMiss", "IbsDcL2TlbMiss", "IbsDcL1TlbHit2M",
            "IbsDcL1TlbHit1G", "IbsDcL2tlbHit2M", "IbsDcMiss", "IbsDcMissAcc");
    if (dc_ld_bnk_con)
    {
        print_hdr(outf, "%s,", "IbsDcLdBnkCon");
    }
    if (dc_st_bnk_con)
    {
        print_hdr(outf, "%s,", "IbsDcStBnkCon");
    }
    if (dc_st_to_ld_fwd)
    {
        print_hdr(outf, "%s,", "IbsDcStToLdFwd");
    }
    if (dc_st_to_ld_can)
    {
        print_hdr(outf, "%s,", "IbsDcStToLdCan");
    }
    print_hdr(outf, "%s,%s,%s,", "IbsDcWcMemAcc", "IbsDcUcMemAcc",
            "IbsDcLockedOp");
    if (fam <= 0x12 || (fam == 0x15 && model < 0x20))
    {
        print_hdr(outf, "%s,", "IbsDcMabHit");
    }
    else
    {
        print_hdr(outf, "%s,", "DcMissNoMabAlloc");
    }
    print_hdr(outf, "%s,%s,%s,", "IbsDcLinAddrValid", "IbsDcPhyAddrValid",
            "IbsDcL2tlbHit1G");
    if (ibs_data3_20_31_48_63)
    {
        print_hdr(outf, "%s,%s,%s,%s,", "IbsL2Miss", "IbsSwPf",
                "IbsOpMemWidth", "IbsOpDcMissOpenMemReqs");
    }
    print_hdr(outf, "%s,", "IbsDcMissLat");
    if (ibs_data3_20_31_48_63)
    {
        print_hdr(outf, "%s,", "IbstlbRefillLat");
    }

    // IbsDcLinAd / IBS_DC_LINADDR
    print_hdr(outf, "%s,", "IbsDcLinAd");

    // IbsDcPhsAd / IBS_DC_PHYSADDR
    print_hdr(outf, "%s,", "IbsDcPhysAd");

    if (brn_trgt)
    {
        print_hdr(outf, "%s,", "IbsBrnTarget");
    }

    if (ibs_op_data_4)
    {
        print_hdr(outf, "%s,", "IbsOpLdResync");
    }

    print_hdr(outf, "%s", "\n");
}

static void output_fetch_header(FILE *outf, uint32_t fam, uint32_t model,
        int fetch_ctl_ext)
{
    // Basic things for each IBS sample. TSC when it was taken, CPU number,
    // PID, TID, and user/kernel mode.
    output_common_header(outf);

    // IBS_FETCH_CTL_PHYADDR_VALID, IBS_DC_LINADDR, and IBS_DC_PHYSADDR
    print_hdr(outf, "%s,%s,%s,", "IbsPhyAddrValid", "IbsFetchLinAd",
            "IbsFetchPhysAd");

    // IbsFetchCtl / IBS_FETCH_CTL
    // Every generation of IBS Fetch has these columns.
    print_hdr(outf, "%s,%s,%s,%s,%s,%s,%s,", "IbsFetchMaxCnt[19:0]",
            "IbsFetchLat", "IbsFetchComp", "IbsIcMiss",
            "IbsL1TlbPgSz", "IbsL1TlbMiss", "IbsL2TlbMiss");

    // Only CZ, ST, and ZN have this field, but there is no CPUID for it.
    if ((fam == 0x15 && model >= 0x60) || fam == 0x17)
        print_hdr(outf, "%s,", "IbsFetchL2Miss");

    // IBS_EXTD_CTL
    if (fetch_ctl_ext)
        print_hdr(outf, "%s,", "IbsItlbRefillLat");

    print_hdr(outf, "%s", "\n");
}

void output_op_entry(FILE *outf, ibs_op_t op, uint32_t family,
        uint32_t model, int brn_resync, int misp_return, int brn_trgt,
        int op_cnt_ext, int rip_invalid_chk, int op_brn_fuse,
        int ibs_op_data_4, int microcode, int ibs_op_data2_4_5,
        int dc_ld_bnk_con, int dc_st_bnk_con, int dc_st_to_ld_fwd,
        int dc_st_to_ld_can, int ibs_data3_20_31_48_63)
{
    // Common stuff
    print_u64(outf, op.tsc);
    fprintf(outf, "%d,%d,%d,%d,", op.cpu, op.tid, op.pid,
            op.kern_mode);

    // IbsOpRip / IBS_OP_RIP
    print_x64(outf, op.op_rip);

    // Data on sampling rate
    uint32_t op_max_cnt = 0;
    if (op_cnt_ext)
        op_max_cnt = op.op_ctl.reg.ibs_op_max_cnt_upper << 20;
    op_max_cnt += (op.op_ctl.reg.ibs_op_max_cnt << 4);
    print_u32(outf, op_max_cnt);

    // IbsOpData / IBS_OP_DATA
    print_u16(outf, op.op_data.reg.ibs_comp_to_ret_ctr);
    print_u16(outf, op.op_data.reg.ibs_tag_to_ret_ctr);
    if (brn_resync)
        print_u8(outf, op.op_data.reg.ibs_op_brn_resync);
    if (misp_return)
        print_u8(outf, op.op_data.reg.ibs_op_misp_return);
    print_u8(outf, op.op_data.reg.ibs_op_brn_ret);
    print_u8(outf, op.op_data.reg.ibs_op_brn_taken);
    print_u8(outf, op.op_data.reg.ibs_op_brn_misp);
    print_u8(outf, op.op_data.reg.ibs_op_brn_ret);
    if (rip_invalid_chk)
        print_u8(outf, op.op_data.reg.ibs_rip_invalid);
    if (op_brn_fuse)
        print_u8(outf, op.op_data.reg.ibs_op_brn_fuse);
    if (microcode)
        print_u8(outf, op.op_data.reg.ibs_op_microcode);

    // IbsOpData2 / IBS_OP_DATA2
    // This register is only valid for load operations that miss in
    // both the L1 and L2. However, until KV+ and BT+, IBS could not
    // tell us if an access was an L2 miss.
    // The first case, we support checking L2, so check all the three things.
    // In the second case, we can't read L2, so go ahead if it's a load
    // L1 miss
    if ((ibs_data3_20_31_48_63 && op.op_data3.reg.ibs_ld_op &&
            op.op_data3.reg.ibs_l2_miss && op.op_data3.reg.ibs_dc_miss)
        ||
        (!ibs_data3_20_31_48_63 && op.op_data3.reg.ibs_ld_op &&
        op.op_data3.reg.ibs_dc_miss))
    {
        if (fam14h_err484 && op.op_data3.reg.ibs_dc_wc_mem_acc)
            fprintf(outf, "-,-,-,");
        else
        {
            switch (op.op_data2.reg.ibs_nb_req_src) {
                case 0:
                    fprintf(outf, "-,");
                    break;
                case 1:
                    if (family == 0x10 || (family == 0x15 && model < 0x10))
                        fprintf(outf, "local_L3,");
                    else
                    {
                        fprintf(outf, "Reserved-%" PRIu8 ",",
                                op.op_data2.reg.ibs_nb_req_src);
                    }
                case 2:
                    fprintf(outf, "other_core_cache,");
                    break;
                case 3:
                    fprintf(outf, "DRAM,");
                    break;
                case 7:
                    fprintf(outf, "Other,");
                    break;
                default:
                    fprintf(outf, "Reserved-%" PRIu8 ",",
                            op.op_data2.reg.ibs_nb_req_src);
                    break;
            }
            if (ibs_op_data2_4_5)
            {
                // This is only valid if the NbIbsReqSrc != 0
                if (op.op_data2.reg.ibs_nb_req_src == 0)
                    fprintf(outf, "-,");
                else if (op.op_data2.reg.ibs_nb_req_dst_node == 1)
                    fprintf(outf, "other_node,");
                else
                    fprintf(outf, "same_node,");

                // This is only valid when NbIbsReqSrc == 2
                if (op.op_data2.reg.ibs_nb_req_src != 2)
                    fprintf(outf, "-,");
                else if (op.op_data2.reg.ibs_nb_req_cache_hit_st == 1)
                    fprintf(outf, "O,");
                else
                    fprintf(outf, "M,");
            }
        }
    }
    else
        fprintf(outf, "-,-,-,");

    // IbsOpData3 / IBS_OP_DATA3
    print_u8(outf, op.op_data3.reg.ibs_ld_op);
    print_u8(outf, op.op_data3.reg.ibs_st_op);
    print_u8(outf, op.op_data3.reg.ibs_dc_l1_tlb_miss);
    print_u8(outf, op.op_data3.reg.ibs_dc_l2_tlb_miss);
    print_u8(outf, op.op_data3.reg.ibs_dc_l1_tlb_hit_2m);
    print_u8(outf, op.op_data3.reg.ibs_dc_l1_tlb_hit_1g);
    print_u8(outf, op.op_data3.reg.ibs_dc_l2_tlb_hit_2m);
    print_u8(outf, op.op_data3.reg.ibs_dc_miss);
    print_u8(outf, op.op_data3.reg.ibs_dc_miss_acc);
    if (dc_ld_bnk_con)
        print_u8(outf, op.op_data3.reg.ibs_dc_ld_bank_con);
    if (dc_st_bnk_con)
        print_u8(outf, op.op_data3.reg.ibs_dc_st_bank_con);
    if (dc_st_to_ld_fwd)
        print_u8(outf, op.op_data3.reg.ibs_dc_st_to_ld_fwd);
    if (dc_st_to_ld_can)
        print_u8(outf, op.op_data3.reg.ibs_dc_st_to_ld_can);
    print_u8(outf, op.op_data3.reg.ibs_dc_wc_mem_acc);
    print_u8(outf, op.op_data3.reg.ibs_dc_uc_mem_acc);
    print_u8(outf, op.op_data3.reg.ibs_dc_locked_op);
    if (fam15h_model01h_err717 && (op.op_data3.reg.ibs_dc_miss -= 0))
        print_u8(outf, 0);
    else
        print_u8(outf, op.op_data3.reg.ibs_dc_no_mab_alloc);
    print_u8(outf, op.op_data3.reg.ibs_lin_addr_valid);
    print_u8(outf, op.op_data3.reg.ibs_phy_addr_valid);
    print_u8(outf, op.op_data3.reg.ibs_dc_l2_tlb_hit_1g);
    if (ibs_data3_20_31_48_63)
    {
        print_u8(outf, op.op_data3.reg.ibs_l2_miss);
        print_u8(outf, op.op_data3.reg.ibs_sw_pf);
        switch(op.op_data3.reg.ibs_op_mem_width)
        {
            case 0:
                fprintf(outf, "0,");
                break;
            case 1:
                fprintf(outf, "1,");
                break;
            case 2:
                fprintf(outf, "2,");
                break;
            case 3:
                fprintf(outf, "4,");
                break;
            case 4:
                fprintf(outf, "8,");
                break;
            case 5:
                fprintf(outf, "16,");
                break;
            default:
                fprintf(outf, "Reserved-%" PRIu8 ",",
                        op.op_data3.reg.ibs_op_mem_width);
                break;
        }
        print_u8(outf, op.op_data3.reg.ibs_op_dc_miss_open_mem_reqs);
    }
    print_u16(outf, op.op_data3.reg.ibs_dc_miss_lat);
    if (ibs_data3_20_31_48_63)
        print_u16(outf, op.op_data3.reg.ibs_tlb_refill_lat);

    // IbsDcLinAd / IBS_DC_LINADDR
    if (op.op_data3.reg.ibs_lin_addr_valid)
        print_x64(outf, op.dc_lin_ad);
    else
        fprintf(outf, "-,");

    // IbsDcPhsAd / IBS_DC_PHYSADDR
    if (op.op_data3.reg.ibs_phy_addr_valid)
        print_x64(outf, op.dc_phys_ad.reg.ibs_dc_phys_addr);
    else
        fprintf(outf, "-,");

    if (brn_trgt)
    {
        if (op.op_data.reg.ibs_op_brn_ret)
            print_x64(outf, op.br_target);
        else
            fprintf(outf, "-,");
    }

    if (ibs_op_data_4)
        print_u8(outf, op.op_data4.reg.ibs_op_ld_resync);

    fprintf(outf, "\n");
}

static void output_fetch_entry(FILE *outf, ibs_fetch_t fetch,
        uint32_t family, uint32_t model, int fetch_ctl_ext)
{
    // Common stuff
    print_u64(outf, fetch.tsc);
    fprintf(outf, "%d,%d,%d,%d,", fetch.cpu, fetch.tid, fetch.pid,
            fetch.kern_mode);

    // IBS_FETCH_CTL_PHYADDR_VALID, IBS_DC_LINADDR, and IBS_DC_PHYSADDR
    print_u8(outf, fetch.fetch_ctl.reg.ibs_phy_addr_valid);
    print_x64(outf, fetch.fetch_lin_ad);
    if (fetch.fetch_ctl.reg.ibs_phy_addr_valid)
        print_x64(outf, fetch.fetch_phys_ad.reg.ibs_fetch_phy_addr);
    else
        fprintf(outf, "-,");

    // IbsFetchCtl / IBS_FETCH_CTL
    // Every generation of IBS Fetch has these columns.
    uint32_t fetch_max_cnt = (fetch.fetch_ctl.reg.ibs_fetch_max_cnt << 4);
    print_u32(outf, fetch_max_cnt);

    print_u16(outf, fetch.fetch_ctl.reg.ibs_fetch_lat);
    print_u8(outf, fetch.fetch_ctl.reg.ibs_fetch_comp);
    print_u8(outf, fetch.fetch_ctl.reg.ibs_ic_miss);

    if (fetch.fetch_ctl.reg.ibs_phy_addr_valid)
    {
        switch (fetch.fetch_ctl.reg.ibs_l1_tlb_pg_sz) {
            case 0:
                fprintf(outf, "4 KB,");
                break;
            case 1:
                fprintf(outf, "2 MB,");
                break;
            case 2:
                fprintf(outf, "1 GB,");
                break;
            case 3:
            default:
                fprintf(outf, "Reserved-%" PRIu8 ",",
                        fetch.fetch_ctl.reg.ibs_l1_tlb_pg_sz);
                    break;
        }
    }
    else
        fprintf(outf, "-,");

    print_u8(outf, fetch.fetch_ctl.reg.ibs_l1_tlb_miss);
    print_u8(outf, fetch.fetch_ctl.reg.ibs_l2_tlb_miss);

    // Only CZ, ST, and ZN have this field, but there is no CPUID for it.
    if ((family == 0x15 && model >= 0x60) || family == 0x17)
    {
        print_u8(outf, fetch.fetch_ctl.reg.ibs_fetch_l2_miss);
    }

    // IBS_EXTD_CTL
    if (fetch_ctl_ext)
    {
        if (fetch.fetch_ctl.reg.ibs_fetch_comp)
            print_u16(outf, fetch.fetch_ctl_extd.reg.ibs_itlb_refill_lat);
        else
            fprintf(outf, "-,");
    }

    print_hdr(outf, "%s", "\n");
}

void do_op_work(void)
{
    uint32_t family = 0, model = 0;
    int brn_resync = 0, misp_return = 0, brn_trgt = 0, op_cnt_ext = 0;
    int rip_invalid_chk = 0, op_brn_fuse = 0, ibs_op_data_4 = 0, microcode = 0;
    int ibs_op_data2_4_5 = 0, dc_ld_bnk_con = 0, dc_st_bnk_con = 0;
    int dc_st_to_ld_fwd = 0, dc_st_to_ld_can = 0, ibs_data3_20_31_48_63 = 0;

    printf("Beginning decode of IBS Op Trace header...");
    parse_op_in_header(&family, &model, &brn_resync, &misp_return, &brn_trgt,
            &op_cnt_ext, &rip_invalid_chk, &op_brn_fuse, &ibs_op_data_4,
            &microcode, &ibs_op_data2_4_5, &dc_ld_bnk_con, &dc_st_bnk_con,
            &dc_st_to_ld_fwd, &dc_st_to_ld_can, &ibs_data3_20_31_48_63);
    printf("Done!\n");

    output_op_header(op_out_fp, family, model, brn_resync, misp_return,
            brn_trgt, op_cnt_ext, rip_invalid_chk, op_brn_fuse, ibs_op_data_4,
            microcode, ibs_op_data2_4_5, dc_ld_bnk_con, dc_st_bnk_con,
            dc_st_to_ld_fwd, dc_st_to_ld_can, ibs_data3_20_31_48_63);

    ibs_op_t op;
    uint64_t num_samples_seen = 0;
    printf("Starting to decode op trace. This may take a while...\n");
    while (fread((char *)&op, sizeof(op), 1, op_in_fp) > 0) {
        num_samples_seen++;
        if (num_samples_seen % 100000 == 0)
        {
            printf("Working on op sample number %" PRIu64 "...\n",
                    num_samples_seen);
        }

        output_op_entry(op_out_fp, op, family, model, brn_resync, misp_return,
                brn_trgt, op_cnt_ext, rip_invalid_chk, op_brn_fuse,
                ibs_op_data_4, microcode, ibs_op_data2_4_5, dc_ld_bnk_con,
                dc_st_bnk_con, dc_st_to_ld_fwd, dc_st_to_ld_can,
                ibs_data3_20_31_48_63);
    }
    printf("Done with op samples!\n");
}

void do_fetch_work(void)
{
    uint32_t family = 0, model = 0;
    int fetch_ctl_ext = 0;

    printf("Beginning decode of IBS Fetch Trace header...");
    parse_fetch_in_header(&family, &model, &fetch_ctl_ext);
    printf("Done!\n");

    output_fetch_header(fetch_out_fp, family, model, fetch_ctl_ext);

    ibs_fetch_t fetch;
    uint64_t num_samples_seen = 0;
    printf("Starting to decode fetch trace. This may take a while...\n");
    while (fread((char *)&fetch, sizeof(fetch), 1, fetch_in_fp) > 0) {
        num_samples_seen++;
        if (num_samples_seen % 100000 == 0)
        {
            printf("Working on fetch sample number %" PRIu64 "...\n",
                    num_samples_seen);
        }

        output_fetch_entry(fetch_out_fp, fetch, family, model, fetch_ctl_ext);
    }
    printf("Done with fetch samples!\n");
}

int main(int argc, char *argv[]) {
    parse_args(argc, argv);

    if (op_in_fp != NULL)
        do_op_work();

    if (fetch_in_fp != NULL)
        do_fetch_work();

    printf("Decoding complete. Exiting application.\n\n");
	exit(EXIT_SUCCESS);
}
