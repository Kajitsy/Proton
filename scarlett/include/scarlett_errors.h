/*
 * Proton-Scarlett — Common Error & HRESULT Contract
 *
 * Clean-room, freestanding C11 / C++20 header shared identically across:
 *   - PE64 DLLs (x86_64-windows, Microsoft x64 ABI)
 *   - Wine unixlib ELF64 modules (x86_64-unix, System V AMD64 ABI)
 *   - Host C11/C++20 unit tests
 *
 * Strictly zero dependencies on <windows.h>, libc, libvulkan, or liburing.
 */

#ifndef SCARLETT_ERRORS_H
#define SCARLETT_ERRORS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SCARLETT_STATIC_ASSERT
#ifdef __cplusplus
#define SCARLETT_STATIC_ASSERT(cond, msg) static_assert((cond), msg)
#else
#define SCARLETT_STATIC_ASSERT(cond, msg) _Static_assert((cond), msg)
#endif
#endif

/*
 * Fixed-width Win32/GDK HRESULT representation.
 * Explicitly int32_t (never `long`) so PE64 (sizeof(long) == 4) and
 * SysV AMD64 Linux (sizeof(long) == 8) share identical ABI layout.
 */
typedef int32_t scarlett_hresult_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_hresult_t) == 4,
                       "scarlett_hresult_t must be exactly 32 bits");

#define SCARLETT_HRESULT_C(u32_val) ((scarlett_hresult_t)(uint32_t)(u32_val))

/* Customer-defined HRESULT builder: Severity=1, Customer=1, Facility=0x05C */
#define SCARLETT_FACILITY_SCARLETT UINT32_C(0x005C)
#define SCARLETT_MAKE_ERR(code)                                                \
    SCARLETT_HRESULT_C(UINT32_C(0xA0000000) |                                  \
                       (SCARLETT_FACILITY_SCARLETT << 16) |                    \
                       ((uint32_t)(code) & UINT32_C(0xFFFF)))

static inline bool scarlett_succeeded(scarlett_hresult_t hr) {
    return hr >= 0;
}

static inline bool scarlett_failed(scarlett_hresult_t hr) {
    return hr < 0;
}

/* ============================================================================
 * Standard Baseline HRESULT Values (prefixed to avoid <winerror.h> clashes)
 * ============================================================================ */

#define SCARLETT_OK                        SCARLETT_HRESULT_C(0x00000000u)
#define SCARLETT_S_FALSE                   SCARLETT_HRESULT_C(0x00000001u)
#define SCARLETT_E_NOTIMPL                 SCARLETT_HRESULT_C(0x80004001u)
#define SCARLETT_E_NOINTERFACE             SCARLETT_HRESULT_C(0x80004002u)
#define SCARLETT_E_POINTER                 SCARLETT_HRESULT_C(0x80004003u)
#define SCARLETT_E_ABORT                   SCARLETT_HRESULT_C(0x80004004u)
#define SCARLETT_E_FAIL                    SCARLETT_HRESULT_C(0x80004005u)
#define SCARLETT_E_UNEXPECTED              SCARLETT_HRESULT_C(0x8000FFFFu)
#define SCARLETT_E_ACCESSDENIED            SCARLETT_HRESULT_C(0x80070005u)
#define SCARLETT_E_HANDLE                  SCARLETT_HRESULT_C(0x80070006u)
#define SCARLETT_E_OUTOFMEMORY             SCARLETT_HRESULT_C(0x8007000Eu)
#define SCARLETT_E_INVALIDARG              SCARLETT_HRESULT_C(0x80070057u)
#define SCARLETT_E_PENDING                 SCARLETT_HRESULT_C(0x8000000Au)

/* ============================================================================
 * Core, Clean-Room, Loader/VFS & Stub Thunk Errors (0xA05C0001..0xA05C00FF)
 * ============================================================================ */

