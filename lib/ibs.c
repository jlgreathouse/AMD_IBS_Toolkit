/*
 * Copyright (C) 2015-2017 Advanced Micro Devices, Inc.
 *
 * This file is distributed under the BSD license described in lib/LICENSE
 *
 * This is a library that implements common functionality for configuring,
 * enabling/disabling, and taking samples from the AMD Research IBS driver.
 *
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <assert.h>
#include <sys/sysinfo.h>

#include "ibs.h"
#include "ibs-uapi.h"

#define MSEC_PER_SEC  1000
#define USEC_PER_MSEC 1000


static unsigned char ibs_debug_on           = DEFAULT_IBS_DEBUG;
static unsigned char ibs_op                 = DEFAULT_IBS_OP;
static unsigned char ibs_fetch              = DEFAULT_IBS_FETCH;
static unsigned char ibs_aggressive_read    = DEFAULT_IBS_AGGRESSIVE_READ;
static unsigned char ibs_read_on_timeout    = DEFAULT_IBS_READ_ON_TIMEOUT;
static unsigned long ibs_poll_timeout       = DEFAULT_IBS_POLL_TIMEOUT;
static unsigned long ibs_poll_num_samples   = DEFAULT_IBS_POLL_NUM_SAMPLES;
static unsigned long ibs_max_cnt            = DEFAULT_IBS_MAX_CNT;

static char * ibs_cpu_list = NULL;

static unsigned long ibs_daemon_max_samples = DEFAULT_IBS_DAEMON_MAX_SAMPLES;
static word_t ibs_daemon_cpu_list           = DEFAULT_IBS_DAEMON_CPU_LIST;
static char * ibs_daemon_op_file            = DEFAULT_IBS_DAEMON_OP_FILE;
static char * ibs_daemon_fetch_file         = DEFAULT_IBS_DAEMON_FETCH_FILE;


    static void
DEFAULT_IBS_DAEMON_OP_WRITE(FILE * fp,
                            ibs_op_t * op)
{
    fprintf(fp, "Got IBS OP sample on cpu %d\n", op->cpu);
}

    static void
DEFAULT_IBS_DAEMON_FETCH_WRITE(FILE * fp,
                               ibs_fetch_t * fetch)
{
    fprintf(fp, "Got IBS FETCH sample on cpu %d\n", fetch->cpu);
}

/* Write format for ops/fetches */
static void (*ibs_daemon_op_write) (FILE * fp, ibs_op_t *)      = DEFAULT_IBS_DAEMON_OP_WRITE;
static void (*ibs_daemon_fetch_write)(FILE * fp, ibs_fetch_t *) = DEFAULT_IBS_DAEMON_FETCH_WRITE;


/* Per CPU IBS stuff */
typedef struct ibs_cpu {
    int op_enabled;
    int op_fd;
    int fetch_enabled;
    int fetch_fd;
    int cpu;
} ibs_cpu_t;

static int       ibs_initialized    = 0;
static pid_t     ibs_daemon         = 0;
static int       ibs_max_op_fd      = -1;
static int       ibs_max_fetch_fd   = -1;
static int       num_cpus           = 0;
ibs_cpu_t *      ibs_cpus           = NULL;


extern int errno;



static int
start_ibs_daemon(void);



#define ibs_debug(fmt, ...)  \
    if (ibs_debug_on) \
printf("IBS_DEBUG [%s:%d:%s]: "fmt"\n", __FILE__, __LINE__, __func__, __VA_ARGS__);

#define ibs_error(fmt, ...)  \
    fprintf(stderr, "IBS_ERROR [%s:%d:%s]: "fmt"\n", __FILE__, __LINE__, __func__, __VA_ARGS__);

#define ibs_error_no(fmt, ...)  \
    fprintf(stderr, "IBS_ERROR [%s:%d:%s]: "fmt": %s\n", __FILE__, __LINE__, __func__, __VA_ARGS__, strerror(errno));



    static void
ibs_op_dev_path(int    cpu,
        char *  path,
        int     path_len)
{
    snprintf(path, path_len, "/dev/cpu/%d/ibs/op", cpu);
}


    static void
