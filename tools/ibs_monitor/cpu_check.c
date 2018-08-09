/*
 * Copyright (C) 2015-2018 Advanced Micro Devices, Inc.
 *
 * This file is distributed under the BSD license described in tools/LICENSE
 *
 * A series of functions to detect CPU features for the AMD Research IBS
 * monitoring utility
 */
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Function for getting CPUID values -- useful for getting the CPU family and
// testing which features our IBS system on this processor supports.
static void cpuid(uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
    asm volatile("cpuid" : "=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx)
            : "0" (*eax), "2" (*ecx));
}

uint32_t cpu_family(void)
{
    uint32_t eax = 0x1;
    uint32_t ebx = 0, ecx = 0, edx = 0;
    cpuid(&eax, &ebx, &ecx, &edx);
    uint32_t family = (eax & 0xf00) >> 8;
    uint32_t ext_family = (eax & 0xff00000) >> 20;
    return family+ext_family;
}

uint32_t cpu_model(void)
{
    uint32_t eax = 0x1;
    uint32_t ebx = 0, ecx = 0, edx = 0;
    cpuid(&eax, &ebx, &ecx, &edx);
    uint32_t base_model = (eax & 0xf0) >> 4;
    uint32_t ext_model = (eax & 0xf0000) >> 16;
    return (ext_model << 4) | base_model;
}

uint32_t cpu_stepping(void)
{
    uint32_t eax = 0x1;
    uint32_t ebx = 0, ecx = 0, edx = 0;
    cpuid(&eax, &ebx, &ecx, &edx);
    return (eax & 0xf);
}

char *cpu_name(void)
{
    char * ret_str = calloc(49, 1);
    if (ret_str != NULL)
    {
        uint32_t eax = 0x80000002;
        uint32_t ebx = 0, ecx = 0, edx = 0;
        cpuid(&eax, &ebx, &ecx, &edx);
        memcpy(ret_str, &eax, 4);
        memcpy(ret_str + 4, &ebx, 4);
        memcpy(ret_str + 8, &ecx, 4);
        memcpy(ret_str + 12, &edx, 4);
        eax = 0x80000003;
        ebx = ecx = edx = 0;
        cpuid(&eax, &ebx, &ecx, &edx);
        memcpy(ret_str + 16, &eax, 4);
        memcpy(ret_str + 20, &ebx, 4);
        memcpy(ret_str + 24, &ecx, 4);
        memcpy(ret_str + 28, &edx, 4);
        eax = 0x80000004;
        ebx = ecx = edx = 0;
        cpuid(&eax, &ebx, &ecx, &edx);
        memcpy(ret_str + 32, &eax, 4);
        memcpy(ret_str + 36, &ebx, 4);
        memcpy(ret_str + 40, &ecx, 4);
        memcpy(ret_str + 44, &edx, 4);
        ret_str[48] = '\0';
    }
    else
    {
        fprintf(stderr, "ERROR. Can't calloc string at %s:%d\n",
                __FILE__, __LINE__);
        exit(EXIT_FAILURE);
    }
    return ret_str;
}

// This function reads the Instruction Based SAmpling Identifies out of CPUID
uint32_t get_deep_ibs_info(void)
{
    uint32_t eax = 0x8000001b;
    uint32_t ebx = 0, ecx = 0, edx = 0;
    cpuid(&eax, &ebx, &ecx, &edx);
    return eax;
}

// Check CPUID_Fn0000_0000 EBX, ECX, EDX for "AuthenticAMD"
void check_amd_processor(void)
{
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    cpuid(&eax, &ebx, &ecx, &edx);
    if (ebx != 0x68747541 || ecx != 0x444D4163 || edx != 0x69746E65)
    {
        char print_buf[13];
        memcpy(print_buf, &ebx, 4);
        memcpy(print_buf + 4, &edx, 4);
        memcpy(print_buf + 8, &ecx, 4);
        print_buf[12] = '\0';
        fprintf(stderr, "ERROR. This is apparently not an AMD processor.\n");
        fprintf(stderr, "   Processor vendor string: %s\n", print_buf);
        fprintf(stderr, "   Should be: AuthenticAMD\n");
        exit(EXIT_FAILURE);
    }
}

void check_basic_ibs_support(void)
{
    check_amd_processor();
    // Check for Family 10h before trying to read the IBS CPUID registers.
    uint32_t fam = cpu_family();
    if (fam < 0x10 || fam == 0x11)
    {
        fprintf(stderr, "ERROR. AMD processor family is 0x%x\n", fam);
        fprintf(stderr, "    Family 0x10 or above is required for IBS.\n");
        fprintf(stderr, "    (Except Family 0x11 -- that is unupported)\n");
        exit(EXIT_FAILURE);
    }

    // Read the IBS bit out of Feature Identifiers in CPUID
    uint32_t eax = 0x80000001;
    uint32_t ebx = 0, ecx = 0, edx = 0;
    cpuid(&eax, &ebx, &ecx, &edx);
    int ibs_support = ecx & (1 << 10);
    ibs_support >>= 10;
    // Family 17h processors with first-generation cores (previously code-named
    // "Zen") will not claim IBS support without a BIOS setting, but our driver
    // can enable the proper settings to turn on IBS. The driver will turn on
    // this bit, so if it's not set, we should fail out.
    if (!ibs_support)
    {
        // If bit 10 of CPUID_Fn8000_0001_ECX is not set, then IBS is disabled
        fprintf(stderr, "ERROR. CPUID says no IBS support.\n");
        fprintf(stderr, "    CPUID_Fn8000_0001_ECX bit 10 is: %d\n",
                ibs_support);
        exit(EXIT_FAILURE);
    }

    // Read the IBS feature flag valid bits in the IBS ID CPUID
    uint32_t ibs_id = get_deep_ibs_info();
    ibs_id &= 1;
    if (!ibs_id)
    {
        fprintf(stderr, "ERROR. CPUID says IBS FV is not valid.\n");
        fprintf(stderr, "The low-order bit in CPUID Fn8000_001B is 0\n");
        exit(EXIT_FAILURE);
    }
}

void check_ibs_op_support(void)
{
    // Check OpSam and OpCnt bits in the IBS ID register to make sure we can do
    // the appropriate IBS work for ops.
    uint32_t ibs_id = get_deep_ibs_info();
    uint32_t op_sam = (ibs_id & (1 << 2)) >> 2;
    uint32_t op_cnt = (ibs_id & (1 << 4)) >> 4;
    if (!op_sam || !op_cnt)
    {
        fprintf(stderr, "ERROR. Cannot perform op sampling according to CPUID.\n");
        fprintf(stderr, "    CPUID_Fn8000_001B_EAX[OpSam] = %u\n", op_sam);
        fprintf(stderr, "    CPUID_Fn8000_001B_EAX[OpCnt] = %u\n", op_cnt);
        exit(EXIT_FAILURE);
    }
}

void check_ibs_fetch_support(void)
{
    // Check FetchSam to make sure we can properly do fetch sampling.
    uint32_t ibs_id = get_deep_ibs_info();
    uint32_t fetch_sam = (ibs_id & (1 << 1)) >> 1;
    if (!fetch_sam)
    {
        fprintf(stderr, "ERROR. Cannot fetch sample according to CPUID.\n");
        fprintf(stderr, "    CPUID_Fn8000_001B_EAX[FetchSam] = %u\n", fetch_sam);
        exit(EXIT_FAILURE);
    }
}
