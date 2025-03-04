#include <stdio.h>
#include <stdbool.h>
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

#define LIB_CALCULATOR_PATH "/vendor/lib64/libcalculator.so"
#define LIB_HAP_PATH "/vendor/lib64/libhap_example.so"
#define LIB_MULTITHREADING_PATH "/vendor/lib64/libmultithreading.so"

typedef int (*run_test_t)(int domain_id, bool is_unsignedpd_enabled);

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

static void print_usage() {
    printf("Usage:\n"
           "    fastrpc_test [-d domain] [-U unsigned_PD] [-e example]\n\n"
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
           "-e example: Specify the example to run.\n"
           "    0: Run the calculator example.\n"
           "    1: Run the HAP example.\n"
           "    2: Run the multithreading example.\n"
           "        Default Value: 0 (calculator)\n"
    );
}

int main(int argc, char *argv[]) {
    int domain_id = 3;  // Default domain ID for CDSP
    int unsignedpd_flag = 1;  // Default to unsigned PD
    bool is_unsignedpd_enabled = true;  // Default to unsigned PD
    int opt = 0;
    int nErr = 0;
    int example = 0;  // Default example: calculator
    const char *lib_path = NULL;
    void *lib_handle = NULL;
    run_test_t run_test = NULL;

    while ((opt = getopt(argc, argv, "d:U:e:")) != -1) {
        switch (opt) {
            case 'd': domain_id = atoi(optarg);
                break;
            case 'U': unsignedpd_flag = atoi(optarg);
                break;
            case 'e': example = atoi(optarg);
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

    // Determine the library path based on the example
    switch (example) {
        case 0:
            lib_path = LIB_CALCULATOR_PATH;
            break;
        case 1:
            lib_path = LIB_HAP_PATH;
            break;
        case 2:
            lib_path = LIB_MULTITHREADING_PATH;
            break;
        default:
            printf("\nERROR 0x%x: Invalid example %d\n", AEE_EBADPARM, example);
            print_usage();
            goto bail;
    }

    // Load the library
    lib_handle = dlopen(lib_path, RTLD_LAZY);
    if (!lib_handle) {
        fprintf(stderr, "Error loading %s: %s\n", lib_path, dlerror());
        nErr = AEE_EUNABLETOLOAD;
        goto bail;
    }

    // Resolve the run_test symbol
    run_test = (run_test_t)dlsym(lib_handle, "run_test");
    if (!run_test) {
        fprintf(stderr, "Error resolving symbol 'run_test': %s\n", dlerror());
        nErr = AEE_ENOSUCHSYMBOL;
        goto bail;
    }

    // Call the run_test function
    nErr = run_test(domain_id, is_unsignedpd_enabled);

bail:
    if (lib_handle) {
        dlclose(lib_handle);
    }

    if (nErr != AEE_SUCCESS) {
        printf("Test failed with error code 0x%x\n", nErr);
    } else {
        printf("Success\n\n");
    }

    return nErr;
}