/* Mandatory Clean-Room refusal on encrypted XVC/XVD packages (zero DRM bypass) */
#define SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED  SCARLETT_MAKE_ERR(0x0001u)
#define SCARLETT_ERR_INVALID_PACKAGE_HEADER         SCARLETT_MAKE_ERR(0x0002u)
#define SCARLETT_ERR_UNIMPLEMENTED_STUB             SCARLETT_MAKE_ERR(0x0003u)
#define SCARLETT_ERR_STRICT_STUB_ABORT              SCARLETT_MAKE_ERR(0x0004u)
#define SCARLETT_ERR_UNIX_BRIDGE_UNAVAILABLE        SCARLETT_MAKE_ERR(0x0005u)
#define SCARLETT_ERR_UNIX_CALL_INVALID_OPCODE       SCARLETT_MAKE_ERR(0x0006u)
#define SCARLETT_ERR_ABI_VERSION_MISMATCH           SCARLETT_MAKE_ERR(0x0007u)
#define SCARLETT_ERR_VFS_OVERLAY_FAILED             SCARLETT_MAKE_ERR(0x0008u)
#define SCARLETT_ERR_VFS_READONLY_VIOLATION         SCARLETT_MAKE_ERR(0x0009u)
#define SCARLETT_ERR_CONFIG_PARSE_FAILED            SCARLETT_MAKE_ERR(0x000Au)

/* ============================================================================
 * XMem / Physical Memory / Aliasing / UMA Bridge Errors (0xA05C0101..0xA05C01FF)
 * ============================================================================ */

/* Conflicting cache attributes across aliased mappings of the same physical page */
#define SCARLETT_ERR_XMEM_CACHE_CONFLICT            SCARLETT_MAKE_ERR(0x0101u)
/* Map/unmap range crossing distinct allocation boundaries */
#define SCARLETT_ERR_XMEM_CROSS_ALLOCATION          SCARLETT_MAKE_ERR(0x0102u)
/* Virtual address outside MEM_GRAPHICS or MEM_TITLE windows */
#define SCARLETT_ERR_XMEM_OUT_OF_RANGE              SCARLETT_MAKE_ERR(0x0103u)
/* Console memory profile budget (Anaconda / Lockhart / DevKit) exceeded */
#define SCARLETT_ERR_XMEM_BUDGET_EXCEEDED           SCARLETT_MAKE_ERR(0x0104u)
/* Attempt to free physical pages while virtual alias mappings remain active */
#define SCARLETT_ERR_XMEM_PAGES_STILL_MAPPED        SCARLETT_MAKE_ERR(0x0105u)
/* Alignment mismatch for 4KB / 64KB / 2MB / 4MB page granularity */
#define SCARLETT_ERR_XMEM_INVALID_ALIGNMENT         SCARLETT_MAKE_ERR(0x0106u)
/* Tier A/B/C UMA bridge initialization or export failure */
#define SCARLETT_ERR_XMEM_UMA_BRIDGE_FAILED         SCARLETT_MAKE_ERR(0x0107u)
/* Tier C userfaultfd UFFDIO_WRITEPROTECT fault or flush failure */
#define SCARLETT_ERR_XMEM_UFFD_FAULT                SCARLETT_MAKE_ERR(0x0108u)

/* ============================================================================
 * RDNA2 Lifter, D3D12_XS & XG_XS Errors (0xA05C0201..0xA05C02FF)
 * ============================================================================ */

#define SCARLETT_ERR_SHADER_INVALID_CONTAINER       SCARLETT_MAKE_ERR(0x0201u)
#define SCARLETT_ERR_SHADER_UNSUPPORTED_ISA         SCARLETT_MAKE_ERR(0x0202u)
#define SCARLETT_ERR_SHADER_SPIRV_INVALID           SCARLETT_MAKE_ERR(0x0203u)
#define SCARLETT_ERR_D3D12_DESCRIPTOR_LIMIT         SCARLETT_MAKE_ERR(0x0204u)
#define SCARLETT_ERR_D3D12_UNSUPPORTED_QUEUE        SCARLETT_MAKE_ERR(0x0205u)
#define SCARLETT_ERR_XG_INVALID_SWIZZLE_MODE        SCARLETT_MAKE_ERR(0x0206u)
#define SCARLETT_ERR_XG_SURFACE_OVERFLOW            SCARLETT_MAKE_ERR(0x0207u)

