// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>  // For PATH_MAX

typedef int (*run_test_t)(int domain_id, bool is_unsignedpd_enabled);

static void print_usage() {
    printf("Usage:\n"
           "    fastrpc_test [-d domain] [-U unsigned_PD] [-t target] [-a arch_version]\n\n"
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
           "-t target: Specify the target platform (android or linux).\n"
           "    Default Value: linux\n"
           "-a arch_version: Specify the architecture version (v68 or v75).\n"
           "    Default Value: v68\n"
    );
}

int main(int argc, char *argv[]) {
    int domain_id = 3;  // Default domain ID for CDSP
    bool is_unsignedpd_enabled = true;  // Default to unsigned PD
    const char *target = "linux";  // Default target platform
    const char *arch_version = "v68";  // Default architecture version
    char abs_lib_path[PATH_MAX];
    char ld_lib_path[PATH_MAX];
    char dsp_lib_path[PATH_MAX];
    DIR *dir;
    struct dirent *entry;
    char full_lib_path[PATH_MAX];
    void *lib_handle = NULL;
    run_test_t run_test = NULL;
    int nErr = 0;

    int opt;
    while ((opt = getopt(argc, argv, "d:U:t:a:")) != -1) {
        switch (opt) {
            case 'd':
                domain_id = atoi(optarg);
                break;
            case 'U':
                is_unsignedpd_enabled = atoi(optarg) != 0;
                break;
            case 't':
                target = optarg;
                break;
            case 'a':
                arch_version = optarg;
                if (strcmp(arch_version, "v68") != 0 && strcmp(arch_version, "v75") != 0) {
                    printf("\nERROR: Invalid architecture version (-a). Must be v68 or v75.\n");
                    print_usage();
                    return -1;
                }
                break;
            default:
                print_usage();
                return -1;
        }
    }

    // Construct the absolute library path
    snprintf(abs_lib_path, sizeof(abs_lib_path), "%s", target);

    if (realpath(abs_lib_path, abs_lib_path) == NULL) {
        fprintf(stderr, "Error resolving path %s: %s\n", abs_lib_path, strerror(errno));
        return -1;
    }

    // Construct the absolute DSP library path
    snprintf(dsp_lib_path, sizeof(dsp_lib_path), "%s", arch_version);

    if (realpath(dsp_lib_path, dsp_lib_path) == NULL) {
        fprintf(stderr, "Error resolving path %s: %s\n", dsp_lib_path, strerror(errno));
        return -1;
    }

    // Construct LD_LIBRARY_PATH and DSP_LIBRARY_PATH
    snprintf(ld_lib_path, sizeof(ld_lib_path), "%s", abs_lib_path);

    if (setenv("LD_LIBRARY_PATH", ld_lib_path, 1) != 0) {
        fprintf(stderr, "Error setting LD_LIBRARY_PATH: %s\n", strerror(errno));
        return -1;
    }

    if (setenv("DSP_LIBRARY_PATH", dsp_lib_path, 1) != 0) {
        fprintf(stderr, "Error setting DSP_LIBRARY_PATH: %s\n", strerror(errno));
        return -1;
    }

    dir = opendir(abs_lib_path);
    if (!dir) {
        fprintf(stderr, "Error opening directory %s: %s\n", abs_lib_path, strerror(errno));
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG && strstr(entry->d_name, ".so")) {
            snprintf(full_lib_path, sizeof(full_lib_path), "%s/%s", abs_lib_path, entry->d_name);

            lib_handle = dlopen(full_lib_path, RTLD_LAZY);
            if (!lib_handle) {
                fprintf(stderr, "Error loading %s: %s\n", full_lib_path, dlerror());
                continue;
            }

            run_test = (run_test_t)dlsym(lib_handle, "run_test");
            if (!run_test) {
                fprintf(stderr, "Symbol 'run_test' not found in %s\n", full_lib_path);
                dlclose(lib_handle);
                continue;
            }

            nErr = run_test(domain_id, is_unsignedpd_enabled);
            if (nErr != 0) {
                printf("Test failed with error code 0x%x in %s\n", nErr, full_lib_path);
            } else {
                printf("Success in %s\n", full_lib_path);
            }

            dlclose(lib_handle);
        }
    }

    closedir(dir);

    if (nErr != 0) {
        printf("Test failed with error code 0x%x\n", nErr);
    } else {
        printf("All tests completed successfully\n");
    }

    return nErr;
}