/*
 * Copyright (C) 2017 Advanced Micro Devices, Inc.
 *
 * This file is distributed under the BSD license described in tools/LICENSE
 */
#ifndef CPU_CHECK_H
#define CPU_CHECK_H

// Find and return the CPU family, model number, or stepping using CPUID
uint32_t cpu_family(void);
uint32_t cpu_model(void);
uint32_t cpu_stepping(void);

// Gather the CPU name out of the CPUID system. This will return a newly
// allocated array. It is the callers responsibility to free this array.
char *cpu_name(void);

// Return the IBS information contained in CPUID Fn8000_0001_EAX.
uint32_t get_deep_ibs_info(void);

// The following functions will check for IBS support by looking at:
// 1) Whether this is an AMD processor
// 2) Whether the processor indicates IBS support in CPUID Fn0000_0001
// 3) Whether it indicates IBS op/fetch support in CPUID Fn8000_0001
// If they detect that they do not support IBS, they will exit the program.
void check_amd_processor(void);
void check_basic_ibs_support(void);
void check_ibs_op_support(void);
void check_ibs_fetch_support(void);


#endif  /* CPU_CHECK_H */
