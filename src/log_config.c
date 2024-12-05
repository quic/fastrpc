// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif // VERIFY_PRINT_ERROR

#ifndef VERIFY_PRINT_ERROR_ALWAYS
#define VERIFY_PRINT_ERROR_ALWAYS
#endif // VERIFY_PRINT_ERROR_ALWAYS

#ifndef VERIFY_PRINT_WARN
#define VERIFY_PRINT_WARN
#endif // VERIFY_PRINT_WARN

#define FARF_ERROR 1

#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "adsp_current_process.h"
#include "adsp_current_process1.h"
#include "adspmsgd_adsp.h"
#include "adspmsgd_adsp1.h"
#include "adspmsgd_internal.h"
#include "apps_std_internal.h"
#include "fastrpc_common.h"
#include "fastrpc_hash_table.h"
#include "fastrpc_internal.h"
#include "rpcmem.h"
#include "verify.h"
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>

#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))
#ifndef AEE_EUNSUPPORTED
#define AEE_EUNSUPPORTED 20 // API is not supported 	50
#endif
#define DEFAULT_ADSPMSGD_MEMORY_SIZE 8192
#define INVALID_HANDLE (remote_handle64)(-1)
#define ERRNO (errno == 0 ? -1 : errno)
#define ADSPMSGD_FILTER                                                        \
  0x1f001f // Filter passed to adspmsgd init API to push DSP messages to logcat

#define MAX_FARF_FILE_SIZE (511)

/*
 * Depending on the operating system, the default DSP_SEARCH_PATH gets fetched.
 * e.g:- _WIN32    :  DSP_SEARCH_PATH=";c:\\Program
 * Files\\Qualcomm\\RFSA\\aDSP;"; LE_ENABLE :
 * DSP_SEARCH_PATH=";/usr/lib/rfsa/adsp;/dsp;"; This is the maximum possible
 * length of the DSP_SEARCH_PATH.
 */
#define ENV_PATH_LEN 256

/*
 * macro to check if a node is present is present
 * in the log_config_watcher hash table
 */
#define VERIFY_LOG_CONFIG_NODE_PRESENT(domain) \
	do { \
		GET_HASH_NODE(struct log_config_watcher_params, domain, me); \
		if (!me) { \
			nErr = AEE_ENOSUCHINSTANCE; \
			FARF(ERROR, "Error 0x%x: %s: unable to find hash-node for domain %d", \
				nErr, __func__, domain); \
			goto bail; \
		} \
	} while(0);

struct log_config_watcher_params {
    int fd;
    int event_fd; // Duplicate fd to quit the poll
    _cstring1_t* paths;
    int* wd;
    uint32 numPaths;
    pthread_attr_t attr;
    pthread_t thread;
    unsigned char stopThread;
    int asidToWatch;
    char* fileToWatch;
    char* asidFileToWatch;
    char* pidFileToWatch;
    boolean adspmsgdEnabled;
    boolean file_watcher_init_flag;
    ADD_DOMAIN_HASH();
};

DECLARE_HASH_TABLE(log_config_watcher, struct log_config_watcher_params);

extern const char* __progname;
void set_runtime_logmask(uint32_t);

const char *get_domain_str(int domain);

/* Delete and free all nodes in hash-table */
void log_config_table_deinit(void) {
	HASH_TABLE_CLEANUP(struct log_config_watcher_params);
}

/* Initialize hash-table */
int log_config_table_init(void) {
	HASH_TABLE_INIT(struct log_config_watcher_params);
	return 0;
}

