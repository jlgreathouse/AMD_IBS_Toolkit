/*
 * Copyright (C) 2015-2017 Advanced Micro Devices, Inc.
 *
 * This application demonstrates how to use the LibIBS "daemon" mode to
 * automatically gather samples while other things happen within your
 * applications. This prevents the need for users to manually launch an
 * IBS monitoring thread, etc. whenever they want to gather IBS samples
 * from within their application.
 *
 * This file is distributed under the BSD license described in tools/LICENSE
 */

 /* This application shows how to use libIBS to write your own custom program
    monitoring tool. By setting up a daemon in the background, it's possible
    to run a custom function every time a series of IBS samples are read in. */
#define _GNU_SOURCE
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include "ibs.h"

/* Emperically, this max count gives us something like 1500 samples 
 * per second on each CPU  */
#define IBS_WATCH_MAX_CNT           0x2ffff
#define IBS_WATCH_SELECT_TIMEOUT    1000
#define IBS_WATCH_SELECT_SAMPLES    512

#define NR_CPUS                     sysconf(_SC_NPROCESSORS_ONLN)
#define IBS_WATCH_MAX_SAMPLES       ((int)(IBS_WATCH_SELECT_SAMPLES * (1.1)) * NR_CPUS)

static int header_written = 0;
static char *global_op_file = NULL;
static char *global_work_dir = NULL;

static void write_header(FILE * fp)
{
    int off = 0;
    char buf[1000] = "";
    
    {
        char hostname[32];
        gethostname(hostname, 32);
        fprintf(fp, "%s\n", hostname);
    }

    off = sprintf(buf, "tsc,");
    off += sprintf(buf + off, "cpu,");
    off += sprintf(buf + off, "pid,");
    off += sprintf(buf + off, "tid,");
    off += sprintf(buf + off, "kern_mode,");
    off += sprintf(buf + off, "data,");
    off += sprintf(buf + off, "data2,");
    sprintf(buf + off, "data3");

    fprintf(fp, "%s\n", buf);
}

static void write_op_misc(ibs_op_t * op, char *buf, int *off)
{
    *off = sprintf(buf, "%" PRIu64, op->tsc);
    *off += sprintf(buf + *off, ",%d", op->cpu);
    *off += sprintf(buf + *off, ",%d", op->pid);
    *off += sprintf(buf + *off, ",%d", op->tid);
    *off += sprintf(buf + *off, ",%d", op->kern_mode);
}

static void write_op_sample(FILE *fp, ibs_op_t *op)
{
    char buf[1000] = "";
    int off = 0;

    // Check for header here, because the IBS Daemon actually opens the
    // output file. As such, we can't write it until the daemon calls
    // the op sample function.
    if (header_written == 0) {
        write_header(fp);
        header_written = 1;
    }

    write_op_misc(op, buf, &off);
    off += sprintf(buf + off, ",0x%" PRIx64, op->op_data.val);
    off += sprintf(buf + off, ",0x%" PRIx64, op->op_data2.val);
    off += sprintf(buf + off, ",0x%" PRIx64, op->op_data3.val);

    fprintf(fp, "%s\n", buf);
}

