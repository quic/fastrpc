// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif
#define VERIFY_PRINT_INFO	0

#include <stdio.h>
#include <dlfcn.h>
#include <unistd.h>
#include "verify.h"
#include "HAP_farf.h"
#include "AEEStdErr.h"
#include <getopt.h>


#ifndef ADSP_DEFAULT_LISTENER_NAME
#define ADSP_DEFAULT_LISTENER_NAME "libadsp_default_listener.so"
#endif
#ifndef ADSP_LIBHIDL_NAME
#define ADSP_LIBHIDL_NAME "libhidlbase.so"
#endif

void print_help() {
  printf(
    "The adsprpcd is a daemon that establishes a connection to the guest PD (Process Domain) on the ADSP (Audio Digital Signal Processor).\n"
    "It uses the FastRPC framework to handle remote function calls from the CPU to the ADSP.\n"
    "This allows applications to perform audio related tasks on the ADSP,\n"
    "such as audio and video processing.\n"
    "It is essential for the following functionalities:\n"
    "1. DSP Process Exception Logs:\n"
    "   The daemon facilitates the transfer of ADSP process exception logs to the HLOS (High-Level Operating System) logging infrastructure.\n"
    "   This ensures that any exceptions occurring within ADSP processes are captured and logged in the HLOS, allowing for effective monitoring and debugging.\n"
    "2. FastRPC Shell Files Execution:\n"
    "   The fastrpc_shell_0 file is an executable file that runs as a process on the ADSP.\n"
    "   This shell file resides in the HLOS file system. If an application attempts to offload tasks to the ADSP but cannot access the shell file directly,\n"
    "   the ADSP utilizes the adsprpcd daemon to read the shell file and create the necessary process on the ADSP.\n"
    "   This mechanism ensures that applications can leverage ADSP capabilities even when direct access to the shell file is restricted.\n"
  );
}

typedef int (*adsp_default_listener_start_t)(int argc, char *argv[]);

int main(int argc, char *argv[]) {

  int nErr = 0;
  void *adsphandler = NULL;
#ifndef NO_HAL
  void *libhidlbaseHandler = NULL;
#endif
  adsp_default_listener_start_t listener_start;

  static struct option long_options[] = {
	  {"help", no_argument, 0, 'h'},
	  {0, 0, 0, 0}
  };

  int opt;
	while ((opt = getopt_long(argc, argv, "h", long_options, NULL)) != -1) {
	  switch (opt) {
		case 'h':
		  print_help();
		  return 0;
		default:
		  fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
		  return 1;
	  }
	}

  VERIFY_EPRINTF("adsp daemon starting");
#ifndef NO_HAL
  if(NULL != (libhidlbaseHandler = dlopen(ADSP_LIBHIDL_NAME, RTLD_NOW))) {
#endif
	  while (1) {
		if(NULL != (adsphandler = dlopen(ADSP_DEFAULT_LISTENER_NAME, RTLD_NOW))) {
		  if(NULL != (listener_start =
		(adsp_default_listener_start_t)dlsym(adsphandler, "adsp_default_listener_start"))) {
			VERIFY_IPRINTF("adsp_default_listener_start called");
			nErr = listener_start(argc, argv);
		  }
		  if(0 != dlclose(adsphandler)) {
			VERIFY_EPRINTF("dlclose failed");
		  }
		} else {
		  VERIFY_EPRINTF("adsp daemon error %s", dlerror());
		}
		if (nErr == AEE_ECONNREFUSED) {
		  VERIFY_EPRINTF("fastRPC device driver is disabled, retrying...");
		}
		VERIFY_EPRINTF("adsp daemon will restart after 25ms...");
		usleep(25000);
	  }
#ifndef NO_HAL
	  if(0 != dlclose(libhidlbaseHandler)) {
		  VERIFY_EPRINTF("libhidlbase dlclose failed");
	  }
  }
#endif
  VERIFY_EPRINTF("adsp daemon exiting %x", nErr);

  return nErr;
}
