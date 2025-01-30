// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

// #ifndef VERIFY_PRINT_ERROR
// #define VERIFY_PRINT_ERROR
// #endif // VERIFY_PRINT_ERROR
// #ifndef VERIFY_PRINT_INFO
// #define VERIFY_PRINT_INFO
// #endif // VERIFY_PRINT_INFO

#ifndef VERIFY_PRINT_WARN
#define VERIFY_PRINT_WARN
#endif // VERIFY_PRINT_WARN
#ifndef VERIFY_PRINT_ERROR_ALWAYS
#define VERIFY_PRINT_ERROR_ALWAYS
#endif // VERIFY_PRINT_ERROR_ALWAYS
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define FARF_ERROR 1
#define FARF_HIGH 1
#define FARF_MED 1
#define FARF_LOW 1
#define FARF_CRITICAL 1 // Push log's to all hooks and persistent buffer.

#include "AEEQList.h"
#include "AEEStdErr.h"
#include "AEEatomic.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "adsp_current_process.h"
#include "adsp_current_process1.h"
#include "adsp_listener1.h"
#include "adsp_perf1.h"
#include "adspmsgd_adsp1.h"
#include "adspmsgd_internal.h"
#include "apps_mem_internal.h"
#include "apps_std_internal.h"
#include "dspsignal.h"
#include "fastrpc_apps_user.h"
#include "fastrpc_async.h"
#include "fastrpc_cap.h"
#include "fastrpc_common.h"
#include "fastrpc_config.h"
#include "fastrpc_context.h"
#include "fastrpc_hash_table.h"
#include "fastrpc_internal.h"
#include "fastrpc_latency.h"
#include "fastrpc_log.h"
#include "fastrpc_mem.h"
#include "fastrpc_notif.h"
#include "fastrpc_perf.h"
#include "fastrpc_pm.h"
#include "fastrpc_procbuf.h"
#include "listener_android.h"
#include "log_config.h"
#include "platform_libs.h"
#include "remotectl.h"
#include "remotectl1.h"
#include "rpcmem_internal.h"
#include "shared.h"
#include "verify.h"
#ifndef NO_HAL
#include "DspClient.h"
#endif
#include "fastrpc_process_attributes.h"
#include "fastrpc_trace.h"

#ifndef ENABLE_UPSTREAM_DRIVER_INTERFACE
#define DSP_MOUNT_LOCATION "/dsp/"
#define DSP_DOM_LOCATION "/dsp/xdsp"
#else
#define DSP_MOUNT_LOCATION "/usr/lib/dsp/"
#define DSP_DOM_LOCATION "/usr/lib/dsp/xdspn"
#endif
#define VENDOR_DSP_LOCATION "/vendor/dsp/"
#define VENDOR_DOM_LOCATION "/vendor/dsp/xdsp/"

#ifdef LE_ENABLE
#define PROPERTY_VALUE_MAX                                                     \
  92 // as this macro is defined in cutils for Android platforms, defined
     // explicitly for LE platform
#elif (defined _ANDROID) || (defined ANDROID)
/// TODO: Bharath #include "cutils/properties.h"
#define PROPERTY_VALUE_MAX 92
#else
#define PROPERTY_VALUE_MAX 92
#endif

#ifndef _WIN32
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#endif // __WIN32

#ifndef INT_MAX
#define INT_MAX (int)(-1)
#endif

#ifndef ULLONG_MAX
#define ULLONG_MAX (unsigned long long int)(-1)
#endif

#define MAX_DMA_HANDLES 256
#define MAX_DLERRSTR_LEN 255
#define ENV_PATH_LEN 256

#define FASTRPC_TRACE_INVOKE_START "fastrpc_trace_invoke_start"
#define FASTRPC_TRACE_INVOKE_END "fastrpc_trace_invoke_end"
#define FASTRPC_TRACE_LOG(k, handle, sc)                                       \
  if (fastrpc_trace == 1 && !IS_STATIC_HANDLE(handle)) {                       \
    FARF(ALWAYS, "%s: sc 0x%x", (k), (sc));                                    \
  }

/* Number of dsp library instances allowed per process. */
#define MAX_LIB_INSTANCE_ALLOWED 1
#define ERRNO (errno ? errno : nErr ? nErr : -1)

static void check_multilib_util(void);

/* Array to store fastrpc library names. */
static const char *fastrpc_library[NUM_DOMAINS] = {
    "libadsprpc.so", "libmdsprpc.so", "libsdsprpc.so", "libcdsprpc.so",
    "libcdsprpc.so"};

/* Array to store env variable names. */
static char *fastrpc_dsp_lib_refcnt[NUM_DOMAINS];
static int total_dsp_lib_refcnt = 0;

/* Function to free the memory allocated for env variable names */
inline static void deinit_fastrpc_dsp_lib_refcnt(void) {
  int ii = 0;

  FOR_EACH_DOMAIN_ID(ii) {
    if (fastrpc_dsp_lib_refcnt[ii]) {
      unsetenv(fastrpc_dsp_lib_refcnt[ii]);
      free(fastrpc_dsp_lib_refcnt[ii]);
      fastrpc_dsp_lib_refcnt[ii] = NULL;
    }
  }
}

enum fastrpc_proc_attr {
  FASTRPC_MODE_DEBUG = 0x1,
  FASTRPC_MODE_PTRACE = 0x2,
  FASTRPC_MODE_CRC = 0x4,
  FASTRPC_MODE_UNSIGNED_MODULE = 0x8,
  FASTRPC_MODE_ADAPTIVE_QOS = 0x10,
  FASTRPC_MODE_SYSTEM_PROCESS = 0x20,
  FASTRPC_MODE_PRIVILEGED = 0x40, // this attribute will be populated in kernel
  // Attribute to enable pd dump feature for both signed/unsigned pd
  FASTRPC_MODE_ENABLE_PDDUMP = 0x80,
  // System attribute to enable pd dump debug data collection on rooted devices
  FASTRPC_MODE_DEBUG_PDDUMP = 0x100,
  // Attribute to enable kernel perf keys data collection
  FASTRPC_MODE_PERF_KERNEL = 0x200,
  // Attribute to enable dsp perf keys data collection
  FASTRPC_MODE_PERF_DSP = 0x400,
  // Attribute to log iregion buffer
  FASTRPC_MODE_ENABLE_IREGION_LOG = 0x800,
  // Attribute to enable QTF tracing on DSP
  FASTRPC_MODE_ENABLE_QTF_TRACING = 0x1000,
  // Attribute to enable debug logging
  FASTRPC_MODE_ENABLE_DEBUG_LOGGING = 0x2000,
  // Attribute to set caller level for heap
  FASTRPC_MODE_CALLER_LEVEL_MASK = 0xE000,
  // Attribute to enable uaf for heap
  FASTRPC_MODE_ENABLE_UAF = 0x10000,
  // Attribute to launch system unsignedPD on CDSP
  FASTRPC_MODE_SYSTEM_UNSIGNED_PD = 0x20000,
  // Reserved attribute bit for sys mon application
  FASTRPC_MODE_SYSMON_RESERVED_BIT = 0x40000000,
  // Attribute to enable log packet
  FASTRPC_MODE_LOG_PACKET = 0x40000,
  // Attribute to set Leak detect for heap. Bits 19-20 are reserved for leak
  // detect.
  FASTRPC_MODE_ENABLE_LEAK_DETECT = 0x180000,
  // Attribute to change caller stack for heap. Bits 21-23 are reserved the call
  // stack num
  FASTRPC_MODE_CALLER_STACK_NUM = 0xE00000,
};

#define M_CRCLIST (64)
#define IS_DEBUG_MODE_ENABLED(var) (var & FASTRPC_MODE_DEBUG)
#define IS_CRC_CHECK_ENABLED(var) (var & FASTRPC_MODE_CRC)
extern int perf_v2_kernel;
extern int perf_v2_dsp;
#define IS_KERNEL_PERF_ENABLED(var)                                            \
  ((var & FASTRPC_MODE_PERF_KERNEL) && perf_v2_kernel)
#define IS_DSP_PERF_ENABLED(var) ((var & FASTRPC_MODE_PERF_DSP) && perf_v2_dsp)
#define IS_QTF_TRACING_ENABLED(var) (var & FASTRPC_MODE_ENABLE_QTF_TRACING)

#define POLY32                                                                 \
  0x04C11DB7 // G(x) = x^32+x^26+x^23+x^22+x^16+x^12
             // +x^11+x^10+x^8+x^7+x^5+x^4+x^2+x+1

#define DEFAULT_UTHREAD_PRIORITY 0xC0
#define DEFAULT_UTHREAD_STACK_SIZE 16 * 1024
#define DEFAULT_PD_INITMEM_SIZE 3 * 1024 * 1024

/* Valid QuRT thread priorities are 1 to 254 */
#define MIN_THREAD_PRIORITY 1
#define MAX_THREAD_PRIORITY 254

/* Remote thread stack size should be between 16 KB and 8 MB */
#define MIN_UTHREAD_STACK_SIZE (16 * 1024)
#define MAX_UTHREAD_STACK_SIZE (8 * 1024 * 1024)

/* Remote userpd init memlen should be between 3MB and 200MB */
#define MIN_PD_INITMEM_SIZE (3 * 1024 * 1024)
#define MAX_PD_INITMEM_SIZE (200 * 1024 * 1024)

#define PM_TIMEOUT_MS 5

enum handle_list_id {
  MULTI_DOMAIN_HANDLE_LIST_ID = 1,
  NON_DOMAIN_HANDLE_LIST_ID = 2,
  REVERSE_HANDLE_LIST_ID = 3,
};

const char *ENV_DEBUG_VAR_NAME[] = {"FASTRPC_PROCESS_ATTRS",
                                    "FASTRPC_DEBUG_TRACE",
                                    "FASTRPC_DEBUG_TESTSIG",
                                    "FASTRPC_PERF_KERNEL",
                                    "FASTRPC_PERF_ADSP",
                                    "FASTRPC_PERF_FREQ",
                                    "FASTRPC_DEBUG_SYSTRACE",
                                    "FASTRPC_DEBUG_PDDUMP",
                                    "FASTRPC_PROCESS_ATTRS_PERSISTENT",
                                    "ro.debuggable"};
const char *ANDROIDP_DEBUG_VAR_NAME[] = {"vendor.fastrpc.process.attrs",
                                         "vendor.fastrpc.debug.trace",
                                         "vendor.fastrpc.debug.testsig",
                                         "vendor.fastrpc.perf.kernel",
                                         "vendor.fastrpc.perf.adsp",
                                         "vendor.fastrpc.perf.freq",
                                         "vendor.fastrpc.debug.systrace",
                                         "vendor.fastrpc.debug.pddump",
                                         "persist.vendor.fastrpc.process.attrs",
                                         "ro.build.type"};
const char *ANDROID_DEBUG_VAR_NAME[] = {"fastrpc.process.attrs",
                                        "fastrpc.debug.trace",
                                        "fastrpc.debug.testsig",
                                        "fastrpc.perf.kernel",
                                        "fastrpc.perf.adsp",
                                        "fastrpc.perf.freq",
                                        "fastrpc.debug.systrace",
                                        "fastrpc.debug.pddump",
                                        "persist.fastrpc.process.attrs",
                                        "ro.build.type"};

const char *SUBSYSTEM_NAME[] = {"adsp",  "mdsp",     "sdsp",     "cdsp",
                                "cdsp1", "reserved", "reserved", "reserved"};

/* Strings for trace event logging */
#define INVOKE_BEGIN_TRACE_STR "fastrpc_msg: userspace_call: begin"
#define INVOKE_END_TRACE_STR "fastrpc_msg: userspace_call: end"

static const size_t invoke_begin_trace_strlen =
    sizeof(INVOKE_BEGIN_TRACE_STR) - 1;
static const size_t invoke_end_trace_strlen = sizeof(INVOKE_END_TRACE_STR) - 1;

int NO_ENV_DEBUG_VAR_NAME_ARRAY_ELEMENTS =
    sizeof(ENV_DEBUG_VAR_NAME) / sizeof(char *);
int NO_ANDROIDP_DEBUG_VAR_NAME_ARRAY_ELEMENTS =
    sizeof(ANDROIDP_DEBUG_VAR_NAME) / sizeof(char *);
int NO_ANDROID_DEBUG_VAR_NAME_ARRAY_ELEMENTS =
    sizeof(ANDROID_DEBUG_VAR_NAME) / sizeof(char *);

/* Shell prefix for signed and unsigned */
const char *const SIGNED_SHELL = "fastrpc_shell_";
const char *const UNSIGNED_SHELL = "fastrpc_shell_unsigned_";

struct handle_info {
  QNode qn;
  struct handle_list *hlist;
  remote_handle64 local;
  remote_handle64 remote;
  char *name;
  /* priority of handle*/
  uint32_t priority;
};

// Fastrpc client notification request node to be queued to <notif_list>
struct fastrpc_notif {
  QNode qn;
  remote_rpc_notif_register_t notif;
};

struct other_handle_list { // For non-domain and reverse handle list
  QList ql;
};

// Fastrpc timer function for RPC timeout
typedef struct fastrpc_timer_info {
  timer_t timer;
  uint64_t timeout_millis;
  int domain;
  int sc;
  int handle;
  pid_t tid;
} fastrpc_timer;

struct fastrpc_apps_info {
  /* Primary data structure of a remote session */
  struct handle_list hlist;

  /* List of reverse handles opened by a remote session */
  struct other_handle_list reverse_hlist;

  ADD_DOMAIN_HASH();
};

/* Declare effective domain-id based hash-table of remote sessions */
DECLARE_HASH_TABLE(fastrpc_apps_info, struct fastrpc_apps_info);

/*
 * Macro to check if remote session is already open on given
 * effective domain id
 */
#define IS_SESSION_OPEN_ALREADY(hlist) (hlist->dev != -1)

/* Mutex to protect notif_list */
static pthread_mutex_t update_notif_list_mut;

static pthread_key_t tlsKey = INVALID_KEY;

// Flag to check if there is any client notification request
static bool fastrpc_notif_flag = false;
static int fastrpc_trace = 0;
static uint32_t fastrpc_wake_lock_enable[NUM_DOMAINS_EXTEND] = {0};

#ifndef NO_HAL
static pthread_mutex_t dsp_client_mut;
static void *dsp_client_instance[NUM_SESSIONS];
#endif

static int domain_init(int domain, int *dev);
static void domain_deinit(int domain);
static int open_device_node(int domain_id);
static int close_device_node(int domain_id, int dev);
extern int apps_mem_table_init(void);
extern void apps_mem_table_deinit(void);

static uint32_t crc_table[256];
uint32 timer_expired = 0;

int get_hlist_domain(struct handle_list *search) {
  struct fastrpc_apps_info *tmp = NULL, *me = NULL;
  int domain = DEFAULT_DOMAIN_ID;

  pthread_mutex_lock(&info.mut);
  HASH_ITER(hh, info.tbl, me, tmp) {
    if (search == &me->hlist) {
      domain = me->domain;
      pthread_mutex_unlock(&info.mut);
      break;
    }
  }
  pthread_mutex_unlock(&info.mut);
  return domain;
}

struct handle_list *get_hlist(int domain) {
  struct fastrpc_apps_info *tmp = NULL;
  GET_HASH_NODE(struct fastrpc_apps_info, domain, tmp);
  if (!tmp) {
    return NULL;
  }
  return &tmp->hlist;
}

bool is_valid_hlist(struct handle_list *hlist) {
  struct fastrpc_apps_info *tmp = NULL, *me = NULL;

  pthread_mutex_lock(&info.mut);
  HASH_ITER(hh, info.tbl, me, tmp) {
    if (hlist == &me->hlist) {
      pthread_mutex_unlock(&info.mut);
      return true;
    }
  }
  pthread_mutex_unlock(&info.mut);
  return false;
}

void set_thread_context(int domain) {
  struct handle_list *hlist;

  hlist = get_hlist(domain);
  if (tlsKey != INVALID_KEY && hlist) {
    pthread_setspecific(tlsKey, (void *)hlist);
  }
}

int get_device_fd(int domain) {
  struct handle_list *hlist;

  hlist = get_hlist(domain);
  if (hlist && (hlist->dev != -1)) {
    return hlist->dev;
  } else {
    return -1;
  }
}

int fastrpc_session_open(int domain, int *dev) {
  int device = -1;
  struct handle_list *hlist;

  hlist = get_hlist(domain);
  if (!hlist)
    return 0;
  if (IS_SESSION_OPEN_ALREADY(hlist)) {
    *dev = hlist->dev;
    return 0;
  }

  device = open_device_node(domain);
  if (device >= 0) {
    *dev = device;
    return 0;
  }
  return AEE_ECONNREFUSED;
}

void fastrpc_session_close(int domain, int dev) {
  struct handle_list *hlist;

  hlist = get_hlist(domain);
  if (!hlist)
    return;
  if ((hlist->dev == INVALID_DEVICE) && (dev != INVALID_DEVICE)) {
    close(dev);
  } else if ((hlist->dev != INVALID_DEVICE) && (dev == INVALID_DEVICE)) {
    close(hlist->dev);
    hlist->dev = INVALID_DEVICE;
  }
  return;
}

int fastrpc_session_get(int domain) {
  int ref = -1;
  struct handle_list *hlist;
  do {
    hlist = get_hlist(domain);
    if (hlist) {
      pthread_mutex_lock(&hlist->mut);
      if (hlist->state == FASTRPC_DOMAIN_STATE_DEINIT) {
        pthread_mutex_unlock(&hlist->mut);
        return AEE_ENOTINITIALIZED;
      }
      hlist->ref++;
      ref = hlist->ref;
      pthread_mutex_unlock(&hlist->mut);
      set_thread_context(domain);
      FARF(RUNTIME_RPC_HIGH, "%s, domain %d, state %d, ref %d\n", __func__,
           domain, hlist->state, ref);
    } else {
      return AEE_ENOTINITIALIZED;
    }
  } while (0);
  return 0;
}

int fastrpc_session_put(int domain) {
  int ref = -1;
  struct handle_list *hlist;
  do {
    hlist = get_hlist(domain);
    if (hlist) {
      pthread_mutex_lock(&hlist->mut);
      hlist->ref--;
      ref = hlist->ref;
      pthread_mutex_unlock(&hlist->mut);
      FARF(RUNTIME_RPC_HIGH, "%s, domain %d, state %d, ref %d\n", __func__,
           domain, hlist->state, ref);
    } else {
      return AEE_ENOTINITIALIZED;
    }
  } while (0);
  return ref;
}

int fastrpc_session_dev(int domain, int *dev) {
  struct handle_list *hlist;

  *dev = INVALID_DEVICE;
  hlist = get_hlist(domain);
  if (!hlist)
    return AEE_ENOTINITIALIZED;
  if (!IS_VALID_EFFECTIVE_DOMAIN_ID(domain))
    return AEE_ENOTINITIALIZED;
  do {
    if (hlist) {
      pthread_mutex_lock(&hlist->mut);
      if (hlist->state == FASTRPC_DOMAIN_STATE_DEINIT) {
        pthread_mutex_unlock(&hlist->mut);
        return AEE_ENOTINITIALIZED;
      }
      if (hlist->dev < 0) {
        pthread_mutex_unlock(&hlist->mut);
        return AEE_ENOTINITIALIZED;
      } else {
        *dev = hlist->dev;
        pthread_mutex_unlock(&hlist->mut);
        return AEE_SUCCESS;
      }
      pthread_mutex_unlock(&hlist->mut);
    } else {
      return AEE_ENOTINITIALIZED;
    }
  } while (0);
  return AEE_ENOTINITIALIZED;
}

int check_rpc_error(int err) {
  if (check_error_code_change_present() == 1) {
    if (err > KERNEL_ERRNO_START && err <= HLOS_ERR_END) // driver or HLOS err
      return 0;
    else if (err > (int)DSP_AEE_EOFFSET &&
             err <= (int)DSP_AEE_EOFFSET + 1024) // DSP err
      return 0;
    else if (err == AEE_ENOSUCH ||
             err == AEE_EINTERRUPTED) // common DSP HLOS err
      return 0;
    else
      return -1;
  } else
    return 0;
}

static void GenCrc32Tab(uint32_t GenPoly, uint32_t *crctab) {
  uint32_t crc;
  int i, j;

  for (i = 0; i < 256; i++) {
    crc = i << 24;
    for (j = 0; j < 8; j++) {
      crc = (crc << 1) ^ (crc & 0x80000000 ? GenPoly : 0);
    }
    crctab[i] = crc;
  }
}

static uint32_t crc32_lut(unsigned char *data, int nbyte, uint32_t *crctab) {
  uint32_t crc = 0;
  if (!data || !crctab)
    return 0;

  while (nbyte--) {
    crc = (crc << 8) ^ crctab[(crc >> 24) ^ *data++];
  }
  return crc;
}

int property_get_int32(const char *name, int value) { return 0; }

int property_get(const char *name, int *def, int *value) { return 0; }

int fastrpc_get_property_int(fastrpc_properties UserPropKey, int defValue) {
  if (((int)UserPropKey > NO_ENV_DEBUG_VAR_NAME_ARRAY_ELEMENTS)) {
    FARF(ERROR,
         "%s: Index %d out-of-bound for ENV_DEBUG_VAR_NAME array of len %d",
         __func__, UserPropKey, NO_ENV_DEBUG_VAR_NAME_ARRAY_ELEMENTS);
    return defValue;
  }
  const char *env = getenv(ENV_DEBUG_VAR_NAME[UserPropKey]);
  if (env != 0)
    return (int)atoi(env);
#if !defined(LE_ENABLE)          // Android platform
#if !defined(SYSTEM_RPC_LIBRARY) // vendor library
  if (((int)UserPropKey > NO_ANDROIDP_DEBUG_VAR_NAME_ARRAY_ELEMENTS)) {
    FARF(
        ERROR,
        "%s: Index %d out-of-bound for ANDROIDP_DEBUG_VAR_NAME array of len %d",
        __func__, UserPropKey, NO_ANDROIDP_DEBUG_VAR_NAME_ARRAY_ELEMENTS);
    return defValue;
  }
  return (int)property_get_int32(ANDROIDP_DEBUG_VAR_NAME[UserPropKey],
                                 defValue);
#else // system library
  if (((int)UserPropKey > NO_ANDROID_DEBUG_VAR_NAME_ARRAY_ELEMENTS)) {
    FARF(ERROR,
         "%s: Index %d out-of-bound for ANDROID_DEBUG_VAR_NAME array of len %d",
         __func__, UserPropKey, NO_ANDROID_DEBUG_VAR_NAME_ARRAY_ELEMENTS);
    return defValue;
  }
  return (int)property_get_int32(ANDROID_DEBUG_VAR_NAME[UserPropKey], defValue);
#endif
#else // non-Android platforms
  return defValue;
#endif
}
int fastrpc_get_property_string(fastrpc_properties UserPropKey, char *value,
                                char *defValue) {
  int len = 0;
  if (((int)UserPropKey > NO_ENV_DEBUG_VAR_NAME_ARRAY_ELEMENTS)) {
    FARF(ERROR,
         "%s: Index %d out-of-bound for ENV_DEBUG_VAR_NAME array of len %d",
         __func__, UserPropKey, NO_ENV_DEBUG_VAR_NAME_ARRAY_ELEMENTS);
    return len;
  }
  char *env = getenv(ENV_DEBUG_VAR_NAME[UserPropKey]);
  if (env != 0) {
    len = strlen(env);
    std_memscpy(value, PROPERTY_VALUE_MAX, env, len + 1);
    return len;
  }
#if !defined(LE_ENABLE)          // Android platform
#if !defined(SYSTEM_RPC_LIBRARY) // vendor library
  if (((int)UserPropKey > NO_ANDROIDP_DEBUG_VAR_NAME_ARRAY_ELEMENTS)) {
    FARF(
        ERROR,
        "%s: Index %d out-of-bound for ANDROIDP_DEBUG_VAR_NAME array of len %d",
        __func__, UserPropKey, NO_ANDROIDP_DEBUG_VAR_NAME_ARRAY_ELEMENTS);
    return len;
  }
  return property_get(ANDROIDP_DEBUG_VAR_NAME[UserPropKey], (int *)value,
                      (int *)defValue);
#else // system library
  if (((int)UserPropKey > NO_ANDROID_DEBUG_VAR_NAME_ARRAY_ELEMENTS)) {
    FARF(ERROR,
         "%s: Index %d out-of-bound for ANDROID_DEBUG_VAR_NAME array of len %d",
         __func__, UserPropKey, NO_ANDROID_DEBUG_VAR_NAME_ARRAY_ELEMENTS);
    return len;
  }
  return property_get(ANDROID_DEBUG_VAR_NAME[UserPropKey], value, defValue);
#endif
#else // non-Android platforms
  if (defValue != NULL) {
    len = strlen(defValue);
    std_memscpy(value, PROPERTY_VALUE_MAX, defValue, len + 1);
  }
  return len;
#endif
}

