#!/usr/bin/env python3
"""
Proton-Scarlett — Wine 11.0 System/Win32 *_APP.dll Forwarder Generator

Parses Wine 11.0 `.spec` files and emits PE64 `.def` forwarder definitions plus
clean-room `.c` files containing `DllMain` and `scarlett_stub_descriptor_t`
diagnostic auto-stub trampolines for any UWP-only symbols absent in Wine's
target `.spec` (Zero Silent Stubs policy).
"""

from __future__ import annotations

import argparse
import dataclasses
import pathlib
import re
import sys
from typing import Dict, List, Optional, Sequence, Tuple


SKIP_LIFECYCLE_EXPORTS = frozenset({
    "DllMain",
    "DllCanUnloadNow",
    "DllGetClassObject",
    "DllRegisterServer",
    "DllUnregisterServer",
    "DllInstall",
    "DriverProc",
    "auxMessage",
    "midMessage",
    "modMessage",
})


@dataclasses.dataclass(frozen=True)
class SpecExport:
    name: str
    target_dll: str
    target_symbol: str
    ordinal: Optional[int] = None
    noname: bool = False
    is_data: bool = False


@dataclasses.dataclass(frozen=True)
class ForwarderSpec:
    output_name: str          # e.g. "KERNEL32_APP"
    dll_filename: str         # e.g. "KERNEL32_APP.dll"
    targets: Tuple[str, ...]  # Wine dll dir/spec names in priority order
    uwp_stubs: Tuple[str, ...] = ()


SYSTEM_FORWARDERS: Tuple[ForwarderSpec, ...] = (
    ForwarderSpec(
        output_name="KERNEL32_APP",
        dll_filename="KERNEL32_APP.dll",
        targets=("kernel32", "kernelbase"),
        uwp_stubs=(
            "AppPolicyGetClrCompat",
            "AppPolicyGetCreateFileAccess",
            "AppPolicyGetLifecycleManagement",
            "OpenPackageInfoByFullName",
            "ClosePackageInfo",
            "GetPackageInfo",
            "GetPackageApplicationIds",
            "GetCurrentPackageApplicationContext",
            "GetCurrentPackageContext",
            "GetCurrentPackageResourcesContext",
            "GetCurrentPackageSecurityContext",
            "GetCurrentPackageInfo2",
            "GetCurrentPackagePath2",
            "GetPackagePathByFullName2",
            "GetApplicationUserModelId",
            "GetApplicationUserModelIdFromToken",
            "GetPackageFamilyNameFromToken",
            "GetPackageFullNameFromToken",
            "FormatApplicationUserModelId",
            "ParseApplicationUserModelId",
            "FindPackagesByPackageFamily",
            "AppContainerDeriveSidFromMoniker",
            "AppContainerFreeMemory",
            "AppContainerLookupMoniker",
            "AppContainerRegisterSid",
            "AppContainerUnregisterSid",
            "CreateAppContainerToken",
            "GetAppContainerAce",
            "GetAppContainerNamedObjectPath",
        ),
    ),
    ForwarderSpec(
        output_name="USER32_APP",
        dll_filename="USER32_APP.dll",
        targets=("user32",),
        uwp_stubs=(
            "CreateWindowInBand",
            "CreateWindowInBandEx",
            "GetWindowBand",
            "SetWindowBand",
            "CheckBannedOneCoreTransformApi",
            "EnableOneCoreTransformMode",
            "IsInDesktopWindowBand",
        ),
    ),
    ForwarderSpec(
        output_name="GDI32_APP",
        dll_filename="GDI32_APP.dll",
        targets=("gdi32",),
    ),
    ForwarderSpec(
        output_name="ADVAPI32_APP",
        dll_filename="ADVAPI32_APP.dll",
        targets=("advapi32",),
    ),
    ForwarderSpec(
        output_name="OLE32_APP",
        dll_filename="OLE32_APP.dll",
        targets=("ole32", "combase"),
    ),
    ForwarderSpec(
        output_name="COMBASE_APP",
        dll_filename="COMBASE_APP.dll",
        targets=("combase",),
    ),
    ForwarderSpec(
        output_name="RPCRT4_APP",
        dll_filename="RPCRT4_APP.dll",
        targets=("rpcrt4",),
    ),
    ForwarderSpec(
        output_name="WS2_32_APP",
        dll_filename="WS2_32_APP.dll",
        targets=("ws2_32",),
    ),
    ForwarderSpec(
        output_name="WINHTTP_APP",
        dll_filename="WINHTTP_APP.dll",
        targets=("winhttp",),
    ),
    ForwarderSpec(
        output_name="CRYPT32_APP",
        dll_filename="CRYPT32_APP.dll",
        targets=("crypt32",),
    ),
    ForwarderSpec(
        output_name="BCRYPT_APP",
        dll_filename="BCRYPT_APP.dll",
        targets=("bcrypt",),
    ),
    ForwarderSpec(
        output_name="MMDEVAPI_APP",
        dll_filename="MMDEVAPI_APP.dll",
        targets=("mmdevapi",),
    ),
    ForwarderSpec(
        output_name="XINPUT1_4_APP",
        dll_filename="XINPUT1_4_APP.dll",
        targets=("xinput1_4",),
    ),
    ForwarderSpec(
        output_name="XINPUTUAP",
        dll_filename="XINPUTUAP.dll",
        targets=("xinput1_4",),
        uwp_stubs=(
            "XInputGetBaseBusInformation",
        ),
    ),
)


