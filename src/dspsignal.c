// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif
#define FARF_ERROR 1
#define FARF_HIGH 0
#define FARF_MEDIUM 0

#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>


#include "dspsignal.h"
#include "fastrpc_common.h"
#include "fastrpc_internal.h"
#include "fastrpc_hash_table.h"
#include "remote.h"
#include "verify.h"
#include "AEEStdErr.h"
#include "HAP_farf.h"

/*
 * macro to check if a particular node is present in the
 * dspsignal_domain_signals_info hash table
 */
#define VERIFY_DSPSIGNAL_NODE_PRESENT(domain) \
	do { \
		GET_HASH_NODE(struct dspsignal_domain_signals, domain, ds); \
		if (!ds) { \
			nErr = AEE_ENOSUCHINSTANCE; \
			FARF(ERROR, "Error 0x%x: %s: unable to find hash-node for domain %d", \
				nErr, __func__, domain); \
			goto bail; \
		} \
	} while(0)


struct dspsignal_domain_signals {
	int dev;
	ADD_DOMAIN_HASH();
};

DECLARE_HASH_TABLE(dspsignal_domain_signals, struct dspsignal_domain_signals);

// Initialize dspsignal_domain_signals hash table
int fastrpc_dspsignal_init(void) {
	HASH_TABLE_INIT(struct dspsignal_domain_signals);
	return 0;
}

void fastrpc_dspsignal_deinit(void) {
	HASH_TABLE_CLEANUP(struct dspsignal_domain_signals);
}

// Dynamically initialize process signals structure.
static AEEResult init_domain_signals(int domain) {

  AEEResult nErr = AEE_SUCCESS;
  struct dspsignal_domain_signals *ds = NULL;

  VERIFY(IS_VALID_EFFECTIVE_DOMAIN_ID(domain));

  GET_HASH_NODE(struct dspsignal_domain_signals, domain, ds);
  if (ds) {
    // Already initialized
    goto bail;
  }

  ALLOC_AND_ADD_NEW_NODE_TO_TABLE(struct dspsignal_domain_signals, domain, ds);

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_session_dev(domain, &ds->dev)));
  VERIFYC(-1 != ds->dev, AEE_ERPC);

bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed (domain %d) errno %s", nErr, __func__,
         domain, strerror(errno));
  }
  return nErr;
}

static int get_domain(int domain) {
  if (domain == -1) {
    domain = get_current_domain();
  }
  return domain;
}

void dspsignal_domain_deinit(int domain) {
    struct dspsignal_domain_signals *ds = NULL;
    AEEResult nErr = AEE_SUCCESS;

    VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
    VERIFY_DSPSIGNAL_NODE_PRESENT(domain);
    DELETE_HASH_NODE(struct dspsignal_domain_signals, domain, ds);
    FARF(ALWAYS, "%s done for domain %d", __func__, domain);
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed (domain %d)", nErr, __func__, domain);
  }
  return;
}

AEEResult dspsignal_create(int domain, uint32_t id, uint32_t flags) {

  AEEResult nErr = AEE_SUCCESS;
  struct dspsignal_domain_signals *ds = NULL;

  VERIFYC(id < DSPSIGNAL_NUM_SIGNALS, AEE_EBADPARM);
  domain = get_domain(domain);
  VERIFYC(flags == 0, AEE_EBADPARM);
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
  VERIFY((nErr = init_domain_signals(domain)) == 0);
  VERIFY_DSPSIGNAL_NODE_PRESENT(domain);
  errno = 0;
  nErr = ioctl_signal_create(ds->dev, id, flags);
  if (nErr) {
    if (errno == ENOTTY) {
      FARF(HIGH, "dspsignal support not present in the FastRPC driver");
      nErr = AEE_EUNSUPPORTED;
    } else {
      nErr = convert_kernel_to_user_error(nErr, errno);
    }
    goto bail;
  }
  FARF(HIGH, "%s: Signal %u created", __func__, id);

bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed (domain %d, ID %u, flags 0x%x) errno %s",
         nErr, __func__, domain, id, flags, strerror(errno));
  }
  return nErr;
}

