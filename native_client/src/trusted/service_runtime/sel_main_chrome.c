/*
 * Copyright (c) 2012 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "native_client/src/public/chrome_main.h"

#include "native_client/src/include/build_config.h"
#include "native_client/src/include/portability.h"
#include "native_client/src/include/portability_io.h"
#include "native_client/src/include/portability_sockets.h"

#include <stdio.h>
#include <string.h>

#include "native_client/src/include/nacl_macros.h"
#include "native_client/src/public/nacl_app.h"
#include "native_client/src/shared/platform/nacl_check.h"
#include "native_client/src/shared/platform/nacl_exit.h"
#include "native_client/src/shared/platform/nacl_log.h"
#include "native_client/src/shared/platform/nacl_secure_random.h"
#include "native_client/src/shared/platform/nacl_sync.h"
#include "native_client/src/shared/platform/nacl_sync_checked.h"
#include "native_client/src/trusted/desc/nacl_desc_io.h"
#include "native_client/src/trusted/fault_injection/fault_injection.h"
#include "native_client/src/trusted/service_runtime/env_cleanser.h"
#include "native_client/src/trusted/service_runtime/include/sys/fcntl.h"
#include "native_client/src/trusted/service_runtime/nacl_all_modules.h"
#include "native_client/src/trusted/service_runtime/nacl_app.h"
#include "native_client/src/trusted/service_runtime/nacl_error_log_hook.h"
#include "native_client/src/trusted/service_runtime/nacl_globals.h"
#include "native_client/src/trusted/service_runtime/nacl_debug_init.h"
#include "native_client/src/trusted/service_runtime/nacl_signal.h"
#include "native_client/src/trusted/service_runtime/osx/mach_exception_handler.h"
#include "native_client/src/trusted/service_runtime/sel_addrspace.h"
#include "native_client/src/trusted/service_runtime/sel_ldr.h"
#include "native_client/src/trusted/service_runtime/sel_main_common.h"
#include "native_client/src/trusted/service_runtime/sel_qualify.h"
#include "native_client/src/trusted/service_runtime/win/exception_patch/ntdll_patch.h"
#include "native_client/src/trusted/validator/rich_file_info.h"
#include "native_client/src/trusted/validator/validation_metadata.h"

static int g_initialized = 0;

static void (*g_fatal_error_handler)(const char *data, size_t bytes) = NULL;

static const int default_argc = 1;
static const char *const default_argv[1] = {"NaClMain"};

#if NACL_LINUX || NACL_OSX
void NaClChromeMainSetUrandomFd(int urandom_fd) {
  CHECK(!g_initialized);
  NaClSecureRngModuleSetUrandomFd(urandom_fd);
}
#endif

void NaClChromeMainInit(void) {
  CHECK(!g_initialized);
  NaClAllModulesInit();
  g_initialized = 1;
}

static void NaClFatalErrorHandlerCallback(void *state,
                                          char *buf,
                                          size_t buf_bytes) {
  CHECK(state == NULL);
  g_fatal_error_handler(buf, buf_bytes);
}

void NaClSetFatalErrorCallback(void (*func)(const char *data, size_t bytes)) {
  CHECK(g_initialized);
  if (g_fatal_error_handler != NULL)
    NaClLog(LOG_FATAL, "NaClSetFatalErrorCallback called twice.\n");
  g_fatal_error_handler = func;
  NaClErrorLogHookInit(NaClFatalErrorHandlerCallback, NULL);
}

struct NaClChromeMainArgs *NaClChromeMainArgsCreate(void) {
  struct NaClChromeMainArgs *args;

  CHECK(g_initialized);
  args = malloc(sizeof(*args));
  if (args == NULL)
    return NULL;
  args->imc_bootstrap_handle = NACL_INVALID_HANDLE;
  args->irt_fd = -1;
  args->irt_desc = NULL;
  args->irt_load_optional = 0;
  args->enable_exception_handling = 0;
  args->enable_debug_stub = 0;
  args->enable_dyncode_syscalls = 1;
  args->pnacl_mode = 0;
  /* TODO(ncbray): default to 0. */
  args->skip_qualification =
      getenv("NACL_DANGEROUS_SKIP_QUALIFICATION_TEST") != NULL;
  args->initial_nexe_max_code_bytes = 0;  /* No limit */
