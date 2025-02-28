#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <dlfcn.h>
#include "AEEStdErr.h"
#include "remote.h"
#include "rpcmem.h"
#include "fastrpc_test.h"

#define LIB_CALCULATOR_PATH "/vendor/lib64/libcalculator.so"
#define CALCULATOR_URI "file:///libcalculator_skel.so?calculator_skel_handle_invoke&_modver=1.0&_idlver=1.2.3"

#ifndef DSP_OFFSET
   #define DSP_OFFSET 0x80000400
#endif

typedef int (*calculator_open_t)(const char *uri, remote_handle64 *handle);
typedef int (*calculator_close_t)(remote_handle64 handle);
typedef int (*calculator_sum_t)(remote_handle64 handle, const int *vec, int vecLen, int64 *res);
typedef int (*calculator_max_t)(remote_handle64 handle, const int *vec, int vecLen, int *res);

domain_t supported_domains[] = {
    {ADSP_DOMAIN_ID, ADSP_DOMAIN},
    {MDSP_DOMAIN_ID, MDSP_DOMAIN},
    {SDSP_DOMAIN_ID, SDSP_DOMAIN},
    {CDSP_DOMAIN_ID, CDSP_DOMAIN},
    {CDSP1_DOMAIN_ID, CDSP1_DOMAIN}
};

static domain_t *get_domain(int domain_id) {
    int i = 0;
    int size = sizeof(supported_domains)/sizeof(domain_t);

    for(i = 0; i < size; i++) {
        if (supported_domains[i].id == domain_id)
            return &supported_domains[i];
    }

    return NULL;
}

static bool is_unsignedpd_supported(int domain_id) {
    int nErr = AEE_SUCCESS;
    if (remote_handle_control) {
        struct remote_dsp_capability dsp_capability_domain = {domain_id, UNSIGNED_PD_SUPPORT, 0};
        nErr = remote_handle_control(DSPRPC_GET_DSP_INFO, &dsp_capability_domain, sizeof(struct remote_dsp_capability));
        if ((nErr & 0xFF) == (AEE_EUNSUPPORTEDAPI & 0xFF)) {
            printf("\nFastRPC Capability API is not supported on this device. Falling back to signed pd.\n");
            return false;
        }
        if (nErr) {
            printf("\nERROR 0x%x: FastRPC Capability API failed. Falling back to signed pd.\n", nErr);
            return false;
        }
        if (dsp_capability_domain.capability == 1) {
            return true;
        }
    } else {
        nErr = AEE_EUNSUPPORTEDAPI;
        printf("remote_dsp_capability interface is not supported on this device. Falling back to signed pd.\n");
        return false;
    }
    return false;
}

