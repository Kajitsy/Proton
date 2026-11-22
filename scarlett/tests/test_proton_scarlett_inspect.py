#!/usr/bin/env python3
"""
CTest: test_proton_scarlett_inspect
Verifies `scarlett/tools/proton-scarlett-inspect` on:
  1. Staged ScarlettArena-Linux package (`scarlett/tests/arena/package/` + `ScarlettArena.exe`)
  2. Source `scarlett/tests/arena/package/` with `--app-db=...`
  3. Synthetic encrypted XVC image (`SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED` + non-zero under `--strict`)
  4. Synthetic unencrypted XVC image + UE5 profile heuristic matching
"""

from __future__ import annotations

import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True


def write_synthetic_pe64(path: str, marker_bytes: bytes = b"") -> None:
    buf = bytearray(0x400 + len(marker_bytes))
    buf[0:2] = b"MZ"
    pe_off = 0x80
    struct.pack_into("<I", buf, 0x3C, pe_off)
    buf[pe_off : pe_off + 4] = b"PE\x00\x00"
    struct.pack_into("<HHIIIHH", buf, pe_off + 4, 0x8664, 1, 0, 0, 0, 240, 0x22)
    struct.pack_into("<H", buf, pe_off + 24, 0x020B)
    struct.pack_into("<I", buf, pe_off + 24 + 56, 0x2000)
    struct.pack_into("<H", buf, pe_off + 24 + 68, 3)
    struct.pack_into("<I", buf, pe_off + 24 + 108, 16)
    if marker_bytes:
        buf[0x400 : 0x400 + len(marker_bytes)] = marker_bytes
    with open(path, "wb") as f:
        f.write(buf)


def write_synthetic_xvc(path: str, *, encrypted: bool) -> None:
    total_size = 0x3000
    buf = bytearray(total_size)
    # 0x200: XVD header ("msft-xvd")
    buf[0x200:0x208] = b"msft-xvd"
    volume_flags = 0x00000001 if encrypted else 0x00000003  # bit 1 = ENCRYPTION_DISABLED
    if encrypted:
        volume_flags |= 0x00000010  # ENCRYPTED_EXPLICIT
    struct.pack_into("<II", buf, 0x208, volume_flags, 3)
    struct.pack_into("<Q", buf, 0x218, total_size)
    struct.pack_into("<7I", buf, 0x284, 0, 0, 0, 0, 80, 0, 0x1000)
    buf[0x2A0:0x2A8] = b"SCARLETT"

    # 0x1000: XVC Block 1 header ("XVCI")
    struct.pack_into("<II", buf, 0x1000, 0x49435658, 1)
    if encrypted:
        struct.pack_into("<6I", buf, 0x1018, 1, 1, 0, 0, 0, 0x10000)
        buf[0x1030:0x1040] = b"\xAA" * 16
    else:
        struct.pack_into("<6I", buf, 0x1018, 0, 0, 0, 0, 0, 0x10000)

    with open(path, "wb") as f:
        f.write(buf)


def run_inspect(tool_path: str, args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, tool_path, *args],
        text=True,
        capture_output=True,
        check=False,
    )


