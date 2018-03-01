/*
 * Copyright (C) 2015-2017 Advanced Micro Devices, Inc.
 *
 * This file is distributed under the BSD license described in tools/LICENSE
 *
 * This program executes another program and collects performance information
 * from IBS during its execution. It dumps the *raw* IBS sample data structures
 * to the file system. We purposefully avoid ecoding the IBS samples until
 * later, so that the decode work does not disturb the application under test.
 *
 * Usage: ./ibs_monitor [-o op_output] [-f fetch_output] program [...]
 * Arguments:
 * (1) op_output:   file to which to save IBS op samples
 * (2) fetch_output:file to which to save fetch samples
 * (3...) program:  program to run and monitor plus any of its arguments
 */

// Add the _GNU_SOURCE define to allow asprintf
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <unistd.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#include "ibs-uapi.h"
#include "ibs_monitor.h"
#include "cpu_check.h"

// Note that this program does not use libIBS. This is an example of a program
// that directly talks to the AMD Research IBS driver using the ioctl()
// controls in ibs-uapi.h.

// Counters to keep track of the number of IBS samples from Ops and Fetches
unsigned long n_op_samples = 0;
unsigned long n_fetch_samples = 0;

// These counters will keep track of the number of IBS samples that were
// taken by the kernel driver, but were overwritten because the user-level
// application did not read out the data fast enough.
// Either increase the sampling rate or increase the buffer size to
// avoid these lost samples.
unsigned long n_lost_op_samples = 0;
unsigned long n_lost_fetch_samples = 0;

// Global variables for IBS driver settings
int op_cnt_max_to_set = 0;
int fetch_cnt_max_to_set = 0;
int buffer_size = 0;
char *global_buffer = NULL;
int poll_percent = 0;
int poll_size = 0;
int poll_timeout = 0;
char *global_work_dir = NULL;
char *ld_debug_out = NULL;

void set_global_defaults(void)
{
    op_cnt_max_to_set = OP_MAX_CNT;
    fetch_cnt_max_to_set = FETCH_MAX_CNT;
    buffer_size = BUFFER_SIZE_B;
    poll_percent = POLL_SIZE_PERCENT;
    poll_size = buffer_size * ((float)poll_percent / 100.);;
    poll_timeout = POLL_TIMEOUT;
}