static int parseLogConfig(int dom, unsigned int mask, char *filenames) {
  struct log_config_watcher_params *me = NULL;
  _cstring1_t *filesToLog = NULL;
  int filesToLogLen = 0;
  char *tempFiles = NULL;
  int nErr = AEE_SUCCESS;
  char *saveptr = NULL;
  char *path = NULL;
  char delim[] = {','};
  int maxPathLen = 0;
  int i = 0;
  remote_handle64 handle;

  VERIFYC(filenames != NULL, AEE_ERPC);

  VERIFYC(NULL !=
              (tempFiles = malloc(sizeof(char) * (std_strlen(filenames) + 1))),
          AEE_ENOMEMORY);
  std_strlcpy(tempFiles, filenames, std_strlen(filenames) + 1);

  // Get the number of folders and max size needed
  path = strtok_r(tempFiles, delim, &saveptr);
  while (path != NULL) {
    maxPathLen = STD_MAX(maxPathLen, (int)std_strlen(path)) + 1;
    filesToLogLen++;
    path = strtok_r(NULL, delim, &saveptr);
  }
  VERIFY_LOG_CONFIG_NODE_PRESENT(dom);
  VERIFY_IPRINTF("%s: #files: %d max_len: %d\n",
                 me->fileToWatch, filesToLogLen, maxPathLen);

  // Allocate memory
  VERIFYC(NULL != (filesToLog = malloc(sizeof(_cstring1_t) * filesToLogLen)),
          AEE_ENOMEMORY);
  for (i = 0; i < filesToLogLen; ++i) {
    VERIFYC(NULL != (filesToLog[i].data = malloc(sizeof(char) * maxPathLen)),
            AEE_ENOMEMORY);
    filesToLog[i].dataLen = maxPathLen;
  }

  // Get the number of folders and max size needed
  std_strlcpy(tempFiles, filenames, std_strlen(filenames) + 1);
  i = 0;
  path = strtok_r(tempFiles, delim, &saveptr);
  while (path != NULL) {
    VERIFYC((filesToLog != NULL) && (filesToLog[i].data != NULL) &&
                filesToLog[i].dataLen >= (int)strlen(path),
            AEE_ERPC);
    std_strlcpy(filesToLog[i].data, path, filesToLog[i].dataLen);
    VERIFY_IPRINTF("%s: %s\n", me->fileToWatch, filesToLog[i].data);
    path = strtok_r(NULL, delim, &saveptr);
    i++;
  }

  handle = get_adsp_current_process1_handle(dom);
  if (handle != INVALID_HANDLE) {
    if (AEE_SUCCESS != (nErr = adsp_current_process1_set_logging_params2(
                            handle, mask, filesToLog, filesToLogLen))) {
      VERIFY(AEE_SUCCESS == (nErr = adsp_current_process1_set_logging_params(
                                 handle, mask, filesToLog, filesToLogLen)));
    }
  } else {
    if (AEE_SUCCESS != (nErr = adsp_current_process_set_logging_params2(
                            mask, filesToLog, filesToLogLen))) {
      VERIFY(AEE_SUCCESS == (nErr = adsp_current_process_set_logging_params(
                                 mask, filesToLog, filesToLogLen)));
    }
  }

bail:
  if (filesToLog) {
    for (i = 0; i < filesToLogLen; ++i) {
      if (filesToLog[i].data != NULL) {
        free(filesToLog[i].data);
        filesToLog[i].data = NULL;
      }
    }
    free(filesToLog);
    filesToLog = NULL;
  }

  if (tempFiles) {
    free(tempFiles);
    tempFiles = NULL;
  }
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: parse log config failed. domain %d, mask %x, "
                   "filename %s\n",
                   nErr, dom, mask, filenames);
  }
  return nErr;
}

