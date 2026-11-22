#!/usr/bin/env python3
"""Unit tests for Proton-Scarlett UWP Bridge detection and environment integration."""

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


proton_mod = load_script_module("proton_uwp_script", "proton")

GZDOOM_EXE = "/mnt/files/Downloads/zdoom_1.0.7.0_x64/zdoom_1.0.7.0_x64/gzdoom.exe"
HEDGEPHYSICS_EXE = "/mnt/files/Downloads/HedgePhysics_1.2.3.0/HedgePhysics_1.2.3.0_x64/HedgePhysics_1.2.3.0_x64/HedgePhysics.exe"


def create_synthetic_pe64(path, *, is_64bit=True, imported_dlls=()):
    """Create a minimal PE binary, optionally with a valid .idata import table."""
    buf = bytearray(0x1000)
    buf[0:2] = b"MZ"
    pe_offset = 0x80
    struct.pack_into("<I", buf, 0x3C, pe_offset)
    buf[pe_offset : pe_offset + 4] = b"PE\x00\x00"

    machine = 0x8664 if is_64bit else 0x014C
    num_sections = 1 if imported_dlls else 0
    opt_size = 240 if is_64bit else 224
    struct.pack_into("<HHIIIHH", buf, pe_offset + 4, machine, num_sections, 0, 0, 0, opt_size, 0x0022)

    opt_off = pe_offset + 24
    opt_magic = 0x020B if is_64bit else 0x010B
    struct.pack_into("<H", buf, opt_off, opt_magic)

    if is_64bit:
        struct.pack_into("<I", buf, opt_off + 108, 16)
    else:
        struct.pack_into("<I", buf, opt_off + 92, 16)

    if imported_dlls:
        sec_rva = 0x1000
        sec_raw = 0x400
        sec_off = opt_off + opt_size
        buf[sec_off : sec_off + 8] = b".idata\x00\x00"
        struct.pack_into("<IIII", buf, sec_off + 8, 0x400, sec_rva, 0x400, sec_raw)
        struct.pack_into("<I", buf, sec_off + 36, 0xC0000040)

        desc_table_off = sec_raw
        names_off = sec_raw + (len(imported_dlls) + 1) * 20
        cur_name_off = names_off
        for idx, dll in enumerate(imported_dlls):
            raw_name = dll.encode("ascii") + b"\x00"
            buf[cur_name_off : cur_name_off + len(raw_name)] = raw_name
            name_rva = sec_rva + (cur_name_off - sec_raw)
            struct.pack_into("<IIIII", buf, desc_table_off + idx * 20, 0, 0, 0, name_rva, 0)
            cur_name_off += len(raw_name)

        imp_size = (len(imported_dlls) + 1) * 20
        if is_64bit:
            struct.pack_into("<II", buf, opt_off + 112 + 8, sec_rva, imp_size)

    with open(path, "wb") as f:
        f.write(buf)


class TestProtonUwpEnvironment(unittest.TestCase):
    EXPECTED_UWP_DLLS = (
        "UWPHOST",
        "VCRUNTIME140_APP",
        "VCRUNTIME140_1_APP",
        "MSVCP140_APP",
        "MSVCP140_1_APP",
        "MSVCP140_2_APP",
        "CONCRT140_APP",
        "vccorlib140_app",
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

    def _assert_uwp_env_and_overrides(self, env, dlloverrides, prefix_dir, expected_name, expected_pub, expected_ver, expected_app_id):
        self.assertEqual(env.get("PROTON_UWP_BRIDGE"), "1")
        self.assertEqual(env.get("PROTON_UWP_PACKAGE_NAME"), expected_name)
        self.assertEqual(env.get("PROTON_UWP_PACKAGE_PUBLISHER"), expected_pub)
        self.assertEqual(env.get("PROTON_UWP_PACKAGE_VERSION"), expected_ver)

        pub_id = proton_mod.compute_uwp_publisher_id(expected_pub)
        expected_family = f"{expected_name}_{pub_id}"
        expected_full = f"{expected_name}_{expected_ver}_x64__{pub_id}"
        expected_aumid = f"{expected_family}!{expected_app_id}"

        self.assertEqual(env.get("PROTON_UWP_PACKAGE_FAMILY_NAME"), expected_family)
        self.assertEqual(env.get("PROTON_UWP_PACKAGE_FULL_NAME"), expected_full)
        self.assertEqual(env.get("PROTON_UWP_AUMID"), expected_aumid)
        self.assertTrue(env.get("PROTON_UWP_MANIFEST", "").startswith("Z:\\"))
        self.assertTrue(env.get("PROTON_UWP_MANIFEST", "").lower().endswith("appxmanifest.xml"))
        self.assertTrue(env.get("PROTON_UWP_PACKAGE_DIR", "").startswith("Z:\\"))

        win_base = f"C:\\users\\steamuser\\AppData\\Local\\Packages\\{expected_family}"
        self.assertEqual(env.get("PROTON_UWP_LOCAL_STATE"), f"{win_base}\\LocalState")
        self.assertEqual(env.get("PROTON_UWP_ROAMING_STATE"), f"{win_base}\\RoamingState")
        self.assertEqual(env.get("PROTON_UWP_TEMP_STATE"), f"{win_base}\\TempState")

        unix_base = os.path.join(prefix_dir, "drive_c", "users", "steamuser", "AppData", "Local", "Packages", expected_family)
        self.assertTrue(os.path.isdir(os.path.join(unix_base, "LocalState")))
        self.assertTrue(os.path.isdir(os.path.join(unix_base, "RoamingState")))
        self.assertTrue(os.path.isdir(os.path.join(unix_base, "TempState")))

        self.assertIn("/opt/proton/files/lib/wine/scarlett/x86_64-windows", env["WINEDLLPATH"].split(":"))
        self.assertIn("/opt/proton/files/lib/wine/scarlett/x86_64-unix", env["WINEDLLPATH"].split(":"))
        self.assertIn("/opt/proton/files/lib/wine/scarlett/x86_64-unix", env["LD_LIBRARY_PATH"].split(":"))

        for dll in self.EXPECTED_UWP_DLLS:
            self.assertEqual(dlloverrides.get(dll), "b", f"Missing =b override for {dll}")

    def test_real_gzdoom_uwp_detection_and_env(self):
        self.assertTrue(os.path.isfile(GZDOOM_EXE), f"Missing test binary: {GZDOOM_EXE}")
        self.assertTrue(proton_mod.detect_uwp_from_executable(GZDOOM_EXE))
        imports = [d.lower() for d in proton_mod.get_pe64_imported_dlls(GZDOOM_EXE)]
        self.assertIn("vccorlib140_app.dll", imports)
        self.assertIn("msvcp140_app.dll", imports)
        self.assertIn("concrt140_app.dll", imports)
        self.assertIn("vcruntime140_app.dll", imports)
        self.assertIn("vcruntime140_1_app.dll", imports)

        with tempfile.TemporaryDirectory() as prefix_dir:
            env = {
                "WINEDLLPATH": "/opt/proton/files/lib/vkd3d:/opt/proton/files/lib/wine",
                "LD_LIBRARY_PATH": "/opt/proton/files/lib/x86_64-linux-gnu",
            }
            dlloverrides = {"steam.exe": "b"}
            activated = proton_mod.apply_uwp_environment(
                env, dlloverrides, "/opt/proton/files/lib", prefix_dir, argv=[GZDOOM_EXE]
            )
            self.assertTrue(activated)
            self._assert_uwp_env_and_overrides(
                env,
                dlloverrides,
                prefix_dir,
                expected_name="d19ff9e2-fbcc-4ab7-969e-c3d7d7b842f3",
                expected_pub="CN=aerisarn",
                expected_ver="1.0.7.0",
                expected_app_id="GZDoom",
            )

    def test_real_hedgephysics_uwp_detection_and_env(self):
        self.assertTrue(os.path.isfile(HEDGEPHYSICS_EXE), f"Missing test binary: {HEDGEPHYSICS_EXE}")
        self.assertTrue(proton_mod.detect_uwp_from_executable(HEDGEPHYSICS_EXE))
        imports = [d.lower() for d in proton_mod.get_pe64_imported_dlls(HEDGEPHYSICS_EXE)]
        self.assertIn("uwphost.dll", imports)

        with tempfile.TemporaryDirectory() as prefix_dir:
            env = {
                "WINEDLLPATH": "/opt/proton/files/lib/vkd3d:/opt/proton/files/lib/wine",
                "LD_LIBRARY_PATH": "/opt/proton/files/lib/x86_64-linux-gnu",
            }
            dlloverrides = {"steam.exe": "b"}
            activated = proton_mod.apply_uwp_environment(
                env, dlloverrides, "/opt/proton/files/lib", prefix_dir, argv=[HEDGEPHYSICS_EXE]
            )
            self.assertTrue(activated)
            self._assert_uwp_env_and_overrides(
                env,
                dlloverrides,
                prefix_dir,
                expected_name="HedgePhysics",
                expected_pub="CN=...",
                expected_ver="1.2.3.0",
                expected_app_id="App",
            )

    def test_normal_pc_exe_does_not_trigger_uwp_mode(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            pc_exe = os.path.join(tmpdir, "NormalPCGame.exe")
            create_synthetic_pe64(pc_exe, is_64bit=True, imported_dlls=("KERNEL32.dll", "USER32.dll"))

            self.assertFalse(proton_mod.detect_uwp_from_executable(pc_exe))

            env = {
                "WINEDLLPATH": "/opt/proton/files/lib/vkd3d:/opt/proton/files/lib/wine",
                "LD_LIBRARY_PATH": "/opt/proton/files/lib/x86_64-linux-gnu",
            }
            orig_env = dict(env)
            dlloverrides = {"steam.exe": "b", "d3d12": "n"}
            orig_overrides = dict(dlloverrides)

            activated = proton_mod.apply_uwp_environment(
                env, dlloverrides, "/opt/proton/files/lib", tmpdir, argv=[pc_exe]
            )
            self.assertFalse(activated)
            self.assertEqual(env, orig_env)
            self.assertEqual(dlloverrides, orig_overrides)

    def test_scarlett_gdk_title_does_not_trigger_uwp_mode(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            gdk_exe = os.path.join(tmpdir, "ScarlettTitle.exe")
            create_synthetic_pe64(gdk_exe, is_64bit=True, imported_dlls=("KERNEL32.dll", "xgameruntime.dll"))
            with open(os.path.join(tmpdir, "MicrosoftGame.Config"), "w", encoding="utf-8") as f:
                f.write('<Game><TargetDeviceFamily Name="Gaming.Xbox.Scarlett.x64"/></Game>')
            with open(os.path.join(tmpdir, "AppxManifest.xml"), "w", encoding="utf-8") as f:
                f.write('<Package><Identity Name="Ignored" Publisher="CN=Test" Version="1.0.0.0"/></Package>')

            self.assertTrue(proton_mod.detect_scarlett_from_executable(gdk_exe))
            self.assertFalse(proton_mod.detect_uwp_from_executable(gdk_exe))

    def test_pe64_uwp_imports_without_manifest_and_parent_dir_manifest(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            imp_exe = os.path.join(tmpdir, "ImpOnly.exe")
            create_synthetic_pe64(imp_exe, is_64bit=True, imported_dlls=("KERNEL32.dll", "XINPUTUAP.dll"))
            self.assertTrue(proton_mod.detect_uwp_from_executable(imp_exe))

            pkg_root = os.path.join(tmpdir, "pkg")
            bin_dir = os.path.join(pkg_root, "bin")
            os.makedirs(bin_dir)
            nested_exe = os.path.join(bin_dir, "Nested.exe")
            create_synthetic_pe64(nested_exe, is_64bit=True, imported_dlls=("KERNEL32.dll",))
            with open(os.path.join(pkg_root, "AppxManifest.xml"), "w", encoding="utf-8") as f:
                f.write(
                    '<?xml version="1.0" encoding="utf-8"?>'
                    '<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10">'
                    '  <Identity Name="NestedApp" Publisher="CN=Test" Version="2.0.0.0" ProcessorArchitecture="x64"/>'
                    '  <Applications><Application Id="MainApp"/></Applications>'
                    '</Package>'
                )
            self.assertTrue(proton_mod.detect_uwp_from_executable(nested_exe))

    def test_explicit_env_disable_and_enable(self):
        with tempfile.TemporaryDirectory() as prefix_dir:
            env_off = {"PROTON_UWP_BRIDGE": "0"}
            overrides_off = {}
            self.assertFalse(
                proton_mod.apply_uwp_environment(
                    env_off, overrides_off, "/opt/proton/files/lib", prefix_dir, argv=[GZDOOM_EXE]
                )
            )
            self.assertEqual(overrides_off, {})

            pc_exe = os.path.join(prefix_dir, "Plain.exe")
            create_synthetic_pe64(pc_exe, is_64bit=True, imported_dlls=("KERNEL32.dll",))
            env_on = {"PROTON_UWP_BRIDGE": "1"}
            overrides_on = {}
            self.assertTrue(
                proton_mod.apply_uwp_environment(
                    env_on, overrides_on, "/opt/proton/files/lib", prefix_dir, argv=[pc_exe]
                )
            )
            self.assertEqual(env_on["PROTON_UWP_BRIDGE"], "1")
            self.assertEqual(overrides_on.get("UWPHOST"), "b")
            self.assertEqual(overrides_on.get("XINPUTUAP"), "b")


if __name__ == "__main__":
    unittest.main(verbosity=2)
