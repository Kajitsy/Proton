/*
 * Proton-Scarlett — Public API for dstorage_xs.dll & dstorage_xs_unix.so
 *
 * Clean-room freestanding C11 / C++20 header matching scarlett/xdl/schemas/dstorage_xs.xdl
 * and scarlett/docs/ABI_CONTRACTS.md.
 *   - Under PE64 compilation (_WIN32): implemented in dstorage_xs_pe.c strictly via
 *     WINE_UNIX_CALL (zero libc / POSIX / liburing / libvulkan dependencies).
 *   - Under Linux host tests: dstorage_xs_pe.c links via
 *     scarlett_dstorage_set_unix_call_bridge() directly to dstorage_xs_unix.so.
 */

#ifndef SCARLETT_DSTORAGE_XS_API_H
#define SCARLETT_DSTORAGE_XS_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dstorage_xs_unix_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Interface GUIDs (Matching scarlett/xdl/schemas/dstorage_xs.xdl)
 * ============================================================================ */

/* IID_IUnknown = {00000000-0000-0000-C000-000000000046} */
#define SCARLETT_IID_IUNKNOWN_INIT                                             \
    {                                                                          \
        UINT32_C(0x00000000), UINT16_C(0x0000), UINT16_C(0x0000),              \
        { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u }             \
    }

/* IID_IDStorageFactoryX = {6924EA0C-C3CD-4826-B10A-F64F4ED927C1} */
#define SCARLETT_IID_IDSTORAGEFACTORYX_INIT                                    \
    {                                                                          \
        UINT32_C(0x6924EA0C), UINT16_C(0xC3CD), UINT16_C(0x4826),              \
        { 0xB1u, 0x0Au, 0xF6u, 0x4Fu, 0x4Eu, 0xD9u, 0x27u, 0xC1u }             \
    }

/* IID_IDStorageFactoryX1 = {6924EA0C-C3CD-4826-B10A-F64F4ED927C2} */
#define SCARLETT_IID_IDSTORAGEFACTORYX1_INIT                                   \
    {                                                                          \
        UINT32_C(0x6924EA0C), UINT16_C(0xC3CD), UINT16_C(0x4826),              \
        { 0xB1u, 0x0Au, 0xF6u, 0x4Fu, 0x4Eu, 0xD9u, 0x27u, 0xC2u }             \
    }

/* IID_IDStorageFactoryX2 = {6924EA0C-C3CD-4826-B10A-F64F4ED927C3} */
#define SCARLETT_IID_IDSTORAGEFACTORYX2_INIT                                   \
    {                                                                          \
        UINT32_C(0x6924EA0C), UINT16_C(0xC3CD), UINT16_C(0x4826),              \
        { 0xB1u, 0x0Au, 0xF6u, 0x4Fu, 0x4Eu, 0xD9u, 0x27u, 0xC3u }             \
    }

/* IID_IDStorageQueueX = {CFDBD83F-9E06-4FDA-8EA5-69042137F49B} */
#define SCARLETT_IID_IDSTORAGEQUEUEX_INIT                                      \
    {                                                                          \
        UINT32_C(0xCFDBD83F), UINT16_C(0x9E06), UINT16_C(0x4FDA),              \
        { 0x8Eu, 0xA5u, 0x69u, 0x04u, 0x21u, 0x37u, 0xF4u, 0x9Bu }             \
    }

/* IID_IDStorageQueueX1 = {CFDBD83F-9E06-4FDA-8EA5-69042137F49C} */
#define SCARLETT_IID_IDSTORAGEQUEUEX1_INIT                                     \
    {                                                                          \
        UINT32_C(0xCFDBD83F), UINT16_C(0x9E06), UINT16_C(0x4FDA),              \
        { 0x8Eu, 0xA5u, 0x69u, 0x04u, 0x21u, 0x37u, 0xF4u, 0x9Cu }             \
    }

/* IID_IDStorageQueueX2 = {CFDBD83F-9E06-4FDA-8EA5-69042137F49D} */
#define SCARLETT_IID_IDSTORAGEQUEUEX2_INIT                                     \
    {                                                                          \
        UINT32_C(0xCFDBD83F), UINT16_C(0x9E06), UINT16_C(0x4FDA),              \
        { 0x8Eu, 0xA5u, 0x69u, 0x04u, 0x21u, 0x37u, 0xF4u, 0x9Du }             \
    }