ibs_fetch_dev_path(int    cpu,
        char * path,
        int    path_len)
{
    snprintf(path, path_len, "/dev/cpu/%d/ibs/fetch", cpu);
}



    static int
ibs_apply_ioctl_on_cpu(int           cmd,
        unsigned long arg,
        int           cpu)
{
    ibs_cpu_t * ibs_cpu = &(ibs_cpus[cpu]);
    int status = 0;

    if (ibs_cpu->op_fd > 0) {
        status = ioctl(ibs_cpu->op_fd, cmd, arg);
        if (status < 0) {
            ibs_error_no("ioctl %d on cpu %d op failed", cmd, cpu);
            return -1;
        }
    }
    else
    {
        ibs_debug("Skipping Op IOCTL for CPU %d", cpu);
    }

    if (ibs_cpu->fetch_fd > 0) {
        status = ioctl(ibs_cpu->fetch_fd, cmd, arg);
        if (status < 0) {
            ibs_error_no("ioctl %d on cpu %d fetch failed", cmd, cpu);
            return -1;
        }
    }
    else
    {
        ibs_debug("Skipping Fetch IOCTL for CPU %d", cpu);
    }

    return 0;
}

/* Options that require us to take some action, usually on the fds */
    static int
ibs_apply_options_on_cpu(int cpu)
{
    int status;

    ibs_debug("Setting IBS max count on CPU %d to %lu", cpu, ibs_max_cnt);
    status = ibs_apply_ioctl_on_cpu(
            SET_MAX_CNT,
            ibs_max_cnt,
            cpu);
    if (status < 0) {
        ibs_error("Could not apply ibs option SET_MAX_CNT on cpu %d", cpu);
        return status;
    }

    ibs_debug("Setting IBS poll size count on CPU %d to %lu", cpu, ibs_poll_num_samples);
    status = ibs_apply_ioctl_on_cpu(
            SET_POLL_SIZE,
            ibs_poll_num_samples,
            cpu);
    if (status < 0) {
        ibs_error("Could not apply ibs option SET_POLL_SIZE on cpu %d", cpu);
        return status;
    }

    return 0;
}

    int
