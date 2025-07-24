// Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif

#include <stdio.h>
#include <dlfcn.h>
#include <unistd.h>
#include "verify.h"
#include "AEEStdErr.h"


#ifndef SDSP_DEFAULT_LISTENER_NAME
#define SDSP_DEFAULT_LISTENER_NAME "libsdsp_default_listener.so"
#endif

typedef int (*adsp_default_listener_start_t)(int argc, char *argv[]);

int main(int argc, char *argv[]) {

  int nErr = 0;
  void *sdsphandler = NULL;
  adsp_default_listener_start_t listener_start;

  VERIFY_EPRINTF("sdsp daemon starting");
  while (1) {
    if(NULL != (sdsphandler = dlopen(SDSP_DEFAULT_LISTENER_NAME, RTLD_NOW))) {
      if(NULL != (listener_start =
        (adsp_default_listener_start_t)dlsym(sdsphandler, "adsp_default_listener_start"))) {
        VERIFY_IPRINTF("sdsp_default_listener_start called");
        listener_start(argc, argv);
      }
      if(0 != dlclose(sdsphandler)) {
        VERIFY_EPRINTF("dlclose failed");
      }
    } else {
      VERIFY_EPRINTF("sdsp daemon error %s", dlerror());
    }
    if (nErr == AEE_ECONNREFUSED) {
      VERIFY_EPRINTF("fastRPC device driver is disabled, retrying...");
    }
    VERIFY_EPRINTF("sdsp daemon will restart after 100ms...");
    usleep(100000);
  }
  VERIFY_EPRINTF("sdsp daemon exiting %x", nErr);
bail:
  return nErr;
}
