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

#ifndef ADSP_DEFAULT_LISTENER_NAME
#define ADSP_DEFAULT_LISTENER_NAME "libadsp_default_listener.so"
#endif
#ifndef ADSP_LIBHIDL_NAME
#define ADSP_LIBHIDL_NAME "libhidlbase.so"
#endif

typedef int (*adsp_default_listener_start_t)(int argc, char *argv[]);

int
main(int argc, char *argv[])
{

	int nErr = 0;
	void *adsphandler = NULL;
#ifndef NO_HAL
	void *libhidlbaseHandler = NULL;
#endif
	adsp_default_listener_start_t listener_start;

	VERIFY_EPRINTF("adsp daemon starting");
#ifndef NO_HAL
	if(NULL
	   != (libhidlbaseHandler = dlopen(ADSP_LIBHIDL_NAME, RTLD_NOW))) {
#endif
		while(1) {
			if(NULL
			   != (adsphandler = dlopen(ADSP_DEFAULT_LISTENER_NAME,
			                            RTLD_NOW))) {
				if(NULL
				   != (listener_start
				       = (adsp_default_listener_start_t)dlsym(
					   adsphandler, "adsp_"
							"defau"
							"lt_"
							"liste"
							"ner_"
							"star"
							"t"))) {
					VERIFY_IPRINTF("a"
					               "d"
					               "s"
					               "p"
					               "_"
					               "d"
					               "e"
					               "f"
					               "a"
					               "u"
					               "l"
					               "t"
					               "_"
					               "l"
					               "i"
					               "s"
					               "t"
					               "e"
					               "n"
					               "e"
					               "r"
					               "_"
					               "s"
					               "t"
					               "a"
					               "r"
					               "t"
					               " "
					               "c"
					               "a"
					               "l"
					               "l"
					               "e"
					               "d");
					nErr = listener_start(argc, argv);
				}
				if(0 != dlclose(adsphandler)) {
					VERIFY_EPRINTF("d"
					               "l"
					               "c"
					               "l"
					               "o"
					               "s"
					               "e"
					               " "
					               "f"
					               "a"
					               "i"
					               "l"
					               "e"
					               "d");
				}
			} else {
				VERIFY_EPRINTF("adsp daemon "
				               "error %s",
				               dlerror());
			}
			if(nErr == AEE_ECONNREFUSED) {
				VERIFY_EPRINTF("fastRPC device "
				               "driver is "
				               "disabled, "
				               "retrying...");
			}
			VERIFY_EPRINTF("adsp daemon will restart after "
			               "25ms...");
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
