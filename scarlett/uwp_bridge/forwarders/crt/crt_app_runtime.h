/*
 * Proton-Scarlett — Clean-room CRT UWP Forwarder Runtime Support
 *
 * Shared freestanding PE64 helpers & clean-room WinRT/C++ runtime
 * implementations for the 7 MSVC UWP runtime forwarder DLLs:
 *   - VCRUNTIME140_APP.dll
 *   - VCRUNTIME140_1_APP.dll
 *   - MSVCP140_APP.dll
 *   - MSVCP140_1_APP.dll
 *   - MSVCP140_2_APP.dll
 *   - CONCRT140_APP.dll
 *   - vccorlib140_app.dll
 *
 * Strict PE64 isolation:
 *   - Under _WIN32: uses Win32 kernel32 APIs ONLY (zero POSIX/Linux calls,
 *     zero msvcrt.dll dependency).
 *   - Under host Linux: compiles cleanly for C11/CTest/Meson unit tests.
 */

#ifndef SCARLETT_CRT_APP_RUNTIME_H
#define SCARLETT_CRT_APP_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "scarlett_abi_common.h"
#include "scarlett_stub_thunk.h"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct crt_app_guid_s {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} crt_app_guid_t;

/*
 * WRL ModuleBase / CreatorMap clean-room ABI layout (standard Windows SDK WRL).
 */
typedef struct crt_wrl_factory_cache_s {
    void *factory; /* IUnknown* */
    union {
        uint32_t flags;
        void *cookie;
    } state;
} crt_wrl_factory_cache_t;

typedef struct crt_wrl_creator_map_s crt_wrl_creator_map_t;

typedef int32_t (
#ifdef _WIN32
    WINAPI
#endif
    *crt_wrl_factory_creator_fn_t)(
    unsigned int *flags,
    const crt_wrl_creator_map_t *entry,
    const crt_app_guid_t *riid,
    void **ppFactory);

typedef const wchar_t *(
#ifdef _WIN32
    WINAPI
#endif
    *crt_wrl_get_runtime_name_fn_t)(void);

struct crt_wrl_creator_map_s {
    crt_wrl_factory_creator_fn_t factory_creator;
    union {
        const crt_app_guid_t *clsid;
        crt_wrl_get_runtime_name_fn_t get_runtime_name;
    } activation_id;
    int (
#ifdef _WIN32
        WINAPI
#endif
        *get_trust_level)(void);
    crt_wrl_factory_cache_t *factory_cache;
    const wchar_t *server_name;
};

typedef struct crt_wrl_module_vtbl_s {
    void *(
#ifdef _WIN32
        __cdecl
#endif
        *dtor)(void *self, unsigned int flags);
    uint32_t (
#ifdef _WIN32
        __cdecl
#endif
        *increment_object_count)(void *self);
    uint32_t (
#ifdef _WIN32
        __cdecl
#endif
        *decrement_object_count)(void *self);
    uint32_t (
#ifdef _WIN32
        __cdecl
#endif
        *get_object_count)(void *self);
    const crt_wrl_creator_map_t **(
#ifdef _WIN32
        __cdecl
#endif
        *get_first_entry_pointer)(void *self);
    const crt_wrl_creator_map_t **(
#ifdef _WIN32
        __cdecl
#endif
        *get_mid_entry_pointer)(void *self);
    const crt_wrl_creator_map_t **(
#ifdef _WIN32
        __cdecl
#endif
        *get_last_entry_pointer)(void *self);
    void *(
#ifdef _WIN32
        __cdecl
#endif
        *get_lock)(void *self);
} crt_wrl_module_vtbl_t;

typedef struct crt_wrl_module_base_s {
    const crt_wrl_module_vtbl_t *vtbl;
} crt_wrl_module_base_t;

/* Freestanding memory zero & string helpers */
void crt_app_zero(void *dst, size_t len);
void crt_app_copy_cstr(char *dst, size_t cap, const char *src);