/* ============================================================================
 * DirectStorage_XS Errors (Official 0x8924xxxx + Scarlett 0xA05C03xx)
 * ============================================================================ */

#define SCARLETT_E_DSTORAGE_ALREADY_RUNNING                SCARLETT_HRESULT_C(0x89240001u)
#define SCARLETT_E_DSTORAGE_INVALID_QUEUE_CAPACITY         SCARLETT_HRESULT_C(0x89240003u)
#define SCARLETT_E_DSTORAGE_END_OF_FILE                    SCARLETT_HRESULT_C(0x89240007u)
#define SCARLETT_E_DSTORAGE_ACCESS_VIOLATION               SCARLETT_HRESULT_C(0x89240009u)
#define SCARLETT_E_DSTORAGE_FILE_NOT_OPEN                  SCARLETT_HRESULT_C(0x8924000Bu)
#define SCARLETT_E_DSTORAGE_INVALID_DESTINATION_SIZE       SCARLETT_HRESULT_C(0x8924000Fu)
#define SCARLETT_E_DSTORAGE_INDEX_BOUND                    SCARLETT_HRESULT_C(0x89240015u)
#define SCARLETT_E_DSTORAGE_INVALID_FILE_HANDLE            SCARLETT_HRESULT_C(0x89240017u)
#define SCARLETT_E_DSTORAGE_INVALID_FILE_OFFSET            SCARLETT_HRESULT_C(0x8924001Au)
#define SCARLETT_E_DSTORAGE_INVALID_FENCE                  SCARLETT_HRESULT_C(0x89240022u)
#define SCARLETT_E_DSTORAGE_INVALID_MEMORY_QUEUE_PRIORITY  SCARLETT_HRESULT_C(0x89240024u)
#define SCARLETT_E_DSTORAGE_DECOMPRESSION_ERROR            SCARLETT_HRESULT_C(0x89240030u)
#define SCARLETT_E_DSTORAGE_COMPRESSED_DATA_TOO_LARGE      SCARLETT_HRESULT_C(0x89240039u)

