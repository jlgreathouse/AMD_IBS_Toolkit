/*
 * Copyright (C) 2015-2017 Advanced Micro Devices, Inc.
 *
 * This is a simple application that uses libIBS to enable IBS and continually
 * read IBS samples out of the driver until the user interrupts it.
 * The IBS samples are not printed -- this application only tells you whether
 * it is successfully pulling samples out of the driver.
 *
 * This file is distributed under the BSD license described in tools/LICENSE
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>

#include "ibs.h"

// This program will attempt to read values out of the IBS driver to see that
// it is properly creating IBS samples and allowing users to read them.
// In the middle of the program, while trying to repeatedly read IBS samples
// from the driver, some useless work is performed in order to create
// pipeline operations for the IBS samples to describe.
int main(int argc, char *argv[])
{

    // By default, we will try to read 10 chunks of samples from the IBS driver
    // However, a command-line argument allows you to pick the number of chunks
    // to read, or (if you pass in a negative number) to read forever.
    int num_rounds = 10;
    if (argc > 2)
    {
        fprintf(stderr, "Too many arguments. Syntax: %s "
            "[optional number of IBS sample reads to try]\n", argv[0]);
        return -1;
    }
    else if (argc <= 1)
    {
        printf("No number of IBS read tries requested. Using default: %d\n",
                num_rounds);
    }
    else
    {
        num_rounds = atoi(argv[1]);
        if (num_rounds > 0)
            printf("Attempting to get IBS samples %d times\n", num_rounds);
        else
        {
            num_rounds = -1;
            printf("Attempting to get IBS samples until this program is "
                    "killed.\n");
        }
    }

    // Get the number of online CPUs and enable all of them in the IBS mask.
    // The simplest way to do this is to allocate a char list with the maximum
    // number of cores and set them all to one
    int max_cores = get_nprocs_conf();
    char *core_map = calloc(max_cores, sizeof(char));
    //memset(core_map, 1, max_cores);
    //^^^^^^^^^^^^ Add this back in if you remove the vvvvvv optional section

    // ============================ OPTIONAL ===================================
    // The following section shows a method for getting the number of cores
    // that are currently enabled. If you know you won't be hotplugging
    // cores, feel free to skip doing this.

    // Run the following command to get a list of all the online processors:
    //lscpu -p=cpu | grep -v \#
    FILE *command_pipe;
    char list_of_cpus[1024];
    memset(core_map, 0, max_cores);
    command_pipe = popen("lscpu -p=cpu | grep -v \\#", "r");
    if (command_pipe == NULL)
    {
        fprintf(stderr, "Could not run lscpu in order to get online CPUs\n");
        exit(-1);
    }
    while (fgets(list_of_cpus, sizeof(list_of_cpus)-1, command_pipe) != NULL)
    {
        int core_num = atoi(list_of_cpus);
        core_map[core_num] = 1;
    }
    if (pclose(command_pipe) != 0)
    {
        // lscpu failed. Let's try again the hard way.
        printf("lscpu does not exist. Directly probing /proc/cpuinfo...\n");
        command_pipe = popen("cat /proc/cpuinfo | grep \"processor\" | awk '{print $3}'", "r");
        if (command_pipe == NULL)
        {
            fprintf(stderr, "Could not probe /proc/cpuinfo in order to get online CPUs\n");
            exit(-1);
        }
        while (fgets(list_of_cpus, sizeof(list_of_cpus)-1, command_pipe) != NULL)
        {
            int core_num = atoi(list_of_cpus);
            core_map[core_num] = 1;
        }
        pclose(command_pipe);
    }
    // ============================ OPTIONAL ===================================


    // The configuration list below shows some basic initialization parameters
    // for the IBS system, as passed through libIBS.
    //
    // Once IBS is enabled, the IBS hardware will count every Nth instruction
    // or op, as set by 16*IBS_MAX_CNT (with some low-order randomization).
    //
    // Whenever one of these samples is taken, it is saved into a kernel-level
    // ring buffer that can be read out from the IBS driver device.
    //
    // It is possible to oll() on this device so that you wait for a certain
    // number of IBS samples or a certain timeout before reading it, to prevent
    // polling overheads from polluting the IBS trace and causing slowdowns.
    // The IBS_POLL_TIMEOUT sets how long you should wait, while
    // IBS_POLL_NUM_SAMPLES sets how many samples should be in the IBS buffer
    // before a poll() call says the device is ready.
    // Setting IBS_READ_ON_TIMEOUT will go ahead and read however many
    // IBS samples are available after a timeout, even if it's less than
    // IBS_POLL_NUM_SAMPLES.
    //
    // The IBS_CPU_LIST is an array that tells libIBS which CPUs to enable
    // IBS on.
    //
    // Finally, IBS op and fetch sampling can be enabled/disabled
    // separately using IBS_OP and IBS_FETCH. They are both enabled by default.

    ibs_option_list_t opts[] = 
    {
        //{IBS_DEBUG, (ibs_val_t)1}, // Uncomment this to enable library debug
        {IBS_POLL_TIMEOUT, (ibs_val_t)1}, // millisecond to wait for IBS samples
        {IBS_POLL_NUM_SAMPLES, (ibs_val_t)2}, // Wait until this number of IBS
                                              // samples exist before reading
        {IBS_READ_ON_TIMEOUT, (ibs_val_t)1}, // If we have a timeout, try
                                             // reading anything in the buffer
                                             // even if we don't have
                                             // IBS_POLL_NUM_SAMPLES in there.
        {IBS_CPU_LIST, (ibs_val_t)core_map}, // Array of CPUs to use for IBS
        {IBS_MAX_CNT, (ibs_val_t)1024}, // 16*{this val} is roughly how many
                                        // instructions/ops between samples
        {IBS_OP, (ibs_val_t)1}, // Enable IBS op sampling
        {IBS_FETCH, (ibs_val_t)0}, // Disable IBS fetch sampling
    };


    int status;

    // Start by initializing the IBS system using the options above.
    status = ibs_initialize(opts, sizeof(opts)/sizeof(ibs_option_list_t), 0);
    if (status < 0) {
        fprintf(stderr, "Could not initialize IBS. %d\n", status);
        return status;
    }

    // Enable IBS on all of the CPUs that were initialized.
    status = ibs_enable_all();
    if (status < 0) {
        fprintf(stderr, "Could not enable IBS on all CPUs. %d\n", status);
        return status;
    }

    // Try to gather num_rounds chunks of IBS samples out of the driver.
    // Do some work between each read so there are samples buffered up.
    // (if num_rounds is < 0, do this forever.
    for (int i = 0; i < num_rounds || num_rounds < 0; i++)
    {
        int rand_val = rand();
        /* Get some samples */
        ibs_sample_t samples[1024];
        ibs_sample_type_t types[1024];
        int num_samples;

        // Artificial work so that there are some op samples to read
        for (int j = 0; j < 1000000; j++)
            rand_val += (rand_val * rand_val);

        num_samples = ibs_sample(sizeof(samples)/sizeof(ibs_sample_t),
            IBS_OP_SAMPLE | IBS_FETCH_SAMPLE,
            (ibs_sample_t *)samples, (ibs_sample_type_t *)types);

        if (status < 0) {
            fprintf(stderr, "Could not get an IBS sample! %d\n", status);
            ibs_finalize();
            return status;
        }

        printf("Got %d samples!\n", num_samples);

        // Do this useless printout so the compiler doesn't destroy are
        // useless artificial work.
        FILE *devnull = fopen("/dev/null", "w");
        fprintf(devnull, "Needless random value: %d\n", rand_val);
        fclose(devnull);
    }

    // Call IBS finalize after you're done to shut down cleanly.
    ibs_finalize();

    return 0;
}
