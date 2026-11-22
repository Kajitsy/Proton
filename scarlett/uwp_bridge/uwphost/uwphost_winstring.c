/*
 * Proton-Scarlett — UWPHOST.dll WinRT HSTRING & Ro* Runtime Exports
 *
 * Exports:
 *   - WindowsCreateString
 *   - WindowsCreateStringReference
 *   - WindowsGetStringRawBuffer
 *   - WindowsDeleteString
 *   - RoInitialize
 *   - RoUninitialize
 *   - RoGetActivationFactory
 *   - RoActivateInstance
 *
 * On _WIN32, delegates HSTRING operations to combase.dll when loaded so
 * HSTRING handles are 100% binary-interoperable across combase.dll,
 * api-ms-win-core-winrt-string-l1-1-0.dll, and UWPHOST.dll.
 * Falls back cleanly to a Wine-ABI-identical clean-room HSTRING layout.
 */

#include "uwphost_internal.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <objbase.h>
#endif

#define UWP_HSTRING_REFERENCE_FLAG 1u

struct uwp_hstring_header_internal {
    uint32_t flags;
    uint32_t length;
    uint32_t padding1;
    uint32_t padding2;
    const wchar_t *str;
};

struct uwp_hstring_private {
    struct uwp_hstring_header_internal header;
    volatile int32_t refcount;
    wchar_t buffer[1];
};

SCARLETT_STATIC_ASSERT(
    sizeof(struct uwp_hstring_header_internal) <= sizeof(UWP_HSTRING_HEADER),
    "uwp_hstring_header_internal must fit inside UWP_HSTRING_HEADER");

static const wchar_t g_empty_wstr[1] = { L'\0' };

#ifdef _WIN32
typedef HRESULT (WINAPI *pfn_WindowsCreateString)(const wchar_t *, uint32_t, UWP_HSTRING *);
typedef HRESULT (WINAPI *pfn_WindowsCreateStringReference)(const wchar_t *, uint32_t, UWP_HSTRING_HEADER *, UWP_HSTRING *);
typedef const wchar_t *(WINAPI *pfn_WindowsGetStringRawBuffer)(UWP_HSTRING, uint32_t *);
typedef HRESULT (WINAPI *pfn_WindowsDeleteString)(UWP_HSTRING);

static volatile LONG g_combase_resolved = 0;
static pfn_WindowsCreateString          g_cb_create_string = NULL;
static pfn_WindowsCreateStringReference g_cb_create_string_ref = NULL;
static pfn_WindowsGetStringRawBuffer    g_cb_get_raw_buffer = NULL;
static pfn_WindowsDeleteString          g_cb_delete_string = NULL;

static void uwp_resolve_combase_winstring(void) {
    if (InterlockedCompareExchange(&g_combase_resolved, 1, 0) == 0) {
        HMODULE h_combase = GetModuleHandleA("combase.dll");
        if (!h_combase) {
            h_combase = LoadLibraryA("combase.dll");
        }
        if (h_combase) {
            g_cb_create_string = (pfn_WindowsCreateString)
                (void *)GetProcAddress(h_combase, "WindowsCreateString");
            g_cb_create_string_ref = (pfn_WindowsCreateStringReference)
                (void *)GetProcAddress(h_combase, "WindowsCreateStringReference");
            g_cb_get_raw_buffer = (pfn_WindowsGetStringRawBuffer)
                (void *)GetProcAddress(h_combase, "WindowsGetStringRawBuffer");
            g_cb_delete_string = (pfn_WindowsDeleteString)
                (void *)GetProcAddress(h_combase, "WindowsDeleteString");
        }
        InterlockedExchange(&g_combase_resolved, 2);
    } else {
        while (InterlockedCompareExchange(&g_combase_resolved, 2, 2) != 2) {
            Sleep(0);
        }
    }
}
#endif

void uwp_winstring_init(void) {
#ifdef _WIN32
    uwp_resolve_combase_winstring();
#endif
}

HRESULT UWP_API WindowsCreateString(const wchar_t *sourceString, uint32_t length, UWP_HSTRING *string) {
    if (!string) {
        return E_INVALIDARG;
    }
    *string = NULL;
#ifdef _WIN32
    uwp_resolve_combase_winstring();
    if (g_cb_create_string) {
        return g_cb_create_string(sourceString, length, string);
    }
#endif
    if (length == 0) {
        return S_OK;
    }
    if (!sourceString) {
        return E_POINTER;
    }

    size_t alloc_size = offsetof(struct uwp_hstring_private, buffer) +
                        ((size_t)length + 1u) * sizeof(wchar_t);
    struct uwp_hstring_private *priv = (struct uwp_hstring_private *)malloc(alloc_size);
    if (!priv) {
        return E_OUTOFMEMORY;
    }
    priv->header.flags = 0u;
    priv->header.length = length;
    priv->header.padding1 = 0u;
    priv->header.padding2 = 0u;
    priv->header.str = priv->buffer;
    priv->refcount = 1;
    memcpy(priv->buffer, sourceString, (size_t)length * sizeof(wchar_t));
    priv->buffer[length] = L'\0';
    *string = (UWP_HSTRING)priv;
    return S_OK;
}