#ifndef E_DSTORAGE_ALREADY_RUNNING
#define E_DSTORAGE_ALREADY_RUNNING                SCARLETT_E_DSTORAGE_ALREADY_RUNNING
#endif
#ifndef E_DSTORAGE_INVALID_QUEUE_CAPACITY
#define E_DSTORAGE_INVALID_QUEUE_CAPACITY         SCARLETT_E_DSTORAGE_INVALID_QUEUE_CAPACITY
#endif
#ifndef E_DSTORAGE_END_OF_FILE
#define E_DSTORAGE_END_OF_FILE                    SCARLETT_E_DSTORAGE_END_OF_FILE
#endif
#ifndef E_DSTORAGE_ACCESS_VIOLATION
#define E_DSTORAGE_ACCESS_VIOLATION               SCARLETT_E_DSTORAGE_ACCESS_VIOLATION
#endif
#ifndef E_DSTORAGE_FILE_NOT_OPEN
#define E_DSTORAGE_FILE_NOT_OPEN                  SCARLETT_E_DSTORAGE_FILE_NOT_OPEN
#endif
#ifndef E_DSTORAGE_INVALID_DESTINATION_SIZE
#define E_DSTORAGE_INVALID_DESTINATION_SIZE       SCARLETT_E_DSTORAGE_INVALID_DESTINATION_SIZE
#endif
#ifndef E_DSTORAGE_INDEX_BOUND
#define E_DSTORAGE_INDEX_BOUND                    SCARLETT_E_DSTORAGE_INDEX_BOUND
#endif
#ifndef E_DSTORAGE_INVALID_FILE_HANDLE
#define E_DSTORAGE_INVALID_FILE_HANDLE            SCARLETT_E_DSTORAGE_INVALID_FILE_HANDLE
#endif
#ifndef E_DSTORAGE_INVALID_FILE_OFFSET
#define E_DSTORAGE_INVALID_FILE_OFFSET            SCARLETT_E_DSTORAGE_INVALID_FILE_OFFSET
#endif
#ifndef E_DSTORAGE_INVALID_FENCE
#define E_DSTORAGE_INVALID_FENCE                  SCARLETT_E_DSTORAGE_INVALID_FENCE
#endif
#ifndef E_DSTORAGE_INVALID_MEMORY_QUEUE_PRIORITY
#define E_DSTORAGE_INVALID_MEMORY_QUEUE_PRIORITY  SCARLETT_E_DSTORAGE_INVALID_MEMORY_QUEUE_PRIORITY
#endif
#ifndef E_DSTORAGE_DECOMPRESSION_ERROR
#define E_DSTORAGE_DECOMPRESSION_ERROR            SCARLETT_E_DSTORAGE_DECOMPRESSION_ERROR
#endif
#ifndef E_DSTORAGE_COMPRESSED_DATA_TOO_LARGE
#define E_DSTORAGE_COMPRESSED_DATA_TOO_LARGE      SCARLETT_E_DSTORAGE_COMPRESSED_DATA_TOO_LARGE
#endif

#define SCARLETT_ERR_DSTORAGE_INVALID_OPTIONS       SCARLETT_MAKE_ERR(0x0301u)
#define SCARLETT_ERR_DSTORAGE_BCPACK_PLUGIN_MISSING SCARLETT_MAKE_ERR(0x0302u)
#define SCARLETT_ERR_DSTORAGE_BCPACK_ABI_MISMATCH   SCARLETT_MAKE_ERR(0x0303u)
#define SCARLETT_ERR_DSTORAGE_BCPACK_DECODE_FAILED  SCARLETT_MAKE_ERR(0x0304u)
#define SCARLETT_ERR_DSTORAGE_IO_URING_FAILED       SCARLETT_MAKE_ERR(0x0305u)
#define SCARLETT_ERR_DSTORAGE_SWIZZLE_FAILED        SCARLETT_MAKE_ERR(0x0306u)

/* ============================================================================
 * XGameSave Official GDK Error Codes (15 codes: 0x80830001..0x8083000F)
 * ============================================================================ */

#define SCARLETT_E_GS_INVALID_CONTAINER_NAME        SCARLETT_HRESULT_C(0x80830001u)
#define SCARLETT_E_GS_NO_ACCESS                     SCARLETT_HRESULT_C(0x80830002u)
#define SCARLETT_E_GS_OUT_OF_LOCAL_STORAGE          SCARLETT_HRESULT_C(0x80830003u)
#define SCARLETT_E_GS_USER_CANCELED                 SCARLETT_HRESULT_C(0x80830004u)
#define SCARLETT_E_GS_UPDATE_TOO_BIG                SCARLETT_HRESULT_C(0x80830005u)
#define SCARLETT_E_GS_QUOTA_EXCEEDED                SCARLETT_HRESULT_C(0x80830006u)
#define SCARLETT_E_GS_PROVIDED_BUFFER_TOO_SMALL     SCARLETT_HRESULT_C(0x80830007u)
#define SCARLETT_E_GS_BLOB_NOT_FOUND                SCARLETT_HRESULT_C(0x80830008u)
#define SCARLETT_E_GS_NO_SERVICE_CONFIGURATION      SCARLETT_HRESULT_C(0x80830009u)
#define SCARLETT_E_GS_CONTAINER_NOT_IN_SYNC         SCARLETT_HRESULT_C(0x8083000Au)
#define SCARLETT_E_GS_CONTAINER_SYNC_FAILED         SCARLETT_HRESULT_C(0x8083000Bu)
#define SCARLETT_E_GS_USER_NOT_REGISTERED_IN_SERVICE SCARLETT_HRESULT_C(0x8083000Cu)
#define SCARLETT_E_GS_HANDLE_EXPIRED                SCARLETT_HRESULT_C(0x8083000Du)
#define SCARLETT_E_GS_ASYNC_FUNCTION_REQUIRED       SCARLETT_HRESULT_C(0x8083000Eu)
#define SCARLETT_E_GS_PROVIDER_MISMATCH             SCARLETT_HRESULT_C(0x8083000Fu)

