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

#ifndef GDSP0_DEFAULT_LISTENER_NAME
#define GDSP0_DEFAULT_LISTENER_NAME "libgdsp0_default_listener.so"
#endif
#ifndef GDSP0_LIBHIDL_NAME
#define GDSP0_LIBHIDL_NAME "libhidlbase.so"
#endif

typedef int (*adsp_default_listener_start_t)(int argc, char *argv[]);

int main(int argc, char *argv[]) {

  int nErr = 0;
  void *gdsp0handler = NULL;
#ifndef NO_HAL
  void *libhidlbaseHandler = NULL;
#endif
  adsp_default_listener_start_t listener_start;

  VERIFY_EPRINTF("gdsp0 daemon starting");
#ifndef NO_HAL
  if (NULL != (libhidlbaseHandler = dlopen(GDSP0_LIBHIDL_NAME, RTLD_NOW))) {
#endif
    while (1) {
      if (NULL !=
          (gdsp0handler = dlopen(GDSP0_DEFAULT_LISTENER_NAME, RTLD_NOW))) {
        if (NULL != (listener_start = (adsp_default_listener_start_t)dlsym(
                         gdsp0handler, "adsp_default_listener_start"))) {
          VERIFY_IPRINTF("gdsp0_default_listener_start called");
          nErr = listener_start(argc, argv);
        }
        if (0 != dlclose(gdsp0handler)) {
          VERIFY_EPRINTF("dlclose failed");
        }
      } else {
        VERIFY_EPRINTF("gdsp0 daemon error %s", dlerror());
      }
      if (nErr == AEE_ECONNREFUSED) {
        VERIFY_EPRINTF("fastRPC device driver is disabled, daemon exiting...");
        break;
      }
      VERIFY_EPRINTF("gdsp0 daemon will restart after 100ms...");
      usleep(100000);
    }
#ifndef NO_HAL
    if (0 != dlclose(libhidlbaseHandler)) {
      VERIFY_EPRINTF("libhidlbase dlclose failed");
    }
  }
#endif
  VERIFY_EPRINTF("gdsp0 daemon exiting %x", nErr);

  return nErr;
}