// Read log config given the filename
static int readLogConfigFromPath(int dom, const char *base, const char *file) {
  struct log_config_watcher_params *me = NULL;
  int nErr = 0;
  apps_std_FILE fp = -1;
  uint64 len;
  byte *buf = NULL;
  int readlen = 0, eof;
  unsigned int mask = 0;
  char *path = NULL;
  char *filenames = NULL;
  boolean fileExists = FALSE;
  int buf_addr = 0;
  remote_handle64 handle;
  uint64_t farf_logmask = 0;

  len = std_snprintf(0, 0, "%s/%s", base, file) + 1;
  VERIFYC(NULL != (path = malloc(sizeof(char) * len)), AEE_ENOMEMORY);
  std_snprintf(path, (int)len, "%s/%s", base, file);
  VERIFY(AEE_SUCCESS == (nErr = apps_std_fileExists(path, &fileExists)));
  VERIFY_LOG_CONFIG_NODE_PRESENT(dom);
  if (fileExists == FALSE) {
    FARF(RUNTIME_RPC_HIGH, "%s: Couldn't find file: %s\n",
         me->fileToWatch, path);
    nErr = AEE_ENOSUCHFILE;
    goto bail;
  }
  if (me->adspmsgdEnabled == FALSE) {
    handle = get_adspmsgd_adsp1_handle(dom);
    if (handle != INVALID_HANDLE) {
      if ((nErr = adspmsgd_init(handle, ADSPMSGD_FILTER)) ==
          (int)(AEE_EUNSUPPORTED + DSP_AEE_EOFFSET))
        adspmsgd_adsp1_init2(handle);
    } else if ((nErr = adspmsgd_adsp_init2()) ==
               (int)(AEE_EUNSUPPORTED + DSP_AEE_EOFFSET)) {
      nErr = adspmsgd_adsp_init(0, RPCMEM_HEAP_DEFAULT, 0,
                                DEFAULT_ADSPMSGD_MEMORY_SIZE, &buf_addr);
    }
    if (nErr != AEE_SUCCESS) {
      VERIFY_EPRINTF("adspmsgd not supported. nErr=%x\n", nErr);
    } else {
      me->adspmsgdEnabled = TRUE;
    }
    VERIFY_EPRINTF("Found %s. adspmsgd enabled \n", me->fileToWatch);
  }

  VERIFY(AEE_SUCCESS == (nErr = apps_std_fopen(path, "r", &fp)));
  VERIFY(AEE_SUCCESS == (nErr = apps_std_flen(fp, &len)));

  VERIFYM(len <= MAX_FARF_FILE_SIZE, AEE_ERPC,
          "len greater than %d for path %s (%s)\n", nErr, MAX_FARF_FILE_SIZE,
          path, strerror(ERRNO));
  VERIFYC(NULL != (buf = calloc(1, sizeof(byte) * (len + 1))),
          AEE_ENOMEMORY); // extra 1 byte for null character
  VERIFYC(NULL != (filenames = malloc(sizeof(byte) * len)), AEE_ENOMEMORY);
  VERIFY(AEE_SUCCESS == (nErr = apps_std_fread(fp, buf, len, &readlen, &eof)));
  VERIFYC((int)len == readlen, AEE_ERPC);

  FARF(RUNTIME_RPC_HIGH, "%s: Config file %s contents: %s\n",
       me->fileToWatch, path, buf);

  // Parse farf file to get logmasks.
  len = sscanf((const char *)buf, "0x%lx %511s", &farf_logmask, filenames);

  if (farf_logmask == LLONG_MAX || farf_logmask == (uint64_t)LLONG_MIN ||
      farf_logmask == 0) {
    VERIFY_EPRINTF("Error : Invalid FARF logmask!");
  }
  /*
   * Parsing logmask to get userspace and kernel space masks.
   * Example: For farf_logmask = 0x001f001f001f001f, this enables all Runtime
   * levels
   *
   * i.e.: 0x    001f001f        001f001f
   *           |__________|    |__________|
   *             Userspace       DSP space
   */
  mask = farf_logmask & 0xffffffff;
  set_runtime_logmask(farf_logmask >> 32);
  switch (len) {
  case 1:
    FARF(RUNTIME_RPC_HIGH, "%s: Setting log mask:0x%x",
         me->fileToWatch, mask);
    handle = get_adsp_current_process1_handle(dom);
    if (handle != INVALID_HANDLE) {
      if (AEE_SUCCESS != (nErr = adsp_current_process1_set_logging_params2(
                              handle, mask, NULL, 0))) {
        VERIFY(AEE_SUCCESS == (nErr = adsp_current_process1_set_logging_params(
                                   handle, mask, NULL, 0)));
      }
    } else {
      if (AEE_SUCCESS !=
          (nErr = adsp_current_process_set_logging_params2(mask, NULL, 0))) {
        VERIFY(AEE_SUCCESS ==
               (nErr = adsp_current_process_set_logging_params(mask, NULL, 0)));
      }
    }
    break;
  case 2:
    VERIFY(AEE_SUCCESS == (nErr = parseLogConfig(dom, mask, filenames)));
    FARF(RUNTIME_RPC_HIGH, "%s: Setting log mask:0x%x, filename:%s",
         me->fileToWatch, mask, filenames);
    break;
  default:
    VERIFY_EPRINTF("Error : %s: No valid data found in config file %s",
                   me->fileToWatch, path);
    nErr = AEE_EUNSUPPORTED;
    goto bail;
  }

bail:
  if (buf != NULL) {
    free(buf);
    buf = NULL;
  }

  if (filenames != NULL) {
    free(filenames);
    filenames = NULL;
  }

  if (fp != -1) {
    apps_std_fclose(fp);
  }

  if (path != NULL) {
    free(path);
    path = NULL;
  }

  if (nErr != AEE_SUCCESS && nErr != AEE_ENOSUCHFILE) {
    VERIFY_EPRINTF("Error 0x%x: fopen failed for %s/%s. (%s)\n", nErr, base,
                   file, strerror(ERRNO));
  }
  return nErr;
}