/* IID_IDStorageFileX = {5DE95E7B-955A-4868-A73C-243B29F4B8DA} */
#define SCARLETT_IID_IDSTORAGEFILEX_INIT                                       \
    {                                                                          \
        UINT32_C(0x5DE95E7B), UINT16_C(0x955A), UINT16_C(0x4868),              \
        { 0xA7u, 0x3Cu, 0x24u, 0x3Bu, 0x29u, 0xF4u, 0xB8u, 0xDAu }             \
    }

/* IID_IDStorageStatusArrayX = {82397587-7CD5-453B-A02E-31379BD64656} */
#define SCARLETT_IID_IDSTORAGESTATUSARRAYX_INIT                                \
    {                                                                          \
        UINT32_C(0x82397587), UINT16_C(0x7CD5), UINT16_C(0x453B),              \
        { 0xA0u, 0x2Eu, 0x31u, 0x37u, 0x9Bu, 0xD6u, 0x46u, 0x56u }             \
    }

/* ============================================================================
 * Forward Declarations of COM Interfaces
 * ============================================================================ */

typedef struct IDStorageFactoryX_S IDStorageFactoryX;
typedef struct IDStorageFactoryX1_S IDStorageFactoryX1;
typedef struct IDStorageFactoryX2_S IDStorageFactoryX2;
typedef struct IDStorageQueueX_S IDStorageQueueX;
typedef struct IDStorageQueueX1_S IDStorageQueueX1;
typedef struct IDStorageQueueX2_S IDStorageQueueX2;
typedef struct IDStorageFileX_S IDStorageFileX;
typedef struct IDStorageStatusArrayX_S IDStorageStatusArrayX;

/* ============================================================================
 * IDStorageFileX
 * ============================================================================ */

typedef struct IDStorageFileXVtbl_S {
    scarlett_hresult_t (SCARLETT_DSTORAGEXS_CALL *QueryInterface)(
        IDStorageFileX *self, const scarlett_guid_t *riid, void **ppvObject);
    uint32_t (SCARLETT_DSTORAGEXS_CALL *AddRef)(IDStorageFileX *self);
    uint32_t (SCARLETT_DSTORAGEXS_CALL *Release)(IDStorageFileX *self);
    void (SCARLETT_DSTORAGEXS_CALL *Close)(IDStorageFileX *self);
    scarlett_hresult_t (SCARLETT_DSTORAGEXS_CALL *GetFileInformation)(
        IDStorageFileX *self, DSTORAGE_FILE_INFO *info);
    scarlett_hresult_t (SCARLETT_DSTORAGEXS_CALL *GetFileHandle)(
        IDStorageFileX *self, void **outHandle);
} IDStorageFileXVtbl;

struct IDStorageFileX_S {
    const IDStorageFileXVtbl *lpVtbl;
    uint64_t unix_file_handle;
    uint64_t file_size_bytes;
    uint32_t ref_count;
    uint32_t in_use;
};

/* ============================================================================
 * IDStorageStatusArrayX
 * ============================================================================ */

typedef struct IDStorageStatusArrayXVtbl_S {
    scarlett_hresult_t (SCARLETT_DSTORAGEXS_CALL *QueryInterface)(
        IDStorageStatusArrayX *self, const scarlett_guid_t *riid, void **ppvObject);
    uint32_t (SCARLETT_DSTORAGEXS_CALL *AddRef)(IDStorageStatusArrayX *self);
    uint32_t (SCARLETT_DSTORAGEXS_CALL *Release)(IDStorageStatusArrayX *self);
    uint32_t (SCARLETT_DSTORAGEXS_CALL *IsComplete)(
        IDStorageStatusArrayX *self, uint32_t index);
    scarlett_hresult_t (SCARLETT_DSTORAGEXS_CALL *GetHResult)(
        IDStorageStatusArrayX *self, uint32_t index);
} IDStorageStatusArrayXVtbl;

struct IDStorageStatusArrayX_S {
    const IDStorageStatusArrayXVtbl *lpVtbl;
    uint64_t unix_status_handle;
    uint32_t capacity;
    uint32_t ref_count;
    uint32_t in_use;
    uint32_t reserved;
};

