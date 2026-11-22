#!/usr/bin/env python3
"""
Host verification test for all 14 system *_APP.dll + XINPUTUAP.dll forwarders,
UnityPlayer.dll XINPUTUAP import coverage, and kernelx.def export completeness.
"""

from __future__ import annotations

import pathlib
import re
import struct
import subprocess
import sys
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
SYS_DIR = THIS_DIR.parent
FWD_DIR = SYS_DIR.parent
SCARLETT_DIR = FWD_DIR.parent.parent

EXPECTED_DLLS = (
    "KERNEL32_APP",
    "USER32_APP",
    "GDI32_APP",
    "ADVAPI32_APP",
    "OLE32_APP",
    "COMBASE_APP",
    "RPCRT4_APP",
    "WS2_32_APP",
    "WINHTTP_APP",
    "CRYPT32_APP",
    "BCRYPT_APP",
    "MMDEVAPI_APP",
    "XINPUT1_4_APP",
    "XINPUTUAP",
)

UNITY_PLAYER_PATH = pathlib.Path(
    "/mnt/files/Downloads/HedgePhysics_1.2.3.0/HedgePhysics_1.2.3.0_x64/"
    "HedgePhysics_1.2.3.0_x64/UnityPlayer.dll"
)


def parse_def(path: pathlib.Path):
    library = None
    in_exports = False
    exports = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith(";"):
            continue
        if line.upper().startswith("LIBRARY "):
            library = line.split(None, 1)[1].strip()
            continue
        if line.upper() == "EXPORTS":
            in_exports = True
            continue
        if not in_exports:
            continue

        m = re.match(
            r'^([^\s=]+)(?:\s*=\s*("[^"]+"|[^\s]+))?(?:\s+@(\d+))?(?:\s+(NONAME|DATA))*\s*$',
            line,
        )
        if not m:
            raise AssertionError(f"Failed to parse .def line in {path.name}: {line!r}")
        name = m.group(1)
        target = m.group(2)
        ordinal = int(m.group(3)) if m.group(3) else None
        is_noname = "NONAME" in line.split()
        exports.append((name, target, ordinal, is_noname))
    return library, exports


def read_pe_imports(path: pathlib.Path):
    data = path.read_bytes()
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[e_lfanew:e_lfanew + 4] == b"PE\0\0"
    num_sec = struct.unpack_from("<H", data, e_lfanew + 6)[0]
    opt_hdr_size = struct.unpack_from("<H", data, e_lfanew + 20)[0]
    opt_off = e_lfanew + 24
    magic = struct.unpack_from("<H", data, opt_off)[0]
    is64 = magic == 0x20B
    dd_off = opt_off + (112 if is64 else 96)
    imp_rva, _ = struct.unpack_from("<II", data, dd_off + 8)
    sec_off = opt_off + opt_hdr_size
    sections = []
    for i in range(num_sec):
        s = sec_off + i * 40
        vsz, va, rsz, rptr = struct.unpack_from("<IIII", data, s + 8)
        sections.append((va, max(vsz, rsz), rptr))

    def rva2off(rva: int) -> int:
        for va, sz, rptr in sections:
            if va <= rva < va + sz:
                return rptr + (rva - va)
        raise ValueError(f"RVA 0x{rva:x} not mapped")

    def read_cstr(off: int) -> str:
        end = data.find(b"\0", off)
        return data[off:end].decode("ascii", "replace")

    imports = {}
    if imp_rva == 0:
        return imports
    off = rva2off(imp_rva)
    while True:
        oft, _, _, name_rva, ft = struct.unpack_from("<IIIII", data, off)
        off += 20
        if name_rva == 0:
            break
        dll = read_cstr(rva2off(name_rva))
        toff = rva2off(oft if oft else ft)
        syms = []
        while True:
            val = struct.unpack_from("<Q" if is64 else "<I", data, toff)[0]
            toff += 8 if is64 else 4
            if val == 0:
                break
            flag = (1 << 63) if is64 else (1 << 31)
            if val & flag:
                syms.append(f"#{val & 0xFFFF}")
            else:
                syms.append(read_cstr(rva2off(val & 0x7FFFFFFF) + 2))
        imports.setdefault(dll, []).extend(syms)
    return imports


