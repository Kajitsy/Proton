#!/usr/bin/env python3
"""
Proton-Scarlett — MSVC UWP Runtime (*_APP.dll) Forwarder Generator

Parses Wine 11.0 .spec files for the 7 MSVC UWP runtime DLLs:
  - VCRUNTIME140_APP.dll   -> vcruntime140.dll
  - VCRUNTIME140_1_APP.dll -> vcruntime140_1.dll
  - MSVCP140_APP.dll       -> msvcp140.dll
  - MSVCP140_1_APP.dll     -> msvcp140_1.dll
  - MSVCP140_2_APP.dll     -> msvcp140_2.dll
  - CONCRT140_APP.dll      -> concrt140.dll
  - vccorlib140_app.dll    -> vccorlib140.dll

Emits:
  - <DLL>.def  (MinGW GNU ld & lld-link compatible quoted PE64 forwarders + DATA)
  - <DLL>.c    (DllMain + clean-room WinRT/C++ runtime implementations +
                diagnostic auto-stub trampolines for missing/stubbed imports)
"""

from __future__ import annotations

import argparse
import os
import sys
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple


DEFAULT_WINE_DLLS_DIR = "/home/kajitsy/Documents/GitHub/Proton/wine/dlls"


@dataclass(frozen=True)
class CrtDllTarget:
    app_dll: str
    wine_name: str
    c_prefix: str

    @property
    def app_stem(self) -> str:
        return self.app_dll[:-4] if self.app_dll.lower().endswith(".dll") else self.app_dll


CRT_TARGETS: List[CrtDllTarget] = [
    CrtDllTarget("VCRUNTIME140_APP.dll", "vcruntime140", "vcruntime140_app"),
    CrtDllTarget("VCRUNTIME140_1_APP.dll", "vcruntime140_1", "vcruntime140_1_app"),
    CrtDllTarget("MSVCP140_APP.dll", "msvcp140", "msvcp140_app"),
    CrtDllTarget("MSVCP140_1_APP.dll", "msvcp140_1", "msvcp140_1_app"),
    CrtDllTarget("MSVCP140_2_APP.dll", "msvcp140_2", "msvcp140_2_app"),
    CrtDllTarget("CONCRT140_APP.dll", "concrt140", "concrt140_app"),
    CrtDllTarget("vccorlib140_app.dll", "vccorlib140", "vccorlib140_app"),
]


@dataclass(frozen=True)
class SpecExport:
    name: str
    kind: str  # "func", "extern", "stub"
    ordinal: Optional[int]


def _arch_matches_x86_64(archs: Optional[List[str]]) -> bool:
    if archs is None:
        return True
    neg = all(a.startswith("!") for a in archs)
    if neg:
        return "!x86_64" not in archs and "!win64" not in archs
    return "x86_64" in archs or "win64" in archs


def parse_wine_spec(spec_path: str) -> List[SpecExport]:
    exports: List[SpecExport] = []
    seen = set()
    with open(spec_path, "r", encoding="utf-8") as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue
            tokens = line.split()
            if len(tokens) < 3:
                continue
            ord_tok = tokens[0]
            ordinal: Optional[int] = None
            if ord_tok != "@":
                if not ord_tok.isdigit():
                    continue
                ordinal = int(ord_tok)

            cc = tokens[1]
            idx = 2
            archs: Optional[List[str]] = None
            while idx < len(tokens) and tokens[idx].startswith("-"):
                flag = tokens[idx]
                if flag.startswith("-arch="):
                    archs = flag[6:].split(",")
                idx += 1

            if not _arch_matches_x86_64(archs):
                continue
            if idx >= len(tokens):
                continue

            sym_tok = tokens[idx]
            sym_name = sym_tok.split("(", 1)[0]
            if "=" in sym_name:
                sym_name = sym_name.split("=", 1)[0]
            if not sym_name or sym_name == "@" or sym_name in seen:
                continue
            seen.add(sym_name)

            if cc == "extern":
                kind = "extern"
            elif cc == "stub":
                kind = "stub"
            else:
                kind = "func"
            exports.append(SpecExport(name=sym_name, kind=kind, ordinal=ordinal))
    return exports