// Read log config given the watch descriptor
static int readLogConfigFromEvent(int dom, struct inotify_event *event) {
  int i = 0, nErr = AEE_SUCCESS;
  struct log_config_watcher_params *me = NULL;

  VERIFY_LOG_CONFIG_NODE_PRESENT(dom);
  // Ensure we are looking at the right file
  for (i = 0; i < (int)me->numPaths; ++i) {
    if (me->wd[i] == event->wd) {
      if (std_strcmp(me->fileToWatch, event->name) == 0) {
        return readLogConfigFromPath(dom, me->paths[i].data,
                                     me->fileToWatch);
      } else if (std_strcmp(me->asidFileToWatch, event->name) == 0) {
        return readLogConfigFromPath(dom, me->paths[i].data,
                                     me->asidFileToWatch);
      } else if (std_strcmp(me->pidFileToWatch, event->name) == 0) {
        return readLogConfigFromPath(dom, me->paths[i].data,
                                     me->pidFileToWatch);
      }
    }
  }
  VERIFY_IPRINTF("%s: Watch descriptor %d not valid for current process",
                 me->fileToWatch, event->wd);
bail:
  return AEE_SUCCESS;
}

// Read log config given the watch descriptor
static int resetLogConfigFromEvent(int dom, struct inotify_event *event) {
  int i = 0, nErr = AEE_SUCCESS;
  remote_handle64 handle;
  struct log_config_watcher_params *me = NULL;

  VERIFY_LOG_CONFIG_NODE_PRESENT(dom);

  // Ensure we are looking at the right file
  for (i = 0; i < (int)me->numPaths; ++i) {
    if (me->wd[i] == event->wd) {
      if ((std_strcmp(me->fileToWatch, event->name) == 0) ||
          (std_strcmp(me->asidFileToWatch, event->name) == 0) ||
          (std_strcmp(me->pidFileToWatch, event->name) == 0)) {
        if (me->adspmsgdEnabled == TRUE) {
          adspmsgd_stop(dom);
          me->adspmsgdEnabled = FALSE;
          handle = get_adspmsgd_adsp1_handle(dom);
          if (handle != INVALID_HANDLE) {
            adspmsgd_adsp1_deinit(handle);
          } else {
            adspmsgd_adsp_deinit();
          }
        }
        handle = get_adsp_current_process1_handle(dom);
        if (handle != INVALID_HANDLE) {
          return adsp_current_process1_set_logging_params(handle, 0, NULL, 0);
        } else {
          return adsp_current_process_set_logging_params(0, NULL, 0);
        }
      }
    }
  }
  VERIFY_IPRINTF("%s: Watch descriptor %d not valid for current process",
                 me->fileToWatch, event->wd);
bail:
  return AEE_SUCCESS;
}