def arch_matches_x86_64(arch_expr: str) -> bool:
    negate = arch_expr.startswith("!")
    items = [item.strip() for item in (arch_expr[1:] if negate else arch_expr).split(",") if item.strip()]
    has_x64 = any(item in ("x86_64", "win64") for item in items)
    return (not has_x64) if negate else has_x64


def parse_wine_spec(spec_path: pathlib.Path, target_dll: str) -> List[SpecExport]:
    exports: List[SpecExport] = []
    with spec_path.open("r", encoding="utf-8", errors="replace") as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue
            if "#" in line:
                line = line[:line.index("#")].strip()
            if not line:
                continue

            tokens = line.split()
            if len(tokens) < 3:
                continue

            ord_tok = tokens[0]
            cc = tokens[1]
            if cc not in ("stdcall", "cdecl", "varargs", "stub", "extern", "pascal", "thiscall"):
                continue

            idx = 2
            skip = False
            is_noname = False
            while idx < len(tokens) and tokens[idx].startswith("-"):
                flag = tokens[idx]
                if flag == "-i386":
                    skip = True
                    break
                if flag == "-noname":
                    is_noname = True
                elif flag.startswith("-arch="):
                    if not arch_matches_x86_64(flag[6:]):
                        skip = True
                        break
                idx += 1

            if skip or idx >= len(tokens):
                continue

            rest = " ".join(tokens[idx:])
            match = re.match(r"^([^\s(]+)", rest)
            if not match:
                continue
            sym_name = match.group(1)

            if sym_name in SKIP_LIFECYCLE_EXPORTS:
                continue

            ordinal: Optional[int] = int(ord_tok) if ord_tok.isdigit() else None
            if sym_name == "@":
                is_noname = True
                if ordinal is None:
                    continue
                sym_name = f"Ordinal_{ordinal}"

            if is_noname:
                if ordinal is None:
                    continue
                exports.append(
                    SpecExport(
                        name=sym_name,
                        target_dll=target_dll,
                        target_symbol=f"#{ordinal}",
                        ordinal=ordinal,
                        noname=True,
                        is_data=(cc == "extern"),
                    )
                )
            else:
                exports.append(
                    SpecExport(
                        name=sym_name,
                        target_dll=target_dll,
                        target_symbol=sym_name,
                        ordinal=None,
                        noname=False,
                        is_data=(cc == "extern"),
                    )
                )
    return exports


def collect_forwarder_exports(
    spec: ForwarderSpec,
    wine_dlls_dir: pathlib.Path,
) -> Tuple[List[SpecExport], List[str]]:
    seen_names: set[str] = set()
    seen_ordinals: set[int] = set()
    named_exports: List[SpecExport] = []
    ordinal_exports: List[SpecExport] = []

    for target in spec.targets:
        spec_path = wine_dlls_dir / target / f"{target}.spec"
        if not spec_path.is_file():
            raise FileNotFoundError(f"Missing Wine spec file: {spec_path}")
        for exp in parse_wine_spec(spec_path, target):
            if exp.noname:
                assert exp.ordinal is not None
                if exp.ordinal not in seen_ordinals:
                    seen_ordinals.add(exp.ordinal)
                    ordinal_exports.append(exp)
            else:
                if exp.name not in seen_names:
                    seen_names.add(exp.name)
                    named_exports.append(exp)

    # Guard against ordinal collisions with auto-assigned named exports (1..N)
    max_named_ord = len(named_exports) + len(spec.uwp_stubs)
    safe_ordinal_exports = [
        exp for exp in ordinal_exports
        if exp.ordinal is not None and exp.ordinal > max_named_ord
    ]

    missing_stubs: List[str] = []
    for stub_sym in spec.uwp_stubs:
        if stub_sym not in seen_names:
            seen_names.add(stub_sym)
            missing_stubs.append(stub_sym)

    return named_exports + safe_ordinal_exports, missing_stubs