ibs_set_option(ibs_option_t opt,
        ibs_val_t    val)
{
    union
    {
        void (*op_write_fp)(FILE *, ibs_op_t *);
        void (*fetch_write_fp)(FILE *, ibs_fetch_t *);
        void *ptr;
    } func_ptr_cast;
    switch (opt) {
        case IBS_DEBUG:
            ibs_debug_on = (unsigned char)(unsigned long)val;
            ibs_debug("IBS Debugging enabled%s","");
            break;

        case IBS_OP:
            ibs_op = (unsigned char)(unsigned long)val;
            ibs_debug("Setting IBS OP mode to %u", ibs_op);
            break;

        case IBS_FETCH:
            ibs_fetch = (unsigned char)(unsigned long)val;
            ibs_debug("Setting IBS FETCH mode to %u", ibs_fetch);
            break;

        case IBS_AGGRESSIVE_READ:
            ibs_aggressive_read = (unsigned char)(unsigned long)val;
            ibs_debug("Setting IBS AGGRESSIVE_READ to %u", ibs_aggressive_read);
            break;

        case IBS_READ_ON_TIMEOUT:
            ibs_read_on_timeout = (unsigned char)(unsigned long)val;
            ibs_debug("Setting IBS READ_ON_TIMEOUT to %u", ibs_read_on_timeout);
            break;

        case IBS_POLL_TIMEOUT:
            ibs_poll_timeout = (unsigned long)val;
            ibs_debug("Setting IBS POLL_TIMEOUT to %lu ms", (unsigned long)val);
            break;

        case IBS_POLL_NUM_SAMPLES:
            ibs_poll_num_samples = (unsigned long)val;
            ibs_debug("Setting IBS POLL_NUM_SAMPLES to %lu samples", (unsigned long)val);
            break;

        case IBS_MAX_CNT:
            ibs_max_cnt = (unsigned long)val;
            ibs_debug("Setting IBS_MAX_CNT to %lu", ibs_max_cnt);
            break;

        case IBS_CPU_LIST:
            ibs_debug("Setting the IBS_CPU_LIST for %d cores...", num_cpus);
            for (int i = 0; i < num_cpus; i++)
            {
                ibs_debug("  Core %d: %d -> %d\n", i, (int)ibs_cpu_list[i], (int)((char *)val)[i]);
                ibs_cpu_list[i] = ((char *)val)[i];
            }
            break;

        case IBS_DAEMON_MAX_SAMPLES:
            ibs_daemon_max_samples = (unsigned long)val;
            ibs_debug("Setting IBS_DAEMON_MAX_SAMPLES to %lu", ibs_daemon_max_samples);
            break;

        case IBS_DAEMON_CPU_LIST:
            ibs_daemon_cpu_list = (word_t)val;
            ibs_debug("Setting IBS_DAEMON_CPU_LIST to 0x%lx", ibs_daemon_cpu_list);
            break;

        case IBS_DAEMON_OP_FILE:
            ibs_daemon_op_file = (char *)val;
            ibs_debug("Setting IBS_DAEMON_OP_FILE to %s", ibs_daemon_op_file);
            break;

        case IBS_DAEMON_FETCH_FILE:
            ibs_daemon_fetch_file = (char *)val;
            ibs_debug("Setting IBS_DAEMON_FETCH_FILE to %s", ibs_daemon_fetch_file);
            break;

        case IBS_DAEMON_OP_WRITE:
            func_ptr_cast.ptr = val;
            ibs_daemon_op_write = func_ptr_cast.op_write_fp;
            ibs_debug("Set IBS_DAEMON_OP_WRITE %s", "");
            break;

        case IBS_DAEMON_FETCH_WRITE:
            func_ptr_cast.ptr = val;
            ibs_daemon_fetch_write = func_ptr_cast.fetch_write_fp;
            ibs_debug("Set IBS_DAEMON_FETCH_WRITE %s", "");
            break;

        default:
            ibs_error("Unrecognized IBS option: %d", opt);
            return -1;
    }

    return 0;
}

    int
ibs_enable_cpu(int cpu)
{
    int status;
    ibs_cpu_t * ibs_cpu;

    if (!ibs_cpu_list[cpu])
    {
        ibs_error("Trying to enable IBS on non-initialized CPU %d", cpu);
        return -1;
    }

    ibs_cpu = &(ibs_cpus[cpu]);

    if (ibs_cpu->op_fd > 0) {
        status = ioctl(ibs_cpu->op_fd, IBS_ENABLE);
        if (status < 0) {
            ibs_error_no("Cannot enable IBS OP on cpu %d", cpu);
            goto err;
        }

        ibs_debug("Enabled IBS OP on CPU %d", cpu);
        ibs_cpu->op_enabled = 1;
    }

    if (ibs_cpu->fetch_fd > 0) {
        status = ioctl(ibs_cpu->fetch_fd, IBS_ENABLE);
        if (status < 0) {
            ibs_error_no("Cannot enable IBS FETCJ on cpu %d", cpu);
            goto err;
        }

        ibs_debug("Enabled IBS FETCH on CPU %d", cpu);
        ibs_cpu->fetch_enabled = 1;
    }

    return 0;

err:
    ibs_disable_cpu(cpu);
    return status;
}

    int
ibs_enable_all(void)
{
    int cpu, status = 0;

    for (cpu = 0; cpu < num_cpus; cpu++) {
        ibs_debug("Checking if IBS is initialized for CPU %d: %d", cpu, ibs_cpu_list[cpu]);
        if (ibs_cpu_list[cpu]) {
            status = ibs_enable_cpu(cpu);
            if (status < 0) {
                ibs_error("Cannot enable IBS on cpu %d", cpu);
                goto enable_err;
            }
        }
    }

    return 0;

enable_err:
    while (cpu >= 0) {
        if (ibs_cpu_list[cpu]) {
            ibs_disable_cpu(cpu);
        }

        cpu--;
    }

    return status;
}

    void