static void *file_watcher_thread(void *arg) {
  int dom = (int)(uintptr_t)arg;
  int ret = 0, current_errno = 0, env_list_len = 0;
  int length = 0;
  int nErr = AEE_SUCCESS;
  int i = 0;
  char buffer[EVENT_BUF_LEN];
  struct log_config_watcher_params *me = NULL;

  VERIFY_LOG_CONFIG_NODE_PRESENT(dom);
  struct pollfd pfd[] = {{me->fd, POLLIN, 0},
                         {me->event_fd, POLLIN, 0}};
  const char *fileExtension = ".farf";
  int len = 0;
  remote_handle64 handle;
  int file_found = 0;
  char *data_paths = NULL;

  FARF(ALWAYS, "%s starting for domain %d\n", __func__, dom);
  // Check for the presence of the <process_name>.farf file at bootup
  for (i = 0; i < (int)me->numPaths; ++i) {
    if (0 == readLogConfigFromPath(dom, me->paths[i].data,
                                   me->fileToWatch)) {
      file_found = 1;
      VERIFY_IPRINTF("%s: Log config File %s found.\n",
                     me->fileToWatch,
                     me->paths[i].data);
      break;
    }
  }
  if (!file_found) {
    // Allocate single buffer for all the paths.
    data_paths = calloc(1, sizeof(char) * ENV_PATH_LEN);
    if (data_paths) {
      current_errno = errno;
      // Get DSP_LIBRARY_PATH env variable path set by the user.
      ret = apps_std_getenv(DSP_LIBRARY_PATH, data_paths, ENV_PATH_LEN,
                            &env_list_len);
      errno = current_errno;
      // User has not set the env variable. Get default search paths.
      if (ret != 0)
        std_memmove(data_paths, DSP_SEARCH_PATH, std_strlen(DSP_SEARCH_PATH));
      VERIFY_WPRINTF("%s: Couldn't find file %s, errno (%s) at %s\n", __func__,
                     me->fileToWatch, strerror(errno),
                     data_paths);
    } else {
      VERIFY_WPRINTF(
          "%s: Calloc failed for %d bytes. Couldn't find file %s, errno (%s)\n",
          __func__, ENV_PATH_LEN, me->fileToWatch,
          strerror(errno));
    }
  }

  while (me->stopThread == 0) {
    // Block forever
    ret = poll(pfd, 2, -1);
    if (ret < 0) {
      VERIFY_EPRINTF("Error : %s: Error polling for file change. Runtime FARF "
                     "will not work for this process. errno=%x !",
                     me->fileToWatch, errno);
      break;
    } else if (pfd[1].revents & POLLIN) { // Check for exit
      VERIFY_WPRINTF("Warning: %s received exit for domain %d, file %s\n",
                     __func__, dom, me->fileToWatch);
      break;
    } else {
      length = read(me->fd, buffer, EVENT_BUF_LEN);
      i = 0;
      while (i < length) {
        struct inotify_event *event = (struct inotify_event *)&buffer[i];
        if (event->len) {
          // Get the asiD for the current process
          // Do it once only
          if (me->asidToWatch == -1) {
            handle = get_adsp_current_process1_handle(dom);
            if (handle != INVALID_HANDLE) {
              VERIFY(
                  AEE_SUCCESS ==
                  (nErr = adsp_current_process1_getASID(
                       handle, (unsigned int *)&me->asidToWatch)));
            } else {
              VERIFY(
                  AEE_SUCCESS ==
                  (nErr = adsp_current_process_getASID(
                       (unsigned int *)&me->asidToWatch)));
            }
            len = strlen(fileExtension) + strlen(__TOSTR__(INT_MAX));
            VERIFYC(NULL != (me->asidFileToWatch =
                                 malloc(sizeof(char) * len)),
                    AEE_ENOMEMORY);
            snprintf(me->asidFileToWatch, len, "%d%s",
                     me->asidToWatch, fileExtension);
            VERIFY_IPRINTF("%s: Watching ASID file %s\n",
                           me->fileToWatch,
                           me->asidFileToWatch);
          }

          VERIFY_IPRINTF("%s: %s %d.\n", me->fileToWatch,
                         event->name, event->mask);
          if ((event->mask & IN_CREATE) || (event->mask & IN_MODIFY)) {
            VERIFY_IPRINTF("%s: File %s created.\n",
                           me->fileToWatch, event->name);
            if (0 != readLogConfigFromEvent(dom, event)) {
              VERIFY_EPRINTF("Error : %s: Error reading config file %s",
                             me->fileToWatch, me->paths[i].data);
            }
          } else if (event->mask & IN_DELETE) {
            VERIFY_IPRINTF("%s: File %s deleted.\n",
                           me->fileToWatch, event->name);
            if (0 != resetLogConfigFromEvent(dom, event)) {
              VERIFY_EPRINTF(
                  "Error : %s: Error resetting FARF runtime log config",
                  me->fileToWatch);
            }
          }
        }

        i += EVENT_SIZE + event->len;
      }
    }
  }
bail:
  if (data_paths) {
    free(data_paths);
    data_paths = NULL;
  }

  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: %s exited. Runtime FARF will not work for this "
                   "process. filename %s (errno %s)\n",
                   nErr, __func__, me->fileToWatch,
                   strerror(errno));
  } else {
    FARF(ALWAYS, "%s exiting for domain %d\n", __func__, dom);
  }
  return NULL;
}