/* ============================================================================
 * IDStorageQueueX / X1 / X2 (Strict VTable Prefix Inheritance)
 * ============================================================================ */

typedef struct IDStorageQueueX2Vtbl_S {
    /* IDStorageQueueX (slots 0..9) */
    scarlett_hresult_t (SCARLETT_DSTORAGEXS_CALL *QueryInterface)(
        IDStorageQueueX *self, const scarlett_guid_t *riid, void **ppvObject);
    uint32_t (SCARLETT_DSTORAGEXS_CALL *AddRef)(IDStorageQueueX *self);
    uint32_t (SCARLETT_DSTORAGEXS_CALL *Release)(IDStorageQueueX *self);
    void (SCARLETT_DSTORAGEXS_CALL *EnqueueRequest)(
        IDStorageQueueX *self, const DSTORAGE_REQUEST *request);
    void (SCARLETT_DSTORAGEXS_CALL *EnqueueStatus)(
        IDStorageQueueX *self, IDStorageStatusArrayX *statusArray, uint32_t index);
    void (SCARLETT_DSTORAGEXS_CALL *EnqueueSignal)(
        IDStorageQueueX *self, void *fence, uint64_t value);
    void (SCARLETT_DSTORAGEXS_CALL *Submit)(IDStorageQueueX *self);
    void (SCARLETT_DSTORAGEXS_CALL *CancelRequestsWithTag)(
        IDStorageQueueX *self, uint64_t mask, uint64_t value);
    void (SCARLETT_DSTORAGEXS_CALL *Close)(IDStorageQueueX *self);
    scarlett_hresult_t (SCARLETT_DSTORAGEXS_CALL *Query)(
        IDStorageQueueX *self, DSTORAGE_QUEUE_INFO *info);
    /* IDStorageQueueX1 (slot 10) */
    void (SCARLETT_DSTORAGEXS_CALL *EnqueueSetEvent)(
        IDStorageQueueX1 *self, void *eventHandle);
    /* IDStorageQueueX2 (slots 11..12) */
    void (SCARLETT_DSTORAGEXS_CALL *RetrieveErrorRecord)(
        IDStorageQueueX2 *self, DSTORAGE_ERROR_RECORD *record);
    uint32_t (SCARLETT_DSTORAGEXS_CALL *GetCompressionSupport)(
        IDStorageQueueX2 *self, uint32_t format);
} IDStorageQueueX2Vtbl;

typedef IDStorageQueueX2Vtbl IDStorageQueueXVtbl;
typedef IDStorageQueueX2Vtbl IDStorageQueueX1Vtbl;

struct IDStorageQueueX_S {
    const IDStorageQueueX2Vtbl *lpVtbl;
    uint64_t unix_queue_handle;
    uint32_t capacity;
    uint32_t priority;
    uint64_t source_type;
    uint32_t ref_count;
    uint32_t in_use;
};

struct IDStorageQueueX1_S {
    const IDStorageQueueX2Vtbl *lpVtbl;
    uint64_t unix_queue_handle;
    uint32_t capacity;
    uint32_t priority;
    uint64_t source_type;
    uint32_t ref_count;
    uint32_t in_use;
};

struct IDStorageQueueX2_S {
    const IDStorageQueueX2Vtbl *lpVtbl;
    uint64_t unix_queue_handle;
    uint32_t capacity;
    uint32_t priority;
    uint64_t source_type;
    uint32_t ref_count;
    uint32_t in_use;
};

/* ============================================================================
 * IDStorageFactoryX / X1 / X2 (Strict VTable Prefix Inheritance)
 * ============================================================================ */

