/*
 * Proton-Scarlett — XCurl.dll PE64 Shim
 *
 * Strictly freestanding C11:
 *   - Zero direct references to libc, POSIX, or <windows.h>.
 *   - Crosses PE <-> xgameruntime_unix.so boundary ONLY via WINE_UNIX_CALL.
 *   - External undefined symbols in COFF .obj: ONLY __wine_init_unix_call,
 *     __wine_unix_call_dispatcher, __wine_unixlib_handle.
 */

#include <stdarg.h>

#include "xgameruntime_pe_bridge.h"

SCARLETT_XGR_API CURLcode SCARLETT_XGR_CALL curl_global_init(int64_t flags) {
    (void)flags;
    scarlett_xgr_xcurl_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XCURL_GLOBAL_INIT;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XCURL_OP, &params);
    if (scarlett_failed(rc)) return CURLE_FAILED_INIT;
    return (CURLcode)params.out_curl_code;
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL curl_global_cleanup(void) {
    scarlett_xgr_xcurl_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XCURL_GLOBAL_CLEANUP;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_XCURL_OP, &params);
}

SCARLETT_XGR_API const char * SCARLETT_XGR_CALL curl_version(void) {
    return "libcurl/8.9.0-scarlett-cleanroom";
}

SCARLETT_XGR_API CURL * SCARLETT_XGR_CALL curl_easy_init(void) {
    scarlett_xgr_xcurl_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XCURL_EASY_INIT;
    if (scarlett_failed(xgr_pe_unix_call(SCARLETT_XGR_UNIX_XCURL_OP, &params))) {
        return NULL;
    }
    return (CURL *)(uintptr_t)params.handle;
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL curl_easy_cleanup(CURL *curl) {
    scarlett_xgr_xcurl_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XCURL_EASY_CLEANUP;
    params.handle = (uint64_t)(uintptr_t)curl;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_XCURL_OP, &params);
}

SCARLETT_XGR_API CURLcode SCARLETT_XGR_CALL curl_easy_setopt(CURL *curl, CURLoption option, ...) {
    if (curl == NULL) return CURLE_BAD_FUNCTION_ARGUMENT;
    va_list ap;
    va_start(ap, option);

    scarlett_xgr_xcurl_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.handle = (uint64_t)(uintptr_t)curl;
    params.option_id = (uint32_t)option;

    if (option == CURLOPT_URL || option == CURLOPT_POSTFIELDS ||
        option == CURLOPT_CAINFO || option == CURLOPT_CAPATH) {
        const char *str = va_arg(ap, const char *);
        params.subop = SCARLETT_XGR_XCURL_EASY_SETOPT_STR;
        params.str_addr = (uint64_t)(uintptr_t)str;
    } else if (option == CURLOPT_WRITEFUNCTION || option == CURLOPT_WRITEDATA ||
               option == CURLOPT_HTTPHEADER) {
        void *ptr = va_arg(ap, void *);
        params.subop = SCARLETT_XGR_XCURL_EASY_SETOPT_U64;
        params.value_u64 = (uint64_t)(uintptr_t)ptr;
    } else {
        int64_t val = va_arg(ap, int64_t);
        params.subop = SCARLETT_XGR_XCURL_EASY_SETOPT_U64;
        params.value_u64 = (uint64_t)val;
    }
    va_end(ap);

    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XCURL_OP, &params);
    if (scarlett_failed(rc)) return CURLE_BAD_FUNCTION_ARGUMENT;
    return (CURLcode)params.out_curl_code;
}

SCARLETT_XGR_API CURLcode SCARLETT_XGR_CALL curl_easy_perform(CURL *curl) {
    if (curl == NULL) return CURLE_BAD_FUNCTION_ARGUMENT;
    scarlett_xgr_xcurl_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XCURL_EASY_PERFORM;
    params.handle = (uint64_t)(uintptr_t)curl;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XCURL_OP, &params);
    if (scarlett_failed(rc) && params.out_curl_code == CURLE_OK) {
        return CURLE_COULDNT_CONNECT;
    }
    return (CURLcode)params.out_curl_code;
}

SCARLETT_XGR_API CURLcode SCARLETT_XGR_CALL curl_easy_getinfo(CURL *curl, CURLINFO info, ...) {
    if (curl == NULL) return CURLE_BAD_FUNCTION_ARGUMENT;
    va_list ap;
    va_start(ap, info);
    void *out_ptr = va_arg(ap, void *);
    va_end(ap);
    if (out_ptr == NULL) return CURLE_BAD_FUNCTION_ARGUMENT;

    scarlett_xgr_xcurl_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.handle = (uint64_t)(uintptr_t)curl;
    params.option_id = (uint32_t)info;

    if (info == CURLINFO_CAINFO || info == CURLINFO_CAPATH || info == CURLINFO_EFFECTIVE_URL) {
        params.subop = SCARLETT_XGR_XCURL_EASY_GETINFO_STR;
        int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XCURL_OP, &params);
        if (scarlett_failed(rc)) return CURLE_BAD_FUNCTION_ARGUMENT;
        *(const char **)out_ptr = (const char *)(uintptr_t)params.value_u64;
        return (CURLcode)params.out_curl_code;
    }

    params.subop = SCARLETT_XGR_XCURL_EASY_GETINFO_U64;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XCURL_OP, &params);
    if (scarlett_failed(rc)) return CURLE_BAD_FUNCTION_ARGUMENT;
    *(int64_t *)out_ptr = (int64_t)params.value_u64;
    return (CURLcode)params.out_curl_code;
}