/*
        is_first_reverse_rpc_call:

        Checks if first reverse RPC call is already done
        Args:
                @domain	-	Remote subsystem domain ID
                @handle	-	Session handle
                @sc	-	Scalar
        Returns: 1 - if first reverse RPC call already done
                 0 - otherwise
*/

static inline int is_first_reverse_rpc_call(int domain, remote_handle handle,
                                            uint32_t sc) {
  int ret = 0;
  struct handle_list *hlist;

  hlist = get_hlist(domain);
  if (!hlist)
    return 0; // TODO: Check the return value
  if (IS_REVERSE_RPC_CALL(handle, sc) && IS_SESSION_OPEN_ALREADY(hlist)) {
    if (hlist->first_revrpc_done)
      ret = 0;
    else {
      hlist->first_revrpc_done = 1;
      ret = 1;
    }
  }
  return ret;
}

static inline void trace_marker_init(int domain) {
  const char TRACE_MARKER_FILE[] = "/sys/kernel/tracing/trace_marker";
  struct handle_list *hlist;

  hlist = get_hlist(domain);
  if (!hlist)
    return;
  if (IS_QTF_TRACING_ENABLED(hlist->procattrs)) {
    hlist->trace_marker_fd = open(TRACE_MARKER_FILE, O_WRONLY);
    ;
    if (hlist->trace_marker_fd < 0) {
      FARF(ERROR, "Error: %s: failed to open '%s' for domain %d, errno %d (%s)",
           __func__, TRACE_MARKER_FILE, domain, errno, strerror(errno));
    }
  }
}

static inline void trace_marker_deinit(int domain) {
  struct handle_list *hlist;

  hlist = get_hlist(domain);
  if (!hlist)
    return;
  if (hlist->trace_marker_fd > 0) {
    close(hlist->trace_marker_fd);
    hlist->trace_marker_fd = -1;
  }
}

int get_logger_state(int domain) {
  int ret = AEE_EBADSTATE;
  struct handle_list *hlist;

  hlist = get_hlist(domain);
  if (!hlist)
    return ret;
  if (hlist->disable_exit_logs) {
    ret = AEE_SUCCESS;
  }
  return ret;
}

/* Thread function that will be invoked to update remote user PD parameters */
int fastrpc_set_remote_uthread_params(int domain) {
  int nErr = AEE_SUCCESS, paramsLen = 2;
  remote_handle64 handle = INVALID_HANDLE;
  struct fastrpc_thread_params *th_params;
  struct handle_list *hlist;

  hlist = get_hlist(domain);
  if (!hlist)
    return AEE_EBADDOMAIN;
  th_params = &hlist->th_params;
  VERIFYC(th_params != NULL, AEE_ERPC);
  if ((handle = get_remotectl1_handle(domain)) != INVALID_HANDLE) {
    nErr = remotectl1_set_param(handle, th_params->reqID, (uint32_t *)th_params,
                                paramsLen);
    if ((nErr == DSP_AEE_EOFFSET + AEE_ERPC) ||
        nErr == DSP_AEE_EOFFSET + AEE_ENOSUCHMOD) {
      FARF(ALWAYS,
           "Warning 0x%x: %s: remotectl1 domains not supported for domain %d\n",
           nErr, __func__, domain);
      fastrpc_update_module_list(DOMAIN_LIST_DEQUEUE, domain,
                                 NULL, handle, NULL,
                                 FASTRPC_RESERVED_HANDLE_PRIO);

      // Set remotectlhandle to INVALID_HANDLE, so that all subsequent calls are
      // non-domain calls
      hlist->remotectlhandle = INVALID_HANDLE;
      VERIFY(AEE_SUCCESS ==
             (nErr = remotectl_set_param(th_params->reqID,
                                         (uint32_t *)th_params, paramsLen)));
    }
  } else {
    VERIFY(AEE_SUCCESS ==
           (nErr = remotectl_set_param(th_params->reqID, (uint32_t *)th_params,
                                       paramsLen)));
  }
bail:
  if (nErr != AEE_SUCCESS) {
    if (th_params) {
      FARF(ERROR,
           "Error 0x%x: %s failed domain %d thread priority %d stack size %d "
           "(errno %s)",
           nErr, __func__, domain, th_params->thread_priority,
           th_params->stack_size, strerror(errno));
    } else {
      FARF(ERROR, "Error 0x%x: %s failed", nErr, __func__);
    }
  } else {
    FARF(ALWAYS,
         "Successfully set remote user thread priority to %d and stack size to "
         "%d for domain %d",
         th_params->thread_priority, th_params->stack_size, domain);
  }
  return nErr;
}

static inline bool is_valid_local_handle(int domain,
                                         struct handle_info *hinfo) {
  QNode *pn;
  int ii = 0;
  struct handle_list *hlist;
  if (domain == -1) {
    FOR_EACH_EFFECTIVE_DOMAIN_ID(ii) {
      hlist = get_hlist(ii);
      if (!hlist)
        continue;
      pthread_mutex_lock(&hlist->lmut);
      QLIST_FOR_ALL(&hlist->ql, pn) {
        struct handle_info *hi = STD_RECOVER_REC(struct handle_info, qn, pn);
        if (hi == hinfo) {
          pthread_mutex_unlock(&hlist->lmut);
          return true;
        }
      }
      pthread_mutex_unlock(&hlist->lmut);
    }
  } else {
    hlist = get_hlist(domain);
    if (!hlist)
      return false;
    pthread_mutex_lock(&hlist->lmut);
    QLIST_FOR_ALL(&hlist->ql, pn) {
      struct handle_info *hi = STD_RECOVER_REC(struct handle_info, qn, pn);
      if (hi == hinfo) {
        pthread_mutex_unlock(&hlist->lmut);
        return true;
      }
    }
    pthread_mutex_unlock(&hlist->lmut);
  }
  return false;
}

static int verify_local_handle(int domain, remote_handle64 local) {
  struct handle_info *hinfo = (struct handle_info *)(uintptr_t)local;
  int nErr = AEE_SUCCESS;

  VERIFYC((local != (remote_handle64)-1) && hinfo, AEE_EINVHANDLE);
  VERIFYC(is_valid_local_handle(domain, hinfo), AEE_EINVHANDLE);
  VERIFYC(is_valid_hlist(hinfo->hlist), AEE_EINVHANDLE);
  VERIFYC(QNode_IsQueuedZ(&hinfo->qn), AEE_EINVHANDLE);
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(RUNTIME_RPC_HIGH, "Error 0x%x: %s failed. handle 0x%" PRIx64 "\n",
         nErr, __func__, local);
  }
  return nErr;
}

int get_domain_from_handle(remote_handle64 local, int *domain) {
  struct handle_info *hinfo = (struct handle_info *)(uintptr_t)local;
  int dom, nErr = AEE_SUCCESS;

  VERIFY(AEE_SUCCESS == (nErr = verify_local_handle(-1, local)));
  dom = get_hlist_domain(hinfo->hlist);
  VERIFYM(IS_VALID_EFFECTIVE_DOMAIN_ID(dom), AEE_EINVHANDLE,
          "Error 0x%x: domain mapped to handle is out of range domain %d "
          "handle 0x%" PRIx64 "\n",
          nErr, dom, local);
  *domain = dom;
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(RUNTIME_RPC_HIGH, "Error 0x%x: %s failed. handle 0x%" PRIx64 "\n",
         nErr, __func__, local);
  }
  return nErr;
}

/**
 * Function to get domain id from domain name
 * @param[in] : Domain name of DSP
 * @param[int]: Domain name length
 * @return : Domain ID
 */
static int get_domain_from_domain_name(const char *domain_name,
                                       int domain_name_len) {
  int domain = INVALID_DOMAIN_ID;

  if (domain_name_len < std_strlen(SUBSYSTEM_NAME[ADSP_DOMAIN_ID])) {
    FARF(ERROR, "ERROR: %s Invalid domain name length: %u\n", __func__,
         domain_name_len);
    goto bail;
  }
  if (domain_name) {
    if (!std_strncmp(domain_name, SUBSYSTEM_NAME[ADSP_DOMAIN_ID],
                     std_strlen(SUBSYSTEM_NAME[ADSP_DOMAIN_ID]))) {
      domain = ADSP_DOMAIN_ID;
    } else if (!std_strncmp(domain_name, SUBSYSTEM_NAME[MDSP_DOMAIN_ID],
                            std_strlen(SUBSYSTEM_NAME[MDSP_DOMAIN_ID]))) {
      domain = MDSP_DOMAIN_ID;
    } else if (!std_strncmp(domain_name, SUBSYSTEM_NAME[SDSP_DOMAIN_ID],
                            std_strlen(SUBSYSTEM_NAME[SDSP_DOMAIN_ID]))) {
      domain = SDSP_DOMAIN_ID;
    } else if (!std_strncmp(domain_name, SUBSYSTEM_NAME[CDSP1_DOMAIN_ID],
                            std_strlen(SUBSYSTEM_NAME[CDSP1_DOMAIN_ID]))) {
      domain = CDSP1_DOMAIN_ID;
    } else if (!std_strncmp(domain_name, SUBSYSTEM_NAME[CDSP_DOMAIN_ID],
                            std_strlen(SUBSYSTEM_NAME[CDSP_DOMAIN_ID]))) {
      domain = CDSP_DOMAIN_ID;
    } else {
      FARF(ERROR, "ERROR: %s Invalid domain name: %s\n", __func__, domain_name);
    }
  }
  VERIFY_IPRINTF("%s: %d\n", __func__, domain);
bail:
  return domain;
}

static const char *get_domain_from_id(int domain_id) {
  const char *uri_domain_suffix;
  switch (domain_id) {
  case ADSP_DOMAIN_ID:
    uri_domain_suffix = ADSP_DOMAIN;
    break;
  case CDSP_DOMAIN_ID:
    uri_domain_suffix = CDSP_DOMAIN;
    break;
  case CDSP1_DOMAIN_ID:
    uri_domain_suffix = CDSP1_DOMAIN;
    break;
  case MDSP_DOMAIN_ID:
    uri_domain_suffix = MDSP_DOMAIN;
    break;
  case SDSP_DOMAIN_ID:
    uri_domain_suffix = SDSP_DOMAIN;
    break;
  default:
    uri_domain_suffix = "invalid domain";
    break;
  }
  return uri_domain_suffix;
}

#define IS_CONST_HANDLE(h) (((h) < 0xff) ? 1 : 0)

static int get_handle_remote(remote_handle64 local, remote_handle64 *remote) {
  struct handle_info *hinfo = (struct handle_info *)(uintptr_t)local;
  int nErr = AEE_SUCCESS;

  VERIFY(AEE_SUCCESS == (nErr = verify_local_handle(-1, local)));
  *remote = hinfo->remote;
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error %x: get handle remote failed %p\n", nErr, &local);
  }
  return nErr;
}

inline int is_smmu_enabled(void) {
  return (get_hlist(get_current_domain())->info & FASTRPC_INFO_SMMU);
}

/**
 * print_open_handles() - Function to print all open handles.
 * @domain: domain of handles to be printed.
 * Return: void.
 */
static void print_open_handles(int domain) {
  struct handle_info *hi = NULL;
  struct handle_list *hlist = NULL;
  QNode *pn = NULL;

  hlist = get_hlist(domain);
  if (!hlist)
    return;
  FARF(ALWAYS, "List of open handles on domain %d:\n", domain);
  pthread_mutex_lock(&hlist->mut);
  QLIST_FOR_ALL(&hlist->ql, pn) {
    hi = STD_RECOVER_REC(struct handle_info, qn, pn);
    if (hi && hi->name)
      FARF(ALWAYS, "%s, handle 0x%" PRIx64 "", hi->name, hi->remote);
  }
  pthread_mutex_unlock(&hlist->mut);
}

/**
 * get_lib_name() - Function to get lib name from uri.
 * @uri: uri for the lib.
 * Return: @lib_name or NULL
 */
static char *get_lib_name(const char *uri) {
  char *library_name = NULL;
  const char SO_EXTN[] = ".so";
  const char LIB_EXTN[] = "lib";
  const char *start = NULL, *end = NULL;
  unsigned int length = 0;
  int nErr = AEE_SUCCESS;

  VERIFY(uri);
  start = std_strstr(uri, LIB_EXTN);
  if (start) {
    end = std_strstr(start, SO_EXTN);
    if (end && end > start) {
      /* add extension size to print .so also */
      length = (unsigned int)(end - start) + std_strlen(SO_EXTN);
      /* allocate length + 1 to include \0 */
      VERIFYC(NULL != (library_name = (char *)calloc(1, length + 1)),
              AEE_ENOMEMORY);
      std_strlcpy(library_name, start, length + 1);
      return library_name;
    }
  }
bail:
  FARF(ERROR, "Warning 0x%x: %s failed for uri %s", nErr, __func__, uri);
  return NULL;
}

/**
 * get_handle_priority: returns the priority associated with the local handle.
 *
 * @param[in] local            : local handle
 * @param[out] handle_priority : priority associated with local handle
 *
 * returns  AEE_SUCCESS if successful
 *          AEE_EINVHANDLE if invalid handle is passed
 */
static int get_handle_priority(remote_handle64 local,
                               uint32_t *handle_priority) {
  struct handle_info *hinfo = (struct handle_info *)(uintptr_t)local;
  int nErr = AEE_SUCCESS;

  VERIFY(AEE_SUCCESS == (nErr = verify_local_handle(-1, local)));
  *handle_priority = hinfo->priority;
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed for handle 0x%" PRIx64 "\n", nErr,
         __func__, local);
  }
  return nErr;
}

/** get_handle_priority_from_uri : Get handle priority from URI
 *
 * @param[in] uri  : URI string
 * @param[out] handle_priority : Handle priority
 * Returns:
 *		AEE_SUCCESS: if valid uri is extracted from uri
 *		AEE_EBADPARM : if uri has the priority token but invalid user
 *priority
 */
static int get_handle_priority_from_uri(const char *uri,
                                        unsigned int *handle_priority) {
  int nErr = 0;
  unsigned int prio = 0;
  char *priority_uri = NULL;
  char *lib_name = NULL;

  if (NULL != (priority_uri = std_strstr(uri, FASTRPC_URI_PRIORITY_TOKEN))) {
    priority_uri = priority_uri + std_strlen(FASTRPC_URI_PRIORITY_TOKEN);
    prio = strtol(priority_uri, NULL, 10);
    VERIFYC(IS_VALID_USER_HANDLE_PRIORITY(prio), AEE_EBADPARM);
    *handle_priority = prio;
  } else {
    /**
     * if no priority specified in uri, return FASTRPC_RESERVED_HANDLE_PRIO
     */
    lib_name = get_lib_name(uri);
    *handle_priority = FASTRPC_RESERVED_HANDLE_PRIO;
    FARF(ALWAYS, "WARNING: %s: %s lib opened handle without priority token",
         __func__, lib_name);
  }
bail:
  if (nErr) {
    FARF(ERROR, "Error 0x%x: %s: bad user handle priority %u in uri", nErr,
         __func__, prio);
  }
  return nErr;
}

static int fastrpc_alloc_handle(int domain, QList *me, remote_handle64 remote,
                                remote_handle64 *local, const char *name,
                                uint32_t handle_priority) {
  struct handle_info *hinfo = {0};
  struct handle_list *hlist = NULL;
  int nErr = 0;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  VERIFYC(NULL != (hinfo = calloc(1, sizeof(*hinfo))), AEE_ENOMEMORY);
  hinfo->local = (remote_handle64)(uintptr_t)hinfo;
  hinfo->remote = remote;
  hinfo->name = get_lib_name(name);
  hinfo->hlist = hlist;
  /**
   * For user handle open calls, the valid priority ranges from
   * FASTRPC_HANDLE_PRIORITY_MAX and FASTRPC_HANDLE_PRIORITY_MIN.
   * FASTRPC_RESERVED_HANDLE_PRIO is used for framework calls or
   * for backward compatability when the dsp doesn't support
   * priority handles.
   */
  hinfo->priority = handle_priority;
  *local = hinfo->local;

  QNode_CtorZ(&hinfo->qn);
  pthread_mutex_lock(&hlist->lmut);
  QList_PrependNode(me, &hinfo->qn);
  pthread_mutex_unlock(&hlist->lmut);
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR,
         "Error 0x%x: %s failed for local handle 0x%x, remote handle 0x%x, "
         "domain %d\n",
         nErr, __func__, local, remote, domain);
  }
  return nErr;
}

static int fastrpc_free_handle(int domain, QList *me, remote_handle64 local) {
  struct handle_list *hlist = NULL;

  hlist = get_hlist(domain);
  if (!hlist)
    return 0;
  pthread_mutex_lock(&hlist->lmut);
  if (!QList_IsEmpty(me)) {
    QNode *pn = NULL, *pnn = NULL;
    QLIST_NEXTSAFE_FOR_ALL(me, pn, pnn) {
      struct handle_info *hi = STD_RECOVER_REC(struct handle_info, qn, pn);
      if (hi->local == local) {
        QNode_DequeueZ(&hi->qn);
        if (hi->name)
          free(hi->name);
        free(hi);
        hi = NULL;
        break;
      }
    }
  }
  pthread_mutex_unlock(&hlist->lmut);
  return 0;
}

int fastrpc_update_module_list(uint32_t req, int domain, remote_handle64 h,
                               remote_handle64 *local, const char *name,
                               uint32_t priority) {
  int nErr = AEE_SUCCESS;
  struct handle_list *hlist = NULL;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  switch (req) {
  case DOMAIN_LIST_PREPEND: {
    VERIFY(AEE_SUCCESS == (nErr = fastrpc_alloc_handle(domain, &hlist->ql, h,
                                                       local, name, priority)));
    if (IS_CONST_HANDLE(h)) {
      pthread_mutex_lock(&hlist->lmut);
      hlist->constCount++;
      pthread_mutex_unlock(&hlist->lmut);
    } else {
      pthread_mutex_lock(&hlist->lmut);
      hlist->domainsCount++;
      pthread_mutex_unlock(&hlist->lmut);
    }
    break;
  }
  case DOMAIN_LIST_DEQUEUE: {
    VERIFY(AEE_SUCCESS == (nErr = fastrpc_free_handle(domain, &hlist->ql, local)));
    if (IS_CONST_HANDLE(h)) {
      pthread_mutex_lock(&hlist->lmut);
      hlist->constCount--;
      pthread_mutex_unlock(&hlist->lmut);
    } else {
      pthread_mutex_lock(&hlist->lmut);
      hlist->domainsCount--;
      pthread_mutex_unlock(&hlist->lmut);
    }
    break;
  }
  case NON_DOMAIN_LIST_PREPEND: {
    VERIFY(AEE_SUCCESS == (nErr = fastrpc_alloc_handle(domain, &hlist->nql, h,
                                                       local, name, priority)));
    pthread_mutex_lock(&hlist->lmut);
    hlist->nondomainsCount++;
    pthread_mutex_unlock(&hlist->lmut);
    break;
  }
  case NON_DOMAIN_LIST_DEQUEUE: {
    VERIFY(AEE_SUCCESS == (nErr = fastrpc_free_handle(domain, &hlist->nql, local)));
    pthread_mutex_lock(&hlist->lmut);
    hlist->nondomainsCount--;
    pthread_mutex_unlock(&hlist->lmut);
    break;
  }
  case REVERSE_HANDLE_LIST_PREPEND: {
    VERIFY(AEE_SUCCESS == (nErr = fastrpc_alloc_handle(domain, &hlist->rql, h,
                                                       local, name, priority)));
    pthread_mutex_lock(&hlist->lmut);
    hlist->reverseCount++;
    pthread_mutex_unlock(&hlist->lmut);
    break;
  }
  case REVERSE_HANDLE_LIST_DEQUEUE: {
    VERIFY(AEE_SUCCESS == (nErr = fastrpc_free_handle(domain, &hlist->rql, local)));
    pthread_mutex_lock(&hlist->lmut);
    hlist->reverseCount--;
    pthread_mutex_unlock(&hlist->lmut);
    break;
  }
  default: {
    nErr = AEE_EUNSUPPORTEDAPI;
    goto bail;
  }
  }
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR,
         "Error 0x%x: %s failed for request ID %u, handle 0x%x, domain %d\n",
         nErr, __func__, req, h, domain);
  } else {
    FARF(RUNTIME_RPC_HIGH,
         "Library D count %d, C count %d, N count %d, R count %d\n",
         hlist->domainsCount, hlist->constCount, hlist->nondomainsCount,
         hlist->reverseCount);
  }
  return nErr;
}

static void fastrpc_clear_handle_list(uint32_t req, int domain) {
  int nErr = AEE_SUCCESS;
  QNode *pn = NULL;
  char dlerrstr[MAX_DLERRSTR_LEN];
  int dlerr = 0;
  struct handle_list *hlist = NULL;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  switch (req) {
  case MULTI_DOMAIN_HANDLE_LIST_ID: {
    pthread_mutex_lock(&hlist->lmut);
    if (!QList_IsNull(&hlist->ql)) {
      while ((pn = QList_Pop(&hlist->ql))) {
        struct handle_info *hi = STD_RECOVER_REC(struct handle_info, qn, pn);
        free(hi);
        hi = NULL;
      }
    }
    pthread_mutex_unlock(&hlist->lmut);
    break;
  }
  case NON_DOMAIN_HANDLE_LIST_ID: {
    pthread_mutex_lock(&hlist->lmut);
    if (!QList_IsNull(&hlist->nql)) {
      while ((pn = QList_Pop(&hlist->nql))) {
        struct handle_info *h = STD_RECOVER_REC(struct handle_info, qn, pn);
        free(h);
        h = NULL;
      }
    }
    pthread_mutex_unlock(&hlist->lmut);
    break;
  }
  case REVERSE_HANDLE_LIST_ID: {
    if (!QList_IsNull(&hlist->rql)) {
      while ((pn = QList_Pop(&hlist->rql))) {
        struct handle_info *hi = STD_RECOVER_REC(struct handle_info, qn, pn);
        close_reverse_handle(hi->local, dlerrstr, sizeof(dlerrstr), &dlerr);
        free(hi);
        hi = NULL;
      }
    }
    break;
  }
  default: {
    nErr = AEE_EUNSUPPORTEDAPI;
    goto bail;
  }
  }
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed for request ID %u, domain %d\n", nErr,
         __func__, req, domain);
  }
  return;
}

// Notify kernel to awake PM
static int wakelock_control_kernel_pm(int domain, int dev, uint32_t timeout) {
  int nErr = AEE_SUCCESS;
  struct fastrpc_ctrl_pm pm = {0};

  pm.timeout = timeout;
  nErr = ioctl_control(dev, DSPRPC_PM, &pm);
  if (nErr) {
    if (errno == EBADRQC || errno == ENOTTY) {
      VERIFY_WPRINTF("Warning: %s: kernel does not support PM management (%s)",
                     __func__, strerror(errno));
    } else if (errno == EACCES || errno == EPERM) {
      VERIFY_WPRINTF("Warning: %s: application does not have permission for PM "
                     "management (%s)",
                     __func__, strerror(errno));
    }
    FARF(ERROR,
         "Error 0x%x: %s PM control failed for domain %d, dev %d with timeout "
         "%d (errno %s)",
         nErr, __func__, domain, dev, timeout, strerror(errno));
  }
  return nErr;
}

// Callback function for posix timer
static void fastrpc_timer_callback(void *ptr) {
  fastrpc_timer *frpc_timer = (fastrpc_timer *)ptr;
  int nErr = AEE_SUCCESS;
  remote_rpc_process_exception data;

  if (1 == atomic_CompareAndExchange(&timer_expired, 1, 0)) {
    return;
  }

  FARF(ALWAYS,
       "%s fastrpc time out of %d ms on thread %d on domain %d sc 0x%x handle "
       "0x%x\n",
       __func__, frpc_timer->timeout_millis, frpc_timer->tid,
       frpc_timer->domain, frpc_timer->sc, frpc_timer->handle);
  data.domain = frpc_timer->domain;
  nErr = remote_session_control(FASTRPC_REMOTE_PROCESS_EXCEPTION, &data,
                                sizeof(remote_rpc_process_exception));
  if (nErr) {
    FARF(ERROR,
         "%s: Failed to create exception in the remote process on domain %d "
         "(errno %s)",
         __func__, data.domain, strerror(errno));
  }
}