HRESULT UWP_API WindowsCreateStringReference(
    const wchar_t *sourceString,
    uint32_t length,
    UWP_HSTRING_HEADER *hstringHeader,
    UWP_HSTRING *string) {
    if (!string || !hstringHeader) {
        return E_INVALIDARG;
    }
    *string = NULL;
#ifdef _WIN32
    uwp_resolve_combase_winstring();
    if (g_cb_create_string_ref) {
        return g_cb_create_string_ref(sourceString, length, hstringHeader, string);
    }
#endif
    if (sourceString != NULL && sourceString[length] != L'\0') {
        return E_INVALIDARG;
    }
    if (length == 0) {
        return S_OK;
    }
    if (!sourceString) {
        return E_POINTER;
    }

    struct uwp_hstring_header_internal *hdr =
        (struct uwp_hstring_header_internal *)(void *)hstringHeader;
    hdr->flags = UWP_HSTRING_REFERENCE_FLAG;
    hdr->length = length;
    hdr->padding1 = 0u;
    hdr->padding2 = 0u;
    hdr->str = sourceString;
    *string = (UWP_HSTRING)hdr;
    return S_OK;
}

const wchar_t *UWP_API WindowsGetStringRawBuffer(UWP_HSTRING string, uint32_t *length) {
#ifdef _WIN32
    uwp_resolve_combase_winstring();
    if (g_cb_get_raw_buffer) {
        return g_cb_get_raw_buffer(string, length);
    }
#endif
    if (!string) {
        if (length) *length = 0u;
        return g_empty_wstr;
    }
    const struct uwp_hstring_header_internal *hdr =
        (const struct uwp_hstring_header_internal *)(const void *)string;
    if (length) {
        *length = hdr->length;
    }
    return hdr->str ? hdr->str : g_empty_wstr;
}

HRESULT UWP_API WindowsDeleteString(UWP_HSTRING string) {
    if (!string) {
        return S_OK;
    }
#ifdef _WIN32
    uwp_resolve_combase_winstring();
    if (g_cb_delete_string) {
        return g_cb_delete_string(string);
    }
#endif
    struct uwp_hstring_private *priv = (struct uwp_hstring_private *)(void *)string;
    if ((priv->header.flags & UWP_HSTRING_REFERENCE_FLAG) != 0u) {
        return S_OK;
    }
    if (uwp_atomic_dec(&priv->refcount) == 0) {
        free(priv);
    }
    return S_OK;
}

HRESULT uwp_hstring_create_from_wstr(const wchar_t *str, UWP_HSTRING *out) {
    if (!out) {
        return E_INVALIDARG;
    }
    if (!str || str[0] == L'\0') {
        *out = NULL;
        return S_OK;
    }
    return WindowsCreateString(str, (uint32_t)wcslen(str), out);
}

HRESULT UWP_API RoInitialize(UwpRoInitType initType) {
    uwp_host_ensure_initialized();
#ifdef _WIN32
    DWORD coinit = (initType == UWP_RO_INIT_SINGLETHREADED)
                       ? (COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)
                       : COINIT_MULTITHREADED;
    return CoInitializeEx(NULL, coinit);
#else
    (void)initType;
    return S_OK;
#endif
}

void UWP_API RoUninitialize(void) {
#ifdef _WIN32
    CoUninitialize();
#endif
}

HRESULT UWP_API RoGetActivationFactory(UWP_HSTRING activatableClassId, REFIID iid, void **factory) {
    if (!activatableClassId || !iid || !factory) {
        return E_INVALIDARG;
    }
    *factory = NULL;
    return UwpHostTryGetActivationFactory(activatableClassId, iid, factory);
}

HRESULT UWP_API RoActivateInstance(UWP_HSTRING activatableClassId, UwpIInspectable **instance) {
    if (!activatableClassId || !instance) {
        return E_INVALIDARG;
    }
    *instance = NULL;
    UwpIActivationFactory *factory = NULL;
    HRESULT hr = RoGetActivationFactory(activatableClassId, &UWP_IID_IActivationFactory, (void **)&factory);
    if (FAILED(hr)) {
        return hr;
    }
    hr = factory->lpVtbl->ActivateInstance(factory, instance);
    factory->lpVtbl->Release(factory);
    return hr;
}
