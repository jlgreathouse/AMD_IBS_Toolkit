/*
 * Copyright (C) 2015-2017 Advanced Micro Devices, Inc.
 *
 * This file is distributed under the BSD license described in tools/LICENSE
 */

#ifndef IBS_MONITOR_H
#define IBS_MONITOR_H

#define CHECK_ASPRINTF_RET(num_bytes) \
{ \
    if (num_bytes <= 0) \
    { \
        fprintf(stderr, "asprintf in %s:%d failed\n", __FILE__, __LINE__); \
            exit(-1); \
    }\
}

// Defines to help the rest of the program differentiate op and fetch samples.
#define IBS_OP      1
#define IBS_FETCH   2
#define IBS_BOTH    (IBS_OP | IBS_FETCH)

// IBS sampling sizes for IBS op and IBS fetch devices.
// If your program has too much slowdown from IBS, you should increase these
// If you are not getting enough samples to observe interesting things in
// your application, decrease these.
// Op sampling interval
#define OP_MAX_CNT  0x4000  // This will sample roughly 1 out of ~262,000 ops
                            // 16 * 0x4000 (16,384) = 262,144
// Fetch sampling interval
#define FETCH_MAX_CNT   0x1000  // This will sample roughly 1/64k instructions
                                // 16 * 0x1000 (4096) = 65,536

// The buffer size (in bytes) that the driver will use to hold the IBS samples.
// This is a *per core* value.
// The larger this is, the less likely a sample will be stomped before the
// user-level application can bulk read the data out. However, larger buffers
// obviously take up more kernel memory.
#define BUFFER_SIZE_B   (1 << 20)   // 1 MB by default. Increase this if you
                                    // keep getting lost samples.

// Max portion of the IBS driver's buffer that should be full before it wakes
// up the process after a poll(), and says "you should read this data now."
#define POLL_SIZE_PERCENT    75
// Max time (in ms) to wait (poll()) on the IBS driver befor reading out
// data. This prevents needless overhead from the monitor application spinning
// on the read() command.
#define POLL_TIMEOUT    1000

// The IBS Monitor application uses a number of global variables to hold things
// like the file handler outputs, the op and fetch sample rates, and the size
// of the kernel buffer that it will request from the IBS driver.
// This function sets them all to their default values (which are defined
// by the macros above in this .h file
void set_global_defaults(void);

// The following functions will set the global variables, and include some
// limit and error checking.

// Arg 1: Input -   Filename string.
// Arg 2: Output -  Pointer to the FILE* to initialize with this function.
// Arg 3: Output -  Pointer to the variable that will have IBS_OP/FETCH
//                  Added to it.
void set_op_file(char *opt, FILE **opf, int *flavors);
void set_fetch_file(char *opt, FILE **fetchf, int *flavors);
// The sample rates here are the actual sample rates you want. However, the
// bottom 4 bits of what you put in will be randomized in the driver.
void set_global_op_sample_rate(int sample_rate);
void set_global_fetch_sample_rate(int sample_rate);
// Buffer size in KB
void set_global_buffer_size(int in_buffer_size);
void set_global_poll_percent(int in_poll_percent);
// Timeout in ms
void set_global_poll_timeout(int in_poll_timeout);


// Call this early in the application in order to parse the command line
// arguments and fill out global variables, open outputs, etc. etc.
void parse_args(int argc, char *argv[], FILE **opf, FILE **fetchf,
        int *flavors);

/**
 * enable_ibs_flavors - turn on IBS where possible
 * @fds:    (output) file descriptors and events of interest for poll
 * @nopfds: (output) number of op file descriptors successfully set up
 * @nfetchfds:  (output) number of fetch file descriptors successfully set up
 * @flavors:    IBS_OP, IBS_FETCH, or IBS_BOTH
 */
void enable_ibs_flavors(struct pollfd *fds, int *nopfds, int *nfetchfds,
                        int flavors);
void reset_ibs_buffers(const struct pollfd *fds, int nfds);
void poll_ibs(struct pollfd *fds, int nopfds, int nfetchfds, FILE *opf,
              FILE *fetchf);
void flush_ibs_buffers(const struct pollfd *fds, int nopfds, int nfetchfds,
                       FILE *opf, FILE *fetchf);
void disable_ibs(const struct pollfd *fds, int nfds);

#endif        /* IBS_MONITOR_H */