# ============================================================================
# Local overrides (clean-room implementations & diagnostic auto-stub thunks)
# ============================================================================

@dataclass(frozen=True)
class LocalSymbolOverride:
    c_ident: str
    is_data: bool = False
    is_diag_stub: bool = False


LOCAL_OVERRIDES: Dict[str, Dict[str, LocalSymbolOverride]] = {
    "CONCRT140_APP.dll": {
        # Missing from Wine concrt140.spec (# extern commented out); imported by gzdoom.exe
        "?AgentEventGuid@Concurrency@@3U_GUID@@B": LocalSymbolOverride("crt_guid_AgentEventGuid", is_data=True),
        "?ChoreEventGuid@Concurrency@@3U_GUID@@B": LocalSymbolOverride("crt_guid_ChoreEventGuid", is_data=True),
        "?ConcRTEventGuid@Concurrency@@3U_GUID@@B": LocalSymbolOverride("crt_guid_ConcRTEventGuid", is_data=True),
        "?ContextEventGuid@Concurrency@@3U_GUID@@B": LocalSymbolOverride("crt_guid_ContextEventGuid", is_data=True),
        "?LockEventGuid@Concurrency@@3U_GUID@@B": LocalSymbolOverride("crt_guid_LockEventGuid", is_data=True),
        "?PPLParallelForEventGuid@Concurrency@@3U_GUID@@B": LocalSymbolOverride("crt_guid_PPLParallelForEventGuid", is_data=True),
        "?PPLParallelForeachEventGuid@Concurrency@@3U_GUID@@B": LocalSymbolOverride("crt_guid_PPLParallelForeachEventGuid", is_data=True),
        "?PPLParallelInvokeEventGuid@Concurrency@@3U_GUID@@B": LocalSymbolOverride("crt_guid_PPLParallelInvokeEventGuid", is_data=True),
        "?ResourceManagerEventGuid@Concurrency@@3U_GUID@@B": LocalSymbolOverride("crt_guid_ResourceManagerEventGuid", is_data=True),
        "?ScheduleGroupEventGuid@Concurrency@@3U_GUID@@B": LocalSymbolOverride("crt_guid_ScheduleGroupEventGuid", is_data=True),
        "?SchedulerEventGuid@Concurrency@@3U_GUID@@B": LocalSymbolOverride("crt_guid_SchedulerEventGuid", is_data=True),
        "?VirtualProcessorEventGuid@Concurrency@@3U_GUID@@B": LocalSymbolOverride("crt_guid_VirtualProcessorEventGuid", is_data=True),
        # Wine @ stub imported by gzdoom.exe
        "?_UnderlyingYield@details@Concurrency@@YAXXZ": LocalSymbolOverride("crt_impl_concrt_UnderlyingYield"),
    },
    "MSVCP140_2_APP.dll": {
        # Wine @ stub imported by OpenAL32.dll (Kaiser window resampler)
        "__std_smf_cyl_bessel_i": LocalSymbolOverride("crt_impl_std_smf_cyl_bessel_i"),
        "__std_smf_cyl_bessel_if": LocalSymbolOverride("crt_impl_std_smf_cyl_bessel_if"),
    },
    "MSVCP140_APP.dll": {
        # Wine @ stubs imported by UnityPlayer.dll / SDL2.dll / OpenAL32.dll / gzdoom.exe / libgallium_wgl.dll
        "?GetNextAsyncId@platform@details@Concurrency@@YAIXZ": LocalSymbolOverride("crt_impl_msvcp_GetNextAsyncId"),
        "?_ReportUnobservedException@details@Concurrency@@YAXXZ": LocalSymbolOverride("crt_impl_msvcp_ReportUnobservedException"),
        "?_Throw_Cpp_error@std@@YAXH@Z": LocalSymbolOverride("crt_impl_msvcp_Throw_Cpp_error"),
    },
    "vccorlib140_app.dll": {
        # Wine @ stubs imported by BridgeInterface.dll, UnityEngineDelegates.dll,
        # UnityPlayer.dll, SDL2.dll, gzdoom.exe, libgallium_wgl.dll
        "??0IntPtr@Platform@@QEAA@H@Z": LocalSymbolOverride("crt_impl_vcc_IntPtr_ctor_i32"),
        "?ToInt32@IntPtr@Platform@@QEAAHXZ": LocalSymbolOverride("crt_impl_vcc_IntPtr_ToInt32"),
        "?Contains@Rect@Foundation@Windows@@QEAA_NVPoint@23@@Z": LocalSymbolOverride("crt_impl_vcc_Rect_Contains"),
        "??0Object@Platform@@QE$AAA@XZ": LocalSymbolOverride("crt_impl_vcc_Object_ctor"),
        "?AlignedAllocate@Heap@Details@Platform@@SAPEAX_K0@Z": LocalSymbolOverride("crt_impl_vcc_Heap_AlignedAllocate2"),
        "?AlignedAllocate@Heap@Details@Platform@@SAPEAX_K00@Z": LocalSymbolOverride("crt_impl_vcc_Heap_AlignedAllocate3"),
        "?AlignedFree@Heap@Details@Platform@@SAXPEAX@Z": LocalSymbolOverride("crt_impl_vcc_Heap_AlignedFree"),
        "?AlignedFreeException@Heap@Details@Platform@@SAXPEAX@Z": LocalSymbolOverride("crt_impl_vcc_Heap_AlignedFree"),
        "?GetObjectContext@Details@Platform@@YAPEAUIUnknown@@XZ": LocalSymbolOverride("crt_impl_vcc_GetObjectContext"),
        "?GetProxyImpl@Details@Platform@@YAJPEAUIUnknown@@AEBU_GUID@@0PEAPEAU3@@Z": LocalSymbolOverride("crt_impl_vcc_GetProxyImpl"),
        "?ReleaseInContextImpl@Details@Platform@@YAJPEAUIUnknown@@0@Z": LocalSymbolOverride("crt_impl_vcc_ReleaseInContextImpl"),
        "?GetActivationFactory@Details@Platform@@YAJPEAVModuleBase@1WRL@Microsoft@@PEAUHSTRING__@@PEAPEAUIActivationFactory@@@Z": LocalSymbolOverride("crt_impl_vcc_GetActivationFactory"),
        "?TerminateModule@Details@Platform@@YA_NPEAVModuleBase@1WRL@Microsoft@@@Z": LocalSymbolOverride("crt_impl_vcc_TerminateModule"),
        "?__abi_cast_Object_to_String@__abi_details@@YAPE$AAVString@Platform@@_NPE$AAVObject@3@@Z": LocalSymbolOverride("crt_impl_vcc_abi_cast_Object_to_String"),
        "?__abi_cast_String_to_Object@__abi_details@@YAPE$AAVObject@Platform@@PE$AAVString@3@@Z": LocalSymbolOverride("crt_impl_vcc_abi_cast_String_to_Object"),
        "?ReCreateException@Exception@Platform@@SAPE$AAV12@H@Z": LocalSymbolOverride("crt_impl_vcc_ReCreateException"),
        # Diagnostic auto-stub trampolines (never silent S_OK, logs caller RVA + RCX/RDX/R8/R9)
        "?GetIBoxVtable@Details@Platform@@YAPEAXPEAX@Z": LocalSymbolOverride("crt_stub_vcc_GetIBoxVtable", is_diag_stub=True),
        "?GetIBoxArrayVtable@Details@Platform@@YAPEAXPEAX@Z": LocalSymbolOverride("crt_stub_vcc_GetIBoxArrayVtable", is_diag_stub=True),
    },
}