/* Diagnostic logging & Zero-Silent-Stubs trampoline dispatcher */
void crt_app_log(const char *channel, const char *fmt, ...);

scarlett_hresult_t crt_app_stub_dispatch(
    const scarlett_stub_descriptor_t *desc,
    uint64_t return_address,
    uint64_t rcx,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9);

/* Test hooks (no-op / safe in PE64 runtime too) */
void crt_app_stub_set_sink(scarlett_stub_sink_fn_t sink, void *user_data);
void crt_app_stub_get_last_context(scarlett_stub_context_t *out_ctx);
void crt_app_stub_set_strict_override(int strict_override);

/* Dynamic DLL symbol resolution (PE64: GetModuleHandleA/LoadLibraryA + GetProcAddress) */
void *crt_app_resolve_proc(const char *dll_name, const char *proc_name);

/* Clean-room math: C++17 std::cyl_bessel_i / cyl_bessel_if */
double crt_app_cyl_bessel_i(double nu, double x);
float  crt_app_cyl_bessel_if(float nu, float x);

/* Clean-room Concurrency helpers */
uint32_t crt_app_next_async_id(void);
void     crt_app_yield_thread(void);

/* Clean-room Platform::IntPtr / Windows::Foundation::Rect / Platform::Details helpers */
void   *crt_app_intptr_from_i32(void *self, int32_t value);
int32_t crt_app_intptr_to_i32(const void *self);
uint8_t crt_app_rect_contains(const float *rect_xywh, uint64_t point_packed);
void   *crt_app_platform_object_ctor(void *self);

void *crt_app_aligned_alloc(size_t size, size_t alignment);
void  crt_app_aligned_free(void *ptr);

typedef const wchar_t *(
#ifdef _WIN32
    WINAPI
#endif
    *crt_get_hstring_buf_fn_t)(void *hstr, uint32_t *out_len);

int32_t crt_app_wrl_get_activation_factory(
    crt_wrl_module_base_t *module,
    void *activatable_class_id,
    void **out_factory,
    crt_get_hstring_buf_fn_t get_raw_buf);

uint8_t crt_app_wrl_terminate_module(crt_wrl_module_base_t *module);

/*
 * Clean-room C++/CX box vtables:
 *   Platform::Details::GetIBoxVtable(void *object)
 *   Platform::Details::GetIBoxArrayVtable(void *object)
 *
 * The MSVC C++/CX compiler emits a small "box class" per boxed non-primitive
 * value type (enums, value structs) or boxed Platform::Array<T>.  The object
 * starts with the identity interface (Windows.Foundation.IReference<T> /
 * IReferenceArray<T>) and carries a tear-off IPropertyValue interface slot at
 * object + 8.  The IPropertyValue implementation is supplied by vccorlib
 * instead of by the compiler: GetIBoxVtable() / GetIBoxArrayVtable() return a
 * static IPropertyValue-compatible vtable whose IUnknown/IInspectable slots
 * delegate to the box identity object (this - 8).  Both helpers ignore their
 * argument in the Microsoft implementation (the compiler calls them from the
 * box constructor right after the interface slots are installed) and always
 * return the same static vtable.
 *
 * The returned tables have CRT_APP_IBOX_VTABLE_SLOT_COUNT (45) entries: the
 * full IInspectable + IPropertyValue method layout (QI, AddRef, Release,
 * GetIids, GetRuntimeClassName, GetTrustLevel, get_Type, get_IsNumericScalar,
 * 18 scalar getters, 19 array getters).
 */
#define CRT_APP_IBOX_VTABLE_SLOT_COUNT 45u

void *crt_app_ibox_vtable(void);
void *crt_app_ibox_array_vtable(void);

/* Raw vtable slot accessor for host tests / diagnostics (index >= 45 -> NULL). */
void *crt_app_ibox_vtable_slot(void *vtable, uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* SCARLETT_CRT_APP_RUNTIME_H */
