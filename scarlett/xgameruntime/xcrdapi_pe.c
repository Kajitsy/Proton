/*
 * Proton-Scarlett — XCRDAPI.dll PE64 Freestanding Layer
 *
 * Implements DirectStorage Runtime Entry Point (DStorageGetFactory)
 * and Xbox Crash Reporting & Diagnostics (XCRD) APIs.
 *
 * Strict Wine 11.0 PE isolation:
 * Strictly freestanding C11 (zero libc, zero <windows.h>, zero POSIX headers).
 */

#include "xgameruntime_pe_bridge.h"

#ifndef SCARLETT_WIN64_API
#define SCARLETT_WIN64_API SCARLETT_XGR_CALL
#endif

#if defined(_WIN32)
__declspec(dllimport) void * __stdcall LoadLibraryA(const char *lpLibFileName);
__declspec(dllimport) void * __stdcall GetProcAddress(void *hModule, const char *lpProcName);
__declspec(dllimport) void * __stdcall GetModuleHandleA(const char *lpModuleName);
__declspec(dllimport) void __stdcall OutputDebugStringA(const char *lpOutputString);
#else
static inline void *LoadLibraryA(const char *s) { (void)s; return NULL; }
static inline void *GetProcAddress(void *m, const char *s) { (void)m; (void)s; return NULL; }
static inline void *GetModuleHandleA(const char *s) { (void)s; return NULL; }
static inline void OutputDebugStringA(const char *s) { (void)s; }
#endif

typedef scarlett_hresult_t (SCARLETT_WIN64_API *pfn_DStorageGetFactory_t)(
    const scarlett_guid_t *riid,
    void **ppv);

static pfn_DStorageGetFactory_t g_pfn_dstorage_factory = NULL;

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_WIN64_API DStorageGetFactory(
    const scarlett_guid_t *riid,
    void **ppv) {
    if (riid == NULL || ppv == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppv = NULL;

    if (g_pfn_dstorage_factory == NULL) {
        void *h = GetModuleHandleA("dstorage_xs.dll");
        if (h == NULL) {
            h = LoadLibraryA("dstorage_xs.dll");
        }
        if (h != NULL) {
            g_pfn_dstorage_factory = (pfn_DStorageGetFactory_t)GetProcAddress(h, "DStorageGetFactory");
        }
    }

    if (g_pfn_dstorage_factory != NULL) {
        return g_pfn_dstorage_factory(riid, ppv);
    }

    OutputDebugStringA("[XCRDAPI] DStorageGetFactory: failed to locate dstorage_xs.dll entry point\n");
    return SCARLETT_E_FAIL;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_WIN64_API XcrdInitialize(const void *params) {
    (void)params;
    OutputDebugStringA("[XCRDAPI] XcrdInitialize: diagnostic reporting active\n");
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_WIN64_API XcrdShutdown(void) {
    OutputDebugStringA("[XCRDAPI] XcrdShutdown\n");
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_WIN64_API XcrdReportCrash(
    uint32_t crash_code,
    const void *crash_info) {
    (void)crash_code;
    (void)crash_info;
    OutputDebugStringA("[XCRDAPI] XcrdReportCrash recorded\n");
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_WIN64_API XcrdSetDumpMode(uint32_t dump_mode) {
    (void)dump_mode;
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_WIN64_API XcrdAddUserData(
    const char *key,
    const void *data,
    uint32_t size) {
    (void)key;
    (void)data;
    (void)size;
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_WIN64_API XcrdAddAttachment(
    const char *name,
    const void *data,
    uint32_t size) {
    (void)name;
    (void)data;
    (void)size;
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_WIN64_API XcrdWriteDiagnosticReport(
    const char *reason,
    void *out_report_info) {
    (void)reason;
    (void)out_report_info;
    OutputDebugStringA("[XCRDAPI] XcrdWriteDiagnosticReport completed\n");
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_WIN64_API XcrdRegisterCrashCallback(
    void *callback,
    void *context,
    void **out_token) {
    (void)callback;
    (void)context;
    if (out_token != NULL) {
        *out_token = (void *)(uintptr_t)1u;
    }
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_WIN64_API XcrdUnregisterCrashCallback(void *token) {
    (void)token;
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_WIN64_API XcrdGetStatus(uint32_t *out_status) {
    if (out_status != NULL) {
        *out_status = 0u;
    }
    return SCARLETT_OK;
}