void deinitFileWatcher(int dom) {
  int i = 0, nErr = 0;
  uint64 stop = 10;
  remote_handle64 handle;
  ssize_t sz = 0;
  struct log_config_watcher_params *me = NULL;

  VERIFY_LOG_CONFIG_NODE_PRESENT(dom);
  if (me->file_watcher_init_flag) {
    me->stopThread = 1;
    if (0 <= me->event_fd) {
      for (i = 0; i < RETRY_WRITE; i++) {
        VERIFY_IPRINTF(
            "Writing to file_watcher_thread event_fd %d for domain %d\n",
            me->event_fd, dom);
        sz = write(me->event_fd, &stop, sizeof(uint64));
        if ((sz < (ssize_t)sizeof(uint64)) || (sz == -1 && errno == EAGAIN)) {
          VERIFY_WPRINTF("Warning: Written %zd bytes on event_fd %d for domain "
                         "%d (errno = %s): Retrying ...\n",
                         sz, me->event_fd, dom, strerror(errno));
          continue;
        } else {
          break;
        }
      }
    }
    if (sz != sizeof(uint64) && 0 <= me->event_fd) {
      VERIFY_EPRINTF("Error: Written %zd bytes on event_fd %d for domain %d: "
                     "Cannot set exit flag to watcher thread (errno = %s)\n",
                     sz, me->event_fd, dom, strerror(errno));
      // When deinitFileWatcher fail to write dupfd, file watcher thread hangs
      // on poll. Abort in this case.
      raise(SIGABRT);
    }
  }
  if (me->thread) {
    pthread_join(me->thread, NULL);
    me->thread = 0;
  }
  if (me->fileToWatch) {
    free(me->fileToWatch);
    me->fileToWatch = 0;
  }
  if (me->asidFileToWatch) {
    free(me->asidFileToWatch);
    me->asidFileToWatch = 0;
  }
  if (me->pidFileToWatch) {
    free(me->pidFileToWatch);
    me->pidFileToWatch = 0;
  }
  if (me->wd) {
    for (i = 0; i < (int)me->numPaths; ++i) {
      // On success, inotify_add_watch() returns a nonnegative integer watch
      // descriptor
      if (me->wd[i] >= 0) {
        inotify_rm_watch(me->fd, me->wd[i]);
      }
    }
    free(me->wd);
    me->wd = NULL;
  }
  if (me->paths) {
    for (i = 0; i < (int)me->numPaths; ++i) {
      if (me->paths[i].data) {
        free(me->paths[i].data);
        me->paths[i].data = NULL;
      }
    }
    free(me->paths);
    me->paths = NULL;
  }
  if (me->fd != 0) {
    close(me->fd);
    VERIFY_IPRINTF("Closed file watcher fd %d for domain %d\n",
                   me->fd, dom);
    me->fd = 0;
  }
  if (me->adspmsgdEnabled == TRUE) {
    adspmsgd_stop(dom);
    handle = get_adspmsgd_adsp1_handle(dom);
    if (handle != INVALID_HANDLE) {
      adspmsgd_adsp1_deinit(handle);
    } else {
      adspmsgd_adsp_deinit();
    }
    me->adspmsgdEnabled = FALSE;
  }
  if (me->file_watcher_init_flag &&
      (me->event_fd != -1)) {
    close(me->event_fd);
    VERIFY_IPRINTF("Closed file watcher eventfd %d for domain %d\n",
                   me->event_fd, dom);
    me->event_fd = -1;
  }
  me->file_watcher_init_flag = FALSE;
  me->numPaths = 0;
bail:
  return;
}

