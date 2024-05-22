// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

// Need to always define in order to use VERIFY_EPRINTF
#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif

#define FARF_ERROR 1

#include "adsp_default_listener.h"
#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "adsp_default_listener1.h"
#include "fastrpc_common.h"
#include "fastrpc_internal.h"
#include "remote.h"
#include "verify.h"
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#define MAX_DOMAIN_URI_SIZE 12
#define ROOTPD_NAME "rootpd"
#define ATTACH_GUESTOS "attachguestos"
#define CREATE_STATICPD "createstaticpd:"

// Array of supported domain names and its corresponding ID's.
static domain_t supported_domains[] = {{ADSP_DOMAIN_ID, ADSP_DOMAIN},
                                       {MDSP_DOMAIN_ID, MDSP_DOMAIN},
                                       {SDSP_DOMAIN_ID, SDSP_DOMAIN},
                                       {CDSP_DOMAIN_ID, CDSP_DOMAIN}};

// Get domain name for the domain id.
static domain_t *get_domain_uri(int domain_id) {
  int i = 0;
  int size = sizeof(supported_domains) / sizeof(domain_t);

  for (i = 0; i < size; i++) {
    if (supported_domains[i].id == domain_id)
      return &supported_domains[i];
  }

  return NULL;
}

int adsp_default_listener_start(int argc, char *argv[]) {
  struct pollfd pfd;
  eventfd_t event = 0;
  remote_handle64 event_fd = INVALID_HANDLE;
  remote_handle64 fd = INVALID_HANDLE;
  remote_handle64 listener_fd = INVALID_HANDLE;
  int nErr = AEE_SUCCESS, domain_id = INVALID_DOMAIN_ID;
  char *name = NULL;
  char *adsp_default_listener1_URI_domain = NULL;
  int adsp_default_listener1_URI_domain_len =
      strlen(adsp_default_listener1_URI) + MAX_DOMAIN_URI_SIZE;
  domain_t *dsp_domain = NULL;
  int namelen = 0;
  char *eventfd_domain = NULL;
  int eventfdlen = 0;
  (void)argc;
  (void)argv;

  if (argc > 2) {
    /* Two arguments are passed in below format
     * Example: ./adsprpcd audiopd adsp
     * Get domain name from arguments and use domains API.
     */
    VERIFY_IPRINTF("%s started with arguments %s and %s\n", __func__, argv[1],
                   argv[2]);
    VERIFYC(INVALID_DOMAIN_ID != (domain_id = get_domain_from_name(
                                      argv[2], DOMAIN_NAME_STAND_ALONE)),
            AEE_EINVALIDDOMAIN);
    VERIFYC(NULL != (dsp_domain = get_domain_uri(domain_id)),
            AEE_EINVALIDDOMAIN);

    // Allocate memory for URI. Example: "ITRANSPORT_PREFIX
    // createstaticpd:audiopd&dom=adsp"
    namelen = strlen(ITRANSPORT_PREFIX CREATE_STATICPD) + strlen(argv[1]) +
              strlen(ADSP_DOMAIN);
    VERIFYC(NULL != (name = (char *)malloc((namelen + 1) * sizeof(char))),
            AEE_ENOMEMORY);

    // Copy URI to allocated memory
    if (!std_strncmp(argv[1], ROOTPD_NAME, std_strlen(argv[1]))) {
      std_strlcpy(name, ITRANSPORT_PREFIX ATTACH_GUESTOS,
                  strlen(ITRANSPORT_PREFIX ATTACH_GUESTOS) + 1);
    } else {
      std_strlcpy(name, ITRANSPORT_PREFIX CREATE_STATICPD,
                  strlen(ITRANSPORT_PREFIX CREATE_STATICPD) + 1);
      std_strlcat(name, argv[1],
                  strlen(ITRANSPORT_PREFIX CREATE_STATICPD) + strlen(argv[1]) +
                      1);
    }

    // Concatenate domain to the URI
    std_strlcat(name, dsp_domain->uri, namelen + 1);

    // Open static process handle
    VERIFY(AEE_SUCCESS == (nErr = remote_handle64_open(name, &fd)));
    goto start_listener;
  } else if (argc > 1) {
    /* One arguments is passed in below format
     * Example: ./adsprpcd "createstaticpd:audiopd&dom=adsp" (or) ./adsprpcd
     * audiopd Get domain name from arguments and use domains API.
     */
    VERIFY_IPRINTF("%s started with arguments %s\n", __func__, argv[1]);
    domain_id = get_domain_from_name(argv[1], DOMAIN_NAME_IN_URI);

    // If domain name part of arguments, use domains API
    if (domain_id != INVALID_DOMAIN_ID) {
      VERIFY(AEE_SUCCESS == (nErr = remote_handle64_open(argv[1], &fd)));
      goto start_listener;
    }

    // Allocate memory for URI. Example: "ITRANSPORT_PREFIX
    // createstaticpd:audiopd"
    namelen = strlen(ITRANSPORT_PREFIX CREATE_STATICPD) + strlen(argv[1]);
    VERIFYC(NULL != (name = (char *)malloc((namelen + 1) * sizeof(char))),
            AEE_ENOMEMORY);

    // Copy URI to allocated memory
    std_strlcpy(name, ITRANSPORT_PREFIX CREATE_STATICPD,
                strlen(ITRANSPORT_PREFIX CREATE_STATICPD) + 1);
    std_strlcat(name, argv[1], namelen + 1);
  } else {
    // If no arguments passed, default/rootpd daemon of remote subsystem
    namelen = strlen(ITRANSPORT_PREFIX ATTACH_GUESTOS);
    VERIFYC(NULL != (name = (char *)malloc((namelen + 1) * sizeof(char))),
            AEE_ENOMEMORY);
    std_strlcpy(name, ITRANSPORT_PREFIX ATTACH_GUESTOS,
                strlen(ITRANSPORT_PREFIX ATTACH_GUESTOS) + 1);
  }

  // Default case: Open non-domain static process handle
  VERIFY_IPRINTF("%s started with arguments %s\n", __func__, name);
  VERIFY(AEE_SUCCESS == (nErr = remote_handle_open(name, (remote_handle *)&fd)));
start_listener:
  VERIFYC(!setenv("ADSP_LISTENER_MEM_CACHE_SIZE", "1048576", 0), AEE_ESETENV);

  // If domain name part of arguments, use domains API
  if (domain_id != INVALID_DOMAIN_ID) {
    // Allocate memory and copy adsp_default_listener1 URI Example:
    // "adsp_default_listener1_URI&dom=adsp"
    VERIFYC(NULL !=
                (adsp_default_listener1_URI_domain = (char *)malloc(
                     (adsp_default_listener1_URI_domain_len) * sizeof(char))),
            AEE_ENOMEMORY);
    VERIFYC(NULL != (dsp_domain = get_domain_uri(domain_id)),
            AEE_EINVALIDDOMAIN);
    nErr = snprintf(adsp_default_listener1_URI_domain,
                    adsp_default_listener1_URI_domain_len, "%s%s",
                    adsp_default_listener1_URI, dsp_domain->uri);
    if (nErr < 0) {
      VERIFY_EPRINTF("ERROR: %s: %d returned from snprintf\n", __func__, nErr);
      nErr = AEE_EFAILED;
      goto bail;
    }

    // Open default listener handle
    nErr = adsp_default_listener1_open(adsp_default_listener1_URI_domain,
                                       &listener_fd);

    // Register daemon as default listener to static process
    if (nErr == AEE_SUCCESS) {
      VERIFY(0 == (nErr = adsp_default_listener1_register(listener_fd)));
      goto start_poll;
    }
  }

  // Default case: Register non-domain default listener
  VERIFY_IPRINTF("%s domains support is not available for "
                 "adsp_default_listener1, using non-domain API\n",
                 __func__);
  VERIFY(0 ==
         (nErr = remote_handle_open("adsp_default_listener", (remote_handle *)&listener_fd)));
  VERIFY(0 == (nErr = adsp_default_listener_register()));
start_poll:
  // If domain name part of arguments, use domains API
  if (domain_id != INVALID_DOMAIN_ID) {
    // Allocate memory and copy geteventfd URI Example: "ITRANSPORT_PREFIX
    // geteventfd&dom=adsp"
    eventfdlen = strlen(ITRANSPORT_PREFIX "geteventfd") + MAX_DOMAIN_URI_SIZE;
    VERIFYC(NULL !=
                (eventfd_domain = (char *)malloc((eventfdlen) * sizeof(char))),
            AEE_ENOMEMORY);
    nErr = snprintf(eventfd_domain, eventfdlen, "%s%s",
                    ITRANSPORT_PREFIX "geteventfd", dsp_domain->uri);
    if (nErr < 0) {
      VERIFY_EPRINTF("ERROR: %s: %d returned from snprintf\n", __func__, nErr);
      nErr = AEE_EFAILED;
      goto bail;
    }

    // Get even FD to poll on listener thread
    VERIFY(0 == (nErr = remote_handle64_open(eventfd_domain, &event_fd)));
    pfd.fd = (remote_handle)event_fd;
  } else {
    VERIFY(0 == (nErr = remote_handle_open(ITRANSPORT_PREFIX "geteventfd",
                                           (remote_handle *)&pfd.fd)));
  }
  if (name != NULL) {
    free(name);
    name = NULL;
  }
  if (eventfd_domain != NULL) {
    free(eventfd_domain);
    eventfd_domain = NULL;
  }
  if (adsp_default_listener1_URI_domain != NULL) {
    free(adsp_default_listener1_URI_domain);
    adsp_default_listener1_URI_domain = NULL;
  }
  // Poll on listener thread
  pfd.events = POLLIN;
  pfd.revents = 0;
  while (1) {
    VERIFYC(0 < poll(&pfd, 1, -1), AEE_EPOLL);
    VERIFYC(0 == eventfd_read(pfd.fd, &event), AEE_EEVENTREAD);
    if (event) {
      break;
    }
  }
bail:
  if (listener_fd != INVALID_HANDLE) {
    adsp_default_listener1_close(listener_fd);
  }
  if (nErr != AEE_SUCCESS) {
    if (name != NULL) {
      free(name);
      name = NULL;
    }
    if (eventfd_domain != NULL) {
      free(eventfd_domain);
      eventfd_domain = NULL;
    }
    if (adsp_default_listener1_URI_domain != NULL) {
      free(adsp_default_listener1_URI_domain);
      adsp_default_listener1_URI_domain = NULL;
    }
    VERIFY_EPRINTF("Error 0x%x: %s exiting\n", nErr, __func__);
  }
  return nErr;
}