#if NACL_LINUX || NACL_OSX
  args->debug_stub_server_bound_socket_fd = NACL_INVALID_SOCKET;
#endif
#if NACL_WINDOWS
  args->debug_stub_server_port_selected_handler_func = NULL;
#endif
  args->create_memory_object_func = NULL;
  args->validation_cache = NULL;
#if NACL_WINDOWS
  args->broker_duplicate_handle_func = NULL;
  args->attach_debug_exception_handler_func = NULL;
#endif
#if NACL_LINUX || NACL_OSX
  args->number_of_cores = -1;  /* unknown */
#endif
#if NACL_LINUX
  args->prereserved_sandbox_size = 0;
#endif
  args->nexe_desc = NULL;

  args->argc = default_argc;
  args->argv = (char **) default_argv;
  return args;
}

static struct NaClDesc *IrtDescFromFd(int irt_fd) {
  struct NaClDesc *irt_desc;

  if (irt_fd == -1) {
    NaClLog(LOG_FATAL,
            "IrtDescFromFd: Integrated runtime (IRT) not present.\n");
  }

  /* Takes ownership of the FD. */
  irt_desc = NaClDescIoDescFromDescAllocCtor(irt_fd, NACL_ABI_O_RDONLY);
  if (NULL == irt_desc) {
    NaClLog(LOG_FATAL,
            "IrtDescFromFd: failed to construct NaClDesc object from"
            " descriptor\n");
  }

  return irt_desc;
}

static char kFakeIrtName[] = "\0IRT";

static void NaClLoadIrt(struct NaClApp *nap, struct NaClDesc *irt_desc) {
  struct NaClRichFileInfo info;
  struct NaClValidationMetadata metadata;
  NaClErrorCode errcode;

  /* Attach file origin info to the IRT's NaClDesc. */
  NaClRichFileInfoCtor(&info);
  /*
   * For the IRT use a fake file name with null characters at the begining and
   * the end of the name.
   * TODO(ncbray): plumb the real filename in from Chrome.
   * Note that when the file info is attached to the NaClDesc, information from
   * stat is incorporated into the metadata.  There is no functional reason to
   * attach the info to the NaClDesc other than to create the final metadata.
   * TODO(ncbray): API for deriving metadata from a NaClDesc without attaching.
   */
  info.known_file = 1;
  info.file_path = kFakeIrtName;
  info.file_path_length = sizeof(kFakeIrtName);
  NaClSetFileOriginInfo(irt_desc, &info);
  /* Don't free the info struct because we don't own the file path string. */

  NaClMetadataFromNaClDescCtor(&metadata, irt_desc);
  errcode = NaClMainLoadIrt(nap, irt_desc, &metadata);
  if (errcode != LOAD_OK) {
    NaClLog(LOG_FATAL,
            "NaClLoadIrt: Failed to load the integrated runtime (IRT): %s\n",
            NaClErrorString(errcode));
  }

  NaClMetadataDtor(&metadata);
}