// Function to add timer before remote RPC call
static void fastrpc_add_timer(fastrpc_timer *frpc_timer) {
  struct sigevent sigevent;
  struct itimerspec time_spec;
  int err = 0;

  memset(&sigevent, 0, sizeof(sigevent));
  sigevent.sigev_notify = SIGEV_THREAD;
  sigevent.sigev_notify_function =
      (void (*)(union sigval))fastrpc_timer_callback;
  sigevent.sigev_value.sival_ptr = frpc_timer;
  err = timer_create(CLOCK_MONOTONIC, &sigevent, &(frpc_timer->timer));
  if (err) {
    FARF(ERROR, "%s: failed to create timer with error 0x%x\n", __func__, err);
    goto bail;
  }

  time_spec.it_value.tv_sec = frpc_timer->timeout_millis / 1000;
  time_spec.it_value.tv_nsec =
      (frpc_timer->timeout_millis % 1000) * 1000 * 1000;
  err = timer_settime(frpc_timer->timer, 0, &time_spec, NULL);
  if (err) {
    FARF(ERROR, "%s: failed to set timer with error 0x%x\n", __func__, err);
    goto bail;
  }
bail:
  return;
}

// Function to delete timer after remote RPC call
static void fastrpc_delete_timer(timer_t *timer) {
  int nErr = AEE_SUCCESS;
  nErr = timer_delete(*timer);
  if (nErr) {
    FARF(ERROR, "%s: Failed to delete timer", __func__);
  }
}

int remote_handle_invoke_domain(int domain, remote_handle handle,
                                fastrpc_async_descriptor_t *desc, uint32_t sc,
                                remote_arg *pra, uint32_t priority) {
  int dev, total, bufs, handles, i, nErr = 0, wake_lock = 0, rpc_timeout = 0;
  unsigned req;
  uint32_t len;
  struct handle_list *hlist;
  uint32_t *crc_remote = NULL;
  uint32_t *crc_local = NULL;
  uint64_t *perf_kernel = NULL;
  uint64_t *perf_dsp = NULL;
  struct fastrpc_async_job asyncjob = {0}, *job = NULL;
  fastrpc_timer frpc_timer;
  int trace_marker_fd;
  bool trace_enabled = false;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  trace_marker_fd = hlist->trace_marker_fd;
  if (IS_QTF_TRACING_ENABLED(hlist->procattrs) && !IS_STATIC_HANDLE(handle) &&
      trace_marker_fd > 0) {
    write(trace_marker_fd, INVOKE_BEGIN_TRACE_STR, invoke_begin_trace_strlen);
    trace_enabled = true;
  }

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_session_dev(domain, &dev)));

  errno = 0;
  if (fastrpc_wake_lock_enable[domain]) {
    if (!IS_REVERSE_RPC_CALL(handle, sc) ||
        is_first_reverse_rpc_call(domain, handle, sc)) {
      if (!fastrpc_wake_lock())
        wake_lock = 1;
    } else if (IS_REVERSE_RPC_CALL(handle, sc))
      /* Since wake-lock is not released at the end of previous
       * "remote_handle_invoke" for subsequent reverse RPC calls, it doesn't
       * have to be taken again here.
       * It will be released before "ioctl invoke" call to kernel */
      wake_lock = 1;
  }

  if (hlist->setmode) {
    hlist->setmode = 0;
    nErr = ioctl_setmode(dev, hlist->mode);
    if (nErr) {
      nErr = convert_kernel_to_user_error(nErr, errno);
      goto bail;
    }
  }

  bufs = REMOTE_SCALARS_INBUFS(sc) + REMOTE_SCALARS_OUTBUFS(sc);
  handles = REMOTE_SCALARS_INHANDLES(sc) + REMOTE_SCALARS_OUTHANDLES(sc);
  total = bufs + handles;

  INITIALIZE_REMOTE_ARGS(total);

  if (desc) {
    struct timespec time_spec;
    // Check for valid user async descriptor
    VERIFYC(desc->type >= FASTRPC_ASYNC_NO_SYNC &&
                desc->type < FASTRPC_ASYNC_TYPE_MAX,
            AEE_EBADPARM);
    VERIFYC(!(desc->type == FASTRPC_ASYNC_CALLBACK && desc->cb.fn == NULL),
            AEE_EBADPARM);
    pthread_mutex_lock(&hlist->async_init_deinit_mut);
    if (AEE_SUCCESS != (nErr = fastrpc_async_domain_init(domain))) {
      pthread_mutex_unlock(&hlist->async_init_deinit_mut);
      goto bail;
    }
    asyncjob.jobid = ++(hlist->jobid);
    pthread_mutex_unlock(&hlist->async_init_deinit_mut);
    clock_gettime(CLOCK_MONOTONIC, &time_spec);
    asyncjob.jobid = ((((time_spec.tv_sec) / SECONDS_PER_HOUR)
                       << (FASTRPC_ASYNC_TIME_SPEC_POS / 2))
                          << ((FASTRPC_ASYNC_TIME_SPEC_POS + 1) / 2) |
                      (asyncjob.jobid << FASTRPC_ASYNC_JOB_POS) | domain);
    asyncjob.isasyncjob = 1;
    fastrpc_save_async_job(domain, &asyncjob, desc);
    job = &asyncjob;
  }

  req = INVOKE;
  VERIFYC(!(NULL == pra && total > 0), AEE_EBADPARM);
  for (i = 0; i < bufs; i++) {
    set_args(i, pra[i].buf.pv, pra[i].buf.nLen, -1, 0);
    if (pra[i].buf.nLen) {
      void *base;
      int nova = 0, attr = 0, fd = -1;
      VERIFY(AEE_SUCCESS ==
             (nErr = fdlist_fd_from_buf(pra[i].buf.pv, (int)pra[i].buf.nLen,
                                        &nova, &base, &attr, &fd)));
      if (fd != -1) {
        set_args_fd(i, fd);
        req = INVOKE_FD;
      }
      // AsyncRPC doesn't support Non-ion output buffers
      if (asyncjob.isasyncjob && i >= (int)REMOTE_SCALARS_INBUFS(sc)) {
        VERIFYM(fd != -1, AEE_EBADPARM,
                "AsyncRPC doesn't support Non-ion output buffers");
      }
      if (nova) {
        req = INVOKE_ATTRS;
        append_args_attr(i, FASTRPC_ATTR_NOVA);
        // pra[i].buf.pv = (void*)((uintptr_t)pra[i].buf.pv - (uintptr_t)base);
        VERIFY_IPRINTF("nova buffer idx: %d addr: %p size: %d", i,
                       pra[i].buf.pv, pra[i].buf.nLen);
      }
      if (attr & FASTRPC_ATTR_NON_COHERENT) {
        req = INVOKE_ATTRS;
        append_args_attr(i, FASTRPC_ATTR_NON_COHERENT);
        VERIFY_IPRINTF("non-coherent buffer idx: %d addr: %p size: %d", i,
                       pra[i].buf.pv, pra[i].buf.nLen);
      }
      if (attr & FASTRPC_ATTR_COHERENT) {
        req = INVOKE_ATTRS;
        append_args_attr(i, FASTRPC_ATTR_COHERENT);
        VERIFY_IPRINTF("coherent buffer idx: %d addr: %p size: %d", i,
                       pra[i].buf.pv, pra[i].buf.nLen);
      }
      if (attr & FASTRPC_ATTR_FORCE_NOFLUSH) {
        req = INVOKE_ATTRS;
        append_args_attr(i, FASTRPC_ATTR_FORCE_NOFLUSH);
        VERIFY_IPRINTF("force no flush for buffer idx: %d addr: %p size: %d", i,
                       pra[i].buf.pv, pra[i].buf.nLen);
      }
      if (attr & FASTRPC_ATTR_FORCE_NOINVALIDATE) {
        req = INVOKE_ATTRS;
        append_args_attr(i, FASTRPC_ATTR_FORCE_NOINVALIDATE);
        VERIFY_IPRINTF("force no invalidate buffer idx: %d addr: %p size: %d",
                       i, pra[i].buf.pv, pra[i].buf.nLen);
      }
      if (attr & FASTRPC_ATTR_KEEP_MAP) {
        req = INVOKE_ATTRS;
        append_args_attr(i, FASTRPC_ATTR_KEEP_MAP);
        VERIFY_IPRINTF("invoke: mapping with attribute KEEP_MAP");
      }
    }
  }

  for (i = bufs; i < total; i++) {
    unsigned int attr = 0;
    int dma_fd = -1;

    req = INVOKE_ATTRS;
    unregister_dma_handle(pra[i].dma.fd, &len, &attr);
    if (hlist->dma_handle_reverse_rpc_map_capability &&
        (attr & FASTRPC_ATTR_NOMAP)) {
      // Register fd again, for reverse RPC call to retrive FASTRPC_ATTR_NOMAP
      // flag for fd
      remote_register_dma_handle_attr(pra[i].dma.fd, len, FASTRPC_ATTR_NOMAP);
    }
    dma_fd = pra[i].dma.fd;
    set_args(i, (void *)(uintptr_t)pra[i].dma.offset, len, dma_fd, attr);
    append_args_attr(i, FASTRPC_ATTR_NOVA);
  }

  if (IS_CRC_CHECK_ENABLED(hlist->procattrs) && (!IS_STATIC_HANDLE(handle)) &&
      !asyncjob.isasyncjob) {
    int nInBufs = REMOTE_SCALARS_INBUFS(sc);
    crc_local = (uint32_t *)calloc(M_CRCLIST, sizeof(uint32_t));
    crc_remote = (uint32_t *)calloc(M_CRCLIST, sizeof(uint32_t));
    VERIFYC(crc_local != NULL && crc_remote != NULL, AEE_ENOMEMORY);
    VERIFYC(!(NULL == pra && nInBufs > 0), AEE_EBADPARM);
    for (i = 0; (i < nInBufs) && (i < M_CRCLIST); i++)
      crc_local[i] = crc32_lut((unsigned char *)pra[i].buf.pv,
                               (int)pra[i].buf.nLen, crc_table);
    req = INVOKE_CRC;
  }

  if (IS_KERNEL_PERF_ENABLED(hlist->procattrs) && (!IS_STATIC_HANDLE(handle))) {
    perf_kernel = (uint64_t *)calloc(PERF_KERNEL_KEY_MAX, sizeof(uint64_t));
    VERIFYC(perf_kernel != NULL, AEE_ENOMEMORY);
    req = INVOKE_PERF;
  }
  if (IS_DSP_PERF_ENABLED(hlist->procattrs) && (!IS_STATIC_HANDLE(handle))) {
    perf_dsp = (uint64_t *)calloc(PERF_DSP_KEY_MAX, sizeof(uint64_t));
    VERIFYC(perf_dsp != NULL, AEE_ENOMEMORY);
    req = INVOKE_PERF;
  }

  if (!IS_STATIC_HANDLE(handle)) {
    fastrpc_latency_invoke_incr(&hlist->qos);
    if ((rpc_timeout = fastrpc_config_get_rpctimeout()) > 0) {
      frpc_timer.domain = domain;
      frpc_timer.sc = sc;
      frpc_timer.handle = handle;
      frpc_timer.timeout_millis = rpc_timeout;
      frpc_timer.tid = gettid();
      fastrpc_add_timer(&frpc_timer);
    }
  }

  FASTRPC_TRACE_LOG(FASTRPC_TRACE_INVOKE_START, handle, sc);
  if (wake_lock) {
    wakelock_control_kernel_pm(domain, dev, PM_TIMEOUT_MS);
    fastrpc_wake_unlock();
    wake_lock = 0;
  }
  /*
   * If an rpc invoke is made with 0 priority, we take the legacy invoke path
   * where the call will end up at the reserved priority level on dsp.
   * If dsp supports handle priorities, this path is expected to be taken
   * only for framework calls.
   */
  if (IS_VALID_USER_HANDLE_PRIORITY(priority))
    req = INVOKE_PRIORITY;
  // Macros are initializing and destroying pfds and pattrs.
  nErr = ioctl_invoke(dev, req, handle, sc, priority, get_args(), pfds, pattrs,
                      job, crc_remote, perf_kernel, perf_dsp);
  if (nErr) {
    nErr = convert_kernel_to_user_error(nErr, errno);
  }

  if (fastrpc_wake_lock_enable[domain]) {
    if (!fastrpc_wake_lock())
      wake_lock = 1;
  }
  FASTRPC_TRACE_LOG(FASTRPC_TRACE_INVOKE_END, handle, sc);
  if (!IS_STATIC_HANDLE(handle) && rpc_timeout > 0) {
    fastrpc_delete_timer(&(frpc_timer.timer));
  }

  if (IS_CRC_CHECK_ENABLED(hlist->procattrs) && (!IS_STATIC_HANDLE(handle)) &&
      !asyncjob.isasyncjob) {
    int nInBufs = REMOTE_SCALARS_INBUFS(sc);
    VERIFYC(crc_local != NULL && crc_remote != NULL, AEE_ENOMEMORY);
    for (i = nInBufs; i < bufs; i++)
      crc_local[i] = crc32_lut((unsigned char *)pra[i].buf.pv,
                               (int)pra[i].buf.nLen, crc_table);
    for (i = 0; (i < bufs) && (i < M_CRCLIST); i++) {
      if (crc_local[i] != crc_remote[i]) {
        FARF(ERROR, "CRC mismatch for buffer %d[%d], crc local %x remote %x", i,
             bufs, crc_local[i], crc_remote[i]);
        break;
      }
    }
  }

  if (IS_KERNEL_PERF_ENABLED(hlist->procattrs) && (!IS_STATIC_HANDLE(handle)) &&
      !asyncjob.isasyncjob) {
    VERIFYC(perf_kernel != NULL, AEE_ENOMEMORY);
    FARF(ALWAYS,
         "RPCPERF-K  H:0x%x SC:0x%x C:%" PRIu64 " F:%" PRIu64 " ns M:%" PRIu64
         " ns CP:%" PRIu64 " ns L:%" PRIu64 " ns G:%" PRIu64 " ns P:%" PRIu64
         " ns INV:%" PRIu64 " ns INVOKE:%" PRIu64 " ns\n",
         handle, sc, perf_kernel[0], perf_kernel[1], perf_kernel[2],
         perf_kernel[3], perf_kernel[4], perf_kernel[5], perf_kernel[6],
         perf_kernel[7], perf_kernel[8]);
  }
  if (IS_DSP_PERF_ENABLED(hlist->procattrs) && (!IS_STATIC_HANDLE(handle)) &&
      !asyncjob.isasyncjob) {
    VERIFYC(perf_dsp != NULL, AEE_ENOMEMORY);
    FARF(ALWAYS,
         "RPCPERF-D  H:0x%x SC:0x%x C:%" PRIu64 " M_H:%" PRIu64 " us M:%" PRIu64
         " us G:%" PRIu64 " us INVOKE:%" PRIu64 " us P:%" PRIu64
         " us CACHE:%" PRIu64 " us UM:%" PRIu64 " us "
         "UM_H:%" PRIu64 " us R:%" PRIu64 " us E_R:%" PRIu64
         " us J_S_T:%" PRIu64 " us\n",
         handle, sc, perf_dsp[0], perf_dsp[1], perf_dsp[2], perf_dsp[3],
         perf_dsp[4], perf_dsp[5], perf_dsp[6], perf_dsp[7], perf_dsp[8],
         perf_dsp[9], perf_dsp[10], perf_dsp[11]);
  }

  if (!(perf_v2_kernel && perf_v2_dsp)) {
    fastrpc_perf_update(dev, handle, sc);
  }
bail:
  if (asyncjob.isasyncjob) {
    if (!nErr) {
      FARF(RUNTIME_RPC_HIGH, "adsprpc : %s Async job Queued, job 0x%" PRIx64 "",
           __func__, asyncjob.jobid);
      desc->jobid = asyncjob.jobid;
    } else {
      fastrpc_remove_async_job(asyncjob.jobid, false);
      desc->jobid = -1;
    }
  }
  DESTROY_REMOTE_ARGS();
  if (crc_local) {
    free(crc_local);
    crc_local = NULL;
  }
  if (crc_remote) {
    free(crc_remote);
    crc_remote = NULL;
  }
  if (perf_kernel && !asyncjob.isasyncjob) {
    free(perf_kernel);
    perf_kernel = NULL;
  }
  if (perf_dsp && !asyncjob.isasyncjob) {
    free(perf_dsp);
    perf_dsp = NULL;
  }
  if (wake_lock) {
    // Keep holding wake-lock for reverse RPC calls to keep CPU awake for any
    // further processing
    if (!IS_REVERSE_RPC_CALL(handle, sc)) {
      fastrpc_wake_unlock();
      wake_lock = 0;
    }
  }
  if (trace_enabled) {
    write(trace_marker_fd, INVOKE_END_TRACE_STR, invoke_end_trace_strlen);
  }
  if (nErr != AEE_SUCCESS) {
    if ((nErr == -1) && (errno == ECONNRESET)) {
      nErr = AEE_ECONNRESET;
    }
    // FARF(ERROR, "Error 0x%x: %s failed for handle 0x%x on domain %d (sc
    // 0x%x)\n", nErr, __func__, (int)handle, domain, sc);
  }
  return nErr;
}

int remote_handle_invoke(remote_handle handle, uint32_t sc, remote_arg *pra) {
  int domain = -1, nErr = AEE_SUCCESS, ref = 0;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  FARF(RUNTIME_RPC_HIGH, "Entering %s, handle %u sc %X remote_arg %p\n",
       __func__, handle, sc, pra);
  PRINT_WARN_USE_DOMAINS();
  FASTRPC_ATRACE_BEGIN_L("%s called with handle 0x%x , scalar 0x%x", __func__,
                         (int)handle, sc);
  VERIFYC(handle != (remote_handle)-1, AEE_EINVHANDLE);

  domain = get_current_domain();
  FASTRPC_GET_REF(domain);
  VERIFY(AEE_SUCCESS ==
         (nErr = remote_handle_invoke_domain(domain, handle, NULL, sc, pra,
                                             FASTRPC_RESERVED_HANDLE_PRIO)));
bail:
  FASTRPC_PUT_REF(domain);
  if (nErr != AEE_SUCCESS) {
    if (is_process_exiting(domain)) {
      return 0;
    }
    /*
     * handle_info or so name cannot be obtained from remote handles which
     * are used for non-domain calls.
     */
    if (0 == check_rpc_error(nErr)) {
      if (get_logger_state(domain)) {
        FARF(ERROR,
             "Error 0x%x: %s failed for handle 0x%x, method %d on domain %d "
             "(sc 0x%x) (errno %s)\n",
             nErr, __func__, (int)handle, REMOTE_SCALARS_METHOD(sc), domain, sc,
             strerror(errno));
      }
    }
  }
  FASTRPC_ATRACE_END();
  return nErr;
}

int remote_handle64_invoke(remote_handle64 local, uint32_t sc,
                           remote_arg *pra) {
  remote_handle64 remote = 0;
  int nErr = AEE_SUCCESS, domain = -1, ref = 0, priority;
  struct handle_info *h = (struct handle_info *)local;

  if (IS_STATICPD_HANDLE(local)) {
    nErr = AEE_EINVHANDLE;
    FARF(ERROR,
         "Error 0x%x: %s cannot be called for staticPD handle 0x%" PRIx64 "\n",
         nErr, __func__, local);
    goto bail;
  }

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  FASTRPC_ATRACE_BEGIN_L("%s called with handle 0x%x , scalar 0x%x", __func__,
                         (int)local, sc);
  VERIFYC(local != (remote_handle64)-1, AEE_EINVHANDLE);

  VERIFY(AEE_SUCCESS == (nErr = get_domain_from_handle(local, &domain)));
  FASTRPC_GET_REF(domain);
  VERIFY(AEE_SUCCESS == (nErr = get_handle_remote(local, &remote)));
  VERIFY(AEE_SUCCESS == (nErr = get_handle_priority(local, &priority)));
  VERIFYC((IS_VALID_USER_HANDLE_PRIORITY(priority) ||
           IS_RESERVED_HANDLE_PRIORITY(priority)),
          AEE_EBADPARM);
  VERIFY(AEE_SUCCESS == (nErr = remote_handle_invoke_domain(
                             domain, remote, NULL, sc, pra, priority)));
bail:
  FASTRPC_PUT_REF(domain);
  if (nErr != AEE_SUCCESS) {
    if (is_process_exiting(domain)) {
      return 0;
    }
    if (0 == check_rpc_error(nErr)) {
      if (get_logger_state(domain)) {
        FARF(ERROR,
             "Error 0x%x: %s failed for module %s, handle 0x%" PRIx64
             ", method %d on domain %d (sc 0x%x) (errno %s)\n",
             nErr, __func__, h->name, local, REMOTE_SCALARS_METHOD(sc), domain,
             sc, strerror(errno));
      }
    }
  }
  FASTRPC_ATRACE_END();
  return nErr;
}

int remote_handle_invoke_async(remote_handle handle,
                               fastrpc_async_descriptor_t *desc, uint32_t sc,
                               remote_arg *pra) {
  int domain = -1, nErr = AEE_SUCCESS, ref = 0;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  FARF(RUNTIME_RPC_HIGH, "Entering %s, handle %u desc %p sc %X remote_arg %p\n",
       __func__, handle, desc, sc, pra);
  PRINT_WARN_USE_DOMAINS();
  FASTRPC_ATRACE_BEGIN_L("%s called with handle 0x%x , scalar 0x%x", __func__,
                         (int)handle, sc);
  VERIFYC(handle != (remote_handle)-1, AEE_EINVHANDLE);

  domain = get_current_domain();
  FASTRPC_GET_REF(domain);
  VERIFY(AEE_SUCCESS ==
         (nErr = remote_handle_invoke_domain(domain, handle, desc, sc, pra,
                                             FASTRPC_RESERVED_HANDLE_PRIO)));
bail:
  FASTRPC_PUT_REF(domain);
  if (nErr != AEE_SUCCESS) {
    if (0 == check_rpc_error(nErr)) {
      FARF(ERROR,
           "Error 0x%x: %s failed for handle 0x%x, method %d async type %d on "
           "domain %d (sc 0x%x) (errno %s)\n",
           nErr, __func__, (int)handle, REMOTE_SCALARS_METHOD(sc), desc->type,
           domain, sc, strerror(errno));
    }
  }
  FASTRPC_ATRACE_END();
  return nErr;
}

int remote_handle64_invoke_async(remote_handle64 local,
                                 fastrpc_async_descriptor_t *desc, uint32_t sc,
                                 remote_arg *pra) {
  remote_handle64 remote = 0;
  int nErr = AEE_SUCCESS, domain = -1, ref = 0, capability = 0;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  FARF(RUNTIME_RPC_HIGH,
       "Entering %s, handle %llu desc %p sc %X remote_arg %p\n", __func__,
       local, desc, sc, pra);
  FASTRPC_ATRACE_BEGIN_L("%s called with handle 0x%x , scalar 0x%x", __func__,
                         (int)local, sc);
  VERIFYC(local != (remote_handle64)-1, AEE_EINVHANDLE);

  VERIFY(AEE_SUCCESS == (nErr = get_domain_from_handle(local, &domain)));
  FASTRPC_GET_REF(domain);
  VERIFY(AEE_SUCCESS == (nErr = get_handle_remote(local, &remote)));
  nErr = fastrpc_get_cap(domain, HANDLE_PRIORITY_SUPPORT, &capability);
  /* Block all async calls if the dsp supports handle priority */
  VERIFYC((nErr == 0) && (capability != 0), AEE_EUNSUPPORTED);
  VERIFY(AEE_SUCCESS ==
         (nErr = remote_handle_invoke_domain(domain, remote, desc, sc, pra,
                                             FASTRPC_RESERVED_HANDLE_PRIO)));
bail:
  FASTRPC_PUT_REF(domain);
  if (nErr != AEE_SUCCESS) {
    if (0 == check_rpc_error(nErr)) {
      FARF(ERROR,
           "Error 0x%x: %s failed for handle 0x%" PRIx64
           ", method %d on domain %d (sc 0x%x) (errno %s)\n",
           nErr, __func__, local, REMOTE_SCALARS_METHOD(sc), domain, sc,
           strerror(errno));
    }
  }
  FASTRPC_ATRACE_END();
  return nErr;
}