void set_op_file(char *opt, FILE **opf, int *flavors)
{
    if (opf == NULL || flavors == NULL)
    {
        perror("Null value in set_op_file\n");
        exit(EXIT_FAILURE);
    }
    *opf = fopen(opt, "w");
    if (*opf == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    *flavors |= IBS_OP;
}

void set_fetch_file(char *opt, FILE **fetchf, int *flavors)
{
    if (fetchf == NULL || flavors == NULL)
    {
        perror("Null value in set_fetch_file\n");
        exit(EXIT_FAILURE);
    }
    *fetchf = fopen(opt, "w");
    if (*fetchf == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    *flavors |= IBS_FETCH;
}

void set_working_dir(char *opt)
{
    global_work_dir = opt;
}

void set_ld_debug_name(char *opt)
{
    ld_debug_out = opt;
}

void set_global_op_sample_rate(int sample_rate)
{
    int max_sample_rate = 0;
    // Check for proper IBS support before we try to read the CPUID information
    // about the maximum sample rate.
    check_amd_processor();
    check_basic_ibs_support();
    check_ibs_op_support();

    if (sample_rate < 0x90)
    {
        fprintf(stderr, "Attempting to set IBS op sample rate too low - %d\n", sample_rate);
        fprintf(stderr, "This generation core should not be set below %d\n", 0x90);
        exit(EXIT_FAILURE);
    }
    uint32_t ibs_id = get_deep_ibs_info();
    uint32_t extra_bits = (ibs_id & (1 << 6)) >> 6;
    if (!extra_bits)
        max_sample_rate = 1<<20;
    else
        max_sample_rate = 1<<27;

    if (sample_rate >= max_sample_rate)
    {
        fprintf(stderr, "Attempting to set IBS op sample rate too high - %d\n", sample_rate);
        fprintf(stderr, "This generation core can only support up to: %d\n", max_sample_rate-1);
        exit(EXIT_FAILURE);
    }
    op_cnt_max_to_set = sample_rate >> 4;
}

void set_global_fetch_sample_rate(int sample_rate)
{
    int max_sample_rate = 1<<20;
    if (sample_rate < 0)
    {
        fprintf(stderr, "Attempting to set IBS op sample rate too low - %d\n", sample_rate);
        fprintf(stderr, "This generation core should not be set below 0\n");
        exit(EXIT_FAILURE);
    }
    if (sample_rate >= max_sample_rate)
    {
        fprintf(stderr, "Attempting to set IBS fetch sample rate too high - %d\n", sample_rate);
        fprintf(stderr, "This generation core can only support up to: %d\n", max_sample_rate-1);
        exit(EXIT_FAILURE);
    }
    fetch_cnt_max_to_set = sample_rate >> 4;
}

void set_global_buffer_size(int in_buffer_size)
{
    if (in_buffer_size < 1)
    {
        fprintf(stderr, "Attempting to set the buffer size too low - %d\n", in_buffer_size);
        exit(EXIT_FAILURE);
    }
    buffer_size = in_buffer_size * 1024;
}

void set_global_poll_percent(int in_poll_percent)
{
    if (in_poll_percent < 0 || in_poll_percent > 100)
    {
        fprintf(stderr, "Error, poll_percent must be between 0 and 100 - tried %d%%\n", in_poll_percent);
        exit(EXIT_FAILURE);
    }
    poll_percent = in_poll_percent;
}

void set_global_poll_timeout(int in_poll_timeout)
{
    if (in_poll_timeout < 1)
    {
        fprintf(stderr, "Error, cannot set poll timeout to less than 1ms\n");
        exit(EXIT_FAILURE);
    }
    poll_timeout = in_poll_timeout;
}

void parse_args(int argc, char *argv[], FILE **opf, FILE **fetchf, int *flavors)
{
    static struct option longopts[] =
    {
        {"op_file", required_argument, NULL, 'o'},
        {"fetch_file", required_argument, NULL, 'f'},
        {"library_map", required_argument, NULL, 'l'},
        {"op_sample_rate", required_argument, NULL, 'r'},
        {"fetch_sample_rate", required_argument, NULL, 's'},
        {"buffer_size", required_argument, NULL, 'b'},
        {"poll_percent", required_argument, NULL, 'p'},
        {"poll_timeout", required_argument, NULL, 't'},
        {"working_dir", required_argument, NULL, 'w'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    if (opf == NULL || fetchf == NULL)
    {
        perror("Either opf or fetchf is NULL, can't proceed.\n");
        exit(EXIT_FAILURE);
    }

    char c;
    while ((c = getopt_long(argc, argv, "+ho:f:l:r:s:b:p:t:w:", longopts, NULL)) != -1)
    {
        switch (c) {
            case 'h':
                fprintf(stderr, "This program executes another program and\n");
                fprintf(stderr, "collects IBS samples during its execution.\n");
                fprintf(stderr, "Usage: ./ibs_monitor [-o op_output] [-f fetch_output] [-w working_directory] program_to_run [...]\n");
                fprintf(stderr, "--working_dir (or -w) {dir}:\n");
                fprintf(stderr, "       Sets the working direcotry for launching the program to monitor.\n");
                fprintf(stderr, "--op_file (or -o) {filename}:\n");
                fprintf(stderr, "       File to which to save IBS op samples\n");
                fprintf(stderr, "--fetch_file (or -f) {filename}:\n");
                fprintf(stderr, "       File to which to save fetch samples\n");
                fprintf(stderr, "If you skip either of the file arguments, that type of IBS sampling will be disabled.\n");
                fprintf(stderr, "\n");
                fprintf(stderr, "--library_map (or -l) {filename}:\n");
                fprintf(stderr, "       Save LD_DEBUG information about dynamic library mappings.. Off by default.\n");
                fprintf(stderr, "\n");
                fprintf(stderr, "IBS configuration parameters:\n");
                fprintf(stderr, "--op_sample_rate (or -r) {# ops}:\n");
                fprintf(stderr, "       The number of ops between each IBS op sample. Defaults to 256K\n");
                fprintf(stderr, "--fetch_sample_rate (or -s) {# instructions}:\n");
                fprintf(stderr, "       The number if instructions between each IBS fetch sample. Defaults to 64K\n");
                fprintf(stderr, "--buffer_size (or -b) {# kB}:\n");
                fprintf(stderr, "       The size of the per-core in-kernel IBS storage buffer, in kB. Defaults to 1024 kB\n");
                fprintf(stderr, "--poll_percent (or -p) {%%age}:\n");
                fprintf(stderr, "       How full the in-kernel buffer should be before reading it, in %%. Defaults to 75%%\n");
                fprintf(stderr, "--poll_timeout (or -t) {# ms}:\n");
                fprintf(stderr, "       How long to wait on the driver before reading a non-full buffer, in ms. Defaults to 1000 ms\n");
                exit(EXIT_SUCCESS);
            case 'o':
                set_op_file(optarg, opf, flavors);
                break;
            case 'f':
                set_fetch_file(optarg, fetchf, flavors);
                break;
            case 'l':
                set_ld_debug_name(optarg);
                break;
            case 'r':
                set_global_op_sample_rate(atoi(optarg));
                break;
            case 's':
                set_global_fetch_sample_rate(atoi(optarg));
                break;
            case 'b':
                set_global_buffer_size(atoi(optarg));
                break;
            case 'p':
                set_global_poll_percent(atoi(optarg));
                break;
            case 't':
                set_global_poll_timeout(atoi(optarg));
                break;
            case 'w':
                set_working_dir(optarg);
                break;
            case '?':
            default:
                fprintf(stderr, "Found this bad argument: %s\n", argv[optind]);
                break;
        }
    }
}

#define print_hdr(opf, fmt, ...) \
{ \
    char *header_line; \
    int num_bytes = asprintf(&header_line, fmt, __VA_ARGS__); \
    CHECK_ASPRINTF_RET(num_bytes); \
    fwrite(header_line, 1, num_bytes, opf); \
    free(header_line); \
}

static void output_op_header(FILE *opf, char *argv[])
{
    // Check for proper IBS support
    check_amd_processor();
    check_basic_ibs_support();
    check_ibs_op_support();

    // Op header shows that this is an Op file, lists the family, and then
    // lists any model-specific support from CPUID_Fn8000_001B_EAX flags.
    print_hdr(opf, "IBS Op Sample File%s", "\n");

    uint32_t fam = cpu_family();
    uint32_t model = cpu_model();
    uint32_t stepping = cpu_stepping();
    char *name = cpu_name();
    print_hdr(opf, "AMD Processor Family: 0x%x\n", fam);
    print_hdr(opf, "AMD Processor Model: 0x%x\n", model);
    print_hdr(opf, "AMD Processor Stepping: 0x%x\n", stepping);
    print_hdr(opf, "AMD Processor Name: %s\n", name);
    free(name);

    // Save off the version of the ibs_op_t structure that this application
    // will be dumping to disk. This way, old traces can later be read by
    // new versions of the decoder.
    print_hdr(opf, "IBS Op Structure Version: %u\n", IBS_OP_STRUCT_VERSION);

    // The following bits were only available on Family 10h, Family 12h,
    // Family 14h, and Family 15h Models 00h-0Fh
    uint32_t brn_resync = 0, misp_return = 0;
    if (fam == 0x10 || fam == 0x12 || fam == 0x14 ||
            (fam == 0x15 && model < 0x10))
    {
        brn_resync = 1;
        misp_return = 1;
    }

    print_hdr(opf, "IbsOpBrnResync: %u\n", brn_resync);
    print_hdr(opf, "IbsOpMispReturn: %u\n", misp_return);

    uint32_t ibs_id = get_deep_ibs_info();
    uint32_t branch_target = (ibs_id & (1 << 5)) >> 5;
    uint32_t op_cnt_ext = (ibs_id & (1 << 6)) >> 6;
    uint32_t rip_invalid_check = (ibs_id & (1 << 7)) >> 7;
    uint32_t branch_fuse = (ibs_id & (1 << 8)) >> 8;
    uint32_t op_data_4 = (ibs_id & (1 << 10)) >> 10;
    print_hdr(opf, "BrnTrgt: %u\n", branch_target);
    print_hdr(opf, "OpCntExt: %u\n", op_cnt_ext);
    print_hdr(opf, "RipInvalidChk: %u\n", rip_invalid_check);
    print_hdr(opf, "OpBrnFuse: %u\n", branch_fuse);
    print_hdr(opf, "IbsOpData4: %u\n", op_data_4);

    // Unfortunately, this is based on family/model, not CPUID check.
    // This tells us about IBS_OP_DATA[40]
    uint32_t microcode = 0;
    if ((fam == 0x15 && model >= 0x60) ||
        fam == 0x17)
        microcode = 1;
    print_hdr(opf, "Microcode: %u\n", microcode);

    // Family 16h does not have these 2 bits defined.
    uint32_t ibs_op_data2_4_5 = 1;
    if (fam == 0x14 || fam == 0x16)
        ibs_op_data2_4_5 = 0;
    print_hdr(opf, "IBSOpData2_4_5: %u\n", ibs_op_data2_4_5);

    // The following are available on Fam 10h, 12h, and 15h Model 00h-0Fh
    uint32_t ld_bnk_con = 0, st_to_ld_can = 0;
    if (fam <= 0x12 || (fam == 0x15 && model < 0x10))
    {
        ld_bnk_con = 1;
        st_to_ld_can = 1;
    }
    // Available on Fam 10h and 12h
    uint32_t st_bnk_con = 0;
    if (fam <= 0x12)
        st_bnk_con = 1;
    // Available on Fam 10h, 12h, 15h Model 00h-0Fh, and 16h
    uint32_t st_to_ld_fw = 0;
    if (fam <= 0x12 || fam == 0x14 || fam == 0x16 ||
            (fam == 0x15 && model < 0x10))
    {
        st_to_ld_fw = 1;
    }
    print_hdr(opf, "IbsDcLdBnkCon: %u\n", ld_bnk_con);
    print_hdr(opf, "IbsDcStBnkCon: %u\n", st_bnk_con);
    print_hdr(opf, "IbsDcStToLdFwd: %u\n", st_to_ld_fw);
    print_hdr(opf, "IbsDcStToLdCan: %u\n", st_to_ld_can);

    // Available on Fam 15h Models >= 30h, Fam 16h, Fam 17h
    uint32_t ibs_data_3_20_31_48_63 = 0;
    if (fam >= 0x16 || (fam == 0x15 && model >= 0x30))
        ibs_data_3_20_31_48_63 = 1;
    print_hdr(opf, "IbsData3_20_31_48_63: %u\n", ibs_data_3_20_31_48_63);

    int page_size = getpagesize();
    long int num_phys_pages = get_phys_pages();
    uint64_t total_mem_size = (uint64_t)num_phys_pages * (uint64_t)page_size;
    double total_mb = total_mem_size / (1024.*1024.);
    double total_gb = total_mem_size / (1024.*1024.*1024.);
    if (total_gb > 0)
    {
        print_hdr(opf, "Memory Size: %.1f GB\n", total_gb);
    }
    else
    {
        print_hdr(opf, "Memory Size: %f MB\n", total_mb);
    }


    // Also the name of the OS we ran this on
    struct utsname os_info;
    uname(&os_info);
    print_hdr(opf, "System name: %s\n", os_info.nodename);
    print_hdr(opf, "OS: %s %s %s %s\n", os_info.sysname, os_info.release, os_info.version, os_info.machine);

    // Also when we ran things
    char timestamp[512];
    time_t cur_time;
    time(&cur_time);
    strftime(timestamp, 512, "%c", localtime(&cur_time));
    print_hdr(opf, "Timestamp: %s\n", timestamp);

    if (global_work_dir != NULL)
    {
        print_hdr(opf, "Working directory: %s\n", global_work_dir);
    }
    else
    {
        char *cwd;
        cwd = getcwd(NULL, 0);
        if (cwd != NULL)
        {
            print_hdr(opf, "Working directory: %s\n", cwd)
            free(cwd);
        }
        else
        {
            fprintf(stderr, "Unable to find the current working directory.\n");
            fprintf(stderr, "    %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    // Also output what command this was
    print_hdr(opf, "Command line: %s", "");
    int i = 0;
    while (argv[i] != NULL)
    {
        print_hdr(opf, "%s ", argv[i]);
        i++;
    }
    print_hdr(opf, "%s\n", "");

    // Last line of every header before the binary dump of IBS samples starts
    // is 45 equal signs.
    print_hdr(opf, "=============================================%s", "\n");
    return;
}

static void output_fetch_header(FILE *fetchf, char *argv[])
{
    // Check for proper IBS support
    check_amd_processor();
    check_basic_ibs_support();
    check_ibs_fetch_support();

    // Op header shows that this is a Fetch file, lists the family, and then
    // lists any model-specific support from CPUID_Fn8000_001B_EAX flags.
    print_hdr(fetchf, "IBS Fetch Sample File%s", "\n");

    uint32_t fam = cpu_family();
    uint32_t model = cpu_model();
    uint32_t stepping = cpu_stepping();
    char *name = cpu_name();
    print_hdr(fetchf, "AMD Processor Family: 0x%x\n", fam);
    print_hdr(fetchf, "AMD Processor Model: 0x%x\n", model);
    print_hdr(fetchf, "AMD Processor Stepping: 0x%x\n", stepping);
    print_hdr(fetchf, "AMD Processor Name: %s\n", name);
    free(name);

    // Save off the version of the ibs_fetch_t structure that this application
    // will be dumping to disk. This way, old traces can later be read by
    // new versions of the decoder.
    print_hdr(fetchf, "IBS Fetch Structure Version: %u\n",
            IBS_FETCH_STRUCT_VERSION);

    uint32_t ibs_id = get_deep_ibs_info();
    uint32_t ibs_fetch_ctl_extd = (ibs_id & (1 << 9)) >> 9;
    print_hdr(fetchf, "IbsFetchCtlExtd: %u\n", ibs_fetch_ctl_extd);

    // Also output what command this was
    print_hdr(fetchf, "Command line: %s", "");
    int i = 0;
    while (argv[i] != NULL)
    {
        print_hdr(fetchf, "%s ", argv[i]);
        i++;
    }
    print_hdr(fetchf, "%s\n", "");

    // Last line of every header before the binary dump of IBS samples starts
    // is 45 equal signs.
    print_hdr(fetchf, "=============================================%s", "\n");
    return;
}

void output_headers(FILE *opf, FILE* fetchf, int flavors, char *argv[])
{
    int do_op = flavors | IBS_OP;
    if (do_op && opf != NULL)
        output_op_header(opf, argv);

    int do_fetch = flavors | IBS_FETCH;
    if (do_fetch && fetchf != NULL)
        output_fetch_header(fetchf, argv);
}

char **update_environment(void)
{
    extern char **environ;
    uint64_t total_envvar = 0;

    int ld_debug_exists = 0;
    while (environ[total_envvar] != NULL)
    {
        // If LD_DEBUG or LD_DEBUG_OUTPUT are already set, we need to give
        // up, because we don't want to overwrite the user's desired
        // work.
        if (strstr(environ[total_envvar], "LD_DEBUG"))
        {
            fprintf(stderr, "Found the environment variable based on LD_DEBUG.\n");
            fprintf(stderr, "    %s\n", environ[total_envvar]);
            ld_debug_exists = 1;
        }
        total_envvar++;
    }

    if (ld_debug_exists)
    {
        fprintf(stderr, "Because LD_DEBUG is already set in the environment, ");
        fprintf(stderr, "we are not changing it.\n");
        return environ;
    }

    // Need to add 3. One for LD_DEBUG, another for LD_DEBUG_OUTPUT
    // and a final one for the trailing '\0'
    char **new_environ = malloc((total_envvar + 3) * sizeof(char *));
    uint64_t i = 0;
    for (; i < total_envvar; i++)
    {
        new_environ[i] = environ[i];
    }
    // Set LD_DEBUG
    int num_bytes = asprintf(&(new_environ[i]), "LD_DEBUG=files,libs");
    CHECK_ASPRINTF_RET(num_bytes);
    i++;
    // Set LD_DEBUG_OUTPUT. It will append the child's PID to the name.
    num_bytes = asprintf(&(new_environ[i]), "LD_DEBUG_OUTPUT=%s", ld_debug_out);
    CHECK_ASPRINTF_RET(num_bytes);
    i++;
    // Copy the \0 from the orginal environment char**.
    new_environ[i] = environ[total_envvar];
    return new_environ;
}

int main(int argc, char *argv[])
{
    struct pollfd *fds;
    int nopfds = 0;
    int nfetchfds = 0;
    int flavors = 0;
    FILE *opf = NULL;
    FILE *fetchf = NULL;
    int i;
    pid_t cpid;

    set_global_defaults();

    parse_args(argc, argv, &opf, &fetchf, &flavors);
    // Reset argv to real program
    argv = &(argv[optind]);

    output_headers(opf, fetchf, flavors, argv);

    poll_size = buffer_size * ((float)poll_percent/100.);
    global_buffer = malloc(buffer_size);

    int num_cpus = get_nprocs_conf();
    // Add enough space for fetch and op FDs for every core.
    fds = calloc(num_cpus*2, sizeof(struct pollfd));
    enable_ibs_flavors(fds, &nopfds, &nfetchfds, flavors);

    cpid = fork();
    if (cpid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    if (cpid == 0) {    /* Child process */
        if (global_work_dir != NULL)
        {
            int err_chk = chdir(global_work_dir);
            if (err_chk != 0)
            {
                fprintf(stderr, "Unable to change working directory to: %s\n",
                    global_work_dir);
                fprintf(stderr, "    %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
        }
        extern char **environ;
        char ** environ_to_use;
        if (ld_debug_out != NULL)
            environ_to_use = update_environment();
        else
            environ_to_use = environ;
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 11)
        if (execvpe(argv[0], &argv[0], environ_to_use) == -1) {
            fprintf(stderr, "Unable to execute application: %s\n", argv[0]);
            fprintf(stderr, "    %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
#else
        environ = environ_to_use;
        if (execvp(argv[0], &argv[0]) == -1) {
            fprintf(stderr, "Unable to execute application: %s\n", argv[0]);
            fprintf(stderr, "    %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
#endif
        exit(EXIT_SUCCESS);
    }

    reset_ibs_buffers(fds, nopfds + nfetchfds);

    while (!waitpid(cpid, &i, WNOHANG))
        poll_ibs(fds, nopfds, nfetchfds, opf, fetchf);

    flush_ibs_buffers(fds, nopfds, nfetchfds, opf, fetchf);

    disable_ibs(fds, nopfds + nfetchfds);

    // LD_DEBUG_OUTPUT appends the child's PID to the name by default.
    // Let's rename it to remove that.
    if (ld_debug_out)
    {
        char *old_name;
        int num_bytes = asprintf(&old_name, "%s.%d", ld_debug_out, cpid);
        CHECK_ASPRINTF_RET(num_bytes);
        int err = rename(old_name, ld_debug_out);
        if (err)
        {
            fprintf(stderr, "Failed to rename LD_DEBUG_OUTPUT file to ");
            fprintf(stderr, "its correct final name.\n");
            fprintf(stderr, "    %s\n", strerror(err));
        }
    }

    if (opf != NULL || fetchf != NULL)
    {
        printf("\nIBS sampling statistics:\n");
        printf("op_samples,op_samples_lost,fetch_samples,fetch_samples_lost\n");
        printf("%lu,%lu,%lu,%lu\n", n_op_samples, n_lost_op_samples,
                n_fetch_samples, n_lost_fetch_samples);
    }

    free(fds);
    exit(EXIT_SUCCESS);
}

static int fill_out_online_cores(int num_cpus, int num_online_cpus,
        char *cpu_list)
{
    // Skip opening the file if all CPUs are online.
    // Older Linux installs this will always be true and the
    // 'online' file won't exist.
    if (num_cpus == num_online_cpus)
    {
        for (int i = 0; i < num_cpus; i++)
        {
            cpu_list[i] = 1;
        }
        return num_online_cpus;
    }

    FILE *online_fp = fopen("/sys/devices/system/cpu/online", "r");
    if (online_fp == NULL)
    {
        fprintf(stderr, "Could not open /sys/devices/system/cpu/online\n");
        exit(EXIT_FAILURE);
    }

    char *online_cpus = NULL;
    size_t len = 0;
    ssize_t line_len = getline(&online_cpus, &len, online_fp);
    if (line_len == -1 || len == 0)
    {
        fprintf(stderr, "Failed to read the number of online CPUs\n");
        exit(EXIT_FAILURE);
    }

    int i = 0;
    int previous_cpu = -1;
    int num_online_seen = 0;
    while (online_cpus[i] != '\0' && online_cpus[i] != '\n')
    {
        if (online_cpus[i] == ',')
        {
            i++;
            continue;
        }
        else if (online_cpus[i] == '-')
        {
            // This only fills in the missing ones. The end-points
            // are filled in during the normal numbers.
            int next_cpu = atoi(&(online_cpus[i+1]));
            for (int j = previous_cpu + 1; j < next_cpu; j++)
            {
                num_online_seen++;
                cpu_list[j] = 1;
            }
            while(isdigit(online_cpus[i]))
                i++;
            previous_cpu = next_cpu;
        }
        else
        {
            int cpu_num = atoi(&(online_cpus[i]));
            if (cpu_num < num_cpus)
            {
                num_online_seen++;
                cpu_list[cpu_num] = 1;
                previous_cpu = cpu_num;
            }
            else
                break;
        }
        i++;
        if (num_online_seen >= num_online_cpus)
            break;
    }
    fclose(online_fp);
    return previous_cpu;
}

/**
 * enable_ibs_flavors - turn on IBS where possible
 * @fds:    (output) file descriptors and events of interest for poll
 * @nopfds: (output) number of op file descriptors successfully set up
 * @nfetchfds:  (output) number of fetch file descriptors successfully set up
 * @flavors:    IBS_OP, IBS_FETCH, or IBS_BOTH
 */
void enable_ibs_flavors(struct pollfd *fds, int *nopfds, int *nfetchfds,
                        int flavors)
{
    char filename [64];
    int count = 0;
    int cpu;

    n_op_samples = 0;
    n_fetch_samples = 0;
    n_lost_op_samples = 0;
    n_lost_fetch_samples = 0;

    int num_cpus = get_nprocs_conf();
    char *cpu_list = calloc(num_cpus, sizeof(char));
    int num_online_cpus = get_nprocs();
    fill_out_online_cores(num_cpus, num_online_cpus, cpu_list);

    *nopfds = 0;
    if (flavors & IBS_OP) {
        for (cpu = 0; cpu < num_cpus; cpu++) {
            if (!cpu_list[cpu])
                continue;

            sprintf(filename, "/dev/cpu/%d/ibs/op", cpu);
            fds[count].fd = open(filename, O_RDONLY | O_NONBLOCK);
            if (fds[count].fd < 0) {
                fprintf(stderr, "Could not open %s\n", filename);
                continue;
            }

            ioctl(fds[count].fd, SET_BUFFER_SIZE, buffer_size);
            ioctl(fds[count].fd, SET_POLL_SIZE,
                  poll_size / sizeof(ibs_op_t));
            ioctl(fds[count].fd, SET_MAX_CNT, op_cnt_max_to_set);
            if (ioctl(fds[count].fd, IBS_ENABLE)) {
                fprintf(stderr, "IBS op enable failed on cpu %d\n",
                        cpu);
                continue;
            }

            fds[count].events = POLLIN | POLLRDNORM;
            (*nopfds)++;
            count++;
        }
    }

    *nfetchfds = 0;
    if (flavors & IBS_FETCH) {
        for (cpu = 0; cpu < num_cpus; cpu++) {
            if (!cpu_list[cpu])
                continue;

            sprintf(filename, "/dev/cpu/%d/ibs/fetch", cpu);
            fds[count].fd = open(filename, O_RDONLY | O_NONBLOCK);
            if (fds[count].fd < 0) {
                fprintf(stderr, "Could not open %s\n", filename);
                continue;
            }

            ioctl(fds[count].fd, SET_BUFFER_SIZE, buffer_size);
            ioctl(fds[count].fd, SET_POLL_SIZE,
                  poll_size / sizeof(ibs_fetch_t));
            ioctl(fds[count].fd, SET_MAX_CNT, fetch_cnt_max_to_set);
            if (ioctl(fds[count].fd, IBS_ENABLE)) {
                fprintf(stderr, "IBS fetch enable failed on cpu %d\n",
                        cpu);
                continue;
            }

            fds[count].events = POLLIN | POLLRDNORM;
            (*nfetchfds)++;
            count++;
        }
    }

    if (cpu_list)
        free(cpu_list);
}

/**
 * reset_ibs_buffers
 */
void reset_ibs_buffers(const struct pollfd *fds, int nfds)
{
    for (int i = 0; i < nfds; i++)
        ioctl(fds[i].fd, RESET_BUFFER);
}

static inline void read_and_write_op_data(int fd, FILE *fp)
{
    int tmp = 0;
    int num_items = 0;

    tmp = read(fd, global_buffer, buffer_size);
    if (tmp <= 0)
        return;
    num_items = tmp / sizeof(ibs_op_t);

    if (fp != NULL)
    {
        tmp = fwrite(global_buffer, sizeof(ibs_op_t), num_items, fp);
        if (tmp < num_items)
            fprintf(stderr, "Failed to write %d samples\n",
                    num_items - tmp);
    }

    n_op_samples += num_items;
    n_lost_op_samples += ioctl(fd, GET_LOST);
}

static inline void read_and_write_fetch_data(int fd, FILE *fp)
{
    int tmp;
    int num_items = 0;

    tmp = read(fd, global_buffer, buffer_size);
    if (tmp <= 0)
        return;
    num_items = tmp / sizeof(ibs_fetch_t);

    if (fp != NULL)
    {
        tmp = fwrite(global_buffer, sizeof(ibs_fetch_t), num_items, fp);
        if (tmp < num_items)
            fprintf(stderr, "Failed to write %d samples\n",
                    num_items - tmp);
    }

    n_fetch_samples += num_items;
    n_lost_fetch_samples += ioctl(fd, GET_LOST);
}

/**
 * poll_ibs - collect data and write it to some files
 */
void poll_ibs(struct pollfd *fds, int nopfds, int nfetchfds, FILE *opf,
              FILE *fetchf)
{
    int tmp;
    int i;

    /* Wait up to POLL_TIMEOUT if nothing is ready */
    tmp = poll(fds, nopfds + nfetchfds, poll_timeout);
    if (tmp == -1) {
        perror("poll()");
        exit(EXIT_FAILURE);
    } else if (tmp == 0) {
        return;
    }

    /* Something is ready */
    for (i = 0; i < nopfds; i++) {
        if (fds[i].revents)
            read_and_write_op_data(fds[i].fd, opf);
    }
    for (i = nopfds; i < (nopfds + nfetchfds); i++) {
        if (fds[i].revents)
            read_and_write_fetch_data(fds[i].fd, fetchf);
    }
}

/**
 * flush_ibs_buffers - read all data from all fds
 */
void flush_ibs_buffers(const struct pollfd *fds, int nopfds, int nfetchfds,
                       FILE *opf, FILE *fetchf)
{
    int i;

    for (i = 0; i < nopfds; i++)
        read_and_write_op_data(fds[i].fd, opf);
    for (i = nopfds; i < (nopfds + nfetchfds); i++)
        read_and_write_fetch_data(fds[i].fd, fetchf);
}

/**
 * disable_ibs
 */
void disable_ibs(const struct pollfd *fds, int nfds)
{
    for (int i = 0; i < nfds; i++) {
        ioctl(fds[i].fd, IBS_DISABLE);
        close(fds[i].fd);
    }
}