class SysForwardersTests(unittest.TestCase):
    def test_all_14_def_and_c_files_exist_and_parse(self):
        self.assertEqual(len(EXPECTED_DLLS), 14)
        for stem in EXPECTED_DLLS:
            def_path = SYS_DIR / f"{stem}.def"
            c_path = SYS_DIR / f"{stem}.c"
            self.assertTrue(def_path.is_file(), f"Missing {def_path}")
            self.assertTrue(c_path.is_file(), f"Missing {c_path}")

            lib, exports = parse_def(def_path)
            self.assertEqual(lib, f"{stem}.dll")
            self.assertGreater(len(exports), 0, f"{stem}.def has 0 exports")

            seen_names = set()
            seen_ordinals = set()
            named_count = 0
            for name, target, ordinal, is_noname in exports:
                self.assertNotIn(name, seen_names, f"Duplicate symbol {name} in {stem}.def")
                seen_names.add(name)
                if ordinal is not None:
                    self.assertNotIn(
                        ordinal, seen_ordinals, f"Duplicate ordinal {ordinal} in {stem}.def"
                    )
                    seen_ordinals.add(ordinal)
                else:
                    named_count += 1
                self.assertIsNotNone(target, f"Missing target for {name} in {stem}.def")

            for ordinal in seen_ordinals:
                self.assertGreater(
                    ordinal,
                    named_count,
                    f"Ordinal {ordinal} collides with auto-assigned 1..{named_count} in {stem}.def",
                )

    def test_generator_check_mode_matches_checked_in_files(self):
        wine_dlls = pathlib.Path("/home/kajitsy/Documents/GitHub/Proton/wine/dlls")
        if not wine_dlls.is_dir():
            self.skipTest("Wine dlls directory not present")
        res = subprocess.run(
            [sys.executable, str(FWD_DIR / "gen_app_forwarders.py"), "--check"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(res.returncode, 0, res.stderr + res.stdout)

    def test_kernel32_app_and_kernelx_export_package_identity_and_app_policy(self):
        required = {
            "GetCurrentPackageFullName",
            "GetCurrentPackageId",
            "GetCurrentPackageFamilyName",
            "GetCurrentApplicationUserModelId",
            "GetPackagePathByFullName",
            "GetCurrentPackagePath",
            "AppPolicyGetProcessTerminationMethod",
            "AppPolicyGetWindowingModel",
        }
        _, k32_exports = parse_def(SYS_DIR / "KERNEL32_APP.def")
        k32_names = {e[0] for e in k32_exports}
        self.assertTrue(required.issubset(k32_names), f"Missing in KERNEL32_APP.def: {required - k32_names}")

        _, kx_exports = parse_def(SCARLETT_DIR / "xmem" / "kernelx.def")
        kx_names = {e[0] for e in kx_exports}
        self.assertEqual(len(kx_names), len(kx_exports), "Duplicate symbol in kernelx.def")
        self.assertTrue(required.issubset(kx_names), f"Missing in kernelx.def: {required - kx_names}")
        # Preserve original 24 kernelx exports
        self.assertIn("TitleMemoryStatus", kx_names)
        self.assertIn("MapTitleEsramPages", kx_names)
        self.assertIn("KernelXCloseSyncHandle", kx_names)
        self.assertGreaterEqual(len(kx_names), 32)

    def test_unityplayer_xinputuap_imports_covered(self):
        _, xuap_exports = parse_def(SYS_DIR / "XINPUTUAP.def")
        xuap_names = {e[0] for e in xuap_exports}
        self.assertIn("XInputGetState", xuap_names)
        if UNITY_PLAYER_PATH.is_file():
            imports = read_pe_imports(UNITY_PLAYER_PATH)
            xinput_key = next((k for k in imports if k.lower() == "xinputuap.dll"), None)
            self.assertIsNotNone(xinput_key, "UnityPlayer.dll expected to import XINPUTUAP.dll")
            missing = [sym for sym in imports[xinput_key] if sym not in xuap_names]
            self.assertEqual(missing, [], f"Missing XINPUTUAP exports for UnityPlayer.dll: {missing}")

    def test_pe64_c_files_contain_no_posix_headers(self):
        forbidden = ("<unistd.h>", "<dlfcn.h>", "<pthread.h>", "<sys/socket.h>", "<fcntl.h>")
        for stem in EXPECTED_DLLS:
            c_text = (SYS_DIR / f"{stem}.c").read_text(encoding="utf-8")
            for hdr in forbidden:
                self.assertNotIn(hdr, c_text, f"POSIX header {hdr} found in {stem}.c")


if __name__ == "__main__":
    unittest.main(verbosity=2)