int listener_android_geteventfd(int domain, int *fd);
int remote_handle_open_domain(int domain, const char *name, remote_handle *ph,
                              uint64_t *t_spawn, uint64_t *t_load) {
  char dlerrstr[255];
  int dlerr = 0, nErr = AEE_SUCCESS;
  int dev = -1;
  char *pdname_uri = NULL;
  int name_len = 0;
  remote_handle64 handle = INVALID_HANDLE;
  struct handle_list *hlist;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  FASTRPC_ATRACE_BEGIN_L("%s called with domain %d, name %s, handle 0x%x",
                         __func__, domain, name, ph);
  /* If the total reference count exceeds one then exit the application. */
  if (total_dsp_lib_refcnt > MAX_LIB_INSTANCE_ALLOWED) {
    FARF(ERROR,
         "Error: aborting due to %d instances of libxdsprpc. Only %d allowed\n",
         total_dsp_lib_refcnt, MAX_LIB_INSTANCE_ALLOWED);
    deinit_fastrpc_dsp_lib_refcnt();
    exit(EXIT_FAILURE);
  }
  if (!std_strncmp(name, ITRANSPORT_PREFIX "geteventfd",
                   std_strlen(ITRANSPORT_PREFIX "geteventfd"))) {
    FARF(RUNTIME_RPC_HIGH, "getting event fd");
    return listener_android_geteventfd(domain, (int *)ph);
  }
  if (!std_strncmp(name, ITRANSPORT_PREFIX "attachguestos",
                   std_strlen(ITRANSPORT_PREFIX "attachguestos"))) {
    FARF(RUNTIME_RPC_HIGH, "setting attach mode to guestos : %d", domain);
    *ph = ATTACHGUESTOS_HANDLE;
    hlist->dsppd = ROOT_PD;
    return AEE_SUCCESS;
  }
  if (!std_strncmp(name, ITRANSPORT_PREFIX "createstaticpd",
                   std_strlen(ITRANSPORT_PREFIX "createstaticpd"))) {
    FARF(RUNTIME_RPC_HIGH, "creating static pd on domain: %d", domain);
    name_len = strlen(name);
    VERIFYC(NULL !=
                (pdname_uri = (char *)malloc((name_len + 1) * sizeof(char))),
            AEE_ENOMEMORY);
    std_strlcpy(pdname_uri, name, name_len + 1);
    char *pdName = pdname_uri + std_strlen(ITRANSPORT_PREFIX "createstaticpd:");

    /*
     * Support sessions feature for static PDs.
     * For eg, the same app can call 'remote_handle64_open' with
     * "createstaticpd:sensorspd&_dom=adsp&_session=0" and
     * "createstaticpd:oispd&_dom=adsp&_session=1" to create a session
     * on both static PDs.
     */
    if (std_strstr(pdName, get_domain_from_id(
                               GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain))) &&
        std_strstr(pdName, FASTRPC_SESSION_URI)) {
      std_strlcpy(pdName, pdName,
                  (std_strlen(pdName) -
                   std_strlen(get_domain_from_id(
                       GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain))) -
                   std_strlen(FASTRPC_SESSION1_URI) + 1));
    } else if (std_strstr(pdName, get_domain_from_id(domain))) {
      std_strlcpy(
          pdName, pdName,
          (std_strlen(pdName) - std_strlen(get_domain_from_id(domain)) + 1));
    }
    VERIFYC(MAX_DSPPD_NAMELEN > std_strlen(pdName), AEE_EBADPARM);
    std_strlcpy(hlist->dsppdname, pdName, std_strlen(pdName) + 1);
    if (!std_strncmp(pdName, "audiopd", std_strlen("audiopd"))) {
      *ph = AUDIOPD_HANDLE;
      hlist->dsppd = AUDIO_STATICPD;
    } else if (!std_strncmp(pdName, "securepd", std_strlen("securepd"))) {
      FARF(ALWAYS, "%s: attaching to securePD\n", __func__);
      *ph = SECUREPD_HANDLE;
      hlist->dsppd = SECURE_STATICPD;
    } else if (!std_strncmp(pdName, "sensorspd", std_strlen("sensorspd"))) {
      *ph = SENSORPD_HANDLE;
      hlist->dsppd = SENSORS_STATICPD;
    } else if (!std_strncmp(pdName, "rootpd", std_strlen("rootpd"))) {
      *ph = ROOTPD_HANDLE;
      hlist->dsppd = GUEST_OS_SHARED;
    } else if (!std_strncmp(pdName, "oispd", std_strlen("oispd"))) {
      *ph = OISPD_HANDLE;
      hlist->dsppd = OIS_STATICPD;
    }
    return AEE_SUCCESS;
  }
  if (std_strbegins(name, ITRANSPORT_PREFIX "attachuserpd")) {
    FARF(RUNTIME_RPC_HIGH, "setting attach mode to userpd : %d", domain);
    hlist->dsppd = USERPD;
    return AEE_SUCCESS;
  }
  PROFILE_ALWAYS(t_spawn,
                 VERIFY(AEE_SUCCESS == (nErr = domain_init(domain, &dev)));
                 VERIFYM(-1 != dev, AEE_ERPC, "open dev failed\n"););
  PROFILE_ALWAYS(
      t_load,
      if ((handle = get_remotectl1_handle(domain)) != INVALID_HANDLE) {
        nErr = remotectl1_open1(handle, name, (int *)ph, dlerrstr,
                                sizeof(dlerrstr), &dlerr);
        if ((nErr == DSP_AEE_EOFFSET + AEE_ERPC) ||
            nErr == DSP_AEE_EOFFSET + AEE_ENOSUCHMOD) {
          FARF(ALWAYS,
               "Warning 0x%x: %s: remotectl1 domains not supported for domain "
               "%d\n",
               nErr, __func__, domain);
          fastrpc_update_module_list(DOMAIN_LIST_DEQUEUE, domain,
                                     NULL, handle, NULL,
                                     FASTRPC_RESERVED_HANDLE_PRIO);

          // Set remotectlhandle to INVALID_HANDLE, so that all subsequent calls
          // are non-domain calls
          hlist->remotectlhandle = INVALID_HANDLE;
          VERIFY(AEE_SUCCESS ==
                 (nErr = remotectl_open(name, (int *)ph, dlerrstr,
                                        sizeof(dlerrstr), &dlerr)));
        }
      } else {
        VERIFY(AEE_SUCCESS ==
               (nErr = remotectl_open(name, (int *)ph, dlerrstr,
                                      sizeof(dlerrstr), &dlerr)));
      } VERIFY(AEE_SUCCESS == (nErr = dlerr)););
bail:
  if (dlerr != 0) {
    FARF(ERROR,
         "Error 0x%x: %s: dynamic loading failed for %s on domain %d (dlerror "
         "%s) (errno %s)\n",
         nErr, __func__, name, domain, dlerrstr, strerror(errno));
  }
  if (pdname_uri) {
    free(pdname_uri);
    pdname_uri = NULL;
  }
  if (nErr == AEE_ECONNRESET) {
    if (!hlist->domainsCount && !hlist->nondomainsCount) {
      /* Close session if there are no open remote handles */
      hlist->disable_exit_logs = 1;
      domain_deinit(domain);
    }
  }
  FASTRPC_ATRACE_END();
  return nErr;
}

int remote_handle_open(const char *name, remote_handle *ph) {
  int nErr = 0, domain = -1, ref = 0;
  uint64_t t_spawn = 0, t_load = 0;
  remote_handle64 local;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  FARF(RUNTIME_RPC_HIGH, "Entering %s, name %s\n", __func__, name);
  PRINT_WARN_USE_DOMAINS();
  FASTRPC_ATRACE_BEGIN_L("%s for %s", __func__, name);

  if (!name || !ph) {
    FARF(ERROR, "%s: Invalid input", __func__);
    return AEE_EBADPARM;
  }

  domain = get_current_domain();
  FASTRPC_GET_REF(domain);
  VERIFY(AEE_SUCCESS == (nErr = remote_handle_open_domain(domain, name, ph,
                                                          &t_spawn, &t_load)));
  fastrpc_update_module_list(NON_DOMAIN_LIST_PREPEND, domain, *ph, &local, name,
                             FASTRPC_RESERVED_HANDLE_PRIO);
bail:
  if (nErr) {
    if (*ph) {
      remote_handle64_close(*ph);
    FASTRPC_PUT_REF(domain);
    if (0 == check_rpc_error(nErr)) {
      FARF(ERROR, "Error 0x%x: %s failed for %s (errno %s)", nErr, __func__,
           name, strerror(errno));
    }
  } else {
    FARF(ALWAYS,
         "%s: Successfully opened handle 0x%x for %s on domain %d (spawn time "
         "%" PRIu64 " us, load time %" PRIu64 " us)",
         __func__, (int)(*ph), name, domain, t_spawn, t_load);
  }
  FASTRPC_ATRACE_END();
  return nErr;
}

int remote_handle64_open(const char *name, remote_handle64 *ph) {
  remote_handle h = 0;
  remote_handle64 remote = 0, local;
  int domain = -1, nErr = 0, ref = 0;
  uint64_t t_spawn = 0, t_load = 0;
  uint32_t handle_priority = 0, capability = 0;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));
  FARF(RUNTIME_RPC_HIGH, "Entering %s, name %s\n", __func__, name);
  FASTRPC_ATRACE_BEGIN_L("%s for %s", __func__, name);

  if (!name || !ph) {
    FARF(ERROR, "%s: Invalid input", __func__);
    return AEE_EBADPARM;
  }

  domain = get_domain_from_name(name, DOMAIN_NAME_IN_URI);
  VERIFYC(domain >= 0, AEE_EBADPARM);
  FASTRPC_GET_REF(domain);
  VERIFY(AEE_SUCCESS == (nErr = remote_handle_open_domain(domain, name, &h,
                                                          &t_spawn, &t_load)));
  /* Returning local handle to "geteventd" call causes bad fd error when daemon
     polls on it, hence return remote handle (which is the actual fd) for
     "geteventd" call*/
  if (!std_strncmp(name, ITRANSPORT_PREFIX "geteventfd",
                   std_strlen(ITRANSPORT_PREFIX "geteventfd")) ||
      IS_STATICPD_HANDLE(h)) {
    *ph = h;
  } else {
    /*
     * Only if dsp supports opening of priority handles, read the priority
     * token in uri and use it. If not, ignore the token and always open
     * handle at 0 priority.
     * If dsp supports the feature but client has not specified a priority
     * token in the uri, then the handle will be opened at '0' priority
     * but the thread will still be created in thread group 1 on dsp
     * at default priority.
     */
    nErr = fastrpc_get_cap(domain, HANDLE_PRIORITY_SUPPORT, &capability);
    if (nErr == 0 && capability != 0) {
      VERIFY(AEE_SUCCESS ==
             (nErr = get_handle_priority_from_uri(name, &handle_priority)));
    } else {
      handle_priority = FASTRPC_RESERVED_HANDLE_PRIO;
    }
    fastrpc_update_module_list(DOMAIN_LIST_PREPEND, domain, h, &local, name,
                               handle_priority);
    get_handle_remote(local, &remote);
    *ph = local;
  }
bail:
  if (nErr) {
    if (h)
      remote_handle_close(h);
    FASTRPC_PUT_REF(domain);
    if (0 == check_rpc_error(nErr)) {
      FARF(ERROR, "Error 0x%x: %s failed for %s (errno %s)\n", nErr, __func__,
           name, strerror(errno));
    }
  } else {
    struct handle_list *hlist;
    hlist = get_hlist(domain);
    if (hlist) {
      FARF(ALWAYS,
           "%s: Successfully opened handle 0x%" PRIx64 " (remote 0x%" PRIx64
           ") for %s on domain %d (spawn time %" PRIu64
           " us, load time %" PRIu64 " us), num handles %u",
           __func__, (*ph), remote, name, domain, t_spawn, t_load,
           hlist->domainsCount);
    }
  }
  FASTRPC_ATRACE_END();
  return nErr;
}

int remote_handle_close_domain(int domain, remote_handle h) {
  char *dlerrstr = NULL;
  int dlerr = 0, nErr = AEE_SUCCESS;
  size_t err_str_len = MAX_DLERRSTR_LEN * sizeof(char);
  uint64_t t_close = 0;
  remote_handle64 handle = INVALID_HANDLE;

  FASTRPC_ATRACE_BEGIN_L("%s called with handle 0x%x", __func__, (int)h);
  VERIFYC(h != (remote_handle)-1, AEE_EINVHANDLE);
  VERIFYC(NULL != (dlerrstr = (char *)calloc(1, err_str_len)), AEE_ENOMEMORY);
  PROFILE_ALWAYS(
      &t_close,
      if ((handle = get_remotectl1_handle(domain)) != INVALID_HANDLE) {
        nErr = remotectl1_close1(handle, h, dlerrstr, err_str_len, &dlerr);
        if ((nErr == DSP_AEE_EOFFSET + AEE_ERPC) ||
            nErr == DSP_AEE_EOFFSET + AEE_ENOSUCHMOD) {
          struct handle_list *hlist;

          VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
          FARF(ALWAYS,
               "Warning 0x%x: %s: remotectl1 domains not supported for domain "
               "%d\n",
               nErr, __func__, domain);
          fastrpc_update_module_list(DOMAIN_LIST_DEQUEUE, domain,
                                     NULL, handle, NULL,
                                     FASTRPC_RESERVED_HANDLE_PRIO);

          // Set remotectlhandle to INVALID_HANDLE, so that all subsequent calls
          // are non-domain calls
          hlist->remotectlhandle = INVALID_HANDLE;
          nErr = remotectl_close(h, dlerrstr, err_str_len, &dlerr);
        } else if (nErr)
          goto bail;
      } else {
        VERIFY(AEE_SUCCESS ==
               (nErr = remotectl_close(h, dlerrstr, err_str_len, &dlerr)));
      } VERIFY(AEE_SUCCESS == (nErr = dlerr)););
bail:
  if (nErr != AEE_SUCCESS) {
    if (0 == check_rpc_error(nErr)) {
      FARF(ERROR,
           "Error 0x%x: %s failed for handle 0x%x, domain %d (dlerr %s) (errno "
           "%s)\n",
           nErr, __func__, h, domain, dlerrstr, strerror(errno));
    }
  } else {
    FARF(ALWAYS,
         "%s: closed module with handle 0x%x (skel unload time %" PRIu64 " us)",
         __func__, h, t_close);
  }
  if (dlerrstr) {
    free(dlerrstr);
    dlerrstr = NULL;
  }
  if (domain != -1)
    print_open_handles(domain);
  FASTRPC_ATRACE_END();
  return nErr;
}

int remote_handle_close(remote_handle h) {
  int nErr = AEE_SUCCESS, domain = get_current_domain(), ref = 1;

  FARF(RUNTIME_RPC_HIGH, "Entering %s, handle %lu\n", __func__, h);

  PRINT_WARN_USE_DOMAINS();
  VERIFY(AEE_SUCCESS == (nErr = remote_handle_close_domain(domain, h)));
  fastrpc_update_module_list(NON_DOMAIN_LIST_DEQUEUE, domain, NULL, h, NULL,
                             FASTRPC_RESERVED_HANDLE_PRIO);
  FASTRPC_PUT_REF(domain);
bail:
  if (nErr != AEE_SUCCESS) {
    if (is_process_exiting(domain)) {
      return 0;
    }
    /*
     * handle_info or so name cannot be obtained from remote handles which
     * are used for non-domain calls.
     */
    if (0 == check_rpc_error(nErr)) {
      FARF(ERROR, "Error 0x%x: %s failed for handle 0x%x\n", nErr, __func__, h);
    }
  }
  return nErr;
}

int remote_handle64_close(remote_handle64 handle) {
  remote_handle64 remote = 0;
  int domain = -1, nErr = AEE_SUCCESS, ref = 1;
  bool start_deinit = false;
  struct handle_info *hi = (struct handle_info *)handle;
  struct handle_list *hlist;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  if (IS_STATICPD_HANDLE(handle))
    return AEE_SUCCESS;
  FARF(RUNTIME_RPC_HIGH, "Entering %s, handle %llu\n", __func__, handle);
  FASTRPC_ATRACE_BEGIN_L("%s called with handle 0x%" PRIx64 "\n", __func__,
                         handle);
  VERIFYC(handle != (remote_handle64)-1, AEE_EINVHANDLE);
  VERIFY(AEE_SUCCESS == (nErr = get_domain_from_handle(handle, &domain)));
  VERIFY(AEE_SUCCESS == (nErr = get_handle_remote(handle, &remote)));
  set_thread_context(domain);
  /*
   * Terminate remote session if
   *     1. there are no open non-domain handles AND
   *     2. there are no open multi-domain handles, OR
   *        only 1 multi-domain handle is open (for perf reason,
   *        skip closing of it)
   */
  if (hlist->domainsCount <= 1 && !hlist->nondomainsCount)
    start_deinit = true;
  /*
   * If session termination is not initiated and the remote handle is valid,
   * then close the remote handle on DSP.
   */
  if (!start_deinit && remote) {
    VERIFY(AEE_SUCCESS ==
           (nErr = remote_handle_close_domain(domain, (remote_handle)remote)));
  }
  FARF(ALWAYS,
       "%s: closed module %s with handle 0x%" PRIx64 " remote handle 0x%" PRIx64
       ", num of open handles: %u",
       __func__, hi->name, handle, remote, hlist->domainsCount - 1);
  fastrpc_update_module_list(DOMAIN_LIST_DEQUEUE, domain, NULL, handle, NULL,
                             FASTRPC_RESERVED_HANDLE_PRIO);
  FASTRPC_PUT_REF(domain);
bail:
  if (nErr != AEE_EINVHANDLE && IS_VALID_EFFECTIVE_DOMAIN_ID(domain)) {
    if (start_deinit) {
      hlist->disable_exit_logs = 1;
      domain_deinit(domain);
    }
    if (nErr != AEE_SUCCESS) {
      if (is_process_exiting(domain))
        return 0;
      if (0 == check_rpc_error(nErr))
        FARF(ERROR,
             "Error 0x%x: %s close module %s failed for handle 0x%" PRIx64
             " remote handle 0x%" PRIx64
             " (errno %s), num of open handles: %u\n",
             nErr, __func__, hi->name, handle, remote, strerror(errno),
             hlist->domainsCount);
    }
  }
  FASTRPC_ATRACE_END();
  return nErr;
}

static int manage_pm_qos(int domain, remote_handle64 h, uint32_t enable,
                         uint32_t latency) {
  struct handle_list *hlist;
  hlist = get_hlist(domain);
  if (!hlist)
    return AEE_EBADDOMAIN;
  return fastrpc_set_pm_qos(&hlist->qos, enable, latency);
}

static int manage_adaptive_qos(int domain, uint32_t enable) {
  int nErr = AEE_SUCCESS;
  remote_handle64 handle = INVALID_HANDLE;
  struct handle_list *hlist;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  /* If adaptive QoS is already enabled/disabled, then just return */
  if ((enable && hlist->qos.adaptive_qos) ||
      (!enable && !hlist->qos.adaptive_qos))
    return nErr;

  if (hlist->dev != -1) {
    /* If session is already open on DSP, then make rpc call directly to user PD
     */
    if ((handle = get_remotectl1_handle(domain)) != INVALID_HANDLE) {
      nErr = remotectl1_set_param(handle, RPC_ADAPTIVE_QOS, &enable, 1);
      if ((nErr == DSP_AEE_EOFFSET + AEE_ERPC) ||
          nErr == DSP_AEE_EOFFSET + AEE_ENOSUCHMOD) {
        FARF(ALWAYS,
             "Warning 0x%x: %s: remotectl1 domains not supported for domain "
             "%d\n",
             nErr, __func__, domain);
        fastrpc_update_module_list(DOMAIN_LIST_DEQUEUE, domain,
                                   NULL, handle, NULL,
                                   FASTRPC_RESERVED_HANDLE_PRIO);

        // Set remotectlhandle to INVALID_HANDLE, so that all subsequent calls
        // are non-domain calls
        hlist->remotectlhandle = INVALID_HANDLE;
        nErr = remotectl_set_param(RPC_ADAPTIVE_QOS, &enable, 1);
      }
    } else {
      nErr = remotectl_set_param(RPC_ADAPTIVE_QOS, &enable, 1);
    }
    if (nErr) {
      FARF(ERROR,
           "Error: %s: remotectl_set_param failed to reset adaptive QoS on DSP "
           "to %d on domain %d",
           __func__, enable, domain);
      goto bail;
    } else {
      hlist->qos.adaptive_qos = ((enable == RPC_ADAPTIVE_QOS) ? 1 : 0);
    }
  } else {
    /* If session is not created already, then just set process attribute */
    hlist->qos.adaptive_qos = ((enable == RPC_ADAPTIVE_QOS) ? 1 : 0);
  }

  if (enable)
    FARF(ALWAYS, "%s: Successfully enabled adaptive QoS on domain %d", __func__,
         domain);
  else
    FARF(ALWAYS, "%s: Disabled adaptive QoS on domain %d", __func__, domain);
bail:
  return nErr;
}

static int manage_poll_qos(int domain, remote_handle64 h, uint32_t enable,
                           uint32_t latency) {
  int nErr = AEE_SUCCESS, dev = -1;
  const unsigned int MAX_POLL_TIMEOUT = 10000;
  struct fastrpc_ctrl_latency lp = {0};
  struct handle_list *hlist;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  /* Handle will be -1 in non-domains invocation. Create DSP session if
   * necessary  */
  if (h == INVALID_HANDLE) {
    if (!hlist || (hlist && hlist->dev == -1)) {
      VERIFY(AEE_SUCCESS == (nErr = domain_init(domain, &dev)));
      VERIFYM(-1 != dev, AEE_ERPC, "open dev failed\n");
    }
  }
  /* If the multi-domain handle is valid, then verify that session is created
   * already */
  VERIFYC((hlist) && (-1 != (dev = hlist->dev)), AEE_ERPC);

  // Max poll timeout allowed is 10 ms
  VERIFYC(latency < MAX_POLL_TIMEOUT, AEE_EBADPARM);

  /* Update polling mode in kernel */
  lp.enable = enable;
  lp.latency = latency;
  nErr = ioctl_control(dev, DSPRPC_RPC_POLL, &lp);
  if (nErr) {
    nErr = convert_kernel_to_user_error(nErr, errno);
    goto bail;
  }

  /* Update polling mode in DSP */
  if (h == INVALID_HANDLE) {
    VERIFY(AEE_SUCCESS ==
           (nErr = adsp_current_process_poll_mode(enable, latency)));
  } else {
    remote_handle64 handle = get_adsp_current_process1_handle(domain);
    VERIFY(AEE_SUCCESS ==
           (nErr = adsp_current_process1_poll_mode(handle, enable, latency)));
  }
  FARF(ALWAYS,
       "%s: poll mode updated to %u for domain %d, handle 0x%" PRIx64
       " for timeout %u\n",
       __func__, enable, domain, h, latency);
bail:
  if (nErr) {
    FARF(ERROR,
         "Error 0x%x (errno %d): %s failed for domain %d, handle 0x%" PRIx64
         ", enable %u, timeout %u (%s)\n",
         nErr, errno, __func__, domain, h, enable, latency, strerror(errno));
  }
  return nErr;
}

// Notify FastRPC QoS logic of activity outside of the invoke code path.
// This function needs to be in this file to be able to access hlist.
void fastrpc_qos_activity(int domain) {
  struct handle_list *hlist;

  hlist = get_hlist(domain);
  if (IS_VALID_EFFECTIVE_DOMAIN_ID(domain) && hlist) {
    fastrpc_latency_invoke_incr(&hlist->qos);
  }
}

static inline int enable_process_state_notif_on_dsp(int domain) {
  int nErr = AEE_SUCCESS;
  remote_handle64 notif_handle = 0;

  fastrpc_notif_domain_init(domain);
  if ((notif_handle = get_adsp_current_process1_handle(domain)) !=
      INVALID_HANDLE) {
    VERIFY(AEE_SUCCESS ==
           (nErr = adsp_current_process1_enable_notifications(notif_handle)));
  } else {
    VERIFY(AEE_SUCCESS == (nErr = adsp_current_process_enable_notifications()));
  }
bail:
  return nErr;
}

