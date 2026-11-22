#!/usr/bin/env python3
"""
CTest: arena_pe64_binary_check
Verifies:
  1. Genuine freestanding PE64 ScarlettArena.exe (MZ, PE\\0\\0, Machine=0x8664, Magic=0x020B)
  2. Real PE64 COFF imports from Scarlett console DLLs (xmem, d3d12_xs, xgameruntime,
     dstorage_xs, xg_xs, xaudio2_9 @1, GameInput @1, pixevt, xgameplatform, etc.)
  3. Strict 148-byte DDS + DX10 headers on package/assets/*.dds (BC7 + RGBA8)
  4. Proton launcher auto-detection (`PROTON_XBOX_SCARLETT=auto`) on staged package
"""

import importlib.machinery
import importlib.util
import os
import shutil
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True


def verify_dds_dx10(path: str, expected_dxgi: int, expected_w: int, expected_h: int) -> None:
    with open(path, "rb") as f:
        data = f.read()
    assert len(data) > 148, f"{path}: size {len(data)} <= 148"
    magic, dw_size, dw_flags, h, w, pitch, depth, mips = struct.unpack_from("<8I", data, 0)
    assert magic == 0x20534444, f"{path}: bad magic 0x{magic:08x}"
    assert dw_size == 124, f"{path}: DDS_HEADER size {dw_size} != 124"
    assert w == expected_w and h == expected_h, f"{path}: dims {w}x{h} != {expected_w}x{expected_h}"
    pf_size, pf_flags, fourcc = struct.unpack_from("<3I", data, 4 + 72)
    assert pf_size == 32, f"{path}: ddspf size {pf_size} != 32"
    assert (pf_flags & 0x4) and fourcc == 0x30315844, f"{path}: missing DX10 fourcc"
    dxgi_fmt, dim, misc, arr, misc2 = struct.unpack_from("<5I", data, 128)
    assert dxgi_fmt == expected_dxgi, f"{path}: dxgiFormat {dxgi_fmt} != {expected_dxgi}"
    assert dim == 3 and arr == 1, f"{path}: invalid DX10 dim/arr ({dim}/{arr})"

    if expected_dxgi == 98:  # BC7
        expected_payload = ((w + 3) // 4) * ((h + 3) // 4) * 16
    elif expected_dxgi == 28:  # RGBA8
        expected_payload = w * h * 4
    else:
        raise AssertionError(f"Unsupported dxgi_format {expected_dxgi}")
    assert len(data) == 148 + expected_payload, (
        f"{path}: total size {len(data)} != 148 + {expected_payload}"
    )


def verify_pe64_exe(exe_path: str, llvm_readobj: str) -> None:
    with open(exe_path, "rb") as f:
        b = f.read()
    assert b[:2] == b"MZ", "Missing MZ header"
    pe_off = struct.unpack_from("<I", b, 0x3C)[0]
    assert b[pe_off : pe_off + 4] == b"PE\x00\x00", "Missing PE signature"
    machine = struct.unpack_from("<H", b, pe_off + 4)[0]
    opt_magic = struct.unpack_from("<H", b, pe_off + 24)[0]
    assert machine == 0x8664, f"Expected AMD64 0x8664, got 0x{machine:04x}"
    assert opt_magic == 0x020B, f"Expected PE32+ magic 0x020B, got 0x{opt_magic:04x}"

    if llvm_readobj and os.path.isfile(llvm_readobj):
        out = subprocess.check_output([llvm_readobj, "--coff-imports", exe_path], text=True)
        for dll in (
            "xmem.dll",
            "d3d12_xs.dll",
            "xgameruntime.dll",
            "dstorage_xs.dll",
            "xg_xs.dll",
            "xaudio2_9.dll",
            "GameInput.dll",
            "pixevt.dll",
            "xgameplatform.dll",
            "amd_fidelityfx_upscaler_xbox.dll",
        ):
            assert dll in out, f"Missing COFF import for {dll} in ScarlettArena.exe"


def main() -> int:
    if len(sys.argv) < 4:
        print("Usage: test_arena_pe64_and_dds.py <repo_root> <package_dir> <pe64_exe> [llvm-readobj]",
              file=sys.stderr)
        return 2

    repo_root = os.path.abspath(sys.argv[1])
    package_dir = os.path.abspath(sys.argv[2])
    pe64_exe = os.path.abspath(sys.argv[3])
    llvm_readobj = sys.argv[4] if len(sys.argv) > 4 else ""

    assert os.path.isfile(pe64_exe), f"Missing built PE64 executable: {pe64_exe}"
    verify_pe64_exe(pe64_exe, llvm_readobj)

    bc7_path = os.path.join(package_dir, "assets", "arena_albedo_bc7.dds")
    rgba_path = os.path.join(package_dir, "assets", "arena_ui_rgba8.dds")
    verify_dds_dx10(bc7_path, 98, 16, 16)
    verify_dds_dx10(rgba_path, 28, 16, 16)

    # Load repo `proton` module and verify real auto-detection on staged package
    if repo_root not in sys.path:
        sys.path.insert(0, repo_root)
    loader = importlib.machinery.SourceFileLoader("proton_script", os.path.join(repo_root, "proton"))
    spec = importlib.util.spec_from_loader("proton_script", loader)
    proton_mod = importlib.util.module_from_spec(spec)
    loader.exec_module(proton_mod)

    with tempfile.TemporaryDirectory(prefix="scarlett_arena_pkg_stage_") as tmpdir:
        staged_cfg = os.path.join(tmpdir, "MicrosoftGame.Config")
        staged_exe = os.path.join(tmpdir, "ScarlettArena.exe")
        shutil.copy2(os.path.join(package_dir, "MicrosoftGame.Config"), staged_cfg)
        shutil.copy2(pe64_exe, staged_exe)

        assert proton_mod.is_pe64_binary(staged_exe), "proton.is_pe64_binary failed on ScarlettArena.exe"
        assert proton_mod.is_scarlett_game_config(staged_cfg), "proton.is_scarlett_game_config failed"
        assert proton_mod.detect_scarlett_from_executable(staged_exe), "proton auto-detect failed"

        for model in ("anaconda", "lockhart"):
            env = {
                "PROTON_XBOX_MODEL": model,
                "WINEDLLPATH": "/opt/proton/lib/wine",
                "LD_LIBRARY_PATH": "/opt/proton/lib",
            }
            overrides = {"steam.exe": "b"}
            activated = proton_mod.apply_scarlett_environment(
                env, overrides, "/opt/proton/lib", os.path.join(tmpdir, "pfx"), argv=[staged_exe]
            )
            assert activated, f"apply_scarlett_environment did not activate for {model}"
            assert env["PROTON_XBOX_MODEL"] == model
            assert overrides.get("d3d12_xs") == "b"
            assert overrides.get("xgameruntime") == "b"
            assert overrides.get("xaudio2_9") == "b"
            assert overrides.get("GameInput") == "b"

    print("[PASS] arena_pe64_binary_check: ScarlettArena.exe PE64 (0x020B + COFF imports), "
          "148B DDS+DX10 (BC7+RGBA8), and proton auto-detection verified.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
