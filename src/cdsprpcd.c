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

typedef int (*adsp_default_listener_start_t)(int argc, char *argv[]);

int main(int argc, char *argv[]) {

  int nErr = 0;
  void *cdsphandler = NULL;
  adsp_default_listener_start_t listener_start;

  VERIFY_EPRINTF("cdsp daemon starting");
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
  VERIFY_EPRINTF("cdsp daemon exiting %x", nErr);

  return nErr;
}