/*
 * Internal function to get async response from kernel. Waits in kernel until
 * response is received from DSP
 * @ domain: domain to which Async job is submitted
 * @ async_data: IOCTL structure that is sent to kernel to get async response
 * job information returns 0 on success
 *
 */
int get_remote_async_response(int domain, fastrpc_async_jobid *jobid,
                              int *result) {
  int nErr = AEE_SUCCESS, dev = -1;
  uint64_t *perf_kernel = NULL, *perf_dsp = NULL;
  fastrpc_async_jobid job = -1;
  int res = -1;
  remote_handle handle = -1;
  uint32_t sc = 0;
  struct handle_list *hlist;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
  VERIFY(AEE_SUCCESS == (nErr = domain_init(domain, &dev)));
  VERIFYM(-1 != dev, AEE_ERPC, "open dev failed\n");
  if (IS_KERNEL_PERF_ENABLED(hlist->procattrs)) {
    perf_kernel = (uint64_t *)calloc(PERF_KERNEL_KEY_MAX, sizeof(uint64_t));
    VERIFYC(perf_kernel != NULL, AEE_ENOMEMORY);
  }
  if (IS_DSP_PERF_ENABLED(hlist->procattrs)) {
    perf_dsp = (uint64_t *)calloc(PERF_DSP_KEY_MAX, sizeof(uint64_t));
    VERIFYC(perf_dsp != NULL, AEE_ENOMEMORY);
  }
  nErr = ioctl_invoke2_response(dev, &job, &handle, &sc, &res, perf_kernel,
                                perf_dsp);
  if (IS_KERNEL_PERF_ENABLED(hlist->procattrs) && (!IS_STATIC_HANDLE(handle))) {
    VERIFYC(perf_kernel != NULL, AEE_ENOMEMORY);
    FARF(ALWAYS,
         "RPCPERF-K  H:0x%x SC:0x%x C:%" PRIu64 " F:%" PRIu64 " ns M:%" PRIu64
         " ns CP:%" PRIu64 " ns L:%" PRIu64 " ns G:%" PRIu64 " ns P:%" PRIu64
         " ns INV:%" PRIu64 " ns INVOKE:%" PRIu64 " ns\n",
         handle, sc, perf_kernel[0], perf_kernel[1], perf_kernel[2],
         perf_kernel[3], perf_kernel[4], perf_kernel[5], perf_kernel[6],
         perf_kernel[7], perf_kernel[8]);
  }
  if (IS_DSP_PERF_ENABLED(hlist->procattrs) && (!IS_STATIC_HANDLE(handle))) {
    VERIFYC(perf_dsp != NULL, AEE_ENOMEMORY);
    FARF(ALWAYS,
         "RPCPERF-D  H:0x%x SC:0x%x C:%" PRIu64 " M_H:%" PRIu64 " us M:%" PRIu64
         " us G:%" PRIu64 " us INVOKE:%" PRIu64 " us P:%" PRIu64
         " us CACHE:%" PRIu64 " us UM:%" PRIu64 " us "
         "UM_H:%" PRIu64 " us R:%" PRIu64 " us E_R:%" PRIu64
         " us J_S_T:%" PRIu64 " us\n",
         handle, sc, perf_dsp[0], perf_dsp[1], perf_dsp[2], perf_dsp[3],
         perf_dsp[4], perf_dsp[5], perf_dsp[6], perf_dsp[7], perf_dsp[8],
         perf_dsp[9], perf_dsp[10], perf_dsp[11]);
  }
  *jobid = job;
  *result = res;

bail:
  if (perf_kernel) {
    free(perf_kernel);
    perf_kernel = NULL;
  }
  if (perf_dsp) {
    free(perf_dsp);
    perf_dsp = NULL;
  }
  if (nErr) {
    FARF(ERROR,
         "Error 0x%x: %s failed to get async response data for domain %d errno "
         "%s",
         nErr, __func__, domain, strerror(errno));
  }
  return nErr;
}

/* fastrpc_set_qos_latency:
        Send user QoS latency requirement to DSP.
        DSP will use required features to meet the requirement.
        FastRPC driver tries to meet latency requirement but may not be
   guaranteed.
*/
static int fastrpc_set_qos_latency(int domain, remote_handle64 h,
                                   uint32_t latency) {
  int nErr = 0;

  if (h == (remote_handle64)-1) {
    nErr = adsp_current_process_setQoS(latency);
  } else {
    remote_handle64 handle = get_adsp_current_process1_handle(domain);
    nErr = adsp_current_process1_setQoS(handle, latency);
  }
  return nErr;
}

// Notify kernel to enable/disable wakelock control between calls to DSP
static int update_kernel_wakelock_status(int domain, int dev,
                                         uint32_t wl_enable) {
  int nErr = AEE_SUCCESS;
  struct fastrpc_ctrl_wakelock wl = {0};

  wl.enable = wl_enable;
  nErr = ioctl_control(dev, DSPRPC_CONTROL_WAKELOCK, &wl);
  if (nErr) {
    if (errno == EBADRQC || errno == ENOTTY || errno == ENXIO ||
        errno == EINVAL) {
      VERIFY_WPRINTF(
          "Warning: %s: kernel does not support wakelock management (%s)",
          __func__, strerror(errno));
      return AEE_SUCCESS;
    }
    FARF(ERROR,
         "Error 0x%x: %s failed for domain %d, dev %d, wakelock control %d "
         "errno %s",
         nErr, __func__, domain, dev, wl_enable, strerror(errno));
  } else
    FARF(ALWAYS,
         "%s: updated kernel wakelock control status to %d for domain %d",
         __func__, wl_enable, domain);
  return nErr;
}

// Update wakelock status in userspace and notify kernel if necessary
static int wakelock_control(int domain, remote_handle64 h, uint32_t wl_enable) {
  int nErr = AEE_SUCCESS;
  struct handle_list *hlist;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  if (fastrpc_wake_lock_enable[domain] == wl_enable)
    goto bail;

  if (wl_enable) {
    VERIFY(AEE_SUCCESS == (nErr = fastrpc_wake_lock_init()));
  } else {
    VERIFY(AEE_SUCCESS == (nErr = fastrpc_wake_lock_deinit()));
  }
  if (IS_SESSION_OPEN_ALREADY(hlist))
    VERIFY(AEE_SUCCESS == (nErr = update_kernel_wakelock_status(
                               domain, hlist->dev, wl_enable)));
  fastrpc_wake_lock_enable[domain] = wl_enable;
bail:
  if (nErr)
    FARF(ERROR, "Error 0x%x: %s failed for domain %d, handle 0x%x, enable %d",
         nErr, __func__, domain, h, wl_enable);
  else {
    if (wl_enable)
      FARF(ALWAYS, "%s: enabled wakelock control for domain %d", __func__,
           domain);
    else
      FARF(ALWAYS, "%s: disable wakelock control for domain %d", __func__,
           domain);
  }
  return nErr;
}

// Make IOCTL call to kill remote process
static int fastrpc_dsp_process_clean(int domain) {
  int nErr = AEE_SUCCESS, dev;
  struct handle_list *hlist;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  VERIFYM(IS_SESSION_OPEN_ALREADY(hlist), AEE_ERPC,
          "Session not open for domain %d", domain);
  dev = hlist->dev;
  nErr = ioctl_control(dev, DSPRPC_REMOTE_PROCESS_KILL, NULL);
bail:
  if (nErr)
    FARF(ERROR, "Error 0x%x: %s failed for domain %d (errno: %s) ", nErr,
         __func__, domain, strerror(errno));
  return nErr;
}

int remote_handle_control_domain(int domain, remote_handle64 h, uint32_t req,
                                 void *data, uint32_t len) {
  int nErr = AEE_SUCCESS;
  const unsigned int POLL_MODE_PM_QOS_LATENCY = 100;

  FARF(RUNTIME_RPC_HIGH,
       "Entering %s, domain %d, handle %llu, req %d, data %p, size %d\n",
       __func__, domain, h, req, data, len);

  switch (req) {
  case DSPRPC_CONTROL_LATENCY: {
    struct remote_rpc_control_latency *lp =
        (struct remote_rpc_control_latency *)data;
    VERIFYC(lp, AEE_EBADPARM);
    VERIFYC(len == sizeof(struct remote_rpc_control_latency), AEE_EBADPARM);

    switch (lp->enable) {
    /* Only one of PM QoS or adaptive QoS can be enabled */
    case RPC_DISABLE_QOS: {
      VERIFY(AEE_SUCCESS ==
             (nErr = manage_adaptive_qos(domain, RPC_DISABLE_QOS)));
      VERIFY(AEE_SUCCESS ==
             (nErr = manage_pm_qos(domain, h, RPC_DISABLE_QOS, lp->latency)));
      VERIFY(AEE_SUCCESS ==
             (nErr = manage_poll_qos(domain, h, RPC_DISABLE_QOS, lp->latency)));
      /* Error ignored, currently meeting qos requirement is optional. Consider
       * to error out in later targets */
      fastrpc_set_qos_latency(domain, h, FASTRPC_QOS_MAX_LATENCY_USEC);
      break;
    }
    case RPC_PM_QOS: {
      VERIFY(AEE_SUCCESS ==
             (nErr = manage_adaptive_qos(domain, RPC_DISABLE_QOS)));
      VERIFY(AEE_SUCCESS ==
             (nErr = manage_pm_qos(domain, h, RPC_PM_QOS, lp->latency)));
      /* Error ignored, currently meeting qos requirement is optional. Consider
       * to error out in later targets */
      fastrpc_set_qos_latency(domain, h, lp->latency);
      break;
    }
    case RPC_ADAPTIVE_QOS: {
      /* Disable PM QoS if enabled and then enable adaptive QoS */
      VERIFY(AEE_SUCCESS ==
             (nErr = manage_pm_qos(domain, h, RPC_DISABLE_QOS, lp->latency)));
      VERIFY(AEE_SUCCESS ==
             (nErr = manage_adaptive_qos(domain, RPC_ADAPTIVE_QOS)));
      /* Error ignored, currently meeting qos requirement is optional. Consider
       * to error out in later targets */
      fastrpc_set_qos_latency(domain, h, lp->latency);
      break;
    }
    case RPC_POLL_QOS: {
      VERIFY(AEE_SUCCESS ==
             (nErr = manage_poll_qos(domain, h, RPC_POLL_QOS, lp->latency)));

      /*
       * Poll QoS option also enables PM QoS to enable early response from DSP
       * and stop the CPU cores from going into deep sleep low power modes.
       */
      VERIFY(AEE_SUCCESS == (nErr = manage_pm_qos(domain, h, RPC_PM_QOS,
                                                  POLL_MODE_PM_QOS_LATENCY)));
      break;
    }
    default:
      nErr = AEE_EBADPARM;
      FARF(ERROR, "Error: %s: Bad enable parameter %d passed for QoS control",
           __func__, lp->enable);
      goto bail;
    }
    FARF(ALWAYS,
         "%s: requested QOS %d, latency %u for domain %d handle 0x%" PRIx64
         "\n",
         __func__, lp->enable, lp->latency, domain, h);
    break;
  }
  case DSPRPC_GET_DSP_INFO: {
    int dev = -1;
    struct remote_dsp_capability *cap = (struct remote_dsp_capability *)data;

    (void)dev;
    VERIFYC(cap, AEE_EBADPARM);
    VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(cap->domain), AEE_EBADPARM);

    nErr = fastrpc_get_cap(cap->domain, cap->attribute_ID, &cap->capability);
    VERIFY(AEE_SUCCESS == nErr);
    break;
  }
  case DSPRPC_CONTROL_WAKELOCK: {
    struct remote_rpc_control_wakelock *wp =
        (struct remote_rpc_control_wakelock *)data;

    VERIFYC(wp, AEE_EBADPARM);
    VERIFYC(len == sizeof(struct remote_rpc_control_wakelock), AEE_EBADPARM);
    VERIFY(AEE_SUCCESS == (nErr = wakelock_control(domain, h, wp->enable)));
    break;
  }
  case DSPRPC_GET_DOMAIN: {
    remote_rpc_get_domain_t *pGet = (remote_rpc_get_domain_t *)data;

    VERIFYC(pGet, AEE_EBADPARM);
    VERIFYC(len == sizeof(remote_rpc_get_domain_t), AEE_EBADPARM);
    pGet->domain = domain;
    break;
  }
  default:
    nErr = AEE_EUNSUPPORTED;
    FARF(RUNTIME_RPC_LOW,
         "Error: %s: remote handle control called with unsupported request ID "
         "%d",
         __func__, req);
    break;
  }
bail:
  if (nErr != AEE_SUCCESS)
    FARF(ERROR,
         "Error 0x%x: %s failed for request ID %d on domain %d (errno %s)",
         nErr, __func__, req, domain, strerror(errno));
  return nErr;
}

int remote_handle_control(uint32_t req, void *data, uint32_t len) {
  int domain = -1, nErr = AEE_SUCCESS, ref = 0;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  FARF(RUNTIME_HIGH, "Entering %s, req %d, data %p, size %d\n", __func__, req,
       data, len);
  PRINT_WARN_USE_DOMAINS();

  domain = get_current_domain();
  FASTRPC_GET_REF(domain);
  VERIFY(AEE_SUCCESS == (nErr = remote_handle_control_domain(
                             domain, INVALID_HANDLE, req, data, len)));
bail:
  FASTRPC_PUT_REF(domain);
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed for request ID %d (errno %s)", nErr,
         __func__, req, strerror(errno));
    if (0 == check_rpc_error(nErr)) {
      FARF(ERROR, "Error 0x%x: %s failed for request ID %d (errno %s)", nErr,
           __func__, req, strerror(errno));
    }
  }
  return nErr;
}

int remote_handle64_control(remote_handle64 handle, uint32_t req, void *data,
                            uint32_t len) {
  int nErr = AEE_SUCCESS, domain = -1, ref = 0;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  FARF(RUNTIME_HIGH, "Entering %s, handle %llu, req %d, data %p, size %d\n",
       __func__, handle, req, data, len);

  VERIFY(AEE_SUCCESS == (nErr = get_domain_from_handle(handle, &domain)));
  FASTRPC_GET_REF(domain);
  VERIFY(AEE_SUCCESS ==
         (nErr = remote_handle_control_domain(domain, handle, req, data, len)));
bail:
  FASTRPC_PUT_REF(domain);
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed for request ID %d (errno %s)", nErr,
         __func__, req, strerror(errno));
    if (0 == check_rpc_error(nErr)) {
      FARF(ERROR, "Error 0x%x: %s failed for request ID %d (errno %s)", nErr,
           __func__, req, strerror(errno));
    }
  }
  return nErr;
}

static int close_domain_session(int domain) {
  QNode *pn = NULL, *pnn = NULL;
  char dlerrstr[255];
  int dlerr = 0, nErr = AEE_SUCCESS;
  remote_handle64 proc_handle = 0;
  struct handle_list *hlist;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  FARF(ALWAYS,
       "%s: user requested to close fastrpc session on domain %d, dev %d\n",
       __func__, domain, hlist->dev);
  VERIFY(hlist->dev != -1);
  proc_handle = get_adsp_current_process1_handle(domain);
  if (proc_handle != INVALID_HANDLE) {
    adsp_current_process1_exit(proc_handle);
  } else {
    adsp_current_process_exit();
  }
  pthread_mutex_lock(&hlist->lmut);
  if (!QList_IsEmpty(&hlist->nql)) {
    QLIST_NEXTSAFE_FOR_ALL(&hlist->nql, pn, pnn) {
      struct handle_info *hi = STD_RECOVER_REC(struct handle_info, qn, pn);
      VERIFYC(NULL != hi, AEE_EINVHANDLE);
      pthread_mutex_unlock(&hlist->lmut);
      remote_handle_close(hi->remote);
      pthread_mutex_lock(&hlist->lmut);
    }
  }
  if (!QList_IsEmpty(&hlist->rql)) {
    QLIST_NEXTSAFE_FOR_ALL(&hlist->rql, pn, pnn) {
      struct handle_info *hi = STD_RECOVER_REC(struct handle_info, qn, pn);
      VERIFYC(NULL != hi, AEE_EINVHANDLE);
      pthread_mutex_unlock(&hlist->lmut);
      close_reverse_handle(hi->local, dlerrstr, sizeof(dlerrstr), &dlerr);
      pthread_mutex_lock(&hlist->lmut);
    }
  }
  if (!QList_IsEmpty(&hlist->ql)) {
    QLIST_NEXTSAFE_FOR_ALL(&hlist->ql, pn, pnn) {
      struct handle_info *hi = STD_RECOVER_REC(struct handle_info, qn, pn);
      VERIFYC(NULL != hi, AEE_EINVHANDLE);
      pthread_mutex_unlock(&hlist->lmut);
      remote_handle64_close(hi->local);
      pthread_mutex_lock(&hlist->lmut);
    }
  }
  pthread_mutex_unlock(&hlist->lmut);
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed for domain %d (errno %s)", nErr,
         __func__, domain, strerror(errno));
  }
  return nErr;
}

int get_unsigned_pd_attribute(uint32 domain, int *unsigned_module) {
  int nErr = AEE_SUCCESS;
  struct handle_list *hlist;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  VERIFYC(unsigned_module, AEE_EBADPARM);
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
  *unsigned_module = hlist->unsigned_module;
bail:
  return nErr;
}

static int set_unsigned_pd_attribute(int domain, int enable) {
  int nErr = AEE_SUCCESS;
  struct handle_list *hlist;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
  if (hlist->dev != -1) {
    if (hlist->unsigned_module == enable) {
      FARF(HIGH, "%s: %s session already open on domain %d , enable %d ",
           __func__, hlist->unsigned_module ? "Unsigned" : "Signed", domain,
           enable);
    } else {
      nErr = AEE_EALREADYLOADED;
      FARF(ERROR,
           "Error 0x%x: %s: %s session already open on domain %d , enable %d ",
           nErr, __func__, hlist->unsigned_module ? "Unsigned" : "Signed",
           domain, enable);
    }
    goto bail;
  }
  hlist->unsigned_module = enable ? 1 : 0;
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed for domain %d", nErr, __func__, domain);
  }
  return nErr;
}

static int store_domain_thread_params(int domain, int thread_priority,
                                      int stack_size) {
  int nErr = AEE_SUCCESS;
  struct handle_list *hlist;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
  if (thread_priority != -1) {
    if ((thread_priority < MIN_THREAD_PRIORITY) ||
        (thread_priority > MAX_THREAD_PRIORITY)) {
      nErr = AEE_EBADPARM;
      FARF(ERROR,
           "%s: Thread priority %d is invalid! Should be between %d and %d",
           __func__, thread_priority, MIN_THREAD_PRIORITY, MAX_THREAD_PRIORITY);
      goto bail;
    } else {
      hlist->th_params.thread_priority = (uint32_t)thread_priority;
    }
  }
  if (stack_size != -1) {
    if ((stack_size < MIN_UTHREAD_STACK_SIZE) ||
        (stack_size > MAX_UTHREAD_STACK_SIZE)) {
      nErr = AEE_EBADPARM;
      FARF(ERROR, "%s: Stack size %d is invalid! Should be between %d and %d",
           __func__, stack_size, MIN_UTHREAD_STACK_SIZE,
           MAX_UTHREAD_STACK_SIZE);
      goto bail;
    } else
      hlist->th_params.stack_size = (uint32_t)stack_size;
  }
  hlist->th_params.reqID = FASTRPC_THREAD_PARAMS;
  hlist->th_params.update_requested = 1;
  if (hlist->dev != -1) {
    VERIFY(AEE_SUCCESS == (nErr = fastrpc_set_remote_uthread_params(domain)));
    FARF(ALWAYS,
         "Dynamically set remote user thread priority to %d and stack size to "
         "%d for domain %d",
         thread_priority, stack_size, domain);
  }
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR,
         "Error 0x%x: %s failed for domain %d for thread priority %d, stack "
         "size %d",
         nErr, __func__, domain, thread_priority, stack_size);
  }
  return nErr;
}

/* Set attribute to enable/disable pd dump */
static int set_pd_dump_attribute(int domain, int enable) {
  int nErr = AEE_SUCCESS;
  struct handle_list *hlist;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
  if (hlist->dev != -1) {
    nErr = AEE_ERPC;
    FARF(ERROR,
         "%s: Session already open on domain %d ! Request unsigned offload "
         "before making any RPC calls",
         __func__, domain);
    goto bail;
  }
  hlist->pd_dump = enable ? true : false;
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed to enable %d for domain %d", nErr,
         __func__, enable, domain);
  }
  return nErr;
}

/* Set userpd memlen for a requested domain */
static int store_domain_pd_initmem_size(int domain, uint32_t pd_initmem_size) {
  int nErr = AEE_SUCCESS;
  struct handle_list *hlist;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  if ((pd_initmem_size < MIN_PD_INITMEM_SIZE) ||
      (pd_initmem_size > MAX_PD_INITMEM_SIZE)) {
    nErr = AEE_EBADPARM;
    goto bail;
  } else {
    hlist->pd_initmem_size = pd_initmem_size;
  }
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR,
         "Error 0x%x: %s failed for domain %d for pd init mem size :0x%x", nErr,
         __func__, domain, pd_initmem_size);
  }
  return nErr;
}

/* Set remote session parameters like thread stack size, running on unsigned PD,
 * killing remote process PD etc */