EXTRA_C_BODIES: Dict[str, str] = {
    "CONCRT140_APP.dll": """
/* Clean-room Concurrency ETW event GUIDs (missing from Wine concrt140.spec) */
#define CRT_DEFINE_CONCRT_GUID(cname, d1, d2, d3, b0, b1, b2, b3, b4, b5, b6, b7) \\
    const crt_app_guid_t cname = { d1, d2, d3, { b0, b1, b2, b3, b4, b5, b6, b7 } }

CRT_DEFINE_CONCRT_GUID(crt_guid_AgentEventGuid,            0x67067252u, 0x0581u, 0x4E81u, 0xA1u, 0x1Eu, 0x7Eu, 0xA8u, 0x86u, 0x11u, 0x01u, 0x01u);
CRT_DEFINE_CONCRT_GUID(crt_guid_ChoreEventGuid,            0x67067252u, 0x0581u, 0x4E81u, 0xA1u, 0x1Eu, 0x7Eu, 0xA8u, 0x86u, 0x11u, 0x01u, 0x02u);
CRT_DEFINE_CONCRT_GUID(crt_guid_ConcRTEventGuid,           0x67067252u, 0x0581u, 0x4E81u, 0xA1u, 0x1Eu, 0x7Eu, 0xA8u, 0x86u, 0x11u, 0x01u, 0x03u);
CRT_DEFINE_CONCRT_GUID(crt_guid_ContextEventGuid,          0x67067252u, 0x0581u, 0x4E81u, 0xA1u, 0x1Eu, 0x7Eu, 0xA8u, 0x86u, 0x11u, 0x01u, 0x04u);
CRT_DEFINE_CONCRT_GUID(crt_guid_LockEventGuid,             0x67067252u, 0x0581u, 0x4E81u, 0xA1u, 0x1Eu, 0x7Eu, 0xA8u, 0x86u, 0x11u, 0x01u, 0x05u);
CRT_DEFINE_CONCRT_GUID(crt_guid_PPLParallelForEventGuid,   0x67067252u, 0x0581u, 0x4E81u, 0xA1u, 0x1Eu, 0x7Eu, 0xA8u, 0x86u, 0x11u, 0x01u, 0x06u);
CRT_DEFINE_CONCRT_GUID(crt_guid_PPLParallelForeachEventGuid, 0x67067252u, 0x0581u, 0x4E81u, 0xA1u, 0x1Eu, 0x7Eu, 0xA8u, 0x86u, 0x11u, 0x01u, 0x07u);
CRT_DEFINE_CONCRT_GUID(crt_guid_PPLParallelInvokeEventGuid, 0x67067252u, 0x0581u, 0x4E81u, 0xA1u, 0x1Eu, 0x7Eu, 0xA8u, 0x86u, 0x11u, 0x01u, 0x08u);
CRT_DEFINE_CONCRT_GUID(crt_guid_ResourceManagerEventGuid,  0x67067252u, 0x0581u, 0x4E81u, 0xA1u, 0x1Eu, 0x7Eu, 0xA8u, 0x86u, 0x11u, 0x01u, 0x09u);
CRT_DEFINE_CONCRT_GUID(crt_guid_ScheduleGroupEventGuid,    0x67067252u, 0x0581u, 0x4E81u, 0xA1u, 0x1Eu, 0x7Eu, 0xA8u, 0x86u, 0x11u, 0x01u, 0x0Au);
CRT_DEFINE_CONCRT_GUID(crt_guid_SchedulerEventGuid,        0x67067252u, 0x0581u, 0x4E81u, 0xA1u, 0x1Eu, 0x7Eu, 0xA8u, 0x86u, 0x11u, 0x01u, 0x0Bu);
CRT_DEFINE_CONCRT_GUID(crt_guid_VirtualProcessorEventGuid, 0x67067252u, 0x0581u, 0x4E81u, 0xA1u, 0x1Eu, 0x7Eu, 0xA8u, 0x86u, 0x11u, 0x01u, 0x0Cu);

void crt_impl_concrt_UnderlyingYield(void) {
    crt_app_yield_thread();
}
""",
    "MSVCP140_2_APP.dll": """
double crt_impl_std_smf_cyl_bessel_i(double nu, double x) {
    return crt_app_cyl_bessel_i(nu, x);
}

float crt_impl_std_smf_cyl_bessel_if(float nu, float x) {
    return crt_app_cyl_bessel_if(nu, x);
}
""",
    "MSVCP140_APP.dll": """
uint32_t crt_impl_msvcp_GetNextAsyncId(void) {
    return crt_app_next_async_id();
}

void crt_impl_msvcp_ReportUnobservedException(void) {
    crt_app_log("MSVCP140_APP",
                "Concurrency::details::_ReportUnobservedException invoked (ret=%p)",
                __builtin_return_address(0));
}

void crt_impl_msvcp_Throw_Cpp_error(int code) {
    typedef void (*pfn_throw_c_err_t)(int);
    crt_app_log("MSVCP140_APP",
                "std::_Throw_Cpp_error(code=%d, ret=%p)",
                code,
                __builtin_return_address(0));
    pfn_throw_c_err_t fn = (pfn_throw_c_err_t)crt_app_resolve_proc(
        "msvcp140.dll", "?_Throw_C_error@std@@YAXH@Z");
    if (fn) {
        fn(code);
    }
#ifdef _WIN32
    RaiseException(0xE06D7363u, 1u, 0u, NULL);
#endif
}
""",
    "vccorlib140_app.dll": """
static const crt_app_guid_t CRT_IID_IUnknown_Local = {
    0x00000000u, 0x0000u, 0x0000u,
    { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u }
};

typedef struct crt_vcc_iunk_vtbl_s {
    int32_t (
#ifdef _WIN32
        __stdcall
#endif
        *QueryInterface)(void *This, const crt_app_guid_t *riid, void **ppv);
    uint32_t (
#ifdef _WIN32
        __stdcall
#endif
        *AddRef)(void *This);
    uint32_t (
#ifdef _WIN32
        __stdcall
#endif
        *Release)(void *This);
} crt_vcc_iunk_vtbl_t;

typedef struct crt_vcc_iunk_s {
    const crt_vcc_iunk_vtbl_t *lpVtbl;
} crt_vcc_iunk_t;

void *crt_impl_vcc_IntPtr_ctor_i32(void *self, int32_t value) {
    return crt_app_intptr_from_i32(self, value);
}

int32_t crt_impl_vcc_IntPtr_ToInt32(const void *self) {
    return crt_app_intptr_to_i32(self);
}

uint8_t crt_impl_vcc_Rect_Contains(const float *rect_xywh, uint64_t point_packed) {
    return crt_app_rect_contains(rect_xywh, point_packed);
}

void *crt_impl_vcc_Object_ctor(void *self) {
    return crt_app_platform_object_ctor(self);
}

void *crt_impl_vcc_Heap_AlignedAllocate2(size_t alignment, size_t size) {
    return crt_app_aligned_alloc(size, alignment);
}

void *crt_impl_vcc_Heap_AlignedAllocate3(size_t alignment, size_t size, size_t offset) {
    (void)offset;
    return crt_app_aligned_alloc(size, alignment);
}

void crt_impl_vcc_Heap_AlignedFree(void *ptr) {
    crt_app_aligned_free(ptr);
}

void *crt_impl_vcc_GetObjectContext(void) {
    typedef int32_t (
#ifdef _WIN32
        __stdcall
#endif
        *pfn_co_get_obj_ctx_t)(const crt_app_guid_t *riid, void **ppv);
    pfn_co_get_obj_ctx_t fn = (pfn_co_get_obj_ctx_t)crt_app_resolve_proc(
        "combase.dll", "CoGetObjectContext");
    if (!fn) {
        fn = (pfn_co_get_obj_ctx_t)crt_app_resolve_proc("ole32.dll", "CoGetObjectContext");
    }
    void *ctx = NULL;
    if (fn) {
        if (fn(&CRT_IID_IUnknown_Local, &ctx) < 0) {
            ctx = NULL;
        }
    }
    return ctx;
}

int32_t crt_impl_vcc_GetProxyImpl(
    void *obj,
    const crt_app_guid_t *iid,
    void *context,
    void **out_proxy) {
    (void)context;
    if (!out_proxy) return (int32_t)0x80004003u; /* E_POINTER */
    *out_proxy = NULL;
    if (!obj || !iid) return (int32_t)0x80070057u; /* E_INVALIDARG */
    crt_vcc_iunk_t *unk = (crt_vcc_iunk_t *)obj;
    if (!unk->lpVtbl || !unk->lpVtbl->QueryInterface) {
        return (int32_t)0x80004002u; /* E_NOINTERFACE */
    }
    return unk->lpVtbl->QueryInterface(obj, iid, out_proxy);
}

int32_t crt_impl_vcc_ReleaseInContextImpl(void *obj, void *context) {
    (void)context;
    if (obj) {
        crt_vcc_iunk_t *unk = (crt_vcc_iunk_t *)obj;
        if (unk->lpVtbl && unk->lpVtbl->Release) {
            unk->lpVtbl->Release(obj);
        }
    }
    return 0; /* S_OK */
}

int32_t crt_impl_vcc_GetActivationFactory(
    crt_wrl_module_base_t *module,
    void *activatable_class_id,
    void **out_factory) {
    crt_get_hstring_buf_fn_t fn_raw_buf = (crt_get_hstring_buf_fn_t)crt_app_resolve_proc(
        "combase.dll", "WindowsGetStringRawBuffer");
    return crt_app_wrl_get_activation_factory(
        module, activatable_class_id, out_factory, fn_raw_buf);
}

uint8_t crt_impl_vcc_TerminateModule(crt_wrl_module_base_t *module) {
    return crt_app_wrl_terminate_module(module);
}

void *crt_impl_vcc_abi_cast_Object_to_String(uint8_t try_stringable, void *obj) {
    typedef void *(
#ifdef _WIN32
        __stdcall
#endif
        *pfn_obj_to_str_t)(void *, uint8_t);
    if (!obj) return NULL;
    pfn_obj_to_str_t fn = (pfn_obj_to_str_t)crt_app_resolve_proc(
        "vccorlib140.dll",
        "?__abi_ObjectToString@__abi_details@@YAPE$AAVString@Platform@@PE$AAVObject@3@_N@Z");
    if (!fn) {
        crt_app_log("vccorlib140_app", "failed to resolve __abi_ObjectToString");
        return NULL;
    }
    return fn(obj, try_stringable);
}

void *crt_impl_vcc_abi_cast_String_to_Object(void *hstr) {
    typedef void *(
#ifdef _WIN32
        __stdcall
#endif
        *pfn_create_val_t)(int, const void *);
    if (!hstr) return NULL;
    pfn_create_val_t fn = (pfn_create_val_t)crt_app_resolve_proc(
        "vccorlib140.dll",
        "?CreateValue@Details@Platform@@YAPE$AAVObject@2@W4TypeCode@2@PEBX@Z");
    if (!fn) {
        crt_app_log("vccorlib140_app", "failed to resolve Platform::Details::CreateValue");
        return NULL;
    }
    return fn(18 /* PropertyType_String */, &hstr);
}

void *crt_impl_vcc_ReCreateException(int32_t hr) {
    typedef void *(*pfn_create_exc_t)(int32_t);
    pfn_create_exc_t fn = (pfn_create_exc_t)crt_app_resolve_proc(
        "vccorlib140.dll",
        "?CreateException@Exception@Platform@@SAPE$AAV12@H@Z");
    if (!fn) {
        crt_app_log("vccorlib140_app", "failed to resolve Platform::Exception::CreateException");
        return NULL;
    }
    return fn(hr);
}

static const scarlett_stub_descriptor_t g_desc_vcc_GetIBoxVtable = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_NONE,
    SCARLETT_STUB_ORDINAL_NONE,
    SCARLETT_STUB_SLOT_NONE,
    SCARLETT_E_NOTIMPL,
    0u,
    "vccorlib140_app.dll",
    "?GetIBoxVtable@Details@Platform@@YAPEAXPEAX@Z"
};

void *crt_stub_vcc_GetIBoxVtable(uint64_t rcx, uint64_t rdx, uint64_t r8, uint64_t r9) {
    (void)crt_app_stub_dispatch(
        &g_desc_vcc_GetIBoxVtable,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        rcx, rdx, r8, r9);
    return NULL;
}

static const scarlett_stub_descriptor_t g_desc_vcc_GetIBoxArrayVtable = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_NONE,
    SCARLETT_STUB_ORDINAL_NONE,
    SCARLETT_STUB_SLOT_NONE,
    SCARLETT_E_NOTIMPL,
    0u,
    "vccorlib140_app.dll",
    "?GetIBoxArrayVtable@Details@Platform@@YAPEAXPEAX@Z"
};

void *crt_stub_vcc_GetIBoxArrayVtable(uint64_t rcx, uint64_t rdx, uint64_t r8, uint64_t r9) {
    (void)crt_app_stub_dispatch(
        &g_desc_vcc_GetIBoxArrayVtable,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        rcx, rdx, r8, r9);
    return NULL;
}
""",
}