SCARLETT_XGR_API const char * SCARLETT_XGR_CALL curl_easy_strerror(CURLcode code) {
    switch (code) {
        case CURLE_OK:                    return "No error";
        case CURLE_UNSUPPORTED_PROTOCOL:  return "Unsupported protocol";
        case CURLE_FAILED_INIT:           return "Failed initialization";
        case CURLE_URL_MALFORMAT:         return "URL using bad/illegal format or missing URL";
        case CURLE_COULDNT_RESOLVE_HOST:  return "Couldn't resolve host name";
        case CURLE_COULDNT_CONNECT:       return "Couldn't connect to server";
        case CURLE_OPERATION_TIMEDOUT:    return "Timeout was reached";
        case CURLE_BAD_FUNCTION_ARGUMENT: return "A requested feature, protocol or option was not found";
        case CURLE_SSL_CACERT_BADFILE:    return "Problem with the SSL CA cert";
        default:                          return "Unknown error";
    }
}

SCARLETT_XGR_API struct curl_slist * SCARLETT_XGR_CALL curl_slist_append(
    struct curl_slist *list,
    const char *string) {
    scarlett_xgr_xcurl_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XCURL_SLIST_APPEND;
    params.handle = (uint64_t)(uintptr_t)list;
    params.str_addr = (uint64_t)(uintptr_t)string;
    if (scarlett_failed(xgr_pe_unix_call(SCARLETT_XGR_UNIX_XCURL_OP, &params))) {
        return list;
    }
    return (struct curl_slist *)(uintptr_t)params.value_u64;
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL curl_slist_free_all(struct curl_slist *list) {
    scarlett_xgr_xcurl_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XCURL_SLIST_FREE;
    params.handle = (uint64_t)(uintptr_t)list;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_XCURL_OP, &params);
}

SCARLETT_XGR_API CURLM * SCARLETT_XGR_CALL curl_multi_init(void) {
    scarlett_xgr_xcurl_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XCURL_MULTI_INIT;
    if (scarlett_failed(xgr_pe_unix_call(SCARLETT_XGR_UNIX_XCURL_OP, &params))) {
        return NULL;
    }
    return (CURLM *)(uintptr_t)params.handle;
}

SCARLETT_XGR_API CURLMcode SCARLETT_XGR_CALL curl_multi_cleanup(CURLM *multi_handle) {
    scarlett_xgr_xcurl_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XCURL_MULTI_CLEANUP;
    params.handle = (uint64_t)(uintptr_t)multi_handle;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XCURL_OP, &params);
    return scarlett_failed(rc) ? CURLM_INTERNAL_ERROR : (CURLMcode)params.out_curl_code;
}

SCARLETT_XGR_API CURLMcode SCARLETT_XGR_CALL curl_multi_add_handle(CURLM *multi_handle, CURL *curl_handle) {
    scarlett_xgr_xcurl_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XCURL_MULTI_ADD;
    params.handle = (uint64_t)(uintptr_t)multi_handle;
    params.aux_handle = (uint64_t)(uintptr_t)curl_handle;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XCURL_OP, &params);
    return scarlett_failed(rc) ? CURLM_INTERNAL_ERROR : (CURLMcode)params.out_curl_code;
}

SCARLETT_XGR_API CURLMcode SCARLETT_XGR_CALL curl_multi_remove_handle(CURLM *multi_handle, CURL *curl_handle) {
    scarlett_xgr_xcurl_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XCURL_MULTI_REMOVE;
    params.handle = (uint64_t)(uintptr_t)multi_handle;
    params.aux_handle = (uint64_t)(uintptr_t)curl_handle;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XCURL_OP, &params);
    return scarlett_failed(rc) ? CURLM_INTERNAL_ERROR : (CURLMcode)params.out_curl_code;
}

SCARLETT_XGR_API CURLMcode SCARLETT_XGR_CALL curl_multi_perform(CURLM *multi_handle, int *running_handles) {
    scarlett_xgr_xcurl_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XCURL_MULTI_PERFORM;
    params.handle = (uint64_t)(uintptr_t)multi_handle;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XCURL_OP, &params);
    if (running_handles != NULL) *running_handles = (int)params.value_u64;
    return scarlett_failed(rc) ? CURLM_INTERNAL_ERROR : (CURLMcode)params.out_curl_code;
}

SCARLETT_XGR_API CURLMcode SCARLETT_XGR_CALL curl_multi_poll(
    CURLM *multi_handle,
    void *extra_fds,
    uint32_t extra_nfds,
    int timeout_ms,
    int *numfds) {
    (void)extra_fds;
    (void)extra_nfds;
    (void)timeout_ms;
    scarlett_xgr_xcurl_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XCURL_MULTI_POLL;
    params.handle = (uint64_t)(uintptr_t)multi_handle;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XCURL_OP, &params);
    if (numfds != NULL) *numfds = (int)params.value_u64;
    return scarlett_failed(rc) ? CURLM_INTERNAL_ERROR : (CURLMcode)params.out_curl_code;
}

static const scarlett_stub_descriptor_t g_stub_curl_share_init = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_NONE,
    SCARLETT_STUB_ORDINAL_NONE,
    SCARLETT_STUB_SLOT_NONE,
    SCARLETT_ERR_UNIMPLEMENTED_STUB,
    0u,
    "XCurl.dll",
    "curl_share_init"
};

SCARLETT_XGR_API void * SCARLETT_XGR_CALL curl_share_init(void) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    (void)xgr_pe_dispatch_stub(&g_stub_curl_share_init, ret, 0u, 0u, 0u, 0u);
    return NULL;
}

#if defined(_WIN32)
uint32_t SCARLETT_XGR_CALL DllMain(void *hinst, uint32_t reason, void *reserved) {
    (void)hinst;
    (void)reserved;
    if (reason == 1u) {
        if (xgr_pe_init_unix_bridge() != 0) {
            return 0u;
        }
    }
    return 1u;
}
#endif