ibs_disable_cpu(int cpu)
{
    int status;
    ibs_cpu_t * ibs_cpu;

    if (!ibs_cpu_list[cpu]) {
        ibs_error("Trying to disble IBS on non-initialized CPU %d", cpu);
        return;
    }

    ibs_cpu = &(ibs_cpus[cpu]);

    if (ibs_cpu->op_enabled) {
        status = ioctl(ibs_cpu->op_fd, IBS_DISABLE);
        if (status < 0) {
            ibs_error_no("Cannot disable IBS OP on cpu %d", cpu);
        }

        ibs_cpu->op_enabled = 0;
        ibs_debug("Disabled IBS OP on CPU %d", cpu);
    }

    if (ibs_cpu->fetch_enabled > 0) {
        status = ioctl(ibs_cpu->fetch_fd, IBS_DISABLE);
        if (status < 0) {
            ibs_error_no("Cannot disable IBS FETCH on cpu %d", cpu);
        }

        ibs_cpu->fetch_enabled = 0;
        ibs_debug("Disabled IBS FETCH on CPU %d", cpu);
    }
}

    void
ibs_disable_all(void)
{
    int cpu;

    for (cpu = 0; cpu < num_cpus; cpu++) {
        if (ibs_cpu_list[cpu]) {
            ibs_disable_cpu(cpu);
        }
    }
}

    static int
do_ibs_get_sample(ibs_sample_type_t   type,
        int                 fd,
        ibs_sample_t      * samples,
        int                 sample_off,
        unsigned int        max_samples)
{
    unsigned int samples_available;
    int bytes_wanted, bytes_read;

    /* How many samples are available? */
    samples_available = ioctl(fd, FIONREAD);
    switch (samples_available) {
        case -1:
            ibs_error_no("Could not read number of samples in fd %d", fd);
            return -1;

        case 0:
            ibs_error("No samples avaialable in fd %d", fd);
            return -1;

        default:
            if ((ibs_aggressive_read == 0) &&
                    (samples_available < ibs_poll_num_samples))
            {
                ibs_error("%u samples available in fd %d, but select said at least %lu were!!",
                        samples_available,
                        fd,
                        ibs_poll_num_samples);
                return -1;
            }
            break;
    }

    if (samples_available > max_samples)
        samples_available = max_samples;

    ibs_op_t *temp_op_buffer = NULL;
    ibs_fetch_t *temp_fetch_buffer = NULL;
    if (type == IBS_OP_SAMPLE)
    {
        bytes_wanted = samples_available * sizeof(ibs_op_t);
        temp_op_buffer = malloc(bytes_wanted);
        bytes_read = read(fd, (void*)temp_op_buffer, bytes_wanted);
    }
    else
    {
        bytes_wanted = samples_available * sizeof(ibs_fetch_t);
        temp_fetch_buffer = malloc(bytes_wanted);
        bytes_read = read(fd, (void*)temp_fetch_buffer, bytes_wanted);
    }

    switch (bytes_read) {
        case -1:
            ibs_error_no("Could not read samples from fd %d", fd);
            if (temp_op_buffer)
                free(temp_op_buffer);
            if (temp_fetch_buffer)
                free(temp_fetch_buffer);
            return -1;

        case 0:
            ibs_error("Read 0 bytes from fd %d, which should be impossible with O_NONBLOCK", fd);
            if (temp_op_buffer)
                free(temp_op_buffer);
            if (temp_fetch_buffer)
                free(temp_fetch_buffer);
            return -1;

        default:
            if (bytes_read < bytes_wanted) {
                ibs_error("Read %d bytes out %d avaialable. This should not be possible",
                        bytes_read, bytes_wanted);
                if (temp_op_buffer)
                    free(temp_op_buffer);
                if (temp_fetch_buffer)
                    free(temp_fetch_buffer);
                return -1;
            }
            break;
    }

    for (unsigned int i = 0; i < samples_available; i++)
    {
        if (type == IBS_OP_SAMPLE)
            samples[sample_off + i].ibs_sample.op = temp_op_buffer[i];
        else
            samples[sample_off + i].ibs_sample.fetch = temp_fetch_buffer[i];
    }

    if (temp_op_buffer != NULL)
        free(temp_op_buffer);
    if (temp_fetch_buffer != NULL)
        free(temp_fetch_buffer);

    return samples_available;
}

    static int