int remote_session_control(uint32_t req, void *data, uint32_t datalen) {
  int nErr = AEE_SUCCESS, domain = DEFAULT_DOMAIN_ID, ref = 0, ii = 0;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  FARF(RUNTIME_RPC_HIGH, "Entering %s, req %d, data %p, size %d\n", __func__,
       req, data, datalen);
  switch (req) {
  case FASTRPC_THREAD_PARAMS: {
    struct remote_rpc_thread_params *params =
        (struct remote_rpc_thread_params *)data;
    if (!params) {
      nErr = AEE_EBADPARM;
      FARF(RUNTIME_RPC_LOW, "%s: Thread params struct passed is %p", __func__,
           params);
      goto bail;
    }
    VERIFYC(datalen == sizeof(struct remote_rpc_thread_params), AEE_EBADPARM);
    if (params->domain != -1) {
      if (!IS_VALID_EFFECTIVE_DOMAIN_ID(params->domain)) {
        nErr = AEE_EBADPARM;
        FARF(RUNTIME_RPC_LOW, "%s: Invalid domain ID %d passed", __func__,
             params->domain);
        goto bail;
      }
      FASTRPC_GET_REF(params->domain);
      VERIFY(AEE_SUCCESS ==
             (nErr = store_domain_thread_params(params->domain, params->prio,
                                                params->stack_size)));
      FASTRPC_PUT_REF(params->domain);
    } else {
      /* If domain is -1, then set parameters for all domains */
      FOR_EACH_EFFECTIVE_DOMAIN_ID(ii) {
        FASTRPC_GET_REF(ii);
        VERIFY(AEE_SUCCESS == (nErr = store_domain_thread_params(
                                   ii, params->prio, params->stack_size)));
        FASTRPC_PUT_REF(ii);
      }
    }
    FARF(ALWAYS,
         "%s DSP info request for domain %d, thread priority %d, stack size %d",
         __func__, params->domain, params->prio, params->stack_size);
    break;
  }
  case DSPRPC_CONTROL_UNSIGNED_MODULE: {
    // Handle the unsigned module offload request
    struct remote_rpc_control_unsigned_module *um =
        (struct remote_rpc_control_unsigned_module *)data;
    VERIFYC(datalen == sizeof(struct remote_rpc_control_unsigned_module),
            AEE_EBADPARM);
    VERIFYC(um != NULL, AEE_EBADPARM);
    if (um->domain != -1) {
      VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(um->domain), AEE_EBADPARM);
      FASTRPC_GET_REF(um->domain);
      VERIFY(AEE_SUCCESS ==
             (nErr = set_unsigned_pd_attribute(um->domain, um->enable)));
      FASTRPC_PUT_REF(um->domain);
    } else {
      FOR_EACH_EFFECTIVE_DOMAIN_ID(ii) {
        FASTRPC_GET_REF(ii);
        VERIFY(AEE_SUCCESS ==
               (nErr = set_unsigned_pd_attribute(ii, um->enable)));
        FASTRPC_PUT_REF(ii);
      }
    }
    FARF(ALWAYS, "%s Unsigned PD enable %d request for domain %d", __func__,
         um->enable, um->domain);
    break;
  }
  case FASTRPC_RELATIVE_THREAD_PRIORITY: {
    int thread_priority = DEFAULT_UTHREAD_PRIORITY, rel_thread_prio = 0;
    struct remote_rpc_relative_thread_priority *params =
        (struct remote_rpc_relative_thread_priority *)data;

    VERIFYC(datalen == sizeof(struct remote_rpc_relative_thread_priority),
            AEE_EBADPARM);
    VERIFYC(params, AEE_EBADPARM);
    rel_thread_prio = params->relative_thread_priority;

    // handle thread priority overflow and out-of-range conditions
    if (rel_thread_prio < 0) {
      thread_priority = ((~rel_thread_prio) >= DEFAULT_UTHREAD_PRIORITY - 1)
                            ? MIN_THREAD_PRIORITY
                            : (thread_priority + rel_thread_prio);
    } else {
      thread_priority =
          (rel_thread_prio > (MAX_THREAD_PRIORITY - DEFAULT_UTHREAD_PRIORITY))
              ? MAX_THREAD_PRIORITY
              : (thread_priority + rel_thread_prio);
    }
    if (params->domain != -1) {
      VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(params->domain), AEE_EBADPARM);
      FASTRPC_GET_REF(params->domain);
      VERIFY(AEE_SUCCESS == (nErr = store_domain_thread_params(
                                 params->domain, thread_priority, -1)));
      FASTRPC_PUT_REF(params->domain);
    } else {
      FOR_EACH_EFFECTIVE_DOMAIN_ID(ii) {
        FASTRPC_GET_REF(ii);
        VERIFY(AEE_SUCCESS ==
               (nErr = store_domain_thread_params(ii, thread_priority, -1)));
        FASTRPC_PUT_REF(ii);
      }
    }
    FARF(ALWAYS, "%s DSP thread priority request for domain %d, priority %d",
         __func__, req, params->domain, thread_priority);
    break;
  }
  case FASTRPC_REMOTE_PROCESS_KILL: {
    struct remote_rpc_process_clean_params *dp =
        (struct remote_rpc_process_clean_params *)data;

    VERIFYC(datalen == sizeof(struct remote_rpc_process_clean_params),
            AEE_EBADPARM);
    VERIFYC(dp, AEE_EBADPARM);
    domain = dp->domain;
    FASTRPC_GET_REF(dp->domain);
    VERIFY(AEE_SUCCESS == (nErr = fastrpc_dsp_process_clean(domain)));
    FASTRPC_PUT_REF(dp->domain);
    FARF(ALWAYS, "%s Remote process kill request for domain %d", __func__,
         domain);
    break;
  }
  case FASTRPC_SESSION_CLOSE: {
    struct remote_rpc_session_close *sclose =
        (struct remote_rpc_session_close *)data;
    if (!sclose) {
      nErr = AEE_EBADPARM;
      FARF(RUNTIME_RPC_LOW,
           "Error: %s: session close data pointer passed is NULL\n", __func__);
      goto bail;
    }
    VERIFYC(datalen == sizeof(struct remote_rpc_session_close), AEE_EBADPARM);
    if (sclose->domain != -1) {
      VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(sclose->domain), AEE_EBADPARM);
      FASTRPC_GET_REF(sclose->domain);
      VERIFY(AEE_SUCCESS == (nErr = close_domain_session(sclose->domain)));
      FASTRPC_PUT_REF(sclose->domain);
    } else {
      FOR_EACH_EFFECTIVE_DOMAIN_ID(ii) {
        FASTRPC_GET_REF(ii);
        VERIFY(AEE_SUCCESS == (nErr = close_domain_session(ii)));
        FASTRPC_PUT_REF(ii);
      }
    }
    FARF(ALWAYS, "%s Fastrpc session close request for domain %d\n", __func__,
         sclose->domain);
    break;
  }
  case FASTRPC_CONTROL_PD_DUMP: {
    // Handle pd dump enable/disable request
    struct remote_rpc_control_pd_dump *pddump =
        (struct remote_rpc_control_pd_dump *)data;
    VERIFYC(datalen == sizeof(struct remote_rpc_control_pd_dump), AEE_EBADPARM);
    VERIFYC(pddump != NULL, AEE_EBADPARM);
    if (pddump->domain != -1) {
      FASTRPC_GET_REF(pddump->domain);
      VERIFY(AEE_SUCCESS ==
             (nErr = set_pd_dump_attribute(pddump->domain, pddump->enable)));
      FASTRPC_PUT_REF(pddump->domain);
    } else {
      FOR_EACH_EFFECTIVE_DOMAIN_ID(ii) {
        FASTRPC_GET_REF(ii);
        VERIFY(AEE_SUCCESS ==
               (nErr = set_pd_dump_attribute(ii, pddump->enable)));
        FASTRPC_PUT_REF(ii);
      }
    }
    FARF(ALWAYS, "%s PD dump request to enable(%d) for domain %d", __func__,
         pddump->enable, pddump->domain);
    break;
  }
  case FASTRPC_REMOTE_PROCESS_EXCEPTION: {
    // Trigger non fatal exception in the User PD of DSP.
    remote_rpc_process_exception *dp = (remote_rpc_process_exception *)data;
    remote_handle64 handle = 0;
    int ret = 0;

    VERIFYC(datalen == sizeof(remote_rpc_process_exception), AEE_EBADPARM);
    VERIFYC(dp, AEE_EBADPARM);
    domain = dp->domain;
    VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
    /*
     * If any request causes exception on DSP, DSP returns AEE_EBADSTATE for the
     * request thread.
     */
    FASTRPC_GET_REF(domain);
    if ((handle = get_adsp_current_process1_handle(domain)) != INVALID_HANDLE) {
      ret = adsp_current_process1_exception(handle);
    } else {
      ret = adsp_current_process_exception();
    }
    FASTRPC_PUT_REF(domain);
    ret = (ret == AEE_SUCCESS) ? AEE_ERPC : ret;
    VERIFYC(ret == (int)(DSP_AEE_EOFFSET + AEE_EBADSTATE), ret);
    FARF(ALWAYS,
         "%s Remote process exception request for domain %d, handle 0x%" PRIx64
         "\n",
         __func__, domain, handle);
    break;
  }
  case FASTRPC_REMOTE_PROCESS_TYPE: {
    struct remote_process_type *typ = (struct remote_process_type *)data;
    int ret_val = -1;
    struct handle_list *hlist;

    VERIFYC(datalen == sizeof(struct remote_process_type), AEE_EBADPARM);
    VERIFYC(typ, AEE_EBADPARM);
    domain = typ->domain;
    VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
    VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
    ret_val = hlist->unsigned_module;
    if (ret_val != PROCESS_TYPE_UNSIGNED && ret_val != PROCESS_TYPE_SIGNED) {
      typ->process_type = -1;
      nErr = AEE_EBADPARM;
    } else {
      typ->process_type = ret_val;
    }
    break;
  }
  case FASTRPC_REGISTER_STATUS_NOTIFICATIONS: {
    // Handle DSP PD notification request
    struct remote_rpc_notif_register *notif =
        (struct remote_rpc_notif_register *)data;
    VERIFYC(datalen == sizeof(struct remote_rpc_notif_register), AEE_EBADPARM);
    VERIFYC(notif != NULL, AEE_EBADPARM);
    domain = notif->domain;
    VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
    if (domain != -1) {
      FASTRPC_GET_REF(domain);
      VERIFYC(is_status_notif_version2_supported(domain), AEE_EUNSUPPORTED);
      VERIFY(AEE_SUCCESS == (nErr = fastrpc_notif_register(domain, notif)));
      FASTRPC_PUT_REF(domain);
    } else {
      FOR_EACH_EFFECTIVE_DOMAIN_ID(ii) {
        FASTRPC_GET_REF(ii);
        VERIFYC(is_status_notif_version2_supported(ii), AEE_EUNSUPPORTED);
        VERIFY(AEE_SUCCESS == (nErr = fastrpc_notif_register(ii, notif)));
        FASTRPC_PUT_REF(ii);
      }
    }
    FARF(ALWAYS, "%s Register PD status notification request for domain %d\n",
         __func__, domain);
    break;
  }
  case FASTRPC_PD_INITMEM_SIZE: {
    // Handle DSP  User PD init memory size request
    struct remote_rpc_pd_initmem_size *params =
        (struct remote_rpc_pd_initmem_size *)data;
    struct handle_list *hlist;
    VERIFYC(datalen == sizeof(struct remote_rpc_pd_initmem_size), AEE_EBADPARM);
    VERIFYC(params != NULL, AEE_EBADPARM);
    if (params->domain != -1) {
      if (!IS_VALID_EFFECTIVE_DOMAIN_ID(params->domain)) {
        nErr = AEE_EBADPARM;
        FARF(ERROR, "%s: Invalid domain ID %d passed", __func__,
             params->domain);
        goto bail;
      }
      hlist = get_hlist(params->domain);
      if (!hlist)
        goto bail;
      if (hlist->unsigned_module) {
        nErr = AEE_EUNSUPPORTED;
        FARF(ERROR, "Configuring User PD init mem length is not supported for "
                    "unsigned PDs");
        goto bail;
      }
      FASTRPC_GET_REF(params->domain);
      VERIFY(AEE_SUCCESS == (nErr = store_domain_pd_initmem_size(
                                 params->domain, params->pd_initmem_size)));
      FASTRPC_PUT_REF(params->domain);
    } else {
      struct handle_list *hlist;
      /* If domain is -1, then set parameters for all domains */
      FOR_EACH_EFFECTIVE_DOMAIN_ID(ii) {
        hlist = get_hlist(ii);
        if (!hlist)
          continue;
        if (hlist->unsigned_module) {
          FARF(ALWAYS,
               "Warning: %s: Configuring User PD init mem length for domain %d "
               "is not supported for unsigned PDs",
               __func__, ii);
        } else {
          FASTRPC_GET_REF(ii);
          VERIFY(AEE_SUCCESS == (nErr = store_domain_pd_initmem_size(
                                     ii, params->pd_initmem_size)));
          FASTRPC_PUT_REF(ii);
        }
      }
    }
    FARF(ALWAYS,
         "%s DSP userpd memlen request for domain %d, userpd memlen 0x%x",
         __func__, params->domain, params->pd_initmem_size);
    break;
  }
  case FASTRPC_RESERVE_NEW_SESSION: {
    remote_rpc_reserve_new_session_t *sess =
        (remote_rpc_reserve_new_session_t *)data;
    int ii = 0, jj = 0;
    struct handle_list *hlist;

    VERIFYC(datalen == sizeof(remote_rpc_reserve_new_session_t), AEE_EBADPARM);
    VERIFYC(sess && sess->domain_name != NULL && sess->domain_name_len > 0 &&
                sess->session_name && sess->session_name_len > 0,
            AEE_EBADPARM);
    domain = get_domain_from_name(sess->domain_name, DOMAIN_NAME_STAND_ALONE);
    VERIFYC(IS_VALID_DOMAIN_ID(domain), AEE_EBADPARM);
    // Initialize effective domain ID to 2nd session of domain, first session is
    // default usage and cannot be reserved
    ii = domain + NUM_DOMAINS;
    // Initialize session to 1, session 0 is default session
    jj = 1;
    // Set effective_domain_id and session_id to invalid IDs
    sess->effective_domain_id = NUM_DOMAINS_EXTEND;
    sess->session_id = NUM_SESSIONS;
    do {
      hlist = get_hlist(ii);
      if (!hlist)
        break;
      pthread_mutex_lock(&hlist->init);
      if (!hlist->is_session_reserved) {
        hlist->is_session_reserved = true;
        sess->effective_domain_id = ii;
        sess->session_id = jj;
        std_strlcpy(hlist->sessionname, sess->session_name,
                    STD_MIN(sess->session_name_len, (MAX_DSPPD_NAMELEN - 1)));
        pthread_mutex_unlock(&hlist->init);
        break;
      }
      pthread_mutex_unlock(&hlist->init);
      // Increment to next session of domain
      ii = ii + NUM_DOMAINS;
      // Increment the session
      jj++;
    } while (IS_VALID_EFFECTIVE_DOMAIN_ID(ii));
    VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(sess->effective_domain_id),
            AEE_ENOSESSION);
    break;
  }
  case FASTRPC_GET_EFFECTIVE_DOMAIN_ID: {
    remote_rpc_effective_domain_id_t *effec_domain_id =
        (remote_rpc_effective_domain_id_t *)data;
    VERIFYC(datalen == sizeof(remote_rpc_effective_domain_id_t), AEE_EBADPARM);
    VERIFYC(effec_domain_id && effec_domain_id->domain_name &&
                effec_domain_id->domain_name_len > 0 &&
                effec_domain_id->session_id < NUM_SESSIONS,
            AEE_EBADPARM);
    domain = get_domain_from_name(effec_domain_id->domain_name,
                                  DOMAIN_NAME_STAND_ALONE);
    VERIFYC(IS_VALID_DOMAIN_ID(domain), AEE_EBADPARM);
    effec_domain_id->effective_domain_id =
        GET_EFFECTIVE_DOMAIN_ID(domain, effec_domain_id->session_id);
    VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(effec_domain_id->effective_domain_id),
            AEE_ENOSESSION);
    break;
  }
  case FASTRPC_GET_URI: {
    remote_rpc_get_uri_t *rpc_uri = (remote_rpc_get_uri_t *)data;
    int ret_val = -1;

    VERIFYC(datalen == sizeof(remote_rpc_get_uri_t), AEE_EBADPARM);
    VERIFYC(rpc_uri && rpc_uri->domain_name && rpc_uri->domain_name_len > 0 &&
                rpc_uri->session_id < NUM_SESSIONS,
            AEE_EBADPARM);
    domain =
        get_domain_from_name(rpc_uri->domain_name, DOMAIN_NAME_STAND_ALONE);
    VERIFYC(IS_VALID_DOMAIN_ID(domain), AEE_EBADPARM);
    VERIFYC(rpc_uri->module_uri != NULL && rpc_uri->module_uri_len > 0,
            AEE_EBADPARM);
    VERIFYC(rpc_uri->uri != NULL && rpc_uri->uri_len > rpc_uri->module_uri_len,
            AEE_EBADPARM);
    ret_val = snprintf(0, 0, "%s%s%s%s%d", rpc_uri->module_uri,
                       FASTRPC_DOMAIN_URI, SUBSYSTEM_NAME[domain],
                       FASTRPC_SESSION_URI, rpc_uri->session_id);
    if (rpc_uri->uri_len <= ret_val) {
      nErr = AEE_EBADSIZE;
      FARF(ERROR,
           "ERROR 0x%x: %s Session URI length %u is not enough, need %u "
           "characters",
           nErr, __func__, rpc_uri->uri_len, ret_val);
    }
    ret_val = snprintf(rpc_uri->uri, rpc_uri->uri_len, "%s%s%s%s%d",
                       rpc_uri->module_uri, FASTRPC_DOMAIN_URI,
                       SUBSYSTEM_NAME[domain], FASTRPC_SESSION_URI,
                       rpc_uri->session_id);
    if (ret_val < 0) {
      nErr = AEE_EBADSIZE;
      FARF(ERROR, "ERROR 0x%x: %s Invalid Session URI length %u", nErr,
           __func__, rpc_uri->uri_len);
    }
    break;
  }
  case FASTRPC_CONTEXT_CREATE:
    VERIFYC(datalen == sizeof(fastrpc_context_create) && data, AEE_EBADPARM);
    VERIFY(AEE_SUCCESS == (nErr = fastrpc_create_context(data)));
    break;
  case FASTRPC_CONTEXT_DESTROY: {
    fastrpc_context_destroy *dest = (fastrpc_context_destroy *)data;

    VERIFYC(datalen == sizeof(fastrpc_context_destroy) && dest && !dest->flags,
            AEE_EBADPARM);
    VERIFY(AEE_SUCCESS == (nErr = fastrpc_destroy_context(dest->ctx)));
    break;
  }
  default:
    nErr = AEE_EUNSUPPORTED;
    FARF(ERROR, "ERROR 0x%x: %s Unsupported request ID %d", nErr, __func__,
         req);
    break;
  }
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed for request ID %d (errno %s)", nErr,
         __func__, req, strerror(errno));
    if (0 == check_rpc_error(nErr)) {
      FARF(ERROR, "Error 0x%x: %s failed for request ID %d (errno %s)", nErr,
           __func__, req, strerror(errno));
    }
  }
  return nErr;
}

/* Function to get domain id from URI
 * @uri: URI string
 * @type: Type of URI
 * @returns: 0 on success, valid non-zero error code on failure
 */
int get_domain_from_name(const char *uri, uint32_t type) {
  int domain = DEFAULT_DOMAIN_ID;
  char *session_uri = NULL;
  int session_id = 0;

  if (uri && type == DOMAIN_NAME_STAND_ALONE) {
    if (!std_strncmp(uri, ADSP_DOMAIN_NAME, std_strlen(ADSP_DOMAIN_NAME))) {
      domain = ADSP_DOMAIN_ID;
    } else if (!std_strncmp(uri, MDSP_DOMAIN_NAME,
                            std_strlen(MDSP_DOMAIN_NAME))) {
      domain = MDSP_DOMAIN_ID;
    } else if (!std_strncmp(uri, SDSP_DOMAIN_NAME,
                            std_strlen(SDSP_DOMAIN_NAME))) {
      domain = SDSP_DOMAIN_ID;
    } else if (!std_strncmp(uri, CDSP1_DOMAIN_NAME,
                            std_strlen(CDSP1_DOMAIN_NAME))) {
      domain = CDSP1_DOMAIN_ID;
    } else if (!std_strncmp(uri, CDSP_DOMAIN_NAME,
                            std_strlen(CDSP_DOMAIN_NAME))) {
      domain = CDSP_DOMAIN_ID;
    } else {
      domain = INVALID_DOMAIN_ID;
      FARF(ERROR, "Invalid domain name: %s\n", uri);
      return domain;
    }
  }
  if (uri && type == DOMAIN_NAME_IN_URI) {
    if (std_strstr(uri, ADSP_DOMAIN)) {
      domain = ADSP_DOMAIN_ID;
    } else if (std_strstr(uri, MDSP_DOMAIN)) {
      domain = MDSP_DOMAIN_ID;
    } else if (std_strstr(uri, SDSP_DOMAIN)) {
      domain = SDSP_DOMAIN_ID;
    } else if (std_strstr(uri, CDSP1_DOMAIN)) {
      domain = CDSP1_DOMAIN_ID;
    } else if (std_strstr(uri, CDSP_DOMAIN)) {
      domain = CDSP_DOMAIN_ID;
    } else {
      domain = INVALID_DOMAIN_ID;
      FARF(ERROR, "Invalid domain name: %s\n", uri);
      goto bail;
    }
    if (NULL != (session_uri = std_strstr(uri, FASTRPC_SESSION_URI))) {
      session_uri = session_uri + std_strlen(FASTRPC_SESSION_URI);
      // Get Session ID from URI
      session_id = strtol(session_uri, NULL, 10);
      if (session_id < NUM_SESSIONS) {
        domain = GET_EFFECTIVE_DOMAIN_ID(domain, session_id);
      } else {
        domain = INVALID_DOMAIN_ID;
        FARF(ERROR, "Invalid domain name: %s\n", uri);
      }
    }
  }
bail:
  VERIFY_IPRINTF("%s: %d\n", __func__, domain);
  return domain;
}

int fastrpc_get_pd_type(int domain) {
  struct handle_list *hlist = NULL;

  if (NULL != (hlist = get_hlist(domain))) {
    return hlist->dsppd;
  } else {
    return -1;
  }
}

int get_current_domain(void) {
  struct handle_list *list;
  int domain = -1;

  /* user hint to pick default domain id
   * first try tlskey before using default domain
   */
  list = (struct handle_list *)pthread_getspecific(tlsKey);
  domain = get_hlist_domain(list);
  if (!IS_VALID_EFFECTIVE_DOMAIN_ID(domain)) {
    // use default domain if thread tlskey not found
    domain = DEFAULT_DOMAIN_ID;
  }
  return domain;
}

bool is_process_exiting(int domain) {
  int nErr = 0, state = 1;
  struct handle_list *hlist;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  (void)nErr;
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADDOMAIN);
  pthread_mutex_lock(&hlist->mut);
  state = hlist->state;
  pthread_mutex_unlock(&hlist->mut);
  if (state != FASTRPC_DOMAIN_STATE_INIT)
    return true;
  else
    return false;
bail:
  return true;
}

int remote_set_mode(uint32_t mode) {
  int i;
  struct handle_list *hlist;
  FOR_EACH_EFFECTIVE_DOMAIN_ID(i) {
    hlist = get_hlist(i);
    hlist->mode = mode;
    hlist->setmode = 1;
  }
  return AEE_SUCCESS;
}

PL_DEP(fastrpc_apps_user);
PL_DEP(gpls);
PL_DEP(apps_std);
PL_DEP(rpcmem);
PL_DEP(listener_android);
PL_DEP(fastrpc_async);

static int attach_guestos(int domain) {
  int attach;

  switch (GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain)) {
  case MDSP_DOMAIN_ID:
  case ADSP_DOMAIN_ID:
  case CDSP_DOMAIN_ID:
  case SDSP_DOMAIN_ID:
  case CDSP1_DOMAIN_ID:
    attach = USERPD;
    break;
  default:
    attach = ROOT_PD;
    break;
  }
  return attach;
}

static void domain_deinit(int domain) {
  int olddev;
  remote_handle64 handle = 0;
  uint64_t t_kill;
  struct handle_list *hlist;

  hlist = get_hlist(domain);
  if (!hlist)
    return;
  olddev = hlist->dev;
  FARF(ALWAYS, "%s for domain %d: dev %d", __func__, domain, olddev);
  if (olddev != -1) {

    FASTRPC_ATRACE_BEGIN_L("%s called for handle 0x%x, domain %d, dev %d",
                           __func__, handle, domain, olddev);

    handle = get_adsp_current_process1_handle(domain);
    if (handle != INVALID_HANDLE) {
      adsp_current_process1_exit(handle);
    } else {
      adsp_current_process_exit();
    }

    pthread_mutex_lock(&hlist->mut);
    hlist->state = FASTRPC_DOMAIN_STATE_DEINIT;
    pthread_mutex_unlock(&hlist->mut);

    dspsignal_domain_deinit(domain);
    listener_android_domain_deinit(domain);
    hlist->first_revrpc_done = 0;
    pthread_mutex_lock(&hlist->async_init_deinit_mut);
    fastrpc_async_domain_deinit(domain);
    pthread_mutex_unlock(&hlist->async_init_deinit_mut);
    fastrpc_notif_domain_deinit(domain);
    fastrpc_clear_handle_list(MULTI_DOMAIN_HANDLE_LIST_ID, domain);
    fastrpc_clear_handle_list(REVERSE_HANDLE_LIST_ID, domain);
    if (domain == DEFAULT_DOMAIN_ID) {
      fastrpc_clear_handle_list(NON_DOMAIN_HANDLE_LIST_ID, domain);
    }
    fastrpc_perf_deinit();
    fastrpc_latency_deinit(&hlist->qos);
    trace_marker_deinit(domain);
    deinitFileWatcher(domain);
    adspmsgd_stop(domain);
    fastrpc_mem_close(domain);
    apps_mem_deinit(domain);
    hlist->state = 0;
    hlist->ref = 0;

    hlist->cphandle = 0;
    hlist->msghandle = 0;
    hlist->remotectlhandle = 0;
    hlist->listenerhandle = 0;
    hlist->dev = -1;
    hlist->info = -1;
    hlist->dsppd = USERPD;
    memset(hlist->dsppdname, 0, MAX_DSPPD_NAMELEN);
    memset(hlist->sessionname, 0, MAX_DSPPD_NAMELEN);
    PROFILE_ALWAYS(&t_kill, close_device_node(domain, olddev););
    FARF(ALWAYS, "%s: closed device %d on domain %d (kill time %" PRIu64 " us)",
         __func__, olddev, domain, t_kill);
    FASTRPC_ATRACE_END();
  }
  hlist->proc_sharedbuf_cur_addr = NULL;
  if (hlist->proc_sharedbuf) {
    rpcmem_free_internal(hlist->proc_sharedbuf);
    hlist->proc_sharedbuf = NULL;
  }
  // Free the session, on session deinit
  pthread_mutex_lock(&hlist->init);
  hlist->is_session_reserved = false;
  pthread_mutex_unlock(&hlist->init);
  pthread_mutex_lock(&hlist->mut);
  hlist->state = FASTRPC_DOMAIN_STATE_CLEAN;
  pthread_mutex_unlock(&hlist->mut);
}

