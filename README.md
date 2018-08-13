AMD Research Instruction Based Sampling Toolkit
===============================================================================

This repository contains tools which can be used to access the Instruction Based Sampling (IBS) mechanism in AMD microprocessors from Families 10h, 12h, 15h, 16h, and 17h. IBS is a hardware mechanism that samples a subset of all of the instructions going through a processor. For each sampled instruction, a large amount of information is gathered and saved off as a program runs.

This toolkit includes a Linux&reg; kernel driver that helps gather these IBS samples, a user-level application to parse the raw binary dumped by the driver, and a helper application which will run other programs and collect IBS traces about them.

This toolkit was written by AMD Research as a simplified way to gather IBS samples on a wide range of Linux systems. Newer Linux kernels (Beginning in the 3.2 timeframe) have support for IBS as part of the perf\_events system. This toolkit offers a simplified interface to the IBS system, but it also includes a set of directions ([ibs\_with\_perf\_events.txt](ibs_with_perf_events.txt)) for implementing the same functionality the "official" way. In essence, this toolkit may be useful for prototyping a system that uses IBS, which can later be ported to use perf\_events.

Table of Contents
--------------------------------------------------------------------------------

   * [AMD Research IBS Toolkit File Structure](#amd-research-ibs-toolkit-file-structure)
      * [The AMD Research IBS Driver](#the-amd-research-ibs-driver)
      * [A library to configure IBS and read IBS samples](#a-library-to-configure-ibs-and-read-ibs-samples)
      * [A collection of user-level tools to gather and analyze IBS samples](#a-collection-of-user-level-tools-to-gather-and-analyze-ibs-samples)
         * [An application that tests the IBS driver](#an-application-that-tests-the-ibs-driver)
         * [An IBS monitoring program](#an-ibs-monitoring-program)
         * [An application to decode binary IBS dumps](#an-application-to-decode-binary-ibs-dumps)
         * [An application to match IBS samples with their instructions](#an-application-to-match-ibs-samples-with-their-instructions)
         * [An application that uses the libIBS daemon](#an-application-that-uses-the-libibs-daemon)
   * [Building and Installing the AMD Research IBS Driver and Toolkit](#building-and-installing-the-amd-research-ibs-driver-and-toolkit)
   * [AMD Research IBS Toolkit Compatibility](#amd-research-ibs-toolkit-compatibility)
   * [Using the AMD Research IBS Toolkit](#using-the-amd-research-ibs-toolkit)
   * [Background on Instruction Based Sampling](#background-on-instruction-based-sampling)
   * [Trademark Attribution](#trademark-attribution)
      * [Background References](#background-references)

AMD Research IBS Toolkit File Structure
--------------------------------------------------------------------------------

The AMD Research IBS Toolkit is split into three major pieces, each of which is licensed separately. These three pieces are:
### The AMD Research IBS Driver ###
* Located in [./driver/](driver)
* This is a Linux&reg; kernel driver that allows access to IBS traces.
* It is licensed under [GPLv2](driver/LICENSE), with the same caveats as any other Linux kernel license.
*  When installed, this will create two new devices per CPU core:
    - /dev/cpu/<cpu\_id>/ibs/fetch
    - /dev/cpu/<cpu\_id>/ibs/op
* These two devices can be read using poll and read commands. In addition, there are a number if ioctl commands that can be used to configure and query information about the devices.
    - The structs used by reads, and the ioctl commands, are defined in: [./include/ibs-uapi.h](include/ibs-uapi.h)
    - A list of bit value locations from the AMD manuals that describe individual entries in each IBS reading are contained in: [./include/ibs-msr-index.h](include/ibs-msr-index.h)
        - These last two files are dual licensed. You can choose to use them under the [GPLv2](include/LICENSE.gpl) or under a [3-clause BSD license](include/LICENSE.bsd).

### A library to configure IBS and read IBS samples ###
* Located in [./lib/](lib)
* It is licensed under a [3-clause BSD license](lib/LICENSE).
* This library allows user-level programs to easily configure the IBS driver.
    - This includes enabling and disabling IBS, setting driver options such as internal IBS buffer sizes, and setting hardware configuration values.
* This library is also useful for reading IBS samples into meaningful data structures and making them available to other applications.
* This library also has a daemon mode, where a user program can launch an IBS-sample-reading daemon in the background that will dump IBS samples into a file while the regular program runs.

### A collection of user-level tools to gather and analyze IBS samples ###
* Located in [./tools/](tools)
* All of this software is licensed under a [3-clause BSD license](tools/LICENSE).

#### An application that tests the IBS driver ####
* Located in [./tools/ibs\_test/](tools/ibs_test)
* This application checks to see if the AMD Research IBS Driver is installed and configurable. It attempts to open the op sampling device and read samples. It does nothing with these samples.
* The application takes one argument: the number of times to attempt to read IBS samples from the driver before quitting. This is set  by the optional argument to the application.
    - 0 or a negative value for this means "run until killed".

#### An IBS monitoring program ####
* Located in [./tools/ibs\_monitor/](tools/ibs_monitor)
* This application is a wrapper that enables IBS tracing in our driver, runs a target program, and saves off IBS traces into designated files until the target program ends. Afterwards, it disables IBS tracing.
* Essentially, this gathers IBS traces for other programs.

#### An application to decode binary IBS dumps ####
* Located in [./tools/ibs\_decoder/](tools/ibs_decoder)
* By default, the ibs\_monitor application will dump full IBS traces directly to files without doing any decoding on them. This is to prevent the decoding work from interrupting or slowing down the application under test.
* The ibs\_decoder application will read in these binary traces that are essentially dumps of the IBS sample data structures and split them into easy-to-read CSV files.
* In addition, there is a script which will automatically convert these CSV files into R data structures, for further data analysis.

#### An application to match IBS samples with their instructions ####
* Located in [./tools/ibs\_run\_and\_annotate/](tools/ibs_run_and_annotate)
* This application will run the IBS monitor and IBS decoder applications above on a target application. It will automatically run the target application, gather IBS traces, and decode them to a CSV file.
* In addition, it will save enough information about the program's dynamically linked libraries to allow nearly all IBS samples to be "annotated" with the instruction that they represent. If the libraries and target application are built with debug symbols, this tool will also annotate the IBS samples with the line of code that produced the sampled instruction.
* The end result of this run is a new annotated CSV file of IBS samples that also includes the source line of code, offset into the binary or library, AMD64 opcode of the instruction, and a human-readable version of the instruction.

#### An application that uses the libIBS daemon ####
* Located in [./tools/ibs\_daemon/](tools/ibs_daemon)
* This is an example of how to use the [libIBS](lib) daemon to handle IBS sampling within an application. The daemon will start up another thread that will dump IBS traces to a file in a user-defined way.
* This application gathers a collection of op sample traces and dumps them to a small CSV file. It does this until the application ends.
* This is somewhat similar to what the ibs\_monitor application does, but this application demonstrates using the libIBS daemon and taking advantage of its ability to do user-defined handlers for IBS samples before spitting data out to a file.

Building and Installing the AMD Research IBS Driver and Toolkit
--------------------------------------------------------------------------------

Everything in the AMD Research IBS Toolkit can be built from the main directory using the command:

    make

This will build the driver, libIBS, and all of the tools. Alternately, it is also possible to go into each directory and use the `make` command to build only that tool.

The make command uses the CC and CXX environment variables to find its compiler, and it uses the system-wide `cc` and `c++` compilers by default. You can override these to use other compilers (e.g. `clang`), by running e.g.:

    CC=clang CXX=clang++ make

Note that this also allows Clang's scan-build by running:

    scan-build make

In addition, compilation can be done in parallel with `make -j {parallelism #}`

Finally, the `cppcheck` and `pylint` tools can be run on this repo with:

    make check

Before using any IBS-using tools, you should install the IBS driver that you have built. There is a helper script in the ./driver/ directory for this:

    ./driver/install_ibs_driver.sh

Note that, if you don't run this script with sudo, it will attempt to install the driver using a sudo command that will likely ask for your password. You may need to do this every time you boot the system, unless you add the ibs.ko module to your boot-time list of modules to load.

After installing the driver, you should see IBS nodes in the file system at
the following locations for each core ID <core\_id>:

1. /dev/cpu/<core\_id>/ibs/op
2. /dev/cpu/<core\_id>/ibs/fetch


To uninstall the IBS driver, you can either run:

    rmmod ibs

Or you can use the helper script at:

    ./drivers/remove_ibs_driver.sh

The user interface to the driver is documented in [./include/ibs-uapi.h](include/ibs-uapi.h). This file may be included by user application code. See [./tools/ibs\_monitor/](tools/ibs_monitor) for an example of how to interface with the driver.

AMD Research IBS Toolkit Compatibility
--------------------------------------------------------------------------------
This toolkit has been tested to compile and install on the following systems:
* CentOS 5.8 (Linux&reg; kernel 2.6.18-419)
    - Using gcc 4.1.2
* CentOS 6.4 (Linux kernel 2.6.32-358.23.2)
    - Using gcc 4.4.7, clang 3.4.2, cppcheck 1.63
* CentOS 7.3 (Linux kernel 3.10.0-514.10.2)
    - Using gcc 4.8.5, clang 3.4.2, cppcheck 1.75
* OpenSUSE 11.2 (Linux kernel 2.6.31.14-0.8)
    - Using gcc 4.4.1
* OpenSUSE Leap 42.2 (Linux kernel 4.4.49-16)
    - Using gcc 4.8.5, clang 3.8.0, cppcheck 1.70
* Ubuntu 9.04 (Linux kernel 2.6.28-11)
    - Using gcc 4.3.3
* Ubuntu 10.04 LTS (Linux kernel 2.6.32-21)
    - Using gcc 4.4.3
* Ubuntu 12.04.5 LTS (Linux kernel 3.13.0-113)
    - Using gcc 4.6.3, clang 3.0, cppcheck 1.52
* Ubuntu 14.04.1 LTS (Linux kernel 3.19.0)
    - Using gcc 4.8.2, clang 3.4, cppcheck 1.61, pylint 1.1.0
* Ubuntu 14.04.4 LTS (Linux kernel 4.2.0-34)
    - Using gcc 4.9.3, clang 3.5.0, cppcheck 1.61, pylint 1.1.0
* Ubuntu 16.04.2 LTS (Linux kernel 4.4.0-66)
    - Using gcc 5.4.0, clang 3.8.0, cppcheck 1.72, pylint 1.5.2
* Ubuntu 16.10 (Linux kernel 4.8.0-22)
    - Using gcc 6.2.0, clang 3.8.1, cppcheck 1.75
* Ubuntu 18.04.1 LTS (Linux kernel 4.15.0-20)
    - Using gcc 7.3.0, clang 6.0.0, cppcheck 1.81, pylint 1.8.3

In addition, it has been tested on the following processors, though its logic should work for any processors in AMD Families 10h, 12h, 14h, 15h, 16h, or 17h that support IBS:
* AMD Phenom&trade; II X4 B95
    - Family 10h Model 04h (Revision C)
* AMD Phenom&trade; II X6 1090T
    - Family 10h Model 0Ah (Revision E)
* AMD Opteron&trade; 4274 HE
    - Family 15h Model 01h (CPU formerly code-named "Bulldozer")
* AMD A8-5500 APU
    - Family 15h Model 10h (CPU formerly code-named "Piledriver")
* AMD A10-7850K APU
    - Family 15h Model 30h (CPU formerly code-named "Steamroller")
* AMD FX-8800P
    - Family 15h Model 60h (CPU formerly code-named "Excavator")
* AMD Ryzen&trade; 7 1800X
    - Family 17h Model 01h (CPU formerly code-named "Zen")
* AMD EPYC&trade; 7301
    - Family 17h Model 01h (CPU formerly code-named "Zen")
* AMD Ryzen 5 2400GE
    - Family 17h Model 11h (CPU formerly code-named "Zen")

Using the AMD Research IBS Toolkit
--------------------------------------------------------------------------------
The AMD Research IBS Toolkit includes most of the tools necessary to analyze applications using IBS. This includes the driver to access IBS, a monitoring application which automatically gathers IBS samples from an application under test, an application to decode these IBS samples into a human-readable format, and a tool to annotate these samples with application-level information about each instruction.

All of the directions here assume that the IBS driver, contained in [./driver/](driver), has been build and installed successfully.

The simplest mechanism to access IBS traces is the IBS Monitor application in [./tools/ibs\_monitor/](tools/ibs_monitor). This application allows users to pass a target application to be studied. The application will be run with system-wide IBS samples enabled, and the monitor will continually gather these until the program ends. In order to decrease the noise caused by saving these traces out to the target files, the monitor stores IBS traces in a raw format -- basically dumping the data structure directly to file.

After the trace has been gathered, the IBS decoder application can be used to decode these raw IBS traces into a human-readable CSV file. This application is found in [./tools/ibs\_decoder/](tools/ibs_decoder). This CSV file has one line per IBS sample, and each column describes one piece of information contained in that IBS sample.

An example of how to run the IBS Monitor and Decoder is as follows. These commands assume you are in the ./tools/ directory.

The following command will run the requested program with the given command line, and produce two IBS traces. One for Op samples (app.op) and one for Fetch samples (app.fetch).

    ./ibs_monitor/ibs_monitor -o app.op -f app.fetch ${program command line}

The following command will then decode the two IBS traces and save them into their respective CSV files:

    ./ibs_decoder/ibs_decoder -i app.op -o op.csv -f app.fetch -g fetch.csv

The follow command will run both of the above commands back-to-back and also annotate each IBS sample with information about the instruction that it sampled (such as its opcode and which line of code created it):

    ./tools/ibs_run_and_annotate/ibs_run_and_annotate -o -f -d ${output directory} -t ${temp directory} -w ${program working directory} -- ${program command line}

Background on Instruction Based Sampling
--------------------------------------------------------------------------------

AMD Instruction Based Sampling (IBS) is a hardware performance monitoring mechanism that is available on AMD CPUs starting with the Family 10h generation of cores (e.g. processors code-named "Barcelona" and "Shanghai" and Phenom&trade; II branded consumer CPUs were from this generation). It is supported on AMD CPUs up through and including the current Family 17h processors (e.g. the Ryzen&trade; branded consumer CPUs) with various features in each generation.

Traditionally, hardware performance counters increment whenever an event happens inside the CPU core. These events are counted whenever the core sees some event (such as a cache miss). This can lead to overcounting in cores that perform speculative, out-of-order execution, because the instruction that caused the event may never actually commit.

A related limitation of traditional performance counters becomes apparent when performing sampling. Traditional performance counters allow the application to be interrupted whenever a performance counter rolls over from '-1' to '0'. This is often referred to as event-based sampling, since it samples (interrupts on) every Nth event [1], depending on the initial negative value in the counter.

Event-based sampling allows developers to learn where in an applications events occur. However, out-of-order cores may not be able to precisely interrupt on the instruction that caused the Nth event (or, because of the reason mentioned above, may not even know which of many outstanding events is the Nth event). This produces a problem known as 'skid'. A developer that wants to know exactly which instruction causes an event will encounter many difficulties when using traditional performance counters in a speculative, out-of-order core [2].

AMD's solution to this problem is known as Instruction Based Sampling (IBS). In a nutshell, IBS tracks instructions rather than events (hence instruction-based sampling instead of event-based sampling). Every Nth instruction that goes through the core is 'marked'. As it flows through the pipeline, information about many events caused by that instruction are gathered. Then, when the instruction is completed, multiple pieces of information about that instruction's operation are available for logging [3, 4].

IBS on AMD processors is split into two parts: fetch sampling (front-end) and op sampling (back-end). AMD cores operate on AMD64/x86 instructions in the in-order front end of the processor. These are broken down into internal micro-operations for execution in the out-of-order back end of the processor. As such, IBS for front-end operations and IBS for back-end operations work in similar ways, but are completely separate from one another.

Fetch (front-end) sampling counts the number of completed (successfully sent to the decoder) fetches. After observing N fetches (where N is a programmable number), the next fetch attempt is sampled. Information about that fetch operation is gathered. When the fetch operation is either sent to the decoder (i.e. it completes) or is aborted (e.g. due to a page fault), the processor is interrupted and the IBS information about the sampled fetch is made available to the OS through a series of model-specific registers (MSRs).

Depending on the processor family, these Fetch IBS Samples can contain some or all of the following information:

* Whether the fetch completed successfully (i.e. was sent to the decoder)
* The latency (in clock cycles) from the beginning to end of the fetch
* Whether the instruction hit or miss in the L1 and L2 instruction caches
* The size of the virtual memory page that the fetch accessed
* Whether the fetch hit or missed in the L1 and L2 TLBs
* The latency of the TLB refill if there was a TLB miss
* The virtual and physical addresses accessed by the fetch

Op (back-end) sampling can be configured to count either the number of clock cycles or the number of dispatched micro-ops. In either case, once the programmable number of counts has taken place, the next micro-op is tagged. As that micro-op flows through the out-of-order back end of the processor, information about the events it causes are stored. When the op is retired, the processor is interrupted and the IBS information about the sampled op is made available to the OS through a series of MSRs.

Depending on the process family, these Op IBS Samples can contain some or all of the following information:

* The virtual address of the instruction associated with this micro-op
* The number of cycles between completion of the op's work and its retirement
* The number of cycles between tagging the micro-op and its retirement
* Whether the op was a return, resync, or mispredicted return
* Whether the op was a branch and/or fused branch and whether it was mispredicted and/or taken
* The target address of any branch op
* Whether the op was from a micro-coded instruction
* If the op was a load/store, and whether it hit in the cache
* Whether a load/store op hit in the L1 or L2 data caches
* Whether a load/store op hit in the L1 or L2 TLBs and/or the size of the page
* The source (e.g. DRAM, NUMA Node) of any data returned to a memory op from the north bridge
* The width of the op's memory access
* Whether the op was a software prefetch
* The latency of any cache or TLB refill
* The number of outstanding memory accesses when a load's value is returned
* The virtual and physical addresses accessed by any load or store

For more information about the technical details of AMD's Instruction Based Sampling, please refer AMD's various processor manuals: [5-17]

For more information about micro-ops in AMD cores, please refer to AMD's software optimization guides: [5-6, 18-19]. In particular, note that some of the descriptions in these manuals refer to macro-ops and micro-ops. For instance, in Family 17h cores, AMD64 instructions are broken into one or more macro-ops. These macro-ops are dispatched into the back-end of the pipeline, where they may be split into one or two micro-ops. For instane, an instruction that needs both the ALU (to do math or logic operatinos) and AGU (to calculate an address for a load or a store) will be split into two micro-ops. One of those micro-ops will go into the ALU scheduler units and the other will go to the AGU scheduler units. In these Family 17h cores, IBS op sampling actually samples macro-ops at dispatch time.

### Background References ###
1. S. V. Moore, "[A Comparison of Counting and Sampling Modes of Using Performance Monitoring Hardware](http://icl.cs.utk.edu/news_pub/submissions/icl-ut-02-01_perfmodels.pdf)," in Proc. of the Int'l Conf. on Computational Science-Part II (ICCS), 2002.
2. J. Dean, J. Hicks, C. A. Waldspurger, W. E. Weihl, G. Chrysos, "[ProfileMe: Hardware Support for Instruction-Level Profiling on Out-of-Order Processors](https://doi.org/10.1109/MICRO.1997.645821)," in Proc. of the 30th IEEE/ACM Int'l Symp. on Microarchitecture (MICRO-30), 1997.
3. P. J. Drongowski, "[Instruction-Based Sampling: A New Performance Analysis Technique for AMD Family 10h Processors](http://developer.amd.com/wordpress/media/2012/10/AMD_IBS_paper_EN.pdf)," AMD Technical Report, 2007.
4. P. Drongowski, L. Yu, F. Swehosky, S. Suthikulpanit, R. Richter, "[Incorporating Instruction-Based Sampling into AMD CodeAnalyst](https://doi.org/10.1109/ISPASS.2010.5452049)," in Proc. of the 2010 IEEE Int'l Symp. on Performance Analysis of Systems & Software (ISPASS), 2010.
5. Advanced Micro Devices, Inc. "[Software Optimization Guide for AMD Family 10h and 12h Processors](http://support.amd.com/techdocs/40546.pdf)". AMD Publication #40546. Rev. 3.13. Appendix G.
6. Advanced Micro Devices, Inc. "[Software Optimization Guide for AMD Family 15h Processors](https://support.amd.com/TechDocs/47414_15h_sw_opt_guide.pdf)". AMD Publication #47414. Rev. 3.07.  Appendix F.
7. Advanced Micro Devices, Inc. "[BIOS and Kernel Developer's Guide (BKDG) For AMD Family 10h Processors](https://developer.amd.com/wordpress/media/2012/10/31116.pdf)". AMD Publication #31116. Rev. 3.62.
8. Advanced Micro Devices, Inc. "[BIOS and Kernel Developer's Guide (BKDG) For AMD Family 12h Processors](https://support.amd.com/TechDocs/41131.pdf)". AMD Publication #41131. Rev. 3.03.
9. Advanced Micro Devices, Inc. "[BIOS and Kernel Developer's Guide (BKDG) For AMD Family 14h Models 00h-0Fh Processors](http://support.amd.com/TechDocs/43170_14h_Mod_00h-0Fh_BKDG.pdf)". AMD Publication #43170. Rev. 3.03.
10. Advanced Micro Devices, Inc. "[BIOS and Kernel Developer's Guide (BKDG) For AMD Family 15h Models 00h-0Fh Processors](http://support.amd.com/TechDocs/42301_15h_Mod_00h-0Fh_BKDG.pdf)". AMD Publication #42301. Rev. 3.14.
11. Advanced Micro Devices, Inc. "[BIOS and Kernel Developer's Guide (BKDG) For AMD Family 15h Models 10h-1Fh Processors](http://support.amd.com/TechDocs/42300_15h_Mod_10h-1Fh_BKDG.pdf)". AMD Publication #42300. Rev. 3.12.
12. Advanced Micro Devices, Inc. "[BIOS and Kernel Developer's Guide (BKDG) For AMD Family 15h Models 30h-3Fh Processors](https://support.amd.com/TechDocs/49125_15h_Models_30h-3Fh_BKDG.pdf)". AMD Publication #49125. Rev. 3.06.
13. Advanced Micro Devices, Inc. "[BIOS and Kernel Developer's Guide (BKDG) For AMD Family 15h Models 60h-6Fh Processors](http://support.amd.com/TechDocs/50742_15h_Models_60h-6Fh_BKDG.pdf)". AMD Publication #50742. Rev. 3.05.
14. Advanced Micro Devices, Inc. "[BIOS and Kernel Developer's Guide (BKDG) For AMD Family 15h Models 70h-7Fh Processorsi](http://support.amd.com/TechDocs/55072_AMD_Family_15h_Models_70h-7Fh_BKDG.pdf)". AMD Publication #55072. Rev. 3.00.
15. Advanced Micro Devices, Inc. "[BIOS and Kernel Developer's Guide (BKDG) For AMD Family 16h Models 00h-0Fh Processors](http://support.amd.com/TechDocs/48751_16h_bkdg.pdf)". AMD Publication #48751. Rev. 3.03.
16. Advanced Micro Devices, Inc. "[BIOS and Kernel Developer's Guide (BKDG) For AMD Family 16h Models 30h-3Fh Processors](http://support.amd.com/TechDocs/52740_16h_Models_30h-3Fh_BKDG.pdf)". AMD Publication #52740. Rev. 3.06.
17. Advanced Micro Devices, Inc. "[Processor Programming Reference (PPR) for AMD Family 17h Model 01h, Revision B1 Processors](http://support.amd.com/TechDocs/54945_PPR_Family_17h_Models_00h-0Fh.pdf)". AMD Publication #54945.
18. Advanced Micro Devices, Inc. "[Software Optimization Guide for AMD Family 16h Processors](http://support.amd.com/TechDocs/52128_16h_Software_Opt_Guide.zip)". AMD Publication #52128. Rev. 1.1.
19. Advanced Micro Devices, Inc. "[Software Optimization Guide for AMD Family 17h Processors](https://developer.amd.com/wordpress/media/2013/12/55723_3_00.ZIP)". AMD Publication #55723. Rev. 3.00.


Trademark Attribution
--------------------------------------------------------------------------------
&copy; 2017-2018 Advanced Micro Devices, Inc. All rights reserved. AMD, the AMD Arrow logo, AMD Phenom, Opteron, Ryzen, EPYC, and combinations thereof are trademarks of Advanced Micro Devices, Inc. in the United States and/or other jurisdictions. Linux is a registered trademark of Linus Torvalds. Other names are for informational purposes only and may be trademarks of their
respective owners.