do_ibs_get_all_samples(int                 max_samples,
        int                 sample_flags,
        ibs_sample_t      * samples,
        ibs_sample_type_t * sample_types,
        fd_set            * fd_set,
        char *              cpu_list)
{
    int total_new_samples, sample_off, new_samples, cpu;

    sample_off        = 0;
    total_new_samples = 0;

    for (cpu = 0; cpu < num_cpus; cpu++) {
        ibs_cpu_t * ibs_cpu = &(ibs_cpus[cpu]);

        if (!cpu_list[cpu])
            continue;

        /* aggressive_read -> don't even check if the fd has is set. The idea is that
         * at least one cpu has met the threshold, so it might make sense to read all
         * cpus now.
         */
        if ((sample_flags & IBS_OP_SAMPLE) &
                (ibs_cpu->op_fd > 0) &&
                (
                 (ibs_aggressive_read ||
                  (FD_ISSET(ibs_cpu->op_fd, fd_set)))
                )
           )
        {
            new_samples = do_ibs_get_sample(
                    IBS_OP_SAMPLE,
                    ibs_cpu->op_fd,
                    samples,
                    sample_off,
                    max_samples - total_new_samples);

            if (new_samples < 0) {
                ibs_error("Could not get OP sample from cpu %d", cpu);
            } else {
                /* Remember that these are ops */
                {
                    int opt;
                    for (opt = 0; opt < new_samples; opt++) {
                        ibs_sample_type_t * type = &(sample_types[sample_off + opt]);
                        *type = IBS_OP_SAMPLE;
                    }
                }

                total_new_samples += new_samples;
                sample_off        += new_samples;

                if (total_new_samples == max_samples)
                    break;
            }
        }

        if ((sample_flags & IBS_FETCH_SAMPLE) &&
                (ibs_cpu->fetch_fd > 0) &&
                (
                 (ibs_aggressive_read ||
                  (FD_ISSET(ibs_cpu->fetch_fd, fd_set)))
                )
           )
        {
            new_samples = do_ibs_get_sample(
                    IBS_FETCH_SAMPLE,
                    ibs_cpu->fetch_fd,
                    samples,
                    sample_off,
                    max_samples - total_new_samples);

            if (new_samples < 0) {
                ibs_error("Could not get FETCH sample from cpu %d", cpu);
            } else {
                /* Remember that these are fetches */
                {
                    int opt;
                    for (opt = 0; opt < new_samples; opt++) {
                        ibs_sample_type_t * type = &(sample_types[sample_off + opt]);
                        *type = IBS_FETCH_SAMPLE;
                    }
                }

                total_new_samples += new_samples;
                sample_off        += new_samples;

                if (total_new_samples == max_samples)
                    break;
            }
        }
    }

    return total_new_samples;
}

    static int