static const char *get_domain_name(int domain_id) {
  const char *name;
  int domain = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain_id);

  switch (domain) {
  case ADSP_DOMAIN_ID:
    name = ADSPRPC_DEVICE;
    break;
  case SDSP_DOMAIN_ID:
    name = SDSPRPC_DEVICE;
    break;
  case MDSP_DOMAIN_ID:
    name = MDSPRPC_DEVICE;
    break;
  case CDSP_DOMAIN_ID:
    name = CDSPRPC_DEVICE;
    break;
  case CDSP1_DOMAIN_ID:
    name = CDSP1RPC_DEVICE;
    break;
  default:
    name = DEFAULT_DEVICE;
    break;
  }
  return name;
}

/* Opens device node based on the domain
 This function takes care of the backward compatibility to open
 approriate device for following configurations of the device nodes
 1. 4 different device nodes
 2. 1 device node (adsprpc-smd)
 3. 2 device nodes (adsprpc-smd, adsprpc-smd-secure)
 Algorithm
 For ADSP, SDSP, MDSP domains:
   Open secure device node fist
     if no secure device, open actual device node
     if still no device, open default node
     if failed to open the secure node due to permission,
     open default node
 For CDSP domain:
   Open secure device node fist
    If the node does not exist or if no permission, open actual device node
    If still no device, open default node
    If no permission to access the default node, access thorugh HAL.
*/
static int open_device_node(int domain_id) {
  int dev = -1, nErr = 0;
  int domain = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain_id);
  int sess_id = GET_SESSION_ID_FROM_DOMAIN_ID(domain_id);

  switch (domain) {
  case ADSP_DOMAIN_ID:
  case SDSP_DOMAIN_ID:
  case MDSP_DOMAIN_ID:
    dev = open(get_secure_domain_name(domain), O_NONBLOCK);
    if ((dev < 0) && (errno == ENOENT)) {
      FARF(RUNTIME_RPC_HIGH,
           "Device node %s open failed for domain %d (errno %s),"
           "falling back to node %s \n",
           get_secure_domain_name(domain), domain, strerror(errno),
           get_domain_name(domain));
      dev = open(get_domain_name(domain), O_NONBLOCK);
      if ((dev < 0) && (errno == ENOENT)) {
        FARF(RUNTIME_RPC_HIGH,
             "Device node %s open failed for domain %d (errno %s),"
             "falling back to node %s \n",
             get_domain_name(domain), domain, strerror(errno), DEFAULT_DEVICE);
        dev = open(DEFAULT_DEVICE, O_NONBLOCK);
      }
    } else if ((dev < 0) && (errno == EACCES)) {
      // Open the default device node if unable to open the
      // secure device node due to permissions
      FARF(RUNTIME_RPC_HIGH,
           "Device node %s open failed for domain %d (errno %s),"
           "falling back to node %s \n",
           get_secure_domain_name(domain), domain, strerror(errno),
           DEFAULT_DEVICE);
      dev = open(DEFAULT_DEVICE, O_NONBLOCK);
    }
    break;
  case CDSP_DOMAIN_ID:
  case CDSP1_DOMAIN_ID:
    dev = open(get_secure_domain_name(domain), O_NONBLOCK);
    if ((dev < 0) && ((errno == ENOENT) || (errno == EACCES))) {
      FARF(RUNTIME_RPC_HIGH,
           "Device node %s open failed for domain %d (errno %s),"
           "falling back to node %s \n",
           get_secure_domain_name(domain), domain, strerror(errno),
           get_domain_name(domain));
      dev = open(get_domain_name(domain), O_NONBLOCK);
      if ((dev < 0) && ((errno == ENOENT) || (errno == EACCES))) {
        // Open the default device node if actual device node
        // is not present
        FARF(RUNTIME_RPC_HIGH,
             "Device node %s open failed for domain %d (errno %s),"
             "falling back to node %s \n",
             get_domain_name(domain), domain, strerror(errno), DEFAULT_DEVICE);
        dev = open(DEFAULT_DEVICE, O_NONBLOCK);
#ifndef NO_HAL
        if ((dev < 0) && (errno == EACCES)) {
          FARF(ALWAYS,
               "%s: no access to default device of domain %d, open thru HAL, "
               "(sess_id %d)\n",
               __func__, domain, sess_id);
          VERIFYC(sess_id < NUM_SESSIONS, AEE_EBADITEM);
          pthread_mutex_lock(&dsp_client_mut);
          if (!dsp_client_instance[sess_id]) {
            dsp_client_instance[sess_id] = create_dsp_client_instance();
          }
          pthread_mutex_unlock(&dsp_client_mut);
          dev = open_hal_session(dsp_client_instance[sess_id], domain_id);
        }
#endif
      }
    }
    break;
  default:
    break;
  }
  if (dev < 0)
    FARF(ERROR,
         "Error 0x%x: %s failed for domain ID %d, sess ID %d secure dev : %s, "
         "dev : %s. (errno %d, %s) (Either the remote processor is down, or "
         "application does not have permission to access the remote "
         "processor\n",
         nErr, __func__, domain_id, sess_id, get_secure_domain_name(domain),
         get_domain_name(domain), errno, strerror(errno));
  return dev;
}

static int close_device_node(int domain_id, int dev) {
  int nErr = 0, domain = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain_id);

#ifndef NO_HAL
  int sess_id = GET_SESSION_ID_FROM_DOMAIN_ID(domain_id);
  if ((domain == CDSP_DOMAIN_ID) || (domain == CDSP1_DOMAIN_ID)) &&
   dsp_client_instance[sess_id]) {
      nErr = close_hal_session(dsp_client_instance[sess_id], domain_id, dev);
      FARF(ALWAYS, "%s: close device %d thru HAL on session %d\n", __func__,
           dev, sess_id);
    }
  else {
#endif
    nErr = close(dev);
    FARF(ALWAYS, "%s: closed dev %d on domain %d", __func__, dev, domain_id);
#ifndef NO_HAL
  }
#endif
  return nErr;
}

static int get_process_attrs(int domain) {
  int attrs = 0;
  struct handle_list *hlist;

  hlist = get_hlist(domain);
  if (!hlist)
    return attrs;
  attrs = fastrpc_get_property_int(FASTRPC_PROCESS_ATTRS, 0);
  attrs |= fastrpc_get_property_int(FASTRPC_PROCESS_ATTRS_PERSISTENT, 0);
  fastrpc_trace = fastrpc_get_property_int(FASTRPC_DEBUG_TRACE, 0);
  attrs |= hlist->qos.adaptive_qos ? FASTRPC_MODE_ADAPTIVE_QOS : 0;
  attrs |= hlist->unsigned_module ? FASTRPC_MODE_UNSIGNED_MODULE : 0;
  attrs |= (hlist->pd_dump | fastrpc_config_is_pddump_enabled())
               ? FASTRPC_MODE_ENABLE_PDDUMP
               : 0;
  attrs |= fastrpc_get_property_int(FASTRPC_DEBUG_PDDUMP, 0)
               ? FASTRPC_MODE_DEBUG_PDDUMP
               : 0;
  attrs |= (fastrpc_config_is_perfkernel_enabled() |
            fastrpc_get_property_int(FASTRPC_PERF_KERNEL, 0))
               ? FASTRPC_MODE_PERF_KERNEL
               : 0;
  attrs |= (fastrpc_config_is_perfdsp_enabled() |
            fastrpc_get_property_int(FASTRPC_PERF_ADSP, 0))
               ? FASTRPC_MODE_PERF_DSP
               : 0;
  attrs |= fastrpc_config_is_log_iregion_enabled()
               ? FASTRPC_MODE_ENABLE_IREGION_LOG
               : 0;
  attrs |= fastrpc_config_is_qtf_tracing_enabled()
               ? FASTRPC_MODE_ENABLE_QTF_TRACING
               : 0;
  attrs |= (fastrpc_config_get_caller_level() << 13) &
           FASTRPC_MODE_CALLER_LEVEL_MASK;
  attrs |= fastrpc_config_is_uaf_enabled() ? FASTRPC_MODE_ENABLE_UAF : 0;
  attrs |= fastrpc_config_is_debug_logging_enabled()
               ? FASTRPC_MODE_ENABLE_DEBUG_LOGGING
               : 0;
#ifdef SYSTEM_RPC_LIBRARY
  attrs |= FASTRPC_MODE_SYSTEM_PROCESS;
#endif
  attrs |= fastrpc_config_is_sysmon_reserved_bit_enabled()
               ? FASTRPC_MODE_SYSMON_RESERVED_BIT
               : 0;
  attrs |= fastrpc_config_is_logpacket_enabled() ? FASTRPC_MODE_LOG_PACKET : 0;
  attrs |= (fastrpc_config_get_leak_detect() << 19) &
           FASTRPC_MODE_ENABLE_LEAK_DETECT;
  attrs |= (fastrpc_config_get_caller_stack_num() << 21) &
           FASTRPC_MODE_CALLER_STACK_NUM;
  return attrs;
}

static void get_process_testsig(apps_std_FILE *fp, uint64 *ptrlen) {
  int nErr = 0;
  uint64 len = 0;
  char testsig[PROPERTY_VALUE_MAX];

  if (fp == NULL || ptrlen == NULL)
    return;

  if (fastrpc_get_property_string(FASTRPC_DEBUG_TESTSIG, testsig, NULL)) {
    FARF(RUNTIME_RPC_HIGH, "testsig file loading is %s", testsig);
    nErr = apps_std_fopen_with_env(ADSP_LIBRARY_PATH, ";", testsig, "r", fp);
    if (nErr == AEE_SUCCESS && *fp != -1)
      nErr = apps_std_flen(*fp, &len);
  }
  if (nErr)
    len = 0;
  *ptrlen = len;
  return;
}

static int open_shell(int domain_id, apps_std_FILE *fh, int unsigned_shell) {
  char *absName = NULL;
  char *shell_absName = NULL;
  char *domain_str = NULL;
  uint16 shell_absNameLen = 0, absNameLen = 0;
  ;
  int nErr = AEE_SUCCESS;
  int domain = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain_id);
  const char *shell_name = SIGNED_SHELL;

  if (1 == unsigned_shell) {
    shell_name = UNSIGNED_SHELL;
  }

  if (domain == MDSP_DOMAIN_ID) {
    return nErr;
  }
  VERIFYC(NULL != (domain_str = (char *)malloc(sizeof(domain))), AEE_ENOMEMORY);
  snprintf(domain_str, sizeof(domain), "%d", domain);

  shell_absNameLen = std_strlen(shell_name) + std_strlen(domain_str) + 1;

  VERIFYC(NULL !=
              (shell_absName = (char *)malloc(sizeof(char) * shell_absNameLen)),
          AEE_ENOMEMORY);
  std_strlcpy(shell_absName, shell_name, shell_absNameLen);

  std_strlcat(shell_absName, domain_str, shell_absNameLen);

  absNameLen = std_strlen(DSP_MOUNT_LOCATION) + shell_absNameLen + 1;
  VERIFYC(NULL != (absName = (char *)malloc(sizeof(char) * absNameLen)),
          AEE_ENOMEMORY);
  std_strlcpy(absName, DSP_MOUNT_LOCATION, absNameLen);
  std_strlcat(absName, shell_absName, absNameLen);

  nErr = apps_std_fopen(absName, "r", fh);
  if (nErr) {
    absNameLen = std_strlen(DSP_DOM_LOCATION) + shell_absNameLen + 1;
    VERIFYC(NULL !=
                (absName = (char *)realloc(absName, sizeof(char) * absNameLen)),
            AEE_ENOMEMORY);
    std_strlcpy(absName, DSP_MOUNT_LOCATION, absNameLen);
    std_strlcat(absName, SUBSYSTEM_NAME[domain], absNameLen);
    std_strlcat(absName, "/", absNameLen);
    std_strlcat(absName, shell_absName, absNameLen);
    nErr = apps_std_fopen(absName, "r", fh);
  }
  if (nErr) {
    absNameLen = std_strlen(VENDOR_DSP_LOCATION) + shell_absNameLen + 1;
    VERIFYC(NULL !=
                (absName = (char *)realloc(absName, sizeof(char) * absNameLen)),
            AEE_ENOMEMORY);
    std_strlcpy(absName, VENDOR_DSP_LOCATION, absNameLen);
    std_strlcat(absName, shell_absName, absNameLen);

    nErr = apps_std_fopen(absName, "r", fh);
    if (nErr) {
      absNameLen = std_strlen(VENDOR_DOM_LOCATION) + shell_absNameLen + 1;
      VERIFYC(NULL != (absName =
                           (char *)realloc(absName, sizeof(char) * absNameLen)),
              AEE_ENOMEMORY);
      std_strlcpy(absName, VENDOR_DSP_LOCATION, absNameLen);
      std_strlcat(absName, SUBSYSTEM_NAME[domain], absNameLen);
      std_strlcat(absName, "/", absNameLen);
      std_strlcat(absName, shell_absName, absNameLen);

      nErr = apps_std_fopen(absName, "r", fh);
    }
  }
  if (!nErr)
    FARF(ALWAYS, "Successfully opened %s, domain %d", absName, domain);
bail:
  if (domain_str) {
    free(domain_str);
    domain_str = NULL;
  }
  if (shell_absName) {
    free(shell_absName);
    shell_absName = NULL;
  }
  if (absName) {
    free(absName);
    absName = NULL;
  }
  if (nErr != AEE_SUCCESS) {
    if (domain == SDSP_DOMAIN_ID && fh != NULL) {
      nErr = AEE_SUCCESS;
      *fh = -1;
    } else {
      FARF(ERROR,
           "Error 0x%x: %s failed for domain %d search paths used are %s, %s, "
           "%s (errno %s)\n",
           nErr, __func__, domain, DSP_MOUNT_LOCATION, VENDOR_DSP_LOCATION,
           VENDOR_DOM_LOCATION, strerror(errno));
    }
  }
  return nErr;
}

/*
 * Request kernel to do optimizations to reduce RPC overhead.
 *
 * Provide the max user concurrency level to kernel which will
 * enable appropriate optimizations based on that info.
 * Args
 *	@domain	: Remote subsystem ID
 *
 * Return	: 0 on success
 */
static int fastrpc_enable_kernel_optimizations(int domain) {
  int nErr = AEE_SUCCESS, dev = -1,
      dom = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain);
  const uint32_t max_concurrency = 25;
  struct handle_list *hlist;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  dev = hlist->dev;
  if (((dom != CDSP_DOMAIN_ID) && (dom != CDSP1_DOMAIN_ID)) ||
      (hlist->dsppd != USERPD))
    goto bail;
  errno = 0;

  nErr = ioctl_optimization(dev, max_concurrency);
  // TODO:Bharath
  if ((nErr == -1 || nErr == (DSP_AEE_EOFFSET + AEE_ERPC)) &&
      (errno == ENOTTY || errno == EINVAL || errno == EBADRQC)) {
    /*
     * Kernel optimizations not supported. Ignore IOCTL failure
     * TODO: kernel cleanup to return ENOTTY for all unsupported IOCTLs
     */
    nErr = 0;
  }
bail:
  if (nErr) {
    FARF(ERROR, "Error 0x%x: %s failed for domain %d (%s)\n", nErr, __func__,
         domain, strerror(errno));
  }
  /*
   * Since this is a performance optimization, print error but ignore
   * failure until the feature is stable enough.
   */
  return 0;
}

void print_process_attrs(int domain) {
  bool dbgMode = false, crc = false, signedMd = false, unsignedMd = false,
       qos = false, configPDdump = false;
  bool debugPDdump = false, KernelPerf = false, DSPperf = false,
       iregion = false, qtf = false, uaf = false;
  int one_mb = 1024 * 1024;
  int pd_initmem_size = 0;
  bool logpkt = false;
  struct handle_list *hlist;

  hlist = get_hlist(domain);
  if (!hlist)
    return;
  if (IS_DEBUG_MODE_ENABLED(hlist->procattrs))
    dbgMode = true;
  if (IS_CRC_CHECK_ENABLED(hlist->procattrs))
    crc = true;
  if (hlist->unsigned_module) {
    unsignedMd = true;
    pd_initmem_size = 5 * one_mb;
  } else {
    signedMd = true;
    pd_initmem_size = hlist->pd_initmem_size;
  }
  if (hlist->qos.adaptive_qos)
    qos = true;
  if (hlist->pd_dump | fastrpc_config_is_pddump_enabled())
    configPDdump = true;
  if (fastrpc_get_property_int(FASTRPC_DEBUG_PDDUMP, 0))
    debugPDdump = true;
  if (hlist->procattrs & FASTRPC_MODE_PERF_KERNEL)
    KernelPerf = true;
  if (hlist->procattrs & FASTRPC_MODE_PERF_DSP)
    DSPperf = true;
  if (fastrpc_config_is_log_iregion_enabled())
    iregion = true;
  if (fastrpc_config_is_qtf_tracing_enabled())
    qtf = true;
  if (fastrpc_config_is_uaf_enabled())
    uaf = true;
  if (fastrpc_config_is_logpacket_enabled())
    logpkt = true;
  FARF(ALWAYS,
       "Created user PD on domain %d, dbg_trace 0x%x, enabled attr=> RPC "
       "timeout:%d, Dbg Mode:%s, CRC:%s, Unsigned:%s, Signed:%s, Adapt QOS:%s, "
       "PD dump: (Config:%s, Dbg:%s), Perf: (Kernel:%s, DSP:%s), Iregion:%s, "
       "QTF:%s, UAF:%s userPD initmem len:0x%x, Log pkt: %s",
       domain, fastrpc_trace, fastrpc_config_get_rpctimeout(),
       (dbgMode ? "Y" : "N"), (crc ? "Y" : "N"), (unsignedMd ? "Y" : "N"),
       (signedMd ? "Y" : "N"), (qos ? "Y" : "N"), (configPDdump ? "Y" : "N"),
       (debugPDdump ? "Y" : "N"), (KernelPerf ? "Y" : "N"),
       (DSPperf ? "Y" : "N"), (iregion ? "Y" : "N"), (qtf ? "Y" : "N"),
       (uaf ? "Y" : "N"), pd_initmem_size, (logpkt ? "Y" : "N"));
  return;
}

static int remote_init(int domain) {
  int nErr = AEE_SUCCESS, ioErr = 0;
  int dev = -1;
  struct fastrpc_proc_sess_info sess_info = {0};
  apps_std_FILE fh = -1;
  int pd_type = 0, errno_save = 0;
  uint32_t info = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain);
  int one_mb = 1024 * 1024, shared_buf_support = 0;
  char *file = NULL;
  int flags = 0, filelen = 0, memlen = 0, filefd = -1;
  struct handle_list *hlist;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  FARF(RUNTIME_RPC_HIGH, "starting %s for domain %d", __func__, domain);
  /*
   * is_proc_sharedbuf_supported_dsp call should be made before
   * mutex lock (hlist->mut), Since fastrpc_get_cap is also locked
   * by the same mutex
   */
  shared_buf_support = is_proc_sharedbuf_supported_dsp(domain);
  pthread_setspecific(tlsKey, (void *)hlist);
  pd_type = hlist->dsppd;
  VERIFYC(pd_type > DEFAULT_UNUSED && pd_type < MAX_PD_TYPE, AEE_EBADITEM);
  if (hlist->dev == -1) {
    dev = open_device_node(domain);
    VERIFYC(dev >= 0, AEE_ECONNREFUSED);
    // Set session relation info using FASTRPC_INVOKE2_SESS_INFO
    sess_info.domain_id = info;
    sess_info.pd_type = pd_type;
    sess_info.session_id = GET_SESSION_ID_FROM_DOMAIN_ID(domain);

    nErr = ioctl_session_info(dev, &sess_info);
    if (nErr == AEE_SUCCESS) {
      info = sess_info.domain_id;
      // Skip setting session related info in multiple ioctl calls, if
      // FASTRPC_CONTROL_SESS_INFO is supported
      goto set_sess_info_supported;
    } else {
      // Fallback to previous approach, if FASTRPC_CONTROL_SESS_INFO is not
      // supported
      FARF(RUNTIME_RPC_HIGH,
           "%s: FASTRPC_CONTROL_SESS_INFO not supported with error %d",
           __func__, nErr);
    }

    if (pd_type == SENSORS_STATICPD || pd_type == GUEST_OS_SHARED) {
      struct fastrpc_ctrl_smmu smmu = {0};
      smmu.sharedcb = 1;
      if (ioctl_control(dev, DSPRPC_SMMU_SUPPORT, &smmu)) {
        FARF(RUNTIME_RPC_HIGH, "%s: DSPRPC_SMMU_SUPPORT not supported",
             __func__);
      }
    }
    nErr = ioctl_getinfo(dev, &info);
  set_sess_info_supported:
    hlist->info = -1;
    if (nErr == AEE_SUCCESS) {
      hlist->info = info;
    } else if (errno == EACCES) {
      FARF(ERROR,
           "Error %d: %s: app does not have access to fastrpc device of domain "
           "%d (%s)",
           nErr, __func__, domain, strerror(errno));
      goto bail;
    } else if (errno == ECONNREFUSED || (errno == ENODEV)) {
      nErr = AEE_ECONNREFUSED;
      FARF(ERROR, "Error %d: %s: fastRPC device driver is disabled (%s)", nErr,
           __func__, strerror(errno));
      goto bail;
    } else if (nErr) {
      FARF(ERROR, "Error 0x%x: %s: failed to setup fastrpc session in kernel",
           nErr, __func__);
      goto bail;
    }

    // Set session id
    if (IS_EXTENDED_DOMAIN_ID(domain))
      VERIFY(AEE_SUCCESS == (nErr = ioctl_setmode(dev, FASTRPC_SESSION_ID1)));

    FARF(RUNTIME_RPC_HIGH, "%s: device %d opened with info 0x%x (attach %d)",
         __func__, dev, hlist->info, pd_type);
    // keep the memory we used to allocate
    if (pd_type == ROOT_PD || pd_type == GUEST_OS_SHARED ||
        pd_type == SECURE_STATICPD) {
      FARF(RUNTIME_RPC_HIGH,
           "%s: attaching to guest OS/Secure PD (attach %d) for domain %d",
           __func__, pd_type, domain);
      if (pd_type == SECURE_STATICPD) {
        file = calloc(1, (int)(std_strlen(hlist->dsppdname) + 1));
        VERIFYC(file, AEE_ENOMEMORY);
        std_strlcpy((char *)file, hlist->dsppdname,
                    std_strlen(hlist->dsppdname) + 1);
        filelen = std_strlen(hlist->dsppdname) + 1;
      }
      flags = FASTRPC_INIT_ATTACH;
      ioErr = ioctl_init(dev, flags, 0, (byte *)file, filelen, -1, 0, 0, 0, 0);
      if (file) {
        free(file);
        file = NULL;
      }
      VERIFYC((!ioErr || errno == ENOTTY || errno == ENXIO || errno == EINVAL),
              AEE_ERPC);
    } else if (pd_type == AUDIO_STATICPD || pd_type == OIS_STATICPD) {
      FARF(RUNTIME_RPC_HIGH, "%s: creating static user PD for domain %d",
           __func__, domain);
      file = rpcmem_alloc_internal(0, RPCMEM_HEAP_DEFAULT,
                                   (int)(std_strlen(hlist->dsppdname) + 1));
      VERIFYC(file, AEE_ENORPCMEMORY);
      std_strlcpy((char *)file, hlist->dsppdname,
                  std_strlen(hlist->dsppdname) + 1);
      filelen = std_strlen(hlist->dsppdname) + 1;
      flags = FASTRPC_INIT_CREATE_STATIC;
      // 3MB of remote heap for dynamic loading is available only for Audio PD.
      if (pd_type == AUDIO_STATICPD) {
        memlen = 3 * 1024 * 1024;
      }
      ioErr =
          ioctl_init(dev, flags, 0, (byte *)file, filelen, -1, 0, memlen, 0, 0);
      if (ioErr) {
        nErr = convert_kernel_to_user_error(ioErr, errno);
        goto bail;
      }
    } else if (pd_type == SENSORS_STATICPD) {
      FARF(RUNTIME_RPC_HIGH, "%s: attaching to sensors PD for domain %d",
           __func__, domain);
      flags = FASTRPC_INIT_ATTACH_SENSORS;
      ioErr = ioctl_init(dev, flags, 0, (byte *)0, 0, -1, 0, 0, 0, 0);
      VERIFYC((!ioErr || errno == ENOTTY || errno == ENXIO || errno == EINVAL),
              AEE_ERPC);
    } else if (pd_type == USERPD) {
      uint64 len = 0;
      int readlen = 0, eof;
      apps_std_FILE fsig = -1;
      uint64 siglen = 0;

#ifndef VIRTUAL_FASTRPC
#if !defined(SYSTEM_RPC_LIBRARY)
      open_shell(domain, &fh, hlist->unsigned_module);
#endif
#endif

      hlist->procattrs = get_process_attrs(domain);
      if (IS_DEBUG_MODE_ENABLED(hlist->procattrs))
        get_process_testsig(&fsig, &siglen);

      flags = FASTRPC_INIT_CREATE;
      if (fh != -1) {
        VERIFY(AEE_SUCCESS == (nErr = apps_std_flen(fh, &len)));
        filelen = len + siglen;
        VERIFYC(filelen && filelen < INT_MAX, AEE_EFILE);
        file = rpcmem_alloc_internal(0, RPCMEM_HEAP_DEFAULT, (size_t)filelen);
        VERIFYC(file, AEE_ENORPCMEMORY);
        VERIFY(AEE_SUCCESS ==
               (nErr = apps_std_fread(fh, (byte *)file, len, &readlen, &eof)));
        VERIFYC((int)len == readlen, AEE_EFILE);
        filefd = rpcmem_to_fd_internal((void *)file);
        filelen = (int)len;
        VERIFYC(filefd != -1, AEE_ERPC);
      } else {
        siglen = 0;
        fsig = -1;
      }

      if (!(FASTRPC_MODE_UNSIGNED_MODULE & hlist->procattrs)) {
        memlen = hlist->pd_initmem_size;
      } else {
        if (hlist->pd_initmem_size != DEFAULT_PD_INITMEM_SIZE)
          FARF(ERROR, "Setting user PD initial memory length is not supported "
                      "for unsigned PD, using default size\n");
      }
      errno = 0;

      if (shared_buf_support) {
        fastrpc_process_pack_params(dev, domain);
      }
      if (hlist->procattrs) {
        if (siglen && fsig != -1) {
          VERIFY(AEE_SUCCESS ==
                 (nErr = apps_std_fread(fsig, (byte *)(file + len), siglen,
                                        &readlen, &eof)));
          VERIFYC(siglen == (uint64)readlen, AEE_EFILE);
          filelen = len + siglen;
        }
      }
      ioErr = ioctl_init(dev, flags, hlist->procattrs, (byte *)file, filelen,
                         filefd, NULL, memlen, -1, siglen);
      if (ioErr) {
        nErr = ioErr;
        if (errno == ECONNREFUSED) {
          nErr = AEE_ECONNREFUSED;
          FARF(ERROR,
               "Error 0x%x: %s: untrusted app trying to offload to signed "
               "remote process (errno %d, %s). Try offloading to unsignedPD "
               "using remote_session_control",
               nErr, __func__, errno, strerror(errno));
        }
        goto bail;
      }
      print_process_attrs(domain);
    } else {
      FARF(ERROR, "Error: %s called for unknown mode %d", __func__, pd_type);
    }
    hlist->dev = dev;
    dev = -1;
    hlist->disable_exit_logs = 0;
  }
