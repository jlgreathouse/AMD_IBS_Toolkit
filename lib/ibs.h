/*
 * Copyright (C) 2015-2017 Advanced Micro Devices, Inc.
 *
 * This file is distributed under the BSD license described in lib/LICENSE
 *
 * This is a library that implements common functionality for configuring,
 * enabling/disabling, and taking samples from the AMD Research IBS driver.
 *
 */

#ifndef __IBS_H__
#define __IBS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ibs-uapi.h"


#define DEFAULT_IBS_DEBUG            0
#define DEFAULT_IBS_OP               0
#define DEFAULT_IBS_FETCH            0
#define DEFAULT_IBS_AGGRESSIVE_READ  0
#define DEFAULT_IBS_READ_ON_TIMEOUT  1
#define DEFAULT_IBS_POLL_TIMEOUT     1000
#define DEFAULT_IBS_POLL_NUM_SAMPLES 4096
#define DEFAULT_IBS_MAX_CNT			 0x3fff
#define DEFAULT_IBS_CPU_LIST         (word_t)-1

#define DEFAULT_IBS_DAEMON_MAX_SAMPLES  10000
#define DEFAULT_IBS_DAEMON_OP_FILE		"op.ibs"
#define DEFAULT_IBS_DAEMON_FETCH_FILE	"fetch.ibs"
#define DEFAULT_IBS_DAEMON_CPU_LIST     (word_t)-1



typedef uint64_t word_t;
#define BITS_PER_WORD  sizeof(word_t)
#define WORD_OFFSET(b) ((b) / BITS_PER_WORD)
#define BIT_OFFSET(b)  ((b) % BITS_PER_WORD)

#define SET_BIT(WORD, BIT) \
    ((word_t *)&WORD)[WORD_OFFSET(BIT)] |= (1 << BIT_OFFSET(BIT))

#define CLEAR_BIT(WORD, BIT) \
    ((word_t *)&WORD)[WORD_OFFSET(BIT)] &= ~(1 << BIT_OFFSET(BIT))

#define IS_BIT_SET(WORD, BIT) \
    !!(((word_t *)&WORD)[WORD_OFFSET(BIT)] & (1 << BIT_OFFSET(BIT)))

typedef enum {
    IBS_DEBUG,
    IBS_OP,
    IBS_FETCH,
    IBS_AGGRESSIVE_READ,
    IBS_READ_ON_TIMEOUT,
    IBS_POLL_TIMEOUT,
    IBS_POLL_NUM_SAMPLES,
	IBS_MAX_CNT,
    IBS_CPU_LIST,
    IBS_DAEMON_MAX_SAMPLES,
	IBS_DAEMON_CPU_LIST,
    IBS_DAEMON_OP_FILE,
    IBS_DAEMON_FETCH_FILE,
    IBS_DAEMON_OP_WRITE,
    IBS_DAEMON_FETCH_WRITE,
} ibs_option_t;

typedef void * ibs_val_t;


typedef struct ibs_option_list {
    ibs_option_t opt;
    ibs_val_t    val;
} ibs_option_list_t;


typedef enum {
	IBS_OP_SAMPLE    = 0x1,
	IBS_FETCH_SAMPLE = 0x2
} ibs_sample_type_t;


typedef struct ibs_sample {
    union {
        struct ibs_op    op;
        struct ibs_fetch fetch;
    } ibs_sample;
} ibs_sample_t;



/* Initialize IBS with list of options */
int
ibs_initialize(ibs_option_list_t *, int num_opts, int daemonize);

void
ibs_finalize(void);

/* Set an IBS option */
int
ibs_set_option(ibs_option_t, ibs_val_t);

/* Enable IBS on a cpu */
int
ibs_enable_cpu(int cpu);

/* Disable IBS on a cpu */
void
ibs_disable_cpu(int cpu);

/* Enable IBS on all cpus */
int
ibs_enable_all(void);

/* Disable IBS on all cpus */
void
ibs_disable_all(void);

/* Get some IBS samples */
int
ibs_sample(int				   max_samples,
           int                 sample_flags,
           struct ibs_sample * samples,
           ibs_sample_type_t * sample_types);

#ifdef __cplusplus
}
#endif

#endif /* __IBS_H__ */