do_ibs_sample(int           max_samples,
        int                 sample_flags,
        ibs_sample_t      * samples,
        ibs_sample_type_t * sample_types,
        char              * cpu_list)
{
    int max_fd, cpu, status;
    struct timeval timeout;
    fd_set rfds;

    if (max_samples <= 0) {
        ibs_error("max_samples must be > 0. Sent %d instead.", max_samples);
        return -1;
    }

    if (!(sample_flags & IBS_OP_SAMPLE) &&
            !(sample_flags & IBS_FETCH_SAMPLE))
    {
        ibs_error("Must supply IBS_OP_SAMPLE and/or IBS_FETCH_SAMPLE. Sent %d instead.",
                sample_flags);
        return -1;
    }

    FD_ZERO(&rfds);
    for (cpu = 0; cpu < num_cpus; cpu++) {
        if (cpu_list[cpu]) {
            ibs_cpu_t * ibs_cpu = &(ibs_cpus[cpu]);
            if ((sample_flags & IBS_OP_SAMPLE) && ibs_cpu->op_enabled) {
                FD_SET(ibs_cpu->op_fd, &rfds);
            }

            if ((sample_flags & IBS_FETCH_SAMPLE) && ibs_cpu->fetch_enabled) {
                FD_SET(ibs_cpu->fetch_fd, &rfds);
            }
        }
    }

    /* Get max select fd */
    max_fd = (ibs_max_op_fd > ibs_max_fetch_fd) ? ibs_max_op_fd : ibs_max_fetch_fd;

    if (ibs_poll_timeout > 0) {
        timeout.tv_sec  = (ibs_poll_timeout / MSEC_PER_SEC);
        timeout.tv_usec = (ibs_poll_timeout % MSEC_PER_SEC) * USEC_PER_MSEC;
        status = select(max_fd + 1, &rfds, NULL, NULL, &timeout);
    } else {
        status = select(max_fd + 1, &rfds, NULL, NULL, NULL);
    }

    switch (status) {
        case -1:
            if (errno != EINTR)
            {
                ibs_error_no("Select failed.%s", "");
                return -1;
            }
            /* We may still want to read whatever's there */
            if (ibs_read_on_timeout)
                break;

            /* Else we bail */
            return 0;

        case 0:
            ibs_debug("select timed out after %lu ms of no more than %lu samples",
                    ibs_poll_timeout,
                    ibs_poll_num_samples);

            /* We may still want to read whatever's there */
            if (ibs_read_on_timeout)
                break;

            /* Else we bail */
            return 0;

        default:
            break;
    }

    return do_ibs_get_all_samples(
            max_samples,
            sample_flags,
            samples,
            sample_types,
            &rfds,
            cpu_list);
}


/* Get some IBS samples */
    int
ibs_sample(int                 max_samples,
        int                 sample_flags,
        ibs_sample_t      * samples,
        ibs_sample_type_t * sample_types)
{
    return do_ibs_sample(
            max_samples,
            sample_flags,
            samples,
            sample_types,
            ibs_cpu_list);
}

static int is_cpu_online(int cpu_num)
{
    char *online_name;

    // CPU 0 is the bootstrap processor and is always on
    if (cpu_num == 0)
        return 1;

    int num_bytes = asprintf(&online_name,
            "/sys/devices/system/cpu/cpu%d/online", cpu_num);
    if (num_bytes <= 0)
    {
        fprintf(stderr, "asprintf failed at %s:%d\n", __FILE__, __LINE__);
        exit(EXIT_FAILURE);
    }
    FILE *online_fp = fopen(online_name, "r");
    if (online_fp == NULL)
    {
        fprintf(stderr, "Could not open %s\n", online_name);
        exit(EXIT_FAILURE);
    }
    free(online_name);

    char *online_cpus = NULL;
    size_t len = 0;
    ssize_t line_len = getline(&online_cpus, &len, online_fp);
    if (line_len == -1 || len == 0)
    {
        fprintf(stderr, "Failed to read the number of online CPUs\n");
        exit(EXIT_FAILURE);
    }
    fclose(online_fp);

    if (online_cpus[0] == '1')
        return 1;
    else
        return 0;
}

    static int