def render_def(spec: ForwarderSpec, exports: Sequence[SpecExport], stubs: Sequence[str]) -> str:
    lines = [
        f"; Auto-generated by gen_app_forwarders.py from Wine 11.0 .spec files",
        f"LIBRARY {spec.dll_filename}",
        "EXPORTS",
    ]
    for exp in exports:
        if exp.noname and exp.ordinal is not None:
            lines.append(f'    {exp.name} = "{exp.target_dll}.{exp.target_symbol}" @{exp.ordinal} NONAME')
        elif exp.is_data:
            lines.append(f"    {exp.name} = {exp.target_dll}.{exp.target_symbol} DATA")
        else:
            lines.append(f"    {exp.name} = {exp.target_dll}.{exp.target_symbol}")
    for stub_sym in stubs:
        lines.append(f"    {stub_sym} = scarlett_stub_fn_{stub_sym}")
    lines.append("")
    return "\n".join(lines)


def render_c(spec: ForwarderSpec, stubs: Sequence[str]) -> str:
    lines = [
        "/*",
        f" * Proton-Scarlett — {spec.dll_filename} PE64 Forwarder & Diagnostic Stub Module",
        " * Auto-generated by gen_app_forwarders.py (clean-room, zero POSIX/Linux calls).",
        " */",
        "",
        '#include "scarlett_app_stub_runtime.h"',
        "",
    ]

    if stubs:
        lines.append("/* UWP-only symbols missing from Wine target .spec -> diagnostic auto-stubs */")
        for sym in stubs:
            lines.append(
                f'SCARLETT_APP_DEFINE_STUB("{spec.dll_filename}", {sym}, '
                f"SCARLETT_STUB_ORDINAL_NONE, SCARLETT_E_NOTIMPL)"
            )
        lines.append("")

    lines.extend([
        "#ifdef _WIN32",
        "BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved) {",
        "    (void)hinst;",
        "    (void)reserved;",
        "    if (reason == DLL_PROCESS_ATTACH) {",
        "        DisableThreadLibraryCalls(hinst);",
        "    }",
        "    return TRUE;",
        "}",
        "#else",
        f"/* Host test linkage anchor for {spec.dll_filename} */",
        f"const char *scarlett_fwd_{spec.output_name.lower()}_name(void) {{",
        f'    return "{spec.dll_filename}";',
        "}",
        "#endif",
        "",
    ])
    return "\n".join(lines)


def default_wine_dlls_dir() -> pathlib.Path:
    candidates = [
        pathlib.Path("/home/kajitsy/Documents/GitHub/Proton/wine/dlls"),
        pathlib.Path(__file__).resolve().parents[3] / "wine" / "dlls",
    ]
    for c in candidates:
        if c.is_dir():
            return c
    return candidates[0]


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Generate Proton-Scarlett system *_APP.dll forwarders")
    parser.add_argument(
        "--wine-dlls-dir",
        type=pathlib.Path,
        default=default_wine_dlls_dir(),
        help="Path to Wine dlls/ directory containing <dll>/<dll>.spec",
    )
    parser.add_argument(
        "--out-dir",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parent / "sys",
        help="Output directory for generated .def and .c files",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Verify checked-in files match generator output without overwriting",
    )
    args = parser.parse_args(argv)

    if not args.wine_dlls_dir.is_dir():
        print(f"error: Wine dlls directory not found: {args.wine_dlls_dir}", file=sys.stderr)
        return 1

    args.out_dir.mkdir(parents=True, exist_ok=True)
    mismatches: List[str] = []

    for spec in SYSTEM_FORWARDERS:
        exports, stubs = collect_forwarder_exports(spec, args.wine_dlls_dir)
        def_content = render_def(spec, exports, stubs)
        c_content = render_c(spec, stubs)

        def_path = args.out_dir / f"{spec.output_name}.def"
        c_path = args.out_dir / f"{spec.output_name}.c"

        if args.check:
            for p, expected in ((def_path, def_content), (c_path, c_content)):
                if not p.is_file() or p.read_text(encoding="utf-8") != expected:
                    mismatches.append(str(p))
        else:
            def_path.write_text(def_content, encoding="utf-8")
            c_path.write_text(c_content, encoding="utf-8")
            print(
                f"generated {spec.dll_filename:16s}: "
                f"{len(exports):4d} forwarded, {len(stubs):2d} diagnostic stubs"
            )

    if args.check and mismatches:
        print("Mismatched generated files:", file=sys.stderr)
        for m in mismatches:
            print(f"  {m}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