AEEResult dspsignal_destroy(int domain, uint32_t id) {

  AEEResult nErr = AEE_SUCCESS;
  struct dspsignal_domain_signals *ds = NULL;

  VERIFYC(id < DSPSIGNAL_NUM_SIGNALS, AEE_EBADPARM);
  domain = get_domain(domain);
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
  VERIFY_DSPSIGNAL_NODE_PRESENT(domain);
  errno = 0;
  nErr = ioctl_signal_destroy(ds->dev, id);
  if (nErr) {
    nErr = convert_kernel_to_user_error(nErr, errno);
    goto bail;
  }
  FARF(HIGH, "%s: Signal %u destroyed", __func__, id);

bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed (domain %d, ID %u) errno %s", nErr,
         __func__, domain, id, strerror(errno));
  }
  return nErr;
}

AEEResult dspsignal_signal(int domain, uint32_t id) {

  AEEResult nErr = AEE_SUCCESS;
  struct dspsignal_domain_signals *ds = NULL;

  VERIFYC(id < DSPSIGNAL_NUM_SIGNALS, AEE_EBADPARM);
  domain = get_domain(domain);
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
  VERIFY_DSPSIGNAL_NODE_PRESENT(domain);

  FARF(MEDIUM, "%s: Send signal %u", __func__, id);
  errno = 0;
  nErr = ioctl_signal_signal(ds->dev, id);
  if (nErr) {
    nErr = convert_kernel_to_user_error(nErr, errno);
    goto bail;
  }

bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed (domain %d, ID %u) errno %s", nErr,
         __func__, domain, id, strerror(errno));
  }
  return nErr;
}

AEEResult dspsignal_wait(int domain, uint32_t id, uint32_t timeout_usec) {

  AEEResult nErr = AEE_SUCCESS;
  struct dspsignal_domain_signals *ds = NULL;

  VERIFYC(id < DSPSIGNAL_NUM_SIGNALS, AEE_EBADPARM);
  domain = get_domain(domain);
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
  VERIFY_DSPSIGNAL_NODE_PRESENT(domain);
  fastrpc_qos_activity(domain);

  FARF(MEDIUM, "%s: Wait signal %u timeout %u", __func__, id, timeout_usec);
  errno = 0;
  nErr = ioctl_signal_wait(ds->dev, id, timeout_usec);
  if (nErr) {
    if (errno == ETIMEDOUT) {
      FARF(MEDIUM, "%s: Signal %u timed out", __func__, id);
      return AEE_EEXPIRED;
    } else if (errno == EINTR) {
      FARF(MEDIUM, "%s: Signal %u canceled", __func__, id);
      return AEE_EINTERRUPTED;
    } else {
      nErr = convert_kernel_to_user_error(nErr, errno);
      goto bail;
    }
  }

bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed (domain %d, ID %u, timeout %u) errno %s",
         nErr, __func__, domain, id, timeout_usec, strerror(errno));
  }
  return nErr;
}

AEEResult dspsignal_cancel_wait(int domain, uint32_t id) {

  AEEResult nErr = AEE_SUCCESS;
  struct dspsignal_domain_signals *ds = NULL;

  VERIFYC(id < DSPSIGNAL_NUM_SIGNALS, AEE_EBADPARM);
  domain = get_domain(domain);
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
  VERIFY_DSPSIGNAL_NODE_PRESENT(domain);

  FARF(MEDIUM, "%s: Cancel wait signal %u", __func__, id);
  errno = 0;
  nErr = ioctl_signal_cancel_wait(ds->dev, id);
  if (nErr) {
    nErr = convert_kernel_to_user_error(nErr, errno);
    goto bail;
  }

bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed (domain %d, ID %u) errno %s", nErr,
         __func__, domain, id, strerror(errno));
  }
  return nErr;
}