do_ibs_initialize(ibs_option_list_t * options,
        int                 num_options)
{
    char ibs_path[256];
    int status = 0, fd = 0, opt = 0, cpu = 0;

    num_cpus = get_nprocs_conf();
    ibs_cpu_list = calloc(num_cpus, sizeof(char));

    /* Save options */
    for (opt = 0; opt < num_options; opt++) {
        ibs_option_list_t * o = &(options[opt]);
        ibs_set_option(o->opt, o->val);
    }

    int online_cpus = get_nprocs();
    ibs_debug("%d total cpus - %d cpus online", num_cpus, online_cpus);

    /* Allocate memory for cpu structs */
    ibs_cpus = malloc(sizeof(ibs_cpu_t) * num_cpus);
    if (ibs_cpus == NULL) {
        ibs_error_no("malloc failed.%s", "");
        return -1;
    }

    memset(ibs_cpus, 0, sizeof(ibs_cpu_t) * num_cpus);
    for (cpu = 0; cpu < num_cpus; cpu++) {
        ibs_cpu_t * ibs_cpu = &(ibs_cpus[cpu]);
        ibs_cpu->cpu = cpu;
    }

    ibs_max_op_fd    = -1;
    ibs_max_fetch_fd = -1;

    /* Open IBS files and store fds */
    for (cpu = 0; cpu < num_cpus; cpu++) {
        ibs_cpu_t * ibs_cpu = &(ibs_cpus[cpu]);
        int online = is_cpu_online(cpu);
        if (!online)
            continue;

        if (ibs_op) {
            ibs_debug("Opening IBS-Op device on CPU %d", cpu);
            ibs_op_dev_path(cpu, ibs_path, 256);

            fd = open(ibs_path, O_RDONLY | O_NONBLOCK);
            if (fd < 0) {
                ibs_error_no("Failed to open %s", ibs_path);
                goto err;
            }

            ibs_cpu->op_fd = fd;
            if (fd > ibs_max_op_fd) {
                ibs_max_op_fd = fd;
            }
        }

        if (ibs_fetch) {
            ibs_debug("Opening IBS-Fetch device on CPU %d", cpu);
            ibs_fetch_dev_path(cpu, ibs_path, 32);

            fd = open(ibs_path, O_RDONLY | O_NONBLOCK);
            if (fd < 0) {
                ibs_error_no("Failed to open %s", ibs_path);
                goto err;
            }

            ibs_cpu->fetch_fd = fd;
            if (fd > ibs_max_fetch_fd) {
                ibs_max_fetch_fd = fd;
            }
        }

        /* Apply options on the cpus before enabling IBS */
        status = ibs_apply_options_on_cpu(cpu);
        if (status != 0) {
            ibs_error("Could not apply options on cpu %d", cpu);
            goto err;
        }
    }

    ibs_debug("IBS Initialized.%s", "");

    ibs_initialized = 1;
    return 0;

err:
    while (cpu >= 0) {
        ibs_cpu_t * ibs_cpu = &(ibs_cpus[cpu]);
        if (ibs_cpu->op_fd > 0) {
            close(ibs_cpu->op_fd);
            ibs_cpu->op_fd = 0;
        }

        if (ibs_cpu->fetch_fd > 0) {
            close(ibs_cpu->fetch_fd);
            ibs_cpu->fetch_fd = 0;
        }

        cpu--;
    }

    free(ibs_cpus);

    ibs_max_op_fd    = -1;
    ibs_max_fetch_fd = -1;

    return fd;
}


/* Basic initialization */
    int
ibs_initialize(ibs_option_list_t * options,
        int                 num_options,
        int                 daemonize)
{
    int ret;

    if (ibs_initialized) {
        ibs_error("IBS already initialized. %s", "");
        errno = EALREADY;
        return -1;
    }

    ret = do_ibs_initialize(options, num_options);
    if (ret != 0) {
        ibs_error("Failed to initialize IBS. Ret: %d", ret);
        return ret;
    }

    if (daemonize) {
        pid_t pid = fork();

        switch (pid) {
            case -1:
                ibs_error_no("Failed to fork daemon. %s", "");
                return -1;

            case 0:
                break;

            default:
                ibs_daemon = pid;
                return 0;
        }

        start_ibs_daemon();
        exit(ret);
    }

    /* Success */
    return 0;
}


    void
ibs_finalize(void)
{
    if (ibs_daemon) {
        /* Send a signal to kill the daemon */
        kill(ibs_daemon, SIGUSR1);
        ibs_daemon = 0;
        return;
    }

    if (!ibs_initialized) {
        ibs_error("IBS not initialized. %s", "");
        return;
    }

    /* Disable IBS on all cpus */
    ibs_disable_all();

    /* Free resources */
    free(ibs_cpus);

    ibs_max_op_fd    = -1;
    ibs_max_fetch_fd = -1;
    ibs_initialized  = 0;
}



static int die = 0;

    static void