#ifndef E_GS_INVALID_CONTAINER_NAME
#define E_GS_INVALID_CONTAINER_NAME          SCARLETT_E_GS_INVALID_CONTAINER_NAME
#endif
#ifndef E_GS_NO_ACCESS
#define E_GS_NO_ACCESS                       SCARLETT_E_GS_NO_ACCESS
#endif
#ifndef E_GS_OUT_OF_LOCAL_STORAGE
#define E_GS_OUT_OF_LOCAL_STORAGE            SCARLETT_E_GS_OUT_OF_LOCAL_STORAGE
#endif
#ifndef E_GS_USER_CANCELED
#define E_GS_USER_CANCELED                   SCARLETT_E_GS_USER_CANCELED
#endif
#ifndef E_GS_UPDATE_TOO_BIG
#define E_GS_UPDATE_TOO_BIG                  SCARLETT_E_GS_UPDATE_TOO_BIG
#endif
#ifndef E_GS_QUOTA_EXCEEDED
#define E_GS_QUOTA_EXCEEDED                  SCARLETT_E_GS_QUOTA_EXCEEDED
#endif
#ifndef E_GS_PROVIDED_BUFFER_TOO_SMALL
#define E_GS_PROVIDED_BUFFER_TOO_SMALL       SCARLETT_E_GS_PROVIDED_BUFFER_TOO_SMALL
#endif
#ifndef E_GS_BLOB_NOT_FOUND
#define E_GS_BLOB_NOT_FOUND                  SCARLETT_E_GS_BLOB_NOT_FOUND
#endif
#ifndef E_GS_NO_SERVICE_CONFIGURATION
#define E_GS_NO_SERVICE_CONFIGURATION        SCARLETT_E_GS_NO_SERVICE_CONFIGURATION
#endif
#ifndef E_GS_CONTAINER_NOT_IN_SYNC
#define E_GS_CONTAINER_NOT_IN_SYNC           SCARLETT_E_GS_CONTAINER_NOT_IN_SYNC
#endif
#ifndef E_GS_CONTAINER_SYNC_FAILED
#define E_GS_CONTAINER_SYNC_FAILED           SCARLETT_E_GS_CONTAINER_SYNC_FAILED
#endif
#ifndef E_GS_USER_NOT_REGISTERED_IN_SERVICE
#define E_GS_USER_NOT_REGISTERED_IN_SERVICE  SCARLETT_E_GS_USER_NOT_REGISTERED_IN_SERVICE
#endif
#ifndef E_GS_HANDLE_EXPIRED
#define E_GS_HANDLE_EXPIRED                  SCARLETT_E_GS_HANDLE_EXPIRED
#endif
#ifndef E_GS_ASYNC_FUNCTION_REQUIRED
#define E_GS_ASYNC_FUNCTION_REQUIRED         SCARLETT_E_GS_ASYNC_FUNCTION_REQUIRED
#endif
#ifndef E_GS_PROVIDER_MISMATCH
#define E_GS_PROVIDER_MISMATCH               SCARLETT_E_GS_PROVIDER_MISMATCH
#endif