def generate_for_target(target: CrtDllTarget, wine_dlls_dir: str) -> Tuple[str, str]:
    spec_path = os.path.join(wine_dlls_dir, target.wine_name, f"{target.wine_name}.spec")
    if not os.path.isfile(spec_path):
        raise FileNotFoundError(f"Missing Wine spec file: {spec_path}")

    spec_exports = parse_wine_spec(spec_path)
    overrides = LOCAL_OVERRIDES.get(target.app_dll, {})

    def_lines: List[str] = [
        f"; Auto-generated by gen_crt_app_forwarders.py from {target.wine_name}.spec",
        f"LIBRARY {target.app_dll}",
        "EXPORTS",
    ]

    seen_names = set()
    for exp in spec_exports:
        seen_names.add(exp.name)
        ov = overrides.get(exp.name)
        if ov is not None:
            data_suffix = " DATA" if ov.is_data else ""
            def_lines.append(f'    "{exp.name}" = {ov.c_ident}{data_suffix}')
        else:
            data_suffix = " DATA" if exp.kind == "extern" else ""
            def_lines.append(f'    "{exp.name}" = "{target.wine_name}.{exp.name}"{data_suffix}')

    # Append symbols that are missing in Wine's .spec (e.g. Concurrency::PPLParallelForEventGuid)
    for sym_name, ov in sorted(overrides.items()):
        if sym_name in seen_names:
            continue
        seen_names.add(sym_name)
        data_suffix = " DATA" if ov.is_data else ""
        def_lines.append(f'    "{sym_name}" = {ov.c_ident}{data_suffix}')

    def_content = "\n".join(def_lines) + "\n"

    extra_body = EXTRA_C_BODIES.get(target.app_dll, "").strip()
    c_lines: List[str] = [
        "/*",
        f" * Auto-generated by gen_crt_app_forwarders.py for {target.app_dll} -> {target.wine_name}.dll",
        " * Clean-room freestanding PE64 implementation (zero POSIX/Linux calls).",
        " */",
        "",
        '#include "crt_app_runtime.h"',
        "",
    ]
    if extra_body:
        c_lines.append(extra_body)
        c_lines.append("")

    c_lines.extend([
        "#ifdef _WIN32",
        "BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved) {",
        "    (void)hinst;",
        "    (void)reason;",
        "    (void)reserved;",
        "    return TRUE;",
        "}",
        "#endif",
        "",
    ])
    c_content = "\n".join(c_lines)
    return def_content, c_content


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate MSVC UWP CRT *_APP.dll forwarders")
    parser.add_argument("--wine-dlls-dir", default=DEFAULT_WINE_DLLS_DIR)
    parser.add_argument("--out-dir", default=os.path.dirname(os.path.abspath(__file__)))
    parser.add_argument("--check", action="store_true", help="Verify generated files match disk without writing")
    args = parser.parse_args()

    stale: List[str] = []
    for target in CRT_TARGETS:
        def_content, c_content = generate_for_target(target, args.wine_dlls_dir)
        def_path = os.path.join(args.out_dir, f"{target.app_stem}.def")
        c_path = os.path.join(args.out_dir, f"{target.app_stem}.c")

        for path, content in ((def_path, def_content), (c_path, c_content)):
            if args.check:
                if not os.path.isfile(path):
                    stale.append(path)
                else:
                    with open(path, "r", encoding="utf-8") as f:
                        if f.read() != content:
                            stale.append(path)
            else:
                with open(path, "w", encoding="utf-8") as f:
                    f.write(content)
                print(f"Generated {os.path.basename(path)}")

    if args.check:
        if stale:
            print("Stale or missing generated files:", stale, file=sys.stderr)
            return 1
        print(f"All {len(CRT_TARGETS) * 2} generated files match Wine .spec definitions.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