sig_handler(int sig)
{
    if (sig != SIGUSR1 && sig != SIGINT) {
        ibs_error("Child received a weird signal (%d)", sig);
        return;
    }

    die = 1;
}


/* Setup a simple sample loop */
    static int
start_ibs_daemon(void)
{
    int status;
    ibs_sample_t      * samples      = NULL;
    ibs_sample_type_t * sample_types = NULL;
    FILE * op_fp = NULL, * fetch_fp  = NULL;
    unsigned long num_ops = 0, num_fetches = 0, num_samples = 0;

    if (!ibs_fetch && !ibs_op)
        return 0;

    /* Allocate some memory for the samples */
    samples = malloc(sizeof(ibs_sample_t) * ibs_daemon_max_samples);
    if (samples == NULL) {
        ibs_error_no("Cannot malloc %lu samples", ibs_daemon_max_samples);
        return -1;
    }

    sample_types = malloc(sizeof(ibs_sample_type_t) * ibs_daemon_max_samples);
    if (sample_types == NULL) {
        ibs_error_no("Cannot malloc %lu sample types", ibs_daemon_max_samples);
        free(samples);
        return -1;
    }

    /* Open the op file */
    if (ibs_op)
    {
        op_fp = fopen(ibs_daemon_op_file, "w");
        if (op_fp == NULL) {
            ibs_error_no("Cannot open output file %s", ibs_daemon_op_file);
            free(samples);
            free(sample_types);
            return -1;
        }
    }

    /* Open the fetch file */
    if (ibs_fetch)
    {
        fetch_fp = fopen(ibs_daemon_fetch_file, "w");
        if (fetch_fp == NULL) {
            ibs_error_no("Cannot open output file %s", ibs_daemon_fetch_file);
            free(samples);
            free(sample_types);
            if (op_fp)
                fclose(op_fp);
            return -1;
        }
    }

    /* Enable all CPUs */
    status = ibs_enable_all();
    if (status != 0) {
        ibs_error("Cannot enable IBS on all CPUs. Status: %d", status);
        free(sample_types);
        free(samples);
        if (op_fp)
            fclose(op_fp);
        if (fetch_fp)
            fclose(fetch_fp);
        return status;
    }

    /* Register a handler for the parent to kill us with  */
    signal(SIGUSR1, sig_handler);

    /* Also catch ctrl-C */
    signal(SIGINT, sig_handler);

    while (die == 0) {
        int new_samples, i, sample_flags = 0;

        if (ibs_op)
            sample_flags |= IBS_OP_SAMPLE;

        if (ibs_fetch)
            sample_flags |= IBS_FETCH_SAMPLE;

        new_samples = ibs_sample(
                ibs_daemon_max_samples,
                sample_flags,
                samples,
                sample_types);

        if (new_samples >= 0)
            num_samples += new_samples;

        for (i = 0; i < new_samples; i++) {
            ibs_sample_t * sample = &(samples[i]);
            ibs_sample_type_t   type   = sample_types[i];

            if (type == IBS_OP_SAMPLE) {
                assert(sample_flags & IBS_OP_SAMPLE);
                ibs_op_t * op = &(sample->ibs_sample.op);
                ibs_daemon_op_write(op_fp, op);
                num_ops++;
            } else {
                assert(sample_flags & IBS_FETCH_SAMPLE);
                ibs_fetch_t * fetch = &(sample->ibs_sample.fetch);
                ibs_daemon_fetch_write(fetch_fp, fetch);
                num_fetches++;
            }
        }
    }

    if (op_fp)
    {
        fprintf(op_fp, "IBS OP    samples: %lu\n", num_ops);
        fprintf(op_fp, "IBS total samples: %lu\n", num_samples);
        fclose(op_fp);
    }
    if (fetch_fp)
    {
        fprintf(fetch_fp, "IBS FETCH samples: %lu\n", num_fetches);
        fprintf(fetch_fp, "IBS total samples: %lu\n", num_samples);
        fclose(fetch_fp);
    }

    free(samples);
    free(sample_types);

    ibs_finalize();
    return 0;
}
