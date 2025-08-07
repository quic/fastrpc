// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif
#define VERIFY_PRINT_INFO 0

#include "AEEStdErr.h"
#include "HAP_farf.h"
#include "verify.h"
#include "fastrpc_common.h"
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>


#ifndef ADSP_DEFAULT_LISTENER_NAME
#define ADSP_DEFAULT_LISTENER_NAME "libadsp_default_listener.so"
#endif
#ifndef CDSP_DEFAULT_LISTENER_NAME
#define CDSP_DEFAULT_LISTENER_NAME "libcdsp_default_listener.so"
#endif
#ifndef SDSP_DEFAULT_LISTENER_NAME
#define SDSP_DEFAULT_LISTENER_NAME "libsdsp_default_listener.so"
#endif
#ifndef GDSP_DEFAULT_LISTENER_NAME
#define GDSP_DEFAULT_LISTENER_NAME "libcdsp_default_listener.so.1"
#endif

typedef int (*dsp_default_listener_start_t)(int argc, char *argv[]);


int main(int argc, char *argv[]) {
  int nErr = 0;
  void *dsphandler = NULL;
  const char* lib_name;
  const char* dsp_name;
  dsp_default_listener_start_t listener_start;

  #ifdef USE_ADSP
    lib_name = ADSP_DEFAULT_LISTENER_NAME;
    dsp_name = "adsp";
  #elif defined(USE_SDSP)
    lib_name = SDSP_DEFAULT_LISTENER_NAME;
    dsp_name = "sdsp";
  #elif defined(USE_CDSP)
    lib_name = CDSP_DEFAULT_LISTENER_NAME;
    dsp_name = "cdsp";
  #elif defined(USE_GDSP)
    lib_name = GDSP_DEFAULT_LISTENER_NAME;
    dsp_name = "gdsp";
  #else
    goto bail;
  #endif

  VERIFY_IPRINTF("%s daemon starting", dsp_name);
  
  while (1) {
        if (NULL != (dsphandler = dlopen(lib_name,RTLD_NOW))) {
            if (NULL != (listener_start = (dsp_default_listener_start_t)dlsym(
                              dsphandler, "adsp_default_listener_start"))) {
                VERIFY_IPRINTF("adsp_default_listener_start called");
                nErr = listener_start(argc, argv);
            }
            if (0 != dlclose(dsphandler)) {
              VERIFY_IPRINTF("dlclose failed for %s", lib_name);
            }
        } else {
            VERIFY_IPRINTF("%s daemon error %s", dsp_name, dlerror());
        }

        if (nErr == AEE_ECONNREFUSED) {
            VERIFY_IPRINTF("fastRPC device driver is disabled, daemon exiting...");
            break;
        }

        VERIFY_IPRINTF("%s daemon will restart after 100ms...", dsp_name);
        usleep(100000);
  }

  bail:
    VERIFY_IPRINTF("daemon exiting %x", nErr);
    return nErr;
}
