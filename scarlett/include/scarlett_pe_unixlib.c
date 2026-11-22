/*
 * Proton-Scarlett — Freestanding PE64 Wine 11.0 unixlib runtime bridge
 *
 * Provides __wine_unixlib_handle, __wine_unix_call_dispatcher, and
 * __wine_init_unix_call() when linking PE64 DLLs with mingw-w64 without
 * pulling in Wine's libwinecrt0 build dependency. Uses kernel32 + ntdll
 * exported routines (NtQueryVirtualMemory(..., MemoryWineUnixFuncs=1000)).
 */

#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32

#ifndef WINAPI
#define WINAPI __stdcall
#endif

typedef uint32_t NTSTATUS_PE;
typedef uint64_t unixlib_handle_t;
typedef void *HANDLE_PE;
typedef void *HMODULE_PE;

#define SCARLETT_STATUS_DLL_NOT_FOUND ((NTSTATUS_PE)0xC0000135U)
#define SCARLETT_MEMORY_WINE_UNIX_FUNCS 1000U

extern unsigned char __ImageBase[];

__declspec(dllimport) HMODULE_PE WINAPI GetModuleHandleA(const char *lpModuleName);
__declspec(dllimport) void *WINAPI GetProcAddress(HMODULE_PE hModule, const char *lpProcName);
__declspec(dllimport) HANDLE_PE WINAPI GetCurrentProcess(void);

typedef NTSTATUS_PE (WINAPI *pfn_NtQueryVirtualMemory_t)(
    HANDLE_PE ProcessHandle,
    const void *BaseAddress,
    unsigned int MemoryInformationClass,
    void *MemoryInformation,
    size_t MemoryInformationLength,
    size_t *ReturnLength);

typedef NTSTATUS_PE (WINAPI *pfn_wine_unix_dispatcher_t)(
    unixlib_handle_t handle,
    unsigned int code,
    void *args);

static NTSTATUS_PE WINAPI scarlett_unix_call_fallback(
    unixlib_handle_t handle,
    unsigned int code,
    void *args) {
    (void)handle;
    (void)code;
    (void)args;
    return SCARLETT_STATUS_DLL_NOT_FOUND;
}

static NTSTATUS_PE WINAPI scarlett_unix_call_init(
    unixlib_handle_t handle,
    unsigned int code,
    void *args);

unixlib_handle_t __wine_unixlib_handle = 0;
pfn_wine_unix_dispatcher_t __wine_unix_call_dispatcher = scarlett_unix_call_init;

static pfn_wine_unix_dispatcher_t resolve_ntdll_dispatcher(void) {
    HMODULE_PE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) {
        return scarlett_unix_call_fallback;
    }
    void **disp_ptr = (void **)GetProcAddress(ntdll, "__wine_unix_call_dispatcher");
    if (!disp_ptr || !*disp_ptr) {
        return scarlett_unix_call_fallback;
    }
    return (pfn_wine_unix_dispatcher_t)(*disp_ptr);
}

static NTSTATUS_PE WINAPI scarlett_unix_call_init(
    unixlib_handle_t handle,
    unsigned int code,
    void *args) {
    pfn_wine_unix_dispatcher_t resolved = resolve_ntdll_dispatcher();
    __atomic_store_n(&__wine_unix_call_dispatcher, resolved, __ATOMIC_RELEASE);
    return resolved(handle, code, args);
}

NTSTATUS_PE WINAPI __wine_init_unix_call(void) {
    HMODULE_PE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) {
        return SCARLETT_STATUS_DLL_NOT_FOUND;
    }
    pfn_NtQueryVirtualMemory_t pNtQueryVirtualMemory =
        (pfn_NtQueryVirtualMemory_t)GetProcAddress(ntdll, "NtQueryVirtualMemory");
    if (!pNtQueryVirtualMemory) {
        return SCARLETT_STATUS_DLL_NOT_FOUND;
    }
    return pNtQueryVirtualMemory(
        GetCurrentProcess(),
        (const void *)__ImageBase,
        SCARLETT_MEMORY_WINE_UNIX_FUNCS,
        &__wine_unixlib_handle,
        sizeof(__wine_unixlib_handle),
        NULL);
}

#endif /* _WIN32 */