bail:
  // errno is being set to 0 in apps_std_fclose and we need original errno to
  // return proper error to user call
  errno_save = errno;
  if (file) {
    rpcmem_free_internal(file);
    file = NULL;
  }
  if (dev >= 0) {
    close_device_node(domain, dev);
  }
  if (fh != -1) {
    apps_std_fclose(fh);
  }
  if (nErr != AEE_SUCCESS) {
    errno = errno_save;
    if ((nErr == -1) && (errno == ECONNRESET)) {
      nErr = AEE_ECONNRESET;
    }
    FARF(ERROR, "Error 0x%x: %s failed for domain %d, errno %s, ioErr %d\n",
         nErr, __func__, domain, strerror(errno), ioErr);
  }
  FARF(RUNTIME_RPC_HIGH, "Done with %s, err: 0x%x, dev: %d", __func__, nErr,
       hlist->dev);
  return nErr;
}

__attribute__((destructor)) static void close_dev(void) {
  int i;

  FARF(ALWAYS, "%s: unloading library %s", __func__,
       fastrpc_library[DEFAULT_DOMAIN_ID]);
  FOR_EACH_EFFECTIVE_DOMAIN_ID(i) { domain_deinit(i); }
  deinit_fastrpc_dsp_lib_refcnt();
  pl_deinit();
  PL_DEINIT(fastrpc_apps_user);
}

remote_handle64 get_adsp_current_process1_handle(int domain) {
  remote_handle64 local;
  int nErr = AEE_SUCCESS;
  struct handle_list *hlist;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  if (hlist->cphandle) {
    return hlist->cphandle;
  }
  VERIFY(AEE_SUCCESS ==
         (nErr = fastrpc_update_module_list(
              DOMAIN_LIST_PREPEND, domain, _const_adsp_current_process1_handle,
              &local, NULL, FASTRPC_RESERVED_HANDLE_PRIO)));
  hlist->cphandle = local;
  return hlist->cphandle;
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR,
         "Error 0x%x: adsp current process handle failed. domain %d (errno "
         "%s)\n",
         nErr, domain, strerror(errno));
  }
  return INVALID_HANDLE;
}

remote_handle64 get_adspmsgd_adsp1_handle(int domain) {
  remote_handle64 local;
  int nErr = AEE_SUCCESS;
  struct handle_list *hlist;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  if (hlist->msghandle) {
    return hlist->msghandle;
  }
  VERIFY(AEE_SUCCESS ==
         (nErr = fastrpc_update_module_list(
              DOMAIN_LIST_PREPEND, domain, _const_adspmsgd_adsp1_handle, &local,
              NULL, FASTRPC_RESERVED_HANDLE_PRIO)));
  hlist->msghandle = local;
  return hlist->msghandle;
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR,
         "Error 0x%x: get adsp msgd handle failed. domain %d (errno %s)\n",
         nErr, domain, strerror(errno));
  }
  return INVALID_HANDLE;
}

remote_handle64 get_adsp_listener1_handle(int domain) {
  remote_handle64 local;
  int nErr = AEE_SUCCESS;
  struct handle_list *hlist;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  if (hlist->listenerhandle) {
    return hlist->listenerhandle;
  }
  VERIFY(AEE_SUCCESS ==
         (nErr = fastrpc_update_module_list(
              DOMAIN_LIST_PREPEND, domain, _const_adsp_listener1_handle, &local,
              NULL, FASTRPC_RESERVED_HANDLE_PRIO)));
  hlist->listenerhandle = local;
  return hlist->listenerhandle;
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed for domain %d (errno %s)\n", nErr,
         __func__, domain, strerror(errno));
  }
  return INVALID_HANDLE;
}

remote_handle64 get_remotectl1_handle(int domain) {
  remote_handle64 local;
  int nErr = AEE_SUCCESS;
  struct handle_list *hlist;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  // If remotectlhandle is 0 allocate handle, else return handle even though
  // INVALID_HANDLE handle
  if (hlist->remotectlhandle) {
    return hlist->remotectlhandle;
  }
  VERIFY(AEE_SUCCESS ==
         (nErr = fastrpc_update_module_list(
              DOMAIN_LIST_PREPEND, domain, _const_remotectl1_handle, &local,
              NULL, FASTRPC_RESERVED_HANDLE_PRIO)));
  hlist->remotectlhandle = local;
  return hlist->remotectlhandle;
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: remotectl1 handle failed. domain %d (errno %s)\n",
         nErr, domain, strerror(errno));
  }
  return INVALID_HANDLE;
}

remote_handle64 get_adsp_perf1_handle(int domain) {
  remote_handle64 local;
  int nErr = AEE_SUCCESS;
  struct handle_list *hlist;

  VERIFYC(NULL != (hlist = get_hlist(domain)), AEE_EBADDOMAIN);
  if (hlist->adspperfhandle) {
    return hlist->adspperfhandle;
  }
  VERIFY(AEE_SUCCESS ==
         (nErr = fastrpc_update_module_list(
              DOMAIN_LIST_PREPEND, domain, _const_adsp_perf1_handle, &local,
              NULL, FASTRPC_RESERVED_HANDLE_PRIO)));
  hlist->adspperfhandle = local;
  return hlist->adspperfhandle;
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: adsp_perf1 handle failed. domain %d (errno %s)\n",
         nErr, domain, strerror(errno));
  }
  return INVALID_HANDLE;
}

static int set_domain_defaults(struct handle_list *hlist) {
  pthread_mutexattr_t attr;

  hlist->dev = -1;
  hlist->th_params.thread_priority = DEFAULT_UTHREAD_PRIORITY;
  hlist->jobid = 1;
  hlist->info = -1;
  hlist->th_params.stack_size = DEFAULT_UTHREAD_STACK_SIZE;
  sem_init(&hlist->th_params.r_sem, 0,
           0); // Initialize semaphore count to 0
  hlist->dsppd = USERPD;
  hlist->trace_marker_fd = -1;
  hlist->state = FASTRPC_DOMAIN_STATE_CLEAN;
  hlist->pd_initmem_size = DEFAULT_PD_INITMEM_SIZE;
  QList_Ctor(&hlist->ql);
  QList_Ctor(&hlist->nql);
  QList_Ctor(&hlist->rql);
  memset(hlist->dsppdname, 0, MAX_DSPPD_NAMELEN);
  memset(hlist->sessionname, 0, MAX_DSPPD_NAMELEN);
  pthread_mutex_init(&hlist->mut, &attr);
  pthread_mutex_init(&hlist->lmut, 0);
  pthread_mutex_init(&hlist->init, 0);
  pthread_mutex_init(&hlist->async_init_deinit_mut, 0);
  hlist->is_session_reserved = true;
}

static int domain_init(int domain, int *dev) {
  int nErr = AEE_SUCCESS, dom = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain);
  remote_handle64 panic_handle = 0;
  struct err_codes *err_codes_to_send = NULL;
  struct handle_list *hlist;
  struct fastrpc_apps_info *me = NULL;

  GET_HASH_NODE(struct fastrpc_apps_info, domain, me);
  hlist = &me->hlist;
  if (!hlist) {
    ALLOC_AND_ADD_NEW_NODE_TO_TABLE(struct fastrpc_apps_info, domain, me);
    set_domain_defaults(hlist);
  } else {
    pthread_mutex_lock(&hlist->mut);
    if (hlist->state != FASTRPC_DOMAIN_STATE_CLEAN) {
      *dev = hlist->dev;
      pthread_mutex_unlock(&hlist->mut);
      return AEE_SUCCESS;
    }
  }
  VERIFY(AEE_SUCCESS == (nErr = remote_init(domain)));
  if (fastrpc_wake_lock_enable[domain]) {
    VERIFY(AEE_SUCCESS ==
           (nErr = update_kernel_wakelock_status(
                domain, hlist->dev, fastrpc_wake_lock_enable[domain])));
  }
  VERIFY(AEE_SUCCESS == (nErr = fastrpc_mem_open(domain)));
  VERIFY(AEE_SUCCESS == (nErr = apps_mem_init(domain)));

  if (dom == CDSP_DOMAIN_ID || dom == CDSP1_DOMAIN_ID) {
    panic_handle = get_adsp_current_process1_handle(domain);
    if (panic_handle != INVALID_HANDLE) {
      int ret = -1;
      /* If error codes are available in debug config, send panic error codes to
       * dsp to crash. */
      err_codes_to_send = fastrpc_config_get_errcodes();
      if (err_codes_to_send) {
        ret = adsp_current_process1_panic_err_codes(
            panic_handle, err_codes_to_send->err_code,
            err_codes_to_send->num_err_codes);
        if (AEE_SUCCESS == ret) {
          FARF(ALWAYS, "%s : panic error codes sent successfully\n", __func__);
        } else {
          FARF(ERROR, "Error 0x%x: %s : panic error codes send failed\n", ret,
               __func__);
        }
      }
    } else {
      FARF(ALWAYS, "%s : current process handle is not valid\n", __func__);
    }
  }
  VERIFY(AEE_SUCCESS == (nErr = fastrpc_enable_kernel_optimizations(domain)));
  initFileWatcher(domain); // Ignore errors
  trace_marker_init(domain);

  // If client notifications are registered, initialize notification thread and
  // enable notifications on domains
  if (fastrpc_notif_flag) {
    int ret = 0;
    ret = enable_process_state_notif_on_dsp(domain);
    if (ret == (int)(DSP_AEE_EOFFSET + AEE_EUNSUPPORTED)) {
      VERIFY_WPRINTF("Warning: %s: DSP does not support notifications",
                     __func__);
    }
    VERIFYC(ret == AEE_SUCCESS ||
                ret == (int)(DSP_AEE_EOFFSET + AEE_EUNSUPPORTED),
            ret);
  }
#ifdef PD_EXCEPTION_LOGGING
  if ((dom != SDSP_DOMAIN_ID) && hlist->dsppd == ROOT_PD) {
    remote_handle64 handle = 0;
    handle = get_adspmsgd_adsp1_handle(domain);
    if (handle != INVALID_HANDLE) {
      adspmsgd_init(handle, 0x10); // enable PD exception logging
    }
  }
#endif
  fastrpc_perf_init(hlist->dev, domain);
  VERIFY(AEE_SUCCESS == (nErr = fastrpc_latency_init(hlist->dev, &hlist->qos)));
  get_dsp_dma_reverse_rpc_map_capability(domain);
  hlist->state = FASTRPC_DOMAIN_STATE_INIT;
  hlist->ref = 0;
  pthread_mutex_unlock(&hlist->mut);
  VERIFY(AEE_SUCCESS == (nErr = listener_android_domain_init(
                             domain, hlist->th_params.update_requested,
                             &hlist->th_params.r_sem)));
bail:
  if (nErr != AEE_SUCCESS) {
    domain_deinit(domain);
    if (hlist) {
      FARF(ERROR, "Error 0x%x: %s (%d) failed for domain %d (errno %s)\n", nErr,
           __func__, hlist->dev, domain, strerror(errno));
    }
    *dev = -1;
    return nErr;
  }
  if (hlist) {
    FARF(RUNTIME_RPC_LOW, "Done %s with dev %d, err %d", __func__, hlist->dev,
         nErr);
    *dev = hlist->dev;
    return nErr;
  } else {
    *dev = -1;
    FARF(ERROR,
         "Error 0x%x: Unable to get dev as hlist is NULL for domain %d\n", nErr,
         __func__, domain);
    return nErr;
  }
}

static void fastrpc_apps_user_deinit(void) {
  int i;
  struct handle_list *hlist;

  FARF(RUNTIME_RPC_HIGH, "%s called\n", __func__);
  if (tlsKey != INVALID_KEY) {
    pthread_key_delete(tlsKey);
    tlsKey = INVALID_KEY;
  }
  fastrpc_clear_handle_list(NON_DOMAIN_HANDLE_LIST_ID, DEFAULT_DOMAIN_ID);
  FOR_EACH_EFFECTIVE_DOMAIN_ID(i) {
    hlist = get_hlist(i);
    if (!hlist)
      continue;
    fastrpc_clear_handle_list(MULTI_DOMAIN_HANDLE_LIST_ID, i);
    fastrpc_clear_handle_list(REVERSE_HANDLE_LIST_ID, i);
    sem_destroy(&hlist->th_params.r_sem);
    pthread_mutex_destroy(&hlist->mut);
    pthread_mutex_destroy(&hlist->lmut);
    pthread_mutex_destroy(&hlist->init);
    pthread_mutex_destroy(&hlist->async_init_deinit_mut);
  }
  HASH_TABLE_CLEANUP(struct fastrpc_apps_info);
  listener_android_deinit();
#ifndef NO_HAL
  for (i = 0; i < NUM_SESSIONS; i++) {
    destroy_dsp_client_instance(dsp_client_instance[i]);
    dsp_client_instance[i] = NULL;
  }
  pthread_mutex_destroy(&dsp_client_mut);
#endif
  fastrpc_context_table_deinit();
  deinit_process_signals();
  fastrpc_notif_deinit();
  apps_mem_table_deinit();
  fastrpc_wake_lock_deinit();
  fastrpc_log_deinit();
  fastrpc_mem_deinit();
  PL_DEINIT(apps_std);
  PL_DEINIT(rpcmem);
  PL_DEINIT(gpls);

  FARF(ALWAYS, "%s done\n", __func__);
  return;
}

static void exit_thread(void *value) {
  remote_handle64 handle = 0;
  int domain;
  int nErr = AEE_SUCCESS;
  struct handle_list *hlist;

  FOR_EACH_EFFECTIVE_DOMAIN_ID(domain) {
    hlist = get_hlist(domain);
    if (!hlist) {
      FARF(CRITICAL, "%s: Invalid hlist", __func__);
      return;
    }
    if (hlist->dev != -1) {
      if ((handle = get_adsp_current_process1_handle(domain)) !=
          INVALID_HANDLE) {
        nErr = adsp_current_process1_thread_exit(handle);
        if (nErr) {
          FARF(RUNTIME_RPC_HIGH, "%s: nErr:0x%x, dom:%d, h:0x%llx", __func__,
               nErr, domain, handle);
        }
      } else if (domain == DEFAULT_DOMAIN_ID) {
        nErr = adsp_current_process_thread_exit();
        if (nErr) {
          FARF(RUNTIME_RPC_HIGH, "%s: nErr:0x%x, dom:%d", __func__, nErr,
               domain);
        }
      }
    }
  }
  // Set tlsKey to NULL, so that exit_thread won't be called recursively
  pthread_setspecific(tlsKey, (void *)NULL);
}

/*
 * Called only once by fastrpc_init_once
 * Initializes the data structures
 */

static int fastrpc_apps_user_init(void) {
  int nErr = AEE_SUCCESS, domain;
  pthread_mutexattr_t attr;
  struct fastrpc_apps_info *me;

  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

  /* Initializing the fastrpc_apps_info hash table */
  HASH_TABLE_INIT(struct fastrpc_apps_info);
  VERIFY(AEE_SUCCESS == (nErr = PL_INIT(gpls)));
  VERIFY(AEE_SUCCESS == (nErr = PL_INIT(rpcmem)));
  FOR_EACH_EFFECTIVE_DOMAIN_ID(domain) {
    ALLOC_AND_ADD_NEW_NODE_TO_TABLE(struct fastrpc_apps_info, domain, me);
    set_domain_defaults(&me->hlist);
  }
  fastrpc_mem_init();
  fastrpc_context_table_init();
  fastrpc_log_init();
  fastrpc_config_init();
  pthread_mutex_init(&update_notif_list_mut, 0);
#ifndef NO_HAL
  pthread_mutex_init(&dsp_client_mut, 0);
#endif
  listener_android_init();
  VERIFY(AEE_SUCCESS == (nErr = pthread_key_create(&tlsKey, exit_thread)));
  VERIFY(AEE_SUCCESS == (nErr = PL_INIT(apps_std)));
  GenCrc32Tab(POLY32, crc_table);
  fastrpc_notif_init();
  apps_mem_table_init();
bail:
  /*
                   print address of static variable
                   to know duplicate instance of libcdsprpc.so
  */
  if (nErr) {
    FARF(ERROR,
         "Error 0x%x: %s failed. default domain:%x and &fastrpc_trace:%p \n",
         nErr, __func__, DEFAULT_DOMAIN_ID, &fastrpc_trace);
    fastrpc_apps_user_deinit();
  } else {
    FARF(ALWAYS, "%s done. default domain:%x and &fastrpc_trace:%p\n", __func__,
         DEFAULT_DOMAIN_ID, &fastrpc_trace);
  }
  return nErr;
}

PL_DEFINE(fastrpc_apps_user, fastrpc_apps_user_init, fastrpc_apps_user_deinit);

static void frpc_init(void) { PL_INIT(fastrpc_apps_user); }

int fastrpc_init_once(void) {
  static pthread_once_t frpc = PTHREAD_ONCE_INIT;
  int nErr = AEE_SUCCESS;
  VERIFY(AEE_SUCCESS == (nErr = pthread_once(&frpc, (void *)frpc_init)));
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error %x: fastrpc init once failed\n", nErr);
  }
  return nErr == AEE_SUCCESS ? _pl_fastrpc_apps_user()->nErr : nErr;
}

static int rpcmem_init_me(void) {
  rpcmem_init_internal();
  return AEE_SUCCESS;
}

static void rpcmem_deinit_me(void) {
  rpcmem_deinit_internal();
  return;
}

/*
 * check_multilib_util() - Debug utility to terminate the process when multiple
 * instances of dsp library linked to the process by reading process env
 *                         variable values.
 * Return : void.
 */

static void check_multilib_util(void) {
  int nErr = 0, ret = -1, ii = 0;
  const char *env_name = fastrpc_dsp_lib_refcnt[DEFAULT_DOMAIN_ID];

  /* Set env variable of default domain id to 1. */
  ret = setenv(env_name, "1", 1);
  if (ret != 0 && errno == ENOMEM) {
    nErr = ERRNO;
    FARF(ERROR, "Error 0x%x: setenv failed for %s (domain %u), errno is %s\n",
         nErr, env_name, DEFAULT_DOMAIN_ID, strerror(ERRNO));
    /*
     * If failed to set env variable then free the memory allocated to
     * env variables and exit the application.
     */
    deinit_fastrpc_dsp_lib_refcnt();
    exit(EXIT_FAILURE);
  } else {
    total_dsp_lib_refcnt = 1;
  }

  /* Get the values of all env variables and increment the refcount accordingly.
   */
  FOR_EACH_DOMAIN_ID(ii) {
    char *env_val = NULL;
    env_name = fastrpc_dsp_lib_refcnt[ii];

    /* Skip the check to get default domain env variable value as we already set
     * its value. */
    if (ii == DEFAULT_DOMAIN_ID)
      continue;

    env_val = getenv(env_name);
    if (NULL != env_val) {
      /*
       * Add env value to total reference count to check
       * the total number of dsp library instances loaded.
       */
      total_dsp_lib_refcnt += atoi(env_val);

      /* If the total reference count exceeds one then show warning, application
       * will abort on first handle open. */
      if (total_dsp_lib_refcnt > MAX_LIB_INSTANCE_ALLOWED) {
        FARF(ERROR,
             "Warning: %s %d instances of libxdsprpc (already loaded %s). Only "
             "%d allowed\n",
             fastrpc_library[DEFAULT_DOMAIN_ID], total_dsp_lib_refcnt,
             fastrpc_library[ii], MAX_LIB_INSTANCE_ALLOWED);
      }
    }
  }
}

__CONSTRUCTOR_ATTRIBUTE__
static void multidsplib_env_init(void) {
  const char *local_fastrpc_lib_refcnt[NUM_DOMAINS] = {
      "FASTRPC_ADSP_REFCNT", "FASTRPC_MDSP_REFCNT", "FASTRPC_SDSP_REFCNT",
      "FASTRPC_CDSP_REFCNT", "FASTRPC_CDSP1_REFCNT"};
  char buf[64] = {0};
  size_t env_name_len = 0;
  char *env_name = NULL;
  int ii = 0;

  pid_t pid = getpid();

  /* Initialize all global array with env variable names along with process id.
   */
  FOR_EACH_DOMAIN_ID(ii) {
    snprintf(buf, sizeof(buf), "%s_%d", local_fastrpc_lib_refcnt[ii], pid);
    env_name_len = (sizeof(char) * strlen(buf)) + 1;
    env_name = malloc(env_name_len);
    if (env_name) {
      std_strlcpy(env_name, buf, env_name_len);
      fastrpc_dsp_lib_refcnt[ii] = env_name;
    } else {
      FARF(ERROR,
           "Error %d: %s: env variable allocation for %zu bytes failed, domain "
           "%d\n",
           ENOMEM, __func__, env_name_len, ii);
      deinit_fastrpc_dsp_lib_refcnt();
      exit(EXIT_FAILURE);
    }
  }
  check_multilib_util();
  FARF(ALWAYS, "%s: %s loaded", __func__, fastrpc_library[DEFAULT_DOMAIN_ID]);
}

PL_DEFINE(rpcmem, rpcmem_init_me, rpcmem_deinit_me);