/* ============================================================================
 * Network & Runtime Policy Errors (0xA05C0401..0xA05C04FF)
 * ============================================================================ */

#define SCARLETT_ERR_NET_OFFLINE_BLOCKED            SCARLETT_MAKE_ERR(0x0401u)
#define SCARLETT_ERR_NET_LAN_POLICY_BLOCKED         SCARLETT_MAKE_ERR(0x0402u)
#define SCARLETT_ERR_NET_TLS_CERT_UNAVAILABLE       SCARLETT_MAKE_ERR(0x0403u)

/* ============================================================================
 * Diagnostic Symbolic Lookup (Zero External Dependencies)
 * ============================================================================ */

static inline const char *scarlett_error_name(scarlett_hresult_t hr) {
    switch (hr) {
        case SCARLETT_OK:                                return "SCARLETT_OK";
        case SCARLETT_S_FALSE:                           return "SCARLETT_S_FALSE";
        case SCARLETT_E_NOTIMPL:                         return "E_NOTIMPL";
        case SCARLETT_E_NOINTERFACE:                     return "E_NOINTERFACE";
        case SCARLETT_E_POINTER:                         return "E_POINTER";
        case SCARLETT_E_ABORT:                           return "E_ABORT";
        case SCARLETT_E_FAIL:                            return "E_FAIL";
        case SCARLETT_E_UNEXPECTED:                      return "E_UNEXPECTED";
        case SCARLETT_E_ACCESSDENIED:                    return "E_ACCESSDENIED";
        case SCARLETT_E_HANDLE:                          return "E_HANDLE";
        case SCARLETT_E_OUTOFMEMORY:                     return "E_OUTOFMEMORY";
        case SCARLETT_E_INVALIDARG:                      return "E_INVALIDARG";
        case SCARLETT_E_PENDING:                         return "E_PENDING";

        case SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED: return "SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED";
        case SCARLETT_ERR_INVALID_PACKAGE_HEADER:        return "SCARLETT_ERR_INVALID_PACKAGE_HEADER";
        case SCARLETT_ERR_UNIMPLEMENTED_STUB:            return "SCARLETT_ERR_UNIMPLEMENTED_STUB";
        case SCARLETT_ERR_STRICT_STUB_ABORT:             return "SCARLETT_ERR_STRICT_STUB_ABORT";
        case SCARLETT_ERR_UNIX_BRIDGE_UNAVAILABLE:       return "SCARLETT_ERR_UNIX_BRIDGE_UNAVAILABLE";
        case SCARLETT_ERR_UNIX_CALL_INVALID_OPCODE:      return "SCARLETT_ERR_UNIX_CALL_INVALID_OPCODE";
        case SCARLETT_ERR_ABI_VERSION_MISMATCH:          return "SCARLETT_ERR_ABI_VERSION_MISMATCH";
        case SCARLETT_ERR_VFS_OVERLAY_FAILED:            return "SCARLETT_ERR_VFS_OVERLAY_FAILED";
        case SCARLETT_ERR_VFS_READONLY_VIOLATION:        return "SCARLETT_ERR_VFS_READONLY_VIOLATION";
        case SCARLETT_ERR_CONFIG_PARSE_FAILED:           return "SCARLETT_ERR_CONFIG_PARSE_FAILED";

        case SCARLETT_ERR_XMEM_CACHE_CONFLICT:           return "SCARLETT_ERR_XMEM_CACHE_CONFLICT";
        case SCARLETT_ERR_XMEM_CROSS_ALLOCATION:         return "SCARLETT_ERR_XMEM_CROSS_ALLOCATION";
        case SCARLETT_ERR_XMEM_OUT_OF_RANGE:             return "SCARLETT_ERR_XMEM_OUT_OF_RANGE";
        case SCARLETT_ERR_XMEM_BUDGET_EXCEEDED:          return "SCARLETT_ERR_XMEM_BUDGET_EXCEEDED";
        case SCARLETT_ERR_XMEM_PAGES_STILL_MAPPED:       return "SCARLETT_ERR_XMEM_PAGES_STILL_MAPPED";
        case SCARLETT_ERR_XMEM_INVALID_ALIGNMENT:        return "SCARLETT_ERR_XMEM_INVALID_ALIGNMENT";
        case SCARLETT_ERR_XMEM_UMA_BRIDGE_FAILED:        return "SCARLETT_ERR_XMEM_UMA_BRIDGE_FAILED";
        case SCARLETT_ERR_XMEM_UFFD_FAULT:               return "SCARLETT_ERR_XMEM_UFFD_FAULT";

        case SCARLETT_ERR_SHADER_INVALID_CONTAINER:      return "SCARLETT_ERR_SHADER_INVALID_CONTAINER";
        case SCARLETT_ERR_SHADER_UNSUPPORTED_ISA:        return "SCARLETT_ERR_SHADER_UNSUPPORTED_ISA";
        case SCARLETT_ERR_SHADER_SPIRV_INVALID:          return "SCARLETT_ERR_SHADER_SPIRV_INVALID";
        case SCARLETT_ERR_D3D12_DESCRIPTOR_LIMIT:        return "SCARLETT_ERR_D3D12_DESCRIPTOR_LIMIT";
        case SCARLETT_ERR_D3D12_UNSUPPORTED_QUEUE:       return "SCARLETT_ERR_D3D12_UNSUPPORTED_QUEUE";
        case SCARLETT_ERR_XG_INVALID_SWIZZLE_MODE:       return "SCARLETT_ERR_XG_INVALID_SWIZZLE_MODE";
        case SCARLETT_ERR_XG_SURFACE_OVERFLOW:           return "SCARLETT_ERR_XG_SURFACE_OVERFLOW";

        case SCARLETT_E_DSTORAGE_ALREADY_RUNNING:               return "E_DSTORAGE_ALREADY_RUNNING";
        case SCARLETT_E_DSTORAGE_INVALID_QUEUE_CAPACITY:        return "E_DSTORAGE_INVALID_QUEUE_CAPACITY";
        case SCARLETT_E_DSTORAGE_END_OF_FILE:                   return "E_DSTORAGE_END_OF_FILE";
        case SCARLETT_E_DSTORAGE_ACCESS_VIOLATION:              return "E_DSTORAGE_ACCESS_VIOLATION";
        case SCARLETT_E_DSTORAGE_FILE_NOT_OPEN:                 return "E_DSTORAGE_FILE_NOT_OPEN";
        case SCARLETT_E_DSTORAGE_INVALID_DESTINATION_SIZE:      return "E_DSTORAGE_INVALID_DESTINATION_SIZE";
        case SCARLETT_E_DSTORAGE_INDEX_BOUND:                   return "E_DSTORAGE_INDEX_BOUND";
        case SCARLETT_E_DSTORAGE_INVALID_FILE_HANDLE:           return "E_DSTORAGE_INVALID_FILE_HANDLE";
        case SCARLETT_E_DSTORAGE_INVALID_FILE_OFFSET:           return "E_DSTORAGE_INVALID_FILE_OFFSET";
        case SCARLETT_E_DSTORAGE_INVALID_FENCE:                 return "E_DSTORAGE_INVALID_FENCE";
        case SCARLETT_E_DSTORAGE_INVALID_MEMORY_QUEUE_PRIORITY: return "E_DSTORAGE_INVALID_MEMORY_QUEUE_PRIORITY";
        case SCARLETT_E_DSTORAGE_DECOMPRESSION_ERROR:           return "E_DSTORAGE_DECOMPRESSION_ERROR";
        case SCARLETT_E_DSTORAGE_COMPRESSED_DATA_TOO_LARGE:     return "E_DSTORAGE_COMPRESSED_DATA_TOO_LARGE";

        case SCARLETT_ERR_DSTORAGE_INVALID_OPTIONS:       return "SCARLETT_ERR_DSTORAGE_INVALID_OPTIONS";
        case SCARLETT_ERR_DSTORAGE_BCPACK_PLUGIN_MISSING: return "SCARLETT_ERR_DSTORAGE_BCPACK_PLUGIN_MISSING";
        case SCARLETT_ERR_DSTORAGE_BCPACK_ABI_MISMATCH:   return "SCARLETT_ERR_DSTORAGE_BCPACK_ABI_MISMATCH";
        case SCARLETT_ERR_DSTORAGE_BCPACK_DECODE_FAILED:  return "SCARLETT_ERR_DSTORAGE_BCPACK_DECODE_FAILED";
        case SCARLETT_ERR_DSTORAGE_IO_URING_FAILED:       return "SCARLETT_ERR_DSTORAGE_IO_URING_FAILED";
        case SCARLETT_ERR_DSTORAGE_SWIZZLE_FAILED:        return "SCARLETT_ERR_DSTORAGE_SWIZZLE_FAILED";

        case SCARLETT_E_GS_INVALID_CONTAINER_NAME:         return "E_GS_INVALID_CONTAINER_NAME";
        case SCARLETT_E_GS_NO_ACCESS:                      return "E_GS_NO_ACCESS";
        case SCARLETT_E_GS_OUT_OF_LOCAL_STORAGE:           return "E_GS_OUT_OF_LOCAL_STORAGE";
        case SCARLETT_E_GS_USER_CANCELED:                  return "E_GS_USER_CANCELED";
        case SCARLETT_E_GS_UPDATE_TOO_BIG:                 return "E_GS_UPDATE_TOO_BIG";
        case SCARLETT_E_GS_QUOTA_EXCEEDED:                 return "E_GS_QUOTA_EXCEEDED";
        case SCARLETT_E_GS_PROVIDED_BUFFER_TOO_SMALL:      return "E_GS_PROVIDED_BUFFER_TOO_SMALL";
        case SCARLETT_E_GS_BLOB_NOT_FOUND:                 return "E_GS_BLOB_NOT_FOUND";
        case SCARLETT_E_GS_NO_SERVICE_CONFIGURATION:       return "E_GS_NO_SERVICE_CONFIGURATION";
        case SCARLETT_E_GS_CONTAINER_NOT_IN_SYNC:          return "E_GS_CONTAINER_NOT_IN_SYNC";
        case SCARLETT_E_GS_CONTAINER_SYNC_FAILED:          return "E_GS_CONTAINER_SYNC_FAILED";
        case SCARLETT_E_GS_USER_NOT_REGISTERED_IN_SERVICE: return "E_GS_USER_NOT_REGISTERED_IN_SERVICE";
        case SCARLETT_E_GS_HANDLE_EXPIRED:                 return "E_GS_HANDLE_EXPIRED";
        case SCARLETT_E_GS_ASYNC_FUNCTION_REQUIRED:        return "E_GS_ASYNC_FUNCTION_REQUIRED";
        case SCARLETT_E_GS_PROVIDER_MISMATCH:              return "E_GS_PROVIDER_MISMATCH";

        case SCARLETT_ERR_NET_OFFLINE_BLOCKED:             return "SCARLETT_ERR_NET_OFFLINE_BLOCKED";
        case SCARLETT_ERR_NET_LAN_POLICY_BLOCKED:          return "SCARLETT_ERR_NET_LAN_POLICY_BLOCKED";
        case SCARLETT_ERR_NET_TLS_CERT_UNAVAILABLE:        return "SCARLETT_ERR_NET_TLS_CERT_UNAVAILABLE";

        default:                                           return "SCARLETT_ERR_UNKNOWN";
    }
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SCARLETT_ERRORS_H */