def main() -> int:
    if len(sys.argv) < 4:
        print(
            "Usage: test_proton_scarlett_inspect.py <repo_root> <package_dir> <pe64_exe>",
            file=sys.stderr,
        )
        return 2

    repo_root = os.path.abspath(sys.argv[1])
    package_dir = os.path.abspath(sys.argv[2])
    pe64_exe = os.path.abspath(sys.argv[3])
    tool_path = os.path.join(repo_root, "scarlett", "tools", "proton-scarlett-inspect")
    app_db_path = os.path.join(repo_root, "scarlett", "profiles", "scarlett_app_db.toml")

    assert os.path.isfile(tool_path), f"Missing CLI tool: {tool_path}"
    assert os.access(tool_path, os.X_OK), f"CLI tool is not executable: {tool_path}"
    assert os.path.isfile(app_db_path), f"Missing app DB: {app_db_path}"

    # 1. Staged ScarlettArena-Linux package + ScarlettArena.exe (--json --strict)
    with tempfile.TemporaryDirectory(prefix="scarlett_inspect_stage_") as tmpdir:
        shutil.copytree(package_dir, tmpdir, dirs_exist_ok=True)
        if os.path.isfile(pe64_exe):
            shutil.copy2(pe64_exe, os.path.join(tmpdir, "ScarlettArena.exe"))

        res = run_inspect(tool_path, [tmpdir, "--json", "--strict", f"--app-db={app_db_path}"])
        assert res.returncode == 0, f"--strict failed on staged package:\n{res.stdout}\n{res.stderr}"
        data = json.loads(res.stdout)
        assert data["status"] == "PASS"
        assert data["has_encrypted_xvc"] is False
        assert data["microsoft_game_config"]["is_scarlett_target"] is True
        assert data["microsoft_game_config"]["target_device_family_normalized"] == "Gaming.Xbox.Scarlett.x64"
        assert data["microsoft_game_config"]["title_id"] == "5CA11E77"
        assert data["microsoft_game_config"]["msa_app_id"] == "000000004C2B5CA1"
        assert len(data["dds_dx10_assets"]) == 2
        assert all(a["valid_148b_dds_dx10"] and a["header_bytes"] == 148 for a in data["dds_dx10_assets"])
        assert data["app_db"]["matched_profile"] == "scarlett_arena_linux"
        assert "PROTON_XBOX_MODEL" in data["recommended_env"]
        assert "xgameruntime=b" in data["recommended_winedlloverrides"]
        assert "d3d12_xs=b" in data["recommended_winedlloverrides"]

        if os.path.isfile(pe64_exe):
            assert len(data["pe64_binaries"]) == 1
            pe = data["pe64_binaries"][0]
            assert pe["valid_pe64"] is True
            assert pe["machine"] == "0x8664"
            assert pe["optional_magic"] == "0x020B"
            assert pe["missing_scarlett_exports"] == []
            imported_dlls = {imp["dll"].lower() for imp in pe["scarlett_imports"]}
            for expected_dll in (
                "xmem.dll",
                "d3d12_xs.dll",
                "xgameruntime.dll",
                "dstorage_xs.dll",
                "xg_xs.dll",
                "pixevt.dll",
                "xgameplatform.dll",
                "xaudio2_9.dll",
                "gameinput.dll",
                "amd_fidelityfx_upscaler_xbox.dll",
            ):
                assert expected_dll in imported_dlls, f"Missing {expected_dll} in PE64 imports"

        # Also test human-readable output on staged directory
        res_human = run_inspect(tool_path, [tmpdir])
        assert res_human.returncode == 0
        assert "Gaming.Xbox.Scarlett.x64" in res_human.stdout or "Scarlett" in res_human.stdout
        assert "WINEDLLOVERRIDES" in res_human.stdout

    # 2. Direct inspection of source package dir (without ScarlettArena.exe)
    res_pkg = run_inspect(tool_path, [package_dir, "--json"])
    assert res_pkg.returncode == 0
    pkg_data = json.loads(res_pkg.stdout)
    assert pkg_data["microsoft_game_config"]["is_scarlett_target"] is True
    assert all(a["valid_148b_dds_dx10"] for a in pkg_data["dds_dx10_assets"])

    # 3. Synthetic encrypted XVC package -> SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED + strict non-zero
    with tempfile.TemporaryDirectory(prefix="scarlett_inspect_enc_xvc_") as tmpdir:
        with open(os.path.join(tmpdir, "MicrosoftGame.Config"), "w", encoding="utf-8") as f:
            f.write(
                '<?xml version="1.0" encoding="utf-8"?>\n'
                '<Game configVersion="1">\n'
                '  <ExecutableList>\n'
                '    <Executable Name="EncryptedGame.exe" TargetDeviceFamily="XboxGen9"/>\n'
                '  </ExecutableList>\n'
                '  <TitleId>1234ABCD</TitleId>\n'
                '</Game>\n'
            )
        write_synthetic_pe64(os.path.join(tmpdir, "EncryptedGame.exe"), b"UnrealEngine Nanite Lumen")
        write_synthetic_xvc(os.path.join(tmpdir, "Content.xvc"), encrypted=True)

        res_non_strict = run_inspect(tool_path, [tmpdir, "--json"])
        assert res_non_strict.returncode == 0
        enc_data = json.loads(res_non_strict.stdout)
        assert enc_data["has_encrypted_xvc"] is True
        assert "SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED" in enc_data["errors"]
        assert enc_data["app_db"]["matched_profile"] == "ue5_gen9"

        res_strict = run_inspect(tool_path, [tmpdir, "--json", "--strict"])
        assert res_strict.returncode != 0, "Expected non-zero exit code under --strict for encrypted XVC"
        enc_strict = json.loads(res_strict.stdout)
        assert "SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED" in enc_strict["errors"]

    # 4. Synthetic unencrypted XVC package -> PASS under --strict
    with tempfile.TemporaryDirectory(prefix="scarlett_inspect_plain_xvc_") as tmpdir:
        with open(os.path.join(tmpdir, "MicrosoftGame.Config"), "w", encoding="utf-8") as f:
            f.write(
                '<?xml version="1.0" encoding="utf-8"?>\n'
                '<Game configVersion="1">\n'
                '  <ExecutableList>\n'
                '    <Executable Name="PlainGame.exe" TargetDeviceFamily="Gaming.Xbox.Scarlett.x64"/>\n'
                '  </ExecutableList>\n'
                '</Game>\n'
            )
        write_synthetic_pe64(os.path.join(tmpdir, "PlainGame.exe"), b"RAGE9 RageEngine")
        write_synthetic_xvc(os.path.join(tmpdir, "PlainContent.xvc"), encrypted=False)

        res_plain = run_inspect(tool_path, [tmpdir, "--json", "--strict"])
        assert res_plain.returncode == 0, f"Unencrypted XVC failed:\n{res_plain.stdout}\n{res_plain.stderr}"
        plain_data = json.loads(res_plain.stdout)
        assert plain_data["status"] == "PASS"
        assert plain_data["has_encrypted_xvc"] is False
        assert plain_data["app_db"]["matched_profile"] == "rage9_gen9"

    print("[PASS] test_proton_scarlett_inspect: PE64 imports, 148B DDS+DX10, "
          "encrypted XVC rejection, and app_db matching verified.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