// Launch a child process and wait around until it completes.
static void launch_child_work(cpu_set_t *procs, char *argv[])
{
    pid_t cpid = fork();
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
                fprintf(stderr, "Unable to change working direcotry to: %s\n",
                        global_work_dir);
                fprintf(stderr, "    %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
        }

        sched_setaffinity(0, sizeof(cpu_set_t), procs);
        if (execvp(argv[0], &argv[0]) == -1) {
            fprintf(stderr, "Unable to execution application: %s\n", argv[0]);
            fprintf(stderr, "    %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        exit(EXIT_SUCCESS);
    }
    waitpid(cpid, NULL, 0);
}

static void parse_args(int argc, char *argv[])
{
    static struct option longopts[] =
    {
        {"op_file", required_argument, NULL, 'o'},
        {"working_dir", required_argument, NULL, 'w'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    char c;
    while ((c = getopt_long(argc, argv, "+ho:w:", longopts, NULL)) != -1)
    {
        switch (c) {
            case 'h':
                fprintf(stderr, "This program executes another program and\n");
                fprintf(stderr, "collects IBS samples during its execution.\n");
                fprintf(stderr, "Usage: ./ibs_monitor [-o op_output] [-w working_directory] program_to_run [...]\n");
                fprintf(stderr, "--working_dir (or -w):\n");
                fprintf(stderr, "       Sets the working direcotry for launching the program to monitor.\n");
                fprintf(stderr, "--op_file (or -o):\n");
                fprintf(stderr, "       File to which to save IBS op samples\n");
                fprintf(stderr, "If you skip setting the file, IBS sampling will be disabled.\n\n");
                exit(EXIT_SUCCESS);
            case 'o':
                global_op_file = optarg;
                break;
            case 'w':
                global_work_dir = optarg;
                break;
            case '?':
            default:
                fprintf(stderr, "Found this bad argument: %s\n", argv[optind]);
                break;
        }
    }
}

static void bad_exit(int sig)
{
    (void)sig;
    if (global_op_file != NULL)
    {
        ibs_finalize();
        // Give the daemon some time to shut down and write its data.
        usleep(3 * 1000 * IBS_WATCH_SELECT_TIMEOUT);
    }
    exit(EXIT_FAILURE);
}

static void clean_exit(void *cpu_list, void *daemon_cpu_list)
{
    if (global_op_file != NULL)
    {
        ibs_finalize();
        // Give the daemon some time to shut down and write its data.
        usleep(2 * 1000 * IBS_WATCH_SELECT_TIMEOUT);
    }
    if (cpu_list)
        free(cpu_list);
    if (daemon_cpu_list)
        free(daemon_cpu_list);
    exit(EXIT_SUCCESS);
}

int fill_out_online_cores(int num_cpus, int num_online_cpus, char *cpu_list,
        cpu_set_t *procs)
{
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
            int next_cpu = online_cpus[i+1] - '0';
            for (int j = previous_cpu + 1; j < next_cpu; j++)
            {
                num_online_seen++;
                cpu_list[j] = 1;
                CPU_SET(j, procs);
            }
            previous_cpu = next_cpu;
        }
        else
        {
            int cpu_num = online_cpus[i] - '0';
            if (cpu_num < num_cpus)
            {
                num_online_seen++;
                cpu_list[cpu_num] = 1;
                CPU_SET(cpu_num, procs);
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


/* This application will take in another program to run, launch it on a subset of
 * the cores on a system, and monitor it with IBS while it runs. One of the cores
 * on the system is reserved for the tool that reads IBS samples and prints them
 * to the screen -- the target application will not run there.
 * This tool uses libIBS to perform this monitoring. This is an example
 * application to show how to write a simple custom user-level IBS handler
 * without needing to talk to the driver directly.
 * If your IBS sample handlers (e.g. IBS_DAEMON_OP_WRITE function) are not
 * heavyweight, you likely won't need the complexity of keeping a separate
 * core for that work. */
int main(int argc, char *argv[])
{
    parse_args(argc, argv);
    // Reset argv to real program
    argv = &(argv[optind]);

    // Create the list of which CPUs to use for IBS sampling.
    // Start by allocating a char array, one char for each possible CPU
    int num_cpus = get_nprocs_conf();
    char *cpu_list = calloc(num_cpus, sizeof(char));
    char *daemon_cpu_list = calloc(num_cpus, sizeof(char));

    // Now, how many CPUs are online?
    int num_online_cpus = get_nprocs();
    if (num_online_cpus < 2)
    {
        fprintf(stderr, "ERROR. Online 1 CPU core is online.\n");
        fprintf(stderr, "We need one for the daemon and one for the app.\n");
        if (cpu_list)
        {
            free(cpu_list);
            cpu_list = NULL;
        }
        if (daemon_cpu_list)
        {
            free(daemon_cpu_list);
            daemon_cpu_list = NULL;
        }
        return EXIT_FAILURE;
    }
    cpu_set_t procs;
    CPU_ZERO(&procs);
    int last_online_core = fill_out_online_cores(num_cpus, num_online_cpus,
            cpu_list, &procs);
    if (last_online_core < 1)
    {
        fprintf(stderr, "Failed to get any good online cores.\n");
        fprintf(stderr, "Need at least two online cores!\n");
        if (cpu_list)
        {
            free(cpu_list);
            cpu_list = NULL;
        }
        if (daemon_cpu_list)
        {
            free(daemon_cpu_list);
            daemon_cpu_list = NULL;
        }
        return EXIT_FAILURE;
    }
    cpu_list[last_online_core] = 0;
    CPU_CLR(last_online_core, &procs);
    daemon_cpu_list[last_online_core] = 1; // Last CPU for IBS

    // If the user has set an output file for IBS on the command line, we
    // should turn on all the IBS stuff. Otherwise, skip straight to the
    // real work and avoid IBS entirely.
    if (global_op_file != NULL)
    {
        int status, num_opts;
        // Need to do this so that we build cleanly. Cast the function pointer
        // for write_op_sample() into the type of pointer that we can pass in
        // with IBS_DAEMON_OP_WRITE.
        union
        {
            void (*write_op_fptr)(FILE *, ibs_op_t *);
            void *ptr;
        } func_ptr_cast;
        func_ptr_cast.write_op_fptr = write_op_sample;

        ibs_option_list_t opts[] = {
            // Turn on debug printing in this test program.
            {IBS_DEBUG,                 (ibs_val_t)1},
            // Turn on IBS op sampling.
            {IBS_OP,                    (ibs_val_t)1},
            // Set some various configuration parameters for taking IBS samples
            {IBS_POLL_NUM_SAMPLES,      (ibs_val_t)IBS_WATCH_SELECT_SAMPLES},
            {IBS_POLL_TIMEOUT,          (ibs_val_t)IBS_WATCH_SELECT_TIMEOUT},
            {IBS_READ_ON_TIMEOUT,       (ibs_val_t)0},
            {IBS_MAX_CNT,               (ibs_val_t)IBS_WATCH_MAX_CNT},
            // Pass in the list of CPUs the actual application will run on, and
            // thus the cores we should read IBS traces from.
            {IBS_CPU_LIST,              (ibs_val_t)cpu_list},
            {IBS_DAEMON_MAX_SAMPLES,    (ibs_val_t)IBS_WATCH_MAX_SAMPLES},
            // Which core to run the IBS monitoring daemon on.
            {IBS_DAEMON_CPU_LIST,       (ibs_val_t)daemon_cpu_list},
            // File name to write op traces out to
            {IBS_DAEMON_OP_FILE,        (ibs_val_t)global_op_file},
            // Function that will write out op traces.
            {IBS_DAEMON_OP_WRITE,       (ibs_val_t)func_ptr_cast.ptr},
        };
        num_opts = sizeof(opts) / sizeof(ibs_option_list_t);

        // Once we've called initialize, attempts to exit this program should
        // kill the daemon with ibs_finalize(), or it's going to sit around forever.
        signal(SIGINT, bad_exit);
        signal(SIGQUIT, bad_exit);
        signal(SIGTERM, bad_exit);
        signal(SIGHUP, bad_exit);

        status = ibs_initialize(opts, num_opts, 1);
        if (status != 0)
        {
            if (cpu_list)
            {
                free(cpu_list);
                cpu_list = NULL;
            }
            if (daemon_cpu_list)
            {
                free(daemon_cpu_list);
                daemon_cpu_list = NULL;
            }
            return status;
        }
    }

    // Do the real work now.
    launch_child_work(&procs, argv);

    // After the child has come back, stop the IBS daemon and quick.
    clean_exit(cpu_list, daemon_cpu_list);
    if (cpu_list)
    {
        free(cpu_list);
        cpu_list = NULL;
    }
    if (daemon_cpu_list)
    {
        free(daemon_cpu_list);
        daemon_cpu_list = NULL;
    }
    return EXIT_SUCCESS;
}
