// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif
#define VERIFY_PRINT_INFO 0

#include "AEEStdErr.h"
#include "HAP_farf.h"
#include "verify.h"
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>

#ifndef CDSP_DEFAULT_LISTENER_NAME
#define CDSP_DEFAULT_LISTENER_NAME "libcdsp_default_listener.so"
#endif
#ifndef CDSP_LIBHIDL_NAME
#define CDSP_LIBHIDL_NAME "libhidlbase.so"
#endif

void print_help() {
  printf("The cdsprpcd is a daemon that establishes a connection to the guest PD (Process Domain) on the CDSP (Compute Digital Signal Processor).\n");
  printf("It uses the FastRPC framework to handle remote function calls from the CPU to the CDSP.\n");
  printf("This allows applications to perform computationally intensive tasks on the CDSP,\n");
  printf("such as image processing, computer vision, and neural network-related computation.\n");
  printf("It is essential for the following functionalities:\n");
  printf("1. DSP Process Exception Logs:\n");
  printf("   The daemon facilitates the transfer of CDSP process exception logs to the HLOS (High-Level Operating System) logging infrastructure.\n");
  printf("   This ensures that any exceptions occurring within CDSP processes are captured and logged in the HLOS, allowing for effective monitoring and debugging.\n");
  printf("2. FastRPC Shell Files Execution:\n");
  printf("   The fastrpc_shell_3 file is an executable file that runs as a process on the CDSP.\n");
  printf("   This shell file resides in the HLOS file system. If an application attempts to offload tasks to the CDSP but cannot access the shell file directly,\n");
  printf("   the CDSP utilizes the cdsprpcd daemon to read the shell file and create the necessary process on the CDSP.\n");
  printf("   This mechanism ensures that applications can leverage CDSP capabilities even when direct access to the shell file is restricted.\n");
}

typedef int (*adsp_default_listener_start_t)(int argc, char *argv[]);

int main(int argc, char *argv[]) {

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0) {
      print_help();
      return 0;
    }
  }

  int nErr = 0;
  void *cdsphandler = NULL;
#ifndef NO_HAL
  void *libhidlbaseHandler = NULL;
#endif
  adsp_default_listener_start_t listener_start;

  VERIFY_EPRINTF("cdsp daemon starting");
#ifndef NO_HAL
  if (NULL != (libhidlbaseHandler = dlopen(CDSP_LIBHIDL_NAME, RTLD_NOW))) {
#endif
    while (1) {
      if (NULL !=
          (cdsphandler = dlopen(CDSP_DEFAULT_LISTENER_NAME, RTLD_NOW))) {
        if (NULL != (listener_start = (adsp_default_listener_start_t)dlsym(
                         cdsphandler, "adsp_default_listener_start"))) {
          VERIFY_IPRINTF("cdsp_default_listener_start called");
          nErr = listener_start(argc, argv);
        }
        if (0 != dlclose(cdsphandler)) {
          VERIFY_EPRINTF("dlclose failed");
        }
      } else {
        VERIFY_EPRINTF("cdsp daemon error %s", dlerror());
      }
      if (nErr == AEE_ECONNREFUSED) {
        VERIFY_EPRINTF("fastRPC device driver is disabled, daemon exiting...");
        break;
      }
      VERIFY_EPRINTF("cdsp daemon will restart after 100ms...");
      usleep(100000);
    }
#ifndef NO_HAL
    if (0 != dlclose(libhidlbaseHandler)) {
      VERIFY_EPRINTF("libhidlbase dlclose failed");
    }
  }
#endif
  VERIFY_EPRINTF("cdsp daemon exiting %x", nErr);

  return nErr;
}