static int LoadApp(struct NaClApp *nap, struct NaClChromeMainArgs *args) {
  NaClErrorCode errcode = LOAD_OK;
  int has_bootstrap_channel = args->imc_bootstrap_handle != NACL_INVALID_HANDLE;

  CHECK(g_initialized);

  /* Allow or disallow dyncode API based on args. */
  nap->enable_dyncode_syscalls = args->enable_dyncode_syscalls;
  nap->initial_nexe_max_code_bytes = args->initial_nexe_max_code_bytes;
  nap->pnacl_mode = args->pnacl_mode;

#if NACL_LINUX
  g_prereserved_sandbox_size = args->prereserved_sandbox_size;
#endif
#if NACL_LINUX || NACL_OSX
  /*
   * Overwrite value of sc_nprocessors_onln set in NaClAppCtor.  In
   * the Chrome embedding, the outer sandbox was already enabled when
   * the NaClApp Ctor was invoked, so a bogus value was written in
   * sc_nprocessors_onln.
   */
  if (-1 != args->number_of_cores) {
    nap->sc_nprocessors_onln = args->number_of_cores;
  }
#endif

  if (args->create_memory_object_func != NULL)
    NaClSetCreateMemoryObjectFunc(args->create_memory_object_func);

  /* Inject the validation caching interface, if it exists. */
  nap->validation_cache = args->validation_cache;

#if NACL_WINDOWS
  if (args->broker_duplicate_handle_func != NULL)
    NaClSetBrokerDuplicateHandleFunc(args->broker_duplicate_handle_func);
#endif

  NaClAppInitialDescriptorHookup(nap);

  /*
   * NACL_SERVICE_PORT_DESCRIPTOR and NACL_SERVICE_ADDRESS_DESCRIPTOR
   * are 3 and 4.
   */

  /*
   * in order to report load error to the browser plugin through the
   * secure command channel, we do not immediate jump to cleanup code
   * on error.  rather, we continue processing (assuming earlier
   * errors do not make it inappropriate) until the secure command
   * channel is set up, and then bail out.
   */

  /*
   * Ensure this operating system platform is supported.
   */
  if (args->skip_qualification) {
    fprintf(stderr, "PLATFORM QUALIFICATION DISABLED - "
        "Native Client's sandbox will be unreliable!\n");
  } else {
    errcode = NACL_FI_VAL("pq", NaClErrorCode,
                          NaClRunSelQualificationTests());
    if (LOAD_OK != errcode) {
      nap->module_load_status = errcode;
      fprintf(stderr, "Error while loading in SelMain: %s\n",
              NaClErrorString(errcode));
    }
  }

  /*
   * Patch the Windows exception dispatcher to be safe in the case
   * of faults inside x86-64 sandboxed code.  The sandbox is not
   * secure on 64-bit Windows without this.
   */
#if (NACL_WINDOWS && NACL_ARCH(NACL_BUILD_ARCH) == NACL_x86 && \
     NACL_BUILD_SUBARCH == 64)
  NaClPatchWindowsExceptionDispatcher();
#endif
  NaClSignalTestCrashOnStartup();

  nap->enable_exception_handling = args->enable_exception_handling;

  if (args->enable_exception_handling || args->enable_debug_stub) {
#if NACL_LINUX
    /* NaCl's signal handler is always enabled on Linux. */
#elif NACL_OSX
    if (!NaClInterceptMachExceptions()) {
      NaClLog(LOG_FATAL, "LoadApp: Failed to set up Mach exception handler\n");
    }
#elif NACL_WINDOWS
    nap->attach_debug_exception_handler_func =
        args->attach_debug_exception_handler_func;
#else
# error Unknown host OS
#endif
  }
#if NACL_LINUX
  NaClSignalHandlerInit();
#endif

  /* Give debuggers a well known point at which xlate_base is known.  */
  NaClGdbHook(nap);

  if (has_bootstrap_channel) {
    NaClCreateServiceSocket(nap);
    /*
     * LOG_FATAL errors that occur before NaClSetUpBootstrapChannel will
     * not be reported via the crash log mechanism (for Chromium
     * embedding of NaCl, shown in the JavaScript console).
     *
     * Some errors, such as due to NaClRunSelQualificationTests, do not
     * trigger a LOG_FATAL but instead set module_load_status to be sent
     * in the start_module RPC reply.  Log messages associated with such
     * errors would be seen, since NaClSetUpBootstrapChannel will get
     * called.
     */
    NaClSetUpBootstrapChannel(nap, args->imc_bootstrap_handle);
  }

  CHECK(args->nexe_desc != NULL);
  NaClAppLoadModule(nap, args->nexe_desc, NULL, NULL);
  NaClDescUnref(args->nexe_desc);
  args->nexe_desc = NULL;

  if (has_bootstrap_channel) {
    NACL_FI_FATAL("BeforeSecureCommandChannel");
    /*
     * Spawns a thread that uses the command channel.
     * Hereafter any changes to nap should be done while holding locks.
     */
    NaClSecureCommandChannel(nap);

    NaClLog(4, "NaClSecureCommandChannel has spawned channel\n");

    NaClLog(4, "secure service = %"NACL_PRIxPTR"\n",
            (uintptr_t) nap->secure_service);
    NACL_FI_FATAL("BeforeWaitForStartModule");

    if (NULL != nap->secure_service) {
      NaClErrorCode start_result;
      /*
       * wait for start_module RPC call on secure channel thread.
       */
      start_result = NaClWaitForStartModuleCommand(nap);
      if (LOAD_OK == errcode) {
        errcode = start_result;
      }
    }
  }

  NACL_FI_FATAL("BeforeLoadIrt");

  /*
   * error reporting done; can quit now if there was an error earlier.
   */
  if (LOAD_OK != errcode) {
    goto done;
  }

  /*
   * Load the integrated runtime (IRT) library.
   * Skip if irt_load_optional and the nexe doesn't have the usual 256MB
   * segment gap. PNaCl's disabling of the segment gap doesn't actually
   * disable the segment gap. It only only reduces it drastically.
   */
  if (args->irt_load_optional && nap->dynamic_text_end < 0x10000000) {
    NaClLog(1,
            "Skipped NaClLoadIrt, irt_load_optional with dynamic_text_end: %"
            NACL_PRIxPTR"\n", nap->dynamic_text_end);
  } else {
    if (args->irt_fd != -1) {
      CHECK(args->irt_desc == NULL);
      args->irt_desc = IrtDescFromFd(args->irt_fd);
      args->irt_fd = -1;
    }
    if (args->irt_desc != NULL) {
      NaClLoadIrt(nap, args->irt_desc);
      NaClDescUnref(args->irt_desc);
      args->irt_desc = NULL;
    }
  }

  if (args->enable_debug_stub) {
#if NACL_LINUX || NACL_OSX
    if (args->debug_stub_server_bound_socket_fd != NACL_INVALID_SOCKET) {
      NaClDebugSetBoundSocket(args->debug_stub_server_bound_socket_fd);
    }
#endif
    if (!NaClDebugInit(nap)) {
      goto done;
    }
#if NACL_WINDOWS
    if (NULL != args->debug_stub_server_port_selected_handler_func) {
      args->debug_stub_server_port_selected_handler_func(nap->debug_stub_port);
    }
#endif
  }

  return LOAD_OK;