int calculator_test(int domain_id, bool is_unsignedpd_enabled) {
    int nErr = AEE_SUCCESS;
    void *lib_handle = NULL;
    calculator_open_t calculator_open = NULL;
    calculator_close_t calculator_close = NULL;
    calculator_sum_t calculator_sum = NULL;
    calculator_max_t calculator_max = NULL;
    remote_handle64 handleSum = -1;
    remote_handle64 handleMax = -1;
    int *test = NULL;
    int ii, len = 0, resultMax = 0;
    int64 result = 0;
    int calculator_URI_domain_len = strlen(CALCULATOR_URI) + MAX_DOMAIN_URI_SIZE;
    int retry = 10;
    char *calculator_URI_domain = NULL;
    domain_t *my_domain = NULL;

    int num = 1000; // Fixed array size
    len = sizeof(*test) * num;
    printf("\nAllocate %d bytes from ION heap\n", len);

    int heapid = RPCMEM_HEAP_ID_SYSTEM;
#if defined(SLPI) || defined(MDSP)
    heapid = RPCMEM_HEAP_ID_CONTIG;
#endif

    if (0 == (test = (int*)rpcmem_alloc(heapid, RPCMEM_DEFAULT_FLAGS, len))) {
        nErr = AEE_ENORPCMEMORY;
        printf("ERROR 0x%x: memory alloc failed\n", nErr);
        goto bail;
    }

    printf("Creating sequence of numbers from 0 to %d\n", num - 1);
    for (ii = 0; ii < num; ++ii)
        test[ii] = ii;

    // Load the calculator library
    lib_handle = dlopen(LIB_CALCULATOR_PATH, RTLD_LAZY);
    if (!lib_handle) {
        fprintf(stderr, "Error loading %s: %s\n", LIB_CALCULATOR_PATH, dlerror());
        nErr = AEE_EUNABLETOLOAD;
        goto bail;
    }

    // Resolve function symbols
    calculator_open = (calculator_open_t)dlsym(lib_handle, "calculator_open");
    calculator_close = (calculator_close_t)dlsym(lib_handle, "calculator_close");
    calculator_sum = (calculator_sum_t)dlsym(lib_handle, "calculator_sum");
    calculator_max = (calculator_max_t)dlsym(lib_handle, "calculator_max");

    if (!calculator_open || !calculator_close || !calculator_sum || !calculator_max) {
        fprintf(stderr, "Error resolving symbols: %s\n", dlerror());
        nErr = AEE_ENOSUCHSYMBOL;
        goto bail;
    }

    my_domain = get_domain(domain_id);
    if (my_domain == NULL) {
        nErr = AEE_EBADPARM;
        printf("\nERROR 0x%x: unable to get domain struct %d\n", nErr, domain_id);
        goto bail;
    }

    printf("Compute sum on domain %d\n", domain_id);

    if (is_unsignedpd_enabled) {
        if (remote_session_control) {
            struct remote_rpc_control_unsigned_module data;
            data.domain = domain_id;
            data.enable = 1;
            if (AEE_SUCCESS != (nErr = remote_session_control(DSPRPC_CONTROL_UNSIGNED_MODULE, (void*)&data, sizeof(data)))) {
                printf("ERROR 0x%x: remote_session_control failed\n", nErr);
                goto bail;
            }
        } else {
            nErr = AEE_EUNSUPPORTED;
            printf("ERROR 0x%x: remote_session_control interface is not supported on this device\n", nErr);
            goto bail;
        }
    }

    if ((calculator_URI_domain = (char *)malloc(calculator_URI_domain_len)) == NULL) {
        nErr = AEE_ENOMEMORY;
        printf("unable to allocate memory for calculator_URI_domain of size: %d\n", calculator_URI_domain_len);
        goto bail;
    }

    nErr = snprintf(calculator_URI_domain, calculator_URI_domain_len, "%s%s", CALCULATOR_URI, my_domain->uri);
    if (nErr < 0) {
        printf("ERROR 0x%x returned from snprintf\n", nErr);
        nErr = AEE_EFAILED;
        goto bail;
    }

    do {
        if (AEE_SUCCESS == (nErr = calculator_open(calculator_URI_domain, &handleSum))) {
            printf("\nCall calculator_sum on the DSP\n");
            nErr = calculator_sum(handleSum, test, num, &result);
        }

        if (!nErr) {
            printf("Sum = %lld\n", result);
            break;
        } else {
            if (nErr == AEE_ECONNRESET && errno == ECONNRESET) {
                /* In case of a Sub-system restart (SSR), AEE_ECONNRESET is returned by FastRPC
                and errno is set to ECONNRESET by the kernel.*/
                retry--;
                sleep(5); /* Sleep for x number of seconds */
            } else if (nErr == AEE_ENOSUCH || (nErr == (AEE_EBADSTATE + DSP_OFFSET))) {
                /* AEE_ENOSUCH is returned when Protection domain restart (PDR) happens and
                AEE_EBADSTATE is returned from DSP when PD is exiting or crashing.*/
                /* Refer to AEEStdErr.h for more info on error codes*/
                retry -= 2;
            } else {
                break;
            }
        }

        /* Close the handle and retry handle open */
        if (handleSum != -1) {
            if (AEE_SUCCESS != (nErr = calculator_close(handleSum))) {
                printf("ERROR 0x%x: Failed to close handle\n", nErr);
            }
        }
    } while (retry);

    if (nErr) {
        printf("Retry attempt unsuccessful. Timing out....\n");
        printf("ERROR 0x%x: Failed to compute sum on domain %d\n", nErr, domain_id);
    }

    if (AEE_SUCCESS == (nErr = calculator_open(calculator_URI_domain, &handleMax))) {
        printf("\nCall calculator_max on the DSP\n");
        if (AEE_SUCCESS == (nErr = calculator_max(handleMax, test, num, &resultMax))) {
            printf("Max value = %d\n", resultMax);
        }
    }

    if (nErr) {
        printf("ERROR 0x%x: Failed to find max on domain %d\n", nErr, domain_id);
    }

    if (handleSum != -1) {
        if (AEE_SUCCESS != (nErr = calculator_close(handleSum))) {
            printf("ERROR 0x%x: Failed to close handleSum\n", nErr);
        }
    }

    if (handleMax != -1) {
        if (AEE_SUCCESS != (nErr = calculator_close(handleMax))) {
            printf("ERROR 0x%x: Failed to close handleMax\n", nErr);
        }
    }

bail:
    if (calculator_URI_domain) {
        free(calculator_URI_domain);
    }
    if (test) {
        rpcmem_free(test);
    }
    if (lib_handle) {
        dlclose(lib_handle);
    }

    return nErr;
}

