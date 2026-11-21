/*
 * Proton-Scarlett — libHttpClient.dll PE64 Shim
 *
 * Strictly freestanding C11:
 *   - Zero direct references to libc, POSIX, or <windows.h>.
 *   - Crosses PE <-> xgameruntime_unix.so boundary ONLY via WINE_UNIX_CALL.
 *   - External undefined symbols in COFF .obj: ONLY __wine_init_unix_call,
 *     __wine_unix_call_dispatcher, __wine_unixlib_handle.
 */

#include "xgameruntime_pe_bridge.h"

static const uint8_t g_hc_async_id = 1u;

static scarlett_hresult_t SCARLETT_XGR_CALL hc_async_provider(
    XAsyncOp op,
    const XAsyncProviderData *data) {
    (void)op;
    (void)data;
    return SCARLETT_OK;
}

static scarlett_hresult_t hc_complete_async(XAsyncBlock *async, scarlett_hresult_t result_hr) {
    if (async == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_xgr_async_params_t ap;
    xgr_pe_zero(&ap, sizeof(ap));
    ap.async_block_addr = (uint64_t)(uintptr_t)async;
    ap.identity_addr = (uint64_t)(uintptr_t)&g_hc_async_id;
    ap.provider_addr = (uint64_t)(uintptr_t)hc_async_provider;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_ASYNC_BEGIN, &ap);
    if (scarlett_failed(rc)) return rc;
    if (scarlett_failed(ap.out_status)) return ap.out_status;

    xgr_pe_zero(&ap, sizeof(ap));
    ap.async_block_addr = (uint64_t)(uintptr_t)async;
    ap.result_hr = result_hr;
    ap.buffer_size = 0u;
    rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_ASYNC_COMPLETE, &ap);
    return scarlett_failed(rc) ? rc : ap.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCInitialize(const HCInitArgs *args) {
    scarlett_xgr_hc_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_HC_INIT;
    params.handle = (args != NULL) ? args->queue : 0u;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_HC_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL HCCleanup(void) {
    scarlett_xgr_hc_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_HC_CLEANUP;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_HC_OP, &params);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCGetLibVersion(const char **version) {
    if (version == NULL) return SCARLETT_E_POINTER;
    *version = "1.0.0-scarlett";
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallCreate(HCCallHandle *call) {
    if (call == NULL) return SCARLETT_E_POINTER;
    *call = 0u;
    scarlett_xgr_hc_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_HC_CALL_CREATE;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_HC_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *call = params.out_u64;
    return params.out_status;
}

SCARLETT_XGR_API HCCallHandle SCARLETT_XGR_CALL HCCallDuplicateHandle(HCCallHandle call) {
    scarlett_xgr_hc_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_HC_CALL_DUP;
    params.handle = call;
    if (scarlett_failed(xgr_pe_unix_call(SCARLETT_XGR_UNIX_HC_OP, &params))) return 0u;
    return params.out_u64;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallCloseHandle(HCCallHandle call) {
    scarlett_xgr_hc_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_HC_CALL_CLOSE;
    params.handle = call;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_HC_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallRequestSetUrl(
    HCCallHandle call,
    const char *method,
    const char *url) {
    scarlett_xgr_hc_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_HC_SET_URL;
    params.handle = call;
    params.str_a_addr = (uint64_t)(uintptr_t)method;
    params.str_b_addr = (uint64_t)(uintptr_t)url;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_HC_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallRequestSetRequestBodyBytes(
    HCCallHandle call,
    const uint8_t *requestBodyBytes,
    uint32_t requestBodySize) {
    scarlett_xgr_hc_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_HC_SET_BODY;
    params.handle = call;
    params.buf_addr = (uint64_t)(uintptr_t)requestBodyBytes;
    params.buf_size = (uint64_t)requestBodySize;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_HC_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallRequestSetRequestBodyString(
    HCCallHandle call,
    const char *requestBodyString) {
    uint32_t len = (uint32_t)xgr_pe_cstrlen(requestBodyString);
    return HCCallRequestSetRequestBodyBytes(call, (const uint8_t *)requestBodyString, len);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallRequestSetHeader(
    HCCallHandle call,
    const char *headerName,
    const char *headerValue,
    bool allowTracing) {
    (void)allowTracing;
    scarlett_xgr_hc_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_HC_SET_HEADER;
    params.handle = call;
    params.str_a_addr = (uint64_t)(uintptr_t)headerName;
    params.str_b_addr = (uint64_t)(uintptr_t)headerValue;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_HC_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallRequestSetTimeout(
    HCCallHandle call,
    uint32_t timeoutInSeconds) {
    scarlett_xgr_hc_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_HC_SET_TIMEOUT;
    params.handle = call;
    params.u32_a = timeoutInSeconds;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_HC_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallPerformAsync(
    HCCallHandle call,
    XAsyncBlock *async) {
    if (async == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_hc_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_HC_PERFORM;
    params.handle = call;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_HC_OP, &params);
    scarlett_hresult_t hr = scarlett_failed(rc) ? rc : params.out_status;
    return hc_complete_async(async, hr);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallResponseGetStatusCode(
    HCCallHandle call,
    uint32_t *statusCode) {
    if (statusCode == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_hc_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_HC_GET_STATUS_CODE;
    params.handle = call;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_HC_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *statusCode = params.out_u32;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallResponseGetNetworkErrorCode(
    HCCallHandle call,
    scarlett_hresult_t *networkErrorCode,
    uint32_t *platformNetworkErrorCode) {
    scarlett_xgr_hc_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_HC_GET_NET_ERR;
    params.handle = call;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_HC_OP, &params);
    if (scarlett_failed(rc)) return rc;
    if (networkErrorCode != NULL) {
        *networkErrorCode = (scarlett_hresult_t)(uint32_t)params.out_u64;
    }
    if (platformNetworkErrorCode != NULL) {
        *platformNetworkErrorCode = params.out_u32;
    }
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallResponseGetResponseString(
    HCCallHandle call,
    const char **responseString) {
    if (responseString == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_hc_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_HC_GET_RESP_STR;
    params.handle = call;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_HC_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *responseString = (const char *)(uintptr_t)params.out_u64;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallResponseGetResponseBodyBytesSize(
    HCCallHandle call,
    uint64_t *bufferSize) {
    if (bufferSize == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_hc_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_HC_GET_RESP_BYTES;
    params.handle = call;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_HC_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *bufferSize = params.out_u64;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallResponseGetResponseBodyBytes(
    HCCallHandle call,
    uint64_t bufferSize,
    uint8_t *buffer,
    uint64_t *bufferUsed) {
    scarlett_xgr_hc_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_HC_GET_RESP_BYTES;
    params.handle = call;
    params.buf_addr = (uint64_t)(uintptr_t)buffer;
    params.buf_size = bufferSize;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_HC_OP, &params);
    if (bufferUsed != NULL) *bufferUsed = params.out_u64;
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallResponseGetHeader(
    HCCallHandle call,
    const char *headerName,
    const char **headerValue) {
    if (headerValue == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_hc_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_HC_GET_RESP_HEADER;
    params.handle = call;
    params.str_a_addr = (uint64_t)(uintptr_t)headerName;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_HC_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *headerValue = (const char *)(uintptr_t)params.out_u64;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCWebSocketCreate(
    HCWebsocketHandle *websocket,
    void *messageFunc,
    void *binaryMessageFunc,
    void *closeFunc,
    void *functionContext) {
    (void)messageFunc;
    (void)binaryMessageFunc;
    (void)closeFunc;
    (void)functionContext;
    if (websocket == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_hc_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_HC_WS_CREATE;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_HC_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *websocket = params.out_u64;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCWebSocketConnectAsync(
    const char *uri,
    const char *subProtocol,
    HCWebsocketHandle websocket,
    XAsyncBlock *async) {
    if (async == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_hc_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_HC_WS_CONNECT;
    params.handle = websocket;
    params.str_a_addr = (uint64_t)(uintptr_t)uri;
    params.str_b_addr = (uint64_t)(uintptr_t)subProtocol;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_HC_OP, &params);
    scarlett_hresult_t hr = scarlett_failed(rc) ? rc : params.out_status;
    return hc_complete_async(async, hr);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCWebSocketSendMessageAsync(
    HCWebsocketHandle websocket,
    const char *message,
    XAsyncBlock *async) {
    if (async == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_hc_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_HC_WS_SEND;
    params.handle = websocket;
    params.str_a_addr = (uint64_t)(uintptr_t)message;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_HC_OP, &params);
    scarlett_hresult_t hr = scarlett_failed(rc) ? rc : params.out_status;
    return hc_complete_async(async, hr);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCWebSocketDisconnect(
    HCWebsocketHandle websocket) {
    scarlett_xgr_hc_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_HC_WS_DISCONNECT;
    params.handle = websocket;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_HC_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCWebSocketCloseHandle(
    HCWebsocketHandle websocket) {
    scarlett_xgr_hc_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_HC_WS_CLOSE;
    params.handle = websocket;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_HC_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

static const scarlett_stub_descriptor_t g_stub_hc_global_perform = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_NONE,
    SCARLETT_STUB_ORDINAL_NONE,
    SCARLETT_STUB_SLOT_NONE,
    SCARLETT_ERR_UNIMPLEMENTED_STUB,
    0u,
    "libHttpClient.dll",
    "HCGlobalSetHttpCallPerformFunction"
};

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCGlobalSetHttpCallPerformFunction(
    void *performFunc,
    void *context) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    return xgr_pe_dispatch_stub(
        &g_stub_hc_global_perform,
        ret,
        (uint64_t)(uintptr_t)performFunc,
        (uint64_t)(uintptr_t)context,
        0u,
        0u);
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