int initFileWatcher(int domain) {
  int nErr = AEE_SUCCESS;
  const char *fileExtension = ".farf";
  uint32 len = 0;
  uint16 maxPathLen = 0;
  int i = 0;
  char *name = NULL;
  struct log_config_watcher_params *me = NULL;

  GET_HASH_NODE(struct log_config_watcher_params, domain, me);
  if (!me) {
    ALLOC_AND_ADD_NEW_NODE_TO_TABLE(struct log_config_watcher_params, domain, me);
  }
  me->asidToWatch = 0;
  me->event_fd = -1;

  VERIFYC(NULL != (name = std_basename(__progname)), AEE_EBADPARM);

  len = strlen(name) + strlen(fileExtension) + 1;
  VERIFYC(NULL != (me->fileToWatch =
                       malloc(sizeof(char) * len)),
          AEE_ENOMEMORY);
  snprintf(me->fileToWatch, len, "%s%s", name, fileExtension);

  len = strlen(fileExtension) + strlen(__TOSTR__(INT_MAX));
  VERIFYC(NULL != (me->pidFileToWatch =
                       malloc(sizeof(char) * len)),
          AEE_ENOMEMORY);
  snprintf(me->pidFileToWatch, len, "%d%s", getpid(),
           fileExtension);

  VERIFY_IPRINTF("%s: Watching PID file: %s\n",
                 me->fileToWatch, me->pidFileToWatch);

  me->fd = inotify_init();
  if (me->fd < 0) {
    nErr = AEE_ERPC;
    VERIFY_EPRINTF("Error 0x%x: inotify_init failed, invalid fd errno = %s\n",
                   nErr, strerror(errno));
    goto bail;
  }

  // Duplicate the fd, so we can use it to quit polling
  me->event_fd = eventfd(0, 0);
  if (me->event_fd < 0) {
    nErr = AEE_ERPC;
    VERIFY_EPRINTF("Error 0x%x: eventfd in dup failed, invalid fd errno %s\n",
                   nErr, strerror(errno));
    goto bail;
  }
  me->file_watcher_init_flag = TRUE;
  VERIFY_IPRINTF("Opened file watcher fd %d eventfd %d for domain %d\n",
                 me->fd, me->event_fd, domain);

  // Get the required size
  apps_std_get_search_paths_with_env(ADSP_LIBRARY_PATH, ";", NULL, 0,
                                     &me->numPaths, &maxPathLen);

  maxPathLen += +1;

  // Allocate memory
  VERIFYC(NULL != (me->paths = malloc(
                       sizeof(_cstring1_t) * me->numPaths)), AEE_ENOMEMORY);
  VERIFYC(NULL != (me->wd =
                   malloc(sizeof(int) * me->numPaths)),
          AEE_ENOMEMORY);

  for (i = 0; i < (int)me->numPaths; ++i) {
    VERIFYC(NULL != (me->paths[i].data =
                     malloc(sizeof(char) * maxPathLen)), AEE_ENOMEMORY);
    me->paths[i].dataLen = maxPathLen;
  }

  // Get the paths
  VERIFY(AEE_SUCCESS ==
         (nErr = apps_std_get_search_paths_with_env(
              ADSP_LIBRARY_PATH, ";", me->paths,
              me->numPaths, &len, &maxPathLen)));

  maxPathLen += 1;

  VERIFY_IPRINTF("%s: Watching folders:\n", me->fileToWatch);
  for (i = 0; i < (int)me->numPaths; ++i) {
    // Watch for creation, deletion and modification of files in path
    VERIFY_IPRINTF("log file watcher: %s: %s\n",
                   me->fileToWatch, me->paths[i].data);
    if ((me->wd[i] = inotify_add_watch(me->fd, me->paths[i].data,
                   IN_CREATE | IN_DELETE)) < 0) {
      VERIFY_EPRINTF(
          "Error : Unable to add watcher for folder %s : errno is %s\n",
          me->paths[i].data, strerror(ERRNO));
    }
  }

  // Create a thread to watch for file changes
  me->asidToWatch = -1;
  me->stopThread = 0;
  pthread_create(&me->thread, NULL, file_watcher_thread,
                 (void *)(uintptr_t)domain);
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: Failed to register with inotify file %s. "
                   "Runtime FARF will not work for the process %s! errno %d",
                   nErr, me->fileToWatch, name, errno);
    deinitFileWatcher(domain);
  }

  return nErr;
}