static void print_usage() {
    printf("Usage:\n"
           "    fastrpc_test [-d domain] [-U unsigned_PD]\n\n"
           "Options:\n"
           "-d domain: Run on a specific domain.\n"
           "    0: Run the example on ADSP\n"
           "    1: Run the example on MDSP\n"
           "    2: Run the example on SDSP\n"
           "    3: Run the example on CDSP\n"
           "        Default Value: 3(CDSP) for targets having CDSP.\n"
           "-U unsigned_PD: Run on signed or unsigned PD.\n"
           "    0: Run on signed PD.\n"
           "    1: Run on unsigned PD.\n"
           "        Default Value: 1\n"
    );
}

int main(int argc, char *argv[]) {
    int domain_id = 3;  // Default domain ID for CDSP
    int unsignedpd_flag = 1;  // Default to unsigned PD
    bool is_unsignedpd_enabled = true;  // Default to unsigned PD
    int opt = 0;
    int nErr = 0;

    while ((opt = getopt(argc, argv, "d:U:")) != -1) {
        switch (opt) {
            case 'd': domain_id = atoi(optarg);
                break;
            case 'U': unsignedpd_flag = atoi(optarg);
                break;
            default:
                print_usage();
                return -1;
        }
    }

    // Validate the domain ID
    if (get_domain(domain_id) == NULL) {
        printf("\nERROR 0x%x: Invalid domain %d\n", AEE_EBADPARM, domain_id);
        printf("Defaulting to domain 3 (CDSP).\n");
        domain_id = 3;  // Default to CDSP
    }

    // Validate the unsigned_PD flag
    if (unsignedpd_flag < 0 || unsignedpd_flag > 1) {
        nErr = AEE_EBADPARM;
        printf("\nERROR 0x%x: Invalid unsigned PD flag %d\n", nErr, unsignedpd_flag);
        print_usage();
        goto bail;
    }

    // Check if unsigned PD is supported
    if (unsignedpd_flag == 1) {
        is_unsignedpd_enabled = is_unsignedpd_supported(domain_id);
        if (!is_unsignedpd_enabled) {
            printf("Overriding user request for unsigned PD. Only signed offload is allowed on domain %d.\n", domain_id);
            unsignedpd_flag = 0;
        }
    } else {
        is_unsignedpd_enabled = false;
    }

    // Example usage of calculator_test
    nErr = calculator_test(domain_id, is_unsignedpd_enabled);

bail:
    if (nErr != AEE_SUCCESS) {
        printf("Test failed with error code 0x%x\n", nErr);
    } else {
        printf("Success\n\n");
    }

    return nErr;
}