#!/usr/bin/env python3
"""Unit tests for Proton-Scarlett environment integration in proton and default_pfx.py."""

import importlib.machinery
import importlib.util
import os
import struct
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True


REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)


def load_script_module(name, rel_path):
    full_path = os.path.join(REPO_ROOT, rel_path)
    loader = importlib.machinery.SourceFileLoader(name, full_path)
    spec = importlib.util.spec_from_loader(name, loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


proton_mod = load_script_module("proton_script", "proton")
default_pfx_mod = load_script_module("default_pfx_script", "default_pfx.py")


def create_synthetic_pe(path, *, is_64bit=True, wine_builtin_tag=False):
    buf = bytearray(0x200)
    buf[0:2] = b"MZ"
    pe_offset = 0x80
    struct.pack_into("<I", buf, 0x3C, pe_offset)
    if wine_builtin_tag:
        tag = b"Wine builtin DLL\x00"
        buf[0x40 : 0x40 + len(tag)] = tag
    buf[pe_offset : pe_offset + 4] = b"PE\x00\x00"
    machine = 0x8664 if is_64bit else 0x014C
    struct.pack_into("<H", buf, pe_offset + 4, machine)
    opt_magic = 0x020B if is_64bit else 0x010B
    struct.pack_into("<H", buf, pe_offset + 24, opt_magic)
    with open(path, "wb") as f:
        f.write(buf)


class TestProtonScarlettEnvironment(unittest.TestCase):
    EXPECTED_CONSOLE_DLLS = (
        "xgameruntime",
        "xgameplatform",
        "d3d12_xs",
        "xg_xs",
        "xmem",
        "kernelx",
        "pixevt",
        "dstorage_xs",
        "XboxBvhBuilder",
        "GameInput",
        "xaudio2_9",
        "AcpHal",
        "Bink2Scarlett",
        "amd_fidelityfx_loader_xbox",
        "amd_fidelityfx_upscaler_xbox",
        "libHttpClient",
        "XCurl",
        "api-ms-win-core-psm-appnotify-l1-1-0",
    )

    def test_pe64_binary_detection(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            pe64 = os.path.join(tmpdir, "GameScarlett.exe")
            pe32 = os.path.join(tmpdir, "Launcher32.exe")
            not_pe = os.path.join(tmpdir, "script.sh")

            create_synthetic_pe(pe64, is_64bit=True)
            create_synthetic_pe(pe32, is_64bit=False)
            with open(not_pe, "wb") as f:
                f.write(b"\x7fELF\x02\x01\x01\x00")

            self.assertTrue(proton_mod.is_pe64_binary(pe64))
            self.assertFalse(proton_mod.is_pe64_binary(pe32))
            self.assertFalse(proton_mod.is_pe64_binary(not_pe))

    def test_standard_pc_launch_is_unmodified_without_config(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            pc_exe = os.path.join(tmpdir, "PCGame.exe")
            create_synthetic_pe(pc_exe, is_64bit=True)

            env = {
                "WINEDLLPATH": "/opt/proton/files/lib/vkd3d:/opt/proton/files/lib/wine",
                "LD_LIBRARY_PATH": "/opt/proton/files/lib/x86_64-linux-gnu",
            }
            orig_env = dict(env)
            dlloverrides = {"steam.exe": "b", "d3d12": "n"}
            orig_overrides = dict(dlloverrides)

            activated = proton_mod.apply_scarlett_environment(
                env,
                dlloverrides,
                "/opt/proton/files/lib",
                "/tmp/compatdata/pfx",
                argv=[pc_exe],
            )
            self.assertFalse(activated)
            self.assertEqual(env, orig_env)
            self.assertEqual(dlloverrides, orig_overrides)

    def test_pc_microsoft_game_config_does_not_enable_scarlett(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            pc_exe = os.path.join(tmpdir, "WinGDKGame.exe")
            create_synthetic_pe(pc_exe, is_64bit=True)
            cfg_path = os.path.join(tmpdir, "MicrosoftGame.Config")
            with open(cfg_path, "w", encoding="utf-8") as f:
                f.write(
                    '<?xml version="1.0" encoding="utf-8"?>\n'
                    '<Game configVersion="1">\n'
                    '  <ExecutableList>\n'
                    '    <Executable Name="WinGDKGame.exe" TargetDeviceFamily="Windows.Desktop"/>\n'
                    '  </ExecutableList>\n'
                    '</Game>\n'
                )

            env = {}
            dlloverrides = {"steam.exe": "b"}
            activated = proton_mod.apply_scarlett_environment(
                env,
                dlloverrides,
                "/opt/proton/files/lib",
                "/tmp/compatdata/pfx",
                argv=[pc_exe],
            )
            self.assertFalse(activated)
            self.assertNotIn("PROTON_XBOX_MODEL", env)
            self.assertNotIn("xgameruntime", dlloverrides)

    def test_auto_detection_with_scarlett_and_xboxgen9_configs(self):
        for family in ("Scarlett", "Gaming.Xbox.Scarlett.x64", "XboxGen9"):
            with self.subTest(family=family), tempfile.TemporaryDirectory() as tmpdir:
                exe_path = os.path.join(tmpdir, "Title.exe")
                create_synthetic_pe(exe_path, is_64bit=True)
                cfg_path = os.path.join(tmpdir, "MicrosoftGame.Config")
                with open(cfg_path, "w", encoding="utf-8") as f:
                    f.write(
                        '<?xml version="1.0" encoding="utf-8"?>\n'
                        '<Game configVersion="1">\n'
                        f'  <TargetDeviceFamily Name="{family}"/>\n'
                        '</Game>\n'
                    )

                env = {
                    "WINEDLLPATH": "/opt/proton/files/lib/vkd3d:/opt/proton/files/lib/wine",
                    "LD_LIBRARY_PATH": "/opt/proton/files/lib/x86_64-linux-gnu",
                }
                dlloverrides = {"steam.exe": "b", "gameinput": "d"}
                activated = proton_mod.apply_scarlett_environment(
                    env,
                    dlloverrides,
                    "/opt/proton/files/lib",
                    "/tmp/compatdata/pfx",
                    argv=[exe_path],
                )
                self.assertTrue(activated)
                self.assertEqual(env["PROTON_XBOX_SCARLETT"], "auto")
                self.assertEqual(env["PROTON_XBOX_MODEL"], "anaconda")
                self.assertEqual(
                    env["PROTON_XBOX_OVERLAY_DIR"],
                    "/tmp/compatdata/pfx/drive_c/scarlett_overlay",
                )
                self.assertEqual(env["PROTON_XBOX_SHADER_MODE"], "adaptive")
                self.assertEqual(env["PROTON_XBOX_STRICT"], "0")
                self.assertEqual(env["PROTON_XBOX_NET_MODE"], "offline")

                self.assertIn(
                    "/opt/proton/files/lib/wine/scarlett/x86_64-windows",
                    env["WINEDLLPATH"].split(":"),
                )
                self.assertIn(
                    "/opt/proton/files/lib/wine/scarlett/x86_64-unix",
                    env["WINEDLLPATH"].split(":"),
                )
                self.assertIn(
                    "/opt/proton/files/lib/wine/scarlett/x86_64-unix",
                    env["LD_LIBRARY_PATH"].split(":"),
                )

                self.assertNotIn("gameinput", dlloverrides)
                for dll in self.EXPECTED_CONSOLE_DLLS:
                    self.assertEqual(dlloverrides.get(dll), "b", f"Missing =b override for {dll}")

    def test_pe32_with_scarlett_config_is_rejected_in_auto_mode(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            pe32_exe = os.path.join(tmpdir, "Title32.exe")
            create_synthetic_pe(pe32_exe, is_64bit=False)
            cfg_path = os.path.join(tmpdir, "MicrosoftGame.Config")
            with open(cfg_path, "w", encoding="utf-8") as f:
                f.write('<Game><TargetDeviceFamily Name="Scarlett"/></Game>')

            self.assertFalse(proton_mod.resolve_scarlett_mode({}, [pe32_exe]))

    def test_explicit_0_and_1_overrides_and_ca_certs_forwarding(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            exe_path = os.path.join(tmpdir, "Title.exe")
            create_synthetic_pe(exe_path, is_64bit=True)
            cfg_path = os.path.join(tmpdir, "MicrosoftGame.Config")
            with open(cfg_path, "w", encoding="utf-8") as f:
                f.write('<Game><TargetDeviceFamily Name="Scarlett"/></Game>')

            # PROTON_XBOX_SCARLETT=0 forcibly disables even when MicrosoftGame.Config is present
            env_off = {"PROTON_XBOX_SCARLETT": "0"}
            overrides_off = {}
            self.assertFalse(
                proton_mod.apply_scarlett_environment(
                    env_off, overrides_off, "/opt/proton/files/lib", "/tmp/pfx", argv=[exe_path]
                )
            )
            self.assertEqual(overrides_off, {})

            # PROTON_XBOX_SCARLETT=1 forcibly enables and forwards CA certs in passthrough/lan mode
            env_on = {
                "PROTON_XBOX_SCARLETT": "1",
                "PROTON_XBOX_MODEL": "lockhart",
                "PROTON_XBOX_SHADER_MODE": "exact",
                "PROTON_XBOX_STRICT": "1",
                "PROTON_XBOX_BCPACK_PLUGIN": "/opt/plugins/libbcpack.so",
                "PROTON_XBOX_NET_MODE": "passthrough",
                "SSL_CERT_FILE": "/etc/ssl/certs/custom-ca.crt",
                "SSL_CERT_DIR": "/etc/ssl/certs",
            }
            overrides_on = {}
            self.assertTrue(
                proton_mod.apply_scarlett_environment(
                    env_on, overrides_on, "/opt/proton/files/lib", "/tmp/pfx", argv=[]
                )
            )
            self.assertEqual(env_on["PROTON_XBOX_MODEL"], "lockhart")
            self.assertEqual(env_on["PROTON_XBOX_SHADER_MODE"], "exact")
            self.assertEqual(env_on["PROTON_XBOX_STRICT"], "1")
            self.assertEqual(env_on["PROTON_XBOX_BCPACK_PLUGIN"], "/opt/plugins/libbcpack.so")
            self.assertEqual(env_on["PROTON_XBOX_NET_MODE"], "passthrough")
            self.assertEqual(env_on["PRESSURE_VESSEL_IMPORT_CA_CERTS"], "1")
            self.assertIn(
                "/etc/ssl/certs/custom-ca.crt",
                env_on["PRESSURE_VESSEL_FILESYSTEMS_RO"].split(":"),
            )
            self.assertIn(
                "/etc/ssl/certs",
                env_on["PRESSURE_VESSEL_FILESYSTEMS_RO"].split(":"),
            )

    def test_default_pfx_scarlett_integration(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            dist_dir = os.path.join(tmpdir, "dist")
            pfx_dir = os.path.join(tmpdir, "default_pfx")
            sys32_dir = os.path.join(pfx_dir, "drive_c", "windows", "system32")
            wine_pe_dir = os.path.join(dist_dir, "lib", "wine", "x86_64-windows")
            scarlett_pe_dir = os.path.join(dist_dir, "lib", "wine", "scarlett", "x86_64-windows")
            scarlett_unix_dir = os.path.join(dist_dir, "lib", "wine", "scarlett", "x86_64-unix")

            os.makedirs(sys32_dir)
            os.makedirs(wine_pe_dir)

            # Without scarlett directory present
            winedllpath_plain = default_pfx_mod.build_default_pfx_winedllpath(dist_dir, arm64=False)
            self.assertEqual(winedllpath_plain, dist_dir + "/lib/vkd3d")

            # With scarlett directory present
            os.makedirs(scarlett_pe_dir)
            os.makedirs(scarlett_unix_dir)
            winedllpath_scarlett = default_pfx_mod.build_default_pfx_winedllpath(dist_dir, arm64=False)
            self.assertIn(scarlett_pe_dir, winedllpath_scarlett.split(":"))
            self.assertIn(scarlett_unix_dir, winedllpath_scarlett.split(":"))

            # Test setup_dll_symlinks for both standard Wine DLL and Scarlett builtin DLL
            create_synthetic_pe(os.path.join(wine_pe_dir, "kernel32.dll"), is_64bit=True, wine_builtin_tag=True)
            create_synthetic_pe(os.path.join(scarlett_pe_dir, "xgameruntime.dll"), is_64bit=True, wine_builtin_tag=True)
            create_synthetic_pe(os.path.join(sys32_dir, "kernel32.dll"), is_64bit=True, wine_builtin_tag=True)
            create_synthetic_pe(os.path.join(sys32_dir, "xgameruntime.dll"), is_64bit=True, wine_builtin_tag=True)

            default_pfx_mod.setup_dll_symlinks(pfx_dir, dist_dir, arm64=False)

            k32_link = os.path.join(sys32_dir, "kernel32.dll")
            xgr_link = os.path.join(sys32_dir, "xgameruntime.dll")
            self.assertTrue(os.path.islink(k32_link))
            self.assertTrue(os.path.islink(xgr_link))
            self.assertEqual(
                os.path.realpath(k32_link),
                os.path.realpath(os.path.join(wine_pe_dir, "kernel32.dll")),
            )
            self.assertEqual(
                os.path.realpath(xgr_link),
                os.path.realpath(os.path.join(scarlett_pe_dir, "xgameruntime.dll")),
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