typedef struct IDStorageFactoryX2Vtbl_S {
    /* IDStorageFactoryX (slots 0..6) */
    scarlett_hresult_t (SCARLETT_DSTORAGEXS_CALL *QueryInterface)(
        IDStorageFactoryX *self, const scarlett_guid_t *riid, void **ppvObject);
    uint32_t (SCARLETT_DSTORAGEXS_CALL *AddRef)(IDStorageFactoryX *self);
    uint32_t (SCARLETT_DSTORAGEXS_CALL *Release)(IDStorageFactoryX *self);
    scarlett_hresult_t (SCARLETT_DSTORAGEXS_CALL *CreateQueue)(
        IDStorageFactoryX *self,
        const DSTORAGE_QUEUE_DESC *desc,
        const scarlett_guid_t *riid,
        void **ppv);
    scarlett_hresult_t (SCARLETT_DSTORAGEXS_CALL *OpenFile)(
        IDStorageFactoryX *self,
        const uint16_t *path,
        const scarlett_guid_t *riid,
        void **ppv);
    scarlett_hresult_t (SCARLETT_DSTORAGEXS_CALL *CreateStatusArray)(
        IDStorageFactoryX *self,
        uint32_t capacity,
        const char *name,
        const scarlett_guid_t *riid,
        void **ppv);
    scarlett_hresult_t (SCARLETT_DSTORAGEXS_CALL *SetStagingBufferSize)(
        IDStorageFactoryX *self,
        uint32_t size);
    /* IDStorageFactoryX1 (slots 7..8) */
    scarlett_hresult_t (SCARLETT_DSTORAGEXS_CALL *OpenFileUtf8)(
        IDStorageFactoryX1 *self,
        const char *path_utf8,
        const scarlett_guid_t *riid,
        void **ppv);
    uint64_t (SCARLETT_DSTORAGEXS_CALL *GetStagingBufferSize)(
        IDStorageFactoryX1 *self);
    /* IDStorageFactoryX2 (slots 9..10) */
    scarlett_hresult_t (SCARLETT_DSTORAGEXS_CALL *QueryCapabilities)(
        IDStorageFactoryX2 *self,
        scarlett_dstorage_caps_params_t *out_caps);
    scarlett_hresult_t (SCARLETT_DSTORAGEXS_CALL *LoadBcpackPlugin)(
        IDStorageFactoryX2 *self,
        const char *plugin_path_utf8,
        uint32_t *out_plugin_abi_version);
} IDStorageFactoryX2Vtbl;

typedef IDStorageFactoryX2Vtbl IDStorageFactoryXVtbl;
typedef IDStorageFactoryX2Vtbl IDStorageFactoryX1Vtbl;

struct IDStorageFactoryX_S {
    const IDStorageFactoryX2Vtbl *lpVtbl;
    uint32_t ref_count;
    uint32_t initialized;
};

struct IDStorageFactoryX1_S {
    const IDStorageFactoryX2Vtbl *lpVtbl;
    uint32_t ref_count;
    uint32_t initialized;
};

struct IDStorageFactoryX2_S {
    const IDStorageFactoryX2Vtbl *lpVtbl;
    uint32_t ref_count;
    uint32_t initialized;
};

/* ============================================================================
 * Host Unixlib Bridge Hooks (dstorage_xs_unix.so)
 * ============================================================================ */

#if !defined(_WIN32) && !defined(__MINGW32__)
SCARLETT_DSTORAGEXS_API int32_t scarlett_dstorage_unix_call(uint32_t opcode, void *params);
SCARLETT_DSTORAGEXS_API void scarlett_dstorage_set_unix_call_bridge(scarlett_unix_call_fn_t fn);
SCARLETT_DSTORAGEXS_API scarlett_unix_call_fn_t scarlett_dstorage_get_unix_call_bridge(void);
SCARLETT_DSTORAGEXS_API void scarlett_dstorage_set_stub_sink(
    scarlett_stub_sink_fn_t sink,
    void *user_data);
SCARLETT_DSTORAGEXS_API void scarlett_dstorage_reset_runtime(void);
#endif

/* ============================================================================
 * PE64 Exported Functions (dstorage_xs.dll)
 * ============================================================================ */

SCARLETT_DSTORAGEXS_API scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL DStorageGetFactory(
    const scarlett_guid_t *riid,
    void **ppv);

/* Marked [unimplemented] in dstorage_xs.xdl: routed strictly to diagnostic auto-stub thunk */
SCARLETT_DSTORAGEXS_API scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL DStorageSetConfiguration(
    const void *configuration);

/* Unimplemented codec factory export: routed strictly to diagnostic auto-stub thunk */
SCARLETT_DSTORAGEXS_API scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL DStorageCreateCompressionCodec(
    uint32_t format,
    uint32_t numThreads,
    const scarlett_guid_t *riid,
    void **ppv);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SCARLETT_DSTORAGE_XS_API_H */
