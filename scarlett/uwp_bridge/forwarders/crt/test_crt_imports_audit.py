#!/usr/bin/env python3
"""
Proton-Scarlett — Automated CRT *_APP.dll PE64 Import Audit Test

Verifies:
  1. All 7 .def/.c files in scarlett/uwp_bridge/forwarders/crt/ are up to date
     with gen_crt_app_forwarders.py (--check).
  2. Every .def file has valid MinGW/lld syntax, zero duplicate symbols, and
     every local implementation/stub symbol exists in the corresponding .c file.
  3. Every forwarded symbol exists in Wine 11.0's corresponding .spec and
     compiled PE64 DLL (when present).
  4. 100.0% of PE64 imports (including C++ mangled names & ordinals, normal and
     delay-load) from ALL binaries in gzdoom and HedgePhysics targeting any of
     the 7 CRT *_APP.dll libraries resolve against our 7 .def files with 0
     missing symbols and 0 forwards to Wine @ stub functions.
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import subprocess
import sys
from collections import defaultdict
from typing import Dict, List, Set, Tuple

CRT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, CRT_DIR)

from gen_crt_app_forwarders import CRT_TARGETS, DEFAULT_WINE_DLLS_DIR, parse_wine_spec  # noqa: E402


DEFAULT_GAME_DIRS = [
    "/mnt/files/Downloads/zdoom_1.0.7.0_x64/zdoom_1.0.7.0_x64",
    "/mnt/files/Downloads/HedgePhysics_1.2.3.0/HedgePhysics_1.2.3.0_x64/HedgePhysics_1.2.3.0_x64",
]
DEFAULT_WINE_PE_DIR = "/home/kajitsy/Documents/GitHub/Proton/build/dist/files/lib/wine/x86_64-windows"


def _read_cstr(data: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(data):
        return ""
    end = data.find(b"\x00", offset)
    if end == -1:
        end = len(data)
    return data[offset:end].decode("ascii", errors="replace")


class PE64Inspector:
    def __init__(self, path: str) -> None:
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        self.is_pe64 = False
        self.image_base = 0
        self.data_dirs: List[Tuple[int, int]] = []
        self.sections: List[Tuple[int, int, int, int]] = []
        self._parse()

    def _parse(self) -> None:
        data = self.data
        if len(data) < 0x40 or data[:2] != b"MZ":
            return
        pe_off = struct.unpack_from("<I", data, 0x3C)[0]
        if pe_off + 24 > len(data) or data[pe_off : pe_off + 4] != b"PE\x00\x00":
            return
        _, num_sections, _, _, _, opt_hdr_size, _ = struct.unpack_from("<HHIIIHH", data, pe_off + 4)
        opt_off = pe_off + 24
        if opt_off + 2 > len(data):
            return
        magic = struct.unpack_from("<H", data, opt_off)[0]
        if magic != 0x20B:
            return
        self.is_pe64 = True
        self.image_base = struct.unpack_from("<Q", data, opt_off + 24)[0]
        num_rva_sizes = struct.unpack_from("<I", data, opt_off + 108)[0]
        dd_off = opt_off + 112
        for i in range(min(num_rva_sizes, 16)):
            rva, size = struct.unpack_from("<II", data, dd_off + i * 8)
            self.data_dirs.append((rva, size))
        sec_off = opt_off + opt_hdr_size
        for i in range(num_sections):
            off = sec_off + i * 40
            if off + 40 > len(data):
                break
            vsize, vrva, rsize, rptr = struct.unpack_from("<IIII", data, off + 8)
            self.sections.append((vrva, vsize, rptr, rsize))

    def rva_to_off(self, rva: int) -> int | None:
        if rva == 0:
            return None
        for vrva, vsize, rptr, rsize in self.sections:
            span = max(vsize, rsize)
            if vrva <= rva < vrva + span:
                off = rptr + (rva - vrva)
                if off < len(self.data):
                    return off
        if rva < len(self.data):
            return rva
        return None

    def get_imports(self) -> Dict[str, List[Tuple[str, object]]]:
        result: Dict[str, List[Tuple[str, object]]] = defaultdict(list)
        if not self.is_pe64 or len(self.data_dirs) <= 1:
            return result

        def _read_thunks(thunk_rva: int, dll_key: str) -> None:
            t_off = self.rva_to_off(thunk_rva)
            if t_off is None:
                return
            while t_off + 8 <= len(self.data):
                val = struct.unpack_from("<Q", self.data, t_off)[0]
                if val == 0:
                    break
                if val & (1 << 63):
                    result[dll_key].append(("ord", val & 0xFFFF))
                else:
                    hint_off = self.rva_to_off(val & 0x7FFFFFFF)
                    if hint_off is not None and hint_off + 2 <= len(self.data):
                        result[dll_key].append(("name", _read_cstr(self.data, hint_off + 2)))
                t_off += 8

        imp_rva, _ = self.data_dirs[1]
        if imp_rva != 0:
            off = self.rva_to_off(imp_rva)
            while off is not None and off + 20 <= len(self.data):
                ilt_rva, _, _, name_rva, iat_rva = struct.unpack_from("<IIIII", self.data, off)
                if ilt_rva == 0 and name_rva == 0 and iat_rva == 0:
                    break
                dll_off = self.rva_to_off(name_rva)
                dll_name = _read_cstr(self.data, dll_off).lower() if dll_off is not None else ""
                _read_thunks(ilt_rva if ilt_rva != 0 else iat_rva, dll_name)
                off += 20

        if len(self.data_dirs) > 13:
            d_rva, _ = self.data_dirs[13]
            if d_rva != 0:
                off = self.rva_to_off(d_rva)
                while off is not None and off + 32 <= len(self.data):
                    attrs, dll_name_rva, _, _, int_rva, _, _, _ = struct.unpack_from("<IIIIIIII", self.data, off)
                    if dll_name_rva == 0 and int_rva == 0:
                        break
                    if (attrs & 1) == 0 and dll_name_rva > self.image_base:
                        dll_name_rva -= self.image_base
                        int_rva -= self.image_base
                    dll_off = self.rva_to_off(dll_name_rva)
                    dll_name = _read_cstr(self.data, dll_off).lower() if dll_off is not None else ""
                    _read_thunks(int_rva, dll_name)
                    off += 32

        return result

    def get_exports(self) -> Set[str]:
        names: Set[str] = set()
        if not self.is_pe64 or not self.data_dirs:
            return names
        exp_rva, _ = self.data_dirs[0]
        if exp_rva == 0:
            return names
        off = self.rva_to_off(exp_rva)
        if off is None or off + 40 > len(self.data):
            return names
        _, _, _, _, _, _, num_names, _, addr_names_rva, _ = struct.unpack_from("<IIIIIIIIII", self.data, off)
        names_off = self.rva_to_off(addr_names_rva)
        if names_off is None:
            return names
        for i in range(num_names):
            if names_off + i * 4 + 4 > len(self.data):
                break
            nrva = struct.unpack_from("<I", self.data, names_off + i * 4)[0]
            noff = self.rva_to_off(nrva)
            if noff is not None:
                names.add(_read_cstr(self.data, noff))
        return names


def parse_def_exports(def_path: str) -> Dict[str, Tuple[str, bool, bool]]:
    """
    Returns { export_name: (rhs_target, is_forwarder, is_data) }.
    """
    exports: Dict[str, Tuple[str, bool, bool]] = {}
    in_exports = False
    line_re = re.compile(r'^\s*"([^"]+)"\s*=\s*(?:"([^"]+)"|([A-Za-z_][A-Za-z0-9_]*))(\s+DATA)?\s*$')
    with open(def_path, "r", encoding="utf-8") as f:
        for lineno, raw in enumerate(f, 1):
            s = raw.strip()
            if not s or s.startswith(";"):
                continue
            if s.upper().startswith("LIBRARY "):
                continue
            if s.upper() == "EXPORTS":
                in_exports = True
                continue
            if not in_exports:
                raise AssertionError(f"{def_path}:{lineno}: unexpected line before EXPORTS: {s}")
            m = line_re.match(raw)
            if not m:
                raise AssertionError(f"{def_path}:{lineno}: invalid .def export syntax: {s}")
            lhs = m.group(1)
            rhs_quoted = m.group(2)
            rhs_ident = m.group(3)
            is_data = m.group(4) is not None
            if lhs in exports:
                raise AssertionError(f"{def_path}:{lineno}: duplicate export {lhs}")
            if rhs_quoted is not None:
                exports[lhs] = (rhs_quoted, True, is_data)
            else:
                exports[lhs] = (rhs_ident, False, is_data)
    return exports


def run_audit(wine_dlls_dir: str, wine_pe_dir: str, game_dirs: List[str], require_games: bool) -> int:
    # 1. Check generator parity
    chk = subprocess.run(
        [sys.executable, os.path.join(CRT_DIR, "gen_crt_app_forwarders.py"),
         "--wine-dlls-dir", wine_dlls_dir, "--out-dir", CRT_DIR, "--check"],
        capture_output=True, text=True
    )
    if chk.returncode != 0:
        print(chk.stdout, chk.stderr, file=sys.stderr)
        return 1
    print("[PASS] gen_crt_app_forwarders.py --check")

    # 2. Validate all 7 .def and .c files against Wine .spec and Wine PE64 DLLs
    def_exports_by_dll: Dict[str, Dict[str, Tuple[str, bool, bool]]] = {}
    spec_kinds_by_dll: Dict[str, Dict[str, str]] = {}

    for target in CRT_TARGETS:
        def_path = os.path.join(CRT_DIR, f"{target.app_stem}.def")
        c_path = os.path.join(CRT_DIR, f"{target.app_stem}.c")
        spec_path = os.path.join(wine_dlls_dir, target.wine_name, f"{target.wine_name}.spec")

        def_map = parse_def_exports(def_path)
        with open(c_path, "r", encoding="utf-8") as cf:
            c_text = cf.read()

        spec_map = {e.name: e.kind for e in parse_wine_spec(spec_path)}
        spec_kinds_by_dll[target.app_dll.lower()] = spec_map

        pe_exports: Set[str] | None = None
        wine_pe_path = os.path.join(wine_pe_dir, f"{target.wine_name}.dll")
        if os.path.isfile(wine_pe_path):
            pe_exports = PE64Inspector(wine_pe_path).get_exports()

        local_count = 0
        fwd_count = 0
        for sym, (rhs, is_fwd, _is_data) in def_map.items():
            if is_fwd:
                fwd_count += 1
                mod, target_sym = rhs.split(".", 1)
                assert mod == target.wine_name, f"{target.app_dll}: bad forwarder target {rhs}"
                assert target_sym in spec_map, f"{target.app_dll}: forwarder {rhs} missing in {target.wine_name}.spec"
                if pe_exports is not None:
                    assert target_sym in pe_exports, f"{target.app_dll}: forwarder {rhs} missing in {wine_pe_path}"
            else:
                local_count += 1
                assert rhs in c_text, f"{target.app_dll}: local symbol {rhs} missing from {c_path}"

        def_exports_by_dll[target.app_dll.lower()] = def_map
        print(f"[PASS] {target.app_dll}: {len(def_map)} exports ({fwd_count} forwarded -> {target.wine_name}.dll, {local_count} local)")

    # 3. Scan all game PE64 binaries
    available_dirs = [d for d in game_dirs if os.path.isdir(d)]
    if not available_dirs:
        msg = f"No game directories present at {game_dirs}"
        if require_games:
            print(f"[FAIL] {msg}", file=sys.stderr)
            return 1
        print(f"[SKIP] {msg}")
        return 0

    total_binaries = 0
    total_crt_imports = 0
    missing_symbols = []
    forwarded_to_wine_stub = []

    for gdir in available_dirs:
        for root, _, files in os.walk(gdir):
            for fn in sorted(files):
                if not fn.lower().endswith((".exe", ".dll")):
                    continue
                pe = PE64Inspector(os.path.join(root, fn))
                if not pe.is_pe64:
                    continue
                total_binaries += 1
                imps = pe.get_imports()
                for dll_lower, syms in sorted(imps.items()):
                    if dll_lower not in def_exports_by_dll:
                        continue
                    def_map = def_exports_by_dll[dll_lower]
                    spec_map = spec_kinds_by_dll[dll_lower]
                    for kind, val in syms:
                        total_crt_imports += 1
                        if kind != "name" or str(val) not in def_map:
                            missing_symbols.append((fn, dll_lower, kind, val))
                            continue
                        sym_name = str(val)
                        rhs, is_fwd, _ = def_map[sym_name]
                        if is_fwd and spec_map.get(sym_name) == "stub":
                            forwarded_to_wine_stub.append((fn, dll_lower, sym_name, rhs))

    if missing_symbols:
        print(f"[FAIL] {len(missing_symbols)} unresolved CRT imports:", file=sys.stderr)
        for item in missing_symbols:
            print("  ", item, file=sys.stderr)
        return 1

    if forwarded_to_wine_stub:
        print(f"[FAIL] {len(forwarded_to_wine_stub)} CRT imports still forward to Wine @ stub:", file=sys.stderr)
        for item in forwarded_to_wine_stub:
            print("  ", item, file=sys.stderr)
        return 1

    print(
        f"[PASS] Audited {total_binaries} PE64 binaries across {len(available_dirs)} game directories: "
        f"{total_crt_imports}/{total_crt_imports} CRT *_APP.dll imports resolved (100.0%, 0 missing, 0 Wine @ stub forwards)"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Audit CRT *_APP.dll forwarders against game PE64 imports")
    parser.add_argument("--wine-dlls-dir", default=DEFAULT_WINE_DLLS_DIR)
    parser.add_argument("--wine-pe-dir", default=DEFAULT_WINE_PE_DIR)
    parser.add_argument("--require-games", action="store_true")
    args = parser.parse_args()
    return run_audit(args.wine_dlls_dir, args.wine_pe_dir, DEFAULT_GAME_DIRS, args.require_games)


if __name__ == "__main__":
    sys.exit(main())