done:
  fflush(stdout);

  /*
   * If there is a secure command channel, we sent an RPC reply with
   * the reason that the nexe was rejected.  If we exit now, that
   * reply may still be in-flight and the various channel closure (esp
   * reverse channel) may be detected first.  This would result in a
   * crash being reported, rather than the error in the RPC reply.
   * Instead, we wait for the hard-shutdown on the command channel.
   */
  if (LOAD_OK != errcode) {
    NaClBlockIfCommandChannelExists(nap);
  } else {
    /*
     * Don't return LOAD_OK if we had some failure loading.
     */
    errcode = LOAD_INTERNAL;
  }
  return errcode;
}

static int StartApp(struct NaClApp *nap, struct NaClChromeMainArgs *args) {
  int ret_code;
  struct NaClEnvCleanser env_cleanser;
  char const *const *envp;

  NACL_FI_FATAL("BeforeEnvCleanserCtor");

  NaClEnvCleanserCtor(&env_cleanser, 1);
  if (!NaClEnvCleanserInit(&env_cleanser, NaClGetEnviron(), NULL)) {
    NaClLog(LOG_FATAL, "Failed to initialise env cleanser\n");
  }
  envp = NaClEnvCleanserEnvironment(&env_cleanser);

  if (NACL_FI_ERROR_COND(
          "CreateMainThread",
          !NaClCreateMainThread(nap, args->argc, args->argv, envp))) {
    NaClLog(LOG_FATAL, "creating main thread failed\n");
  }
  NACL_FI_FATAL("BeforeEnvCleanserDtor");

  NaClEnvCleanserDtor(&env_cleanser);

  ret_code = NaClWaitForMainThreadToExit(nap);

  if (NACL_ABI_WIFEXITED(nap->exit_status)) {
    /*
     * Under Chrome, a call to _exit() often indicates that something
     * has gone awry, so we report it here to aid debugging.
     *
     * This conditional does not run if the NaCl process was
     * terminated forcibly, which is the normal case under Chrome.
     * This forcible exit is triggered by the renderer closing the
     * trusted SRPC channel, which we record as NACL_ABI_SIGKILL
     * internally.
     */
    NaClLog(LOG_INFO, "NaCl untrusted code called _exit(0x%x)\n", ret_code);
  }
  return ret_code;
}

int NaClChromeMainStart(struct NaClApp *nap,
                        struct NaClChromeMainArgs *args,
                        int *exit_status) {
  int load_ok = LOAD_OK == LoadApp(nap, args);
  if (load_ok) {
    *exit_status = StartApp(nap, args);
  }
  free(args);
  return load_ok;
}
