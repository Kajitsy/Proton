# Proton-Scarlett (`scarlett/`)

Clean-Room Xbox Series X|S (`Gaming.Xbox.Scarlett.x64`) compatibility layer integrated into Proton 11.0 (`CURRENT_PREFIX_VERSION="11.0-100"`).

## Architecture & Build Integration

- **PE64 / Unixlib Split**: Adheres to Wine 11.0 PE (`lib/wine/scarlett/x86_64-windows`) and Linux Unixlib (`lib/wine/scarlett/x86_64-unix`) separation via `__wine_init_unix_call()` and `WINE_UNIX_CALL`. PE modules never link directly against host `libc`, `libvulkan`, or `liburing`.
- **Fail-Loud Policy**: Unimplemented entry points route through diagnostic auto-stub thunks logging DLL name, symbol/ordinal, caller RVA, and Win64 register parameters (`RCX`, `RDX`, `R8`, `R9`), halting immediately when `PROTON_XBOX_STRICT=1`.
- **Clean-Room Compliance**: Zero proprietary Microsoft binaries, cryptographic keys, or DRM circumvention routines. Encrypted XVC images fail immediately with `SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED`.

## Subsystems

- `xdl/` — `scarlett-xdl` IDL compiler & Game OS 2020..2026 `QueryApiImpl` vtable generator
- `loader/` — PE64 custom import resolver, `GAME:` CoW overlay VFS, and unencrypted XVD/XVC parser
- `xmem/` — `xmem.dll` + `kernelx.dll` + `xmem_unix.so` physical page allocator and 3-tier Vulkan UMA bridge
- `dstorage_xs/` — `dstorage_xs.dll` + `dstorage_xs.so` DirectStorage Xbox `io_uring` & GPU decompression pipeline
- `xg_xs/` — `xg_xs.dll` RDNA2 (`GFX10.3`) swizzle/tile surface computer (`SW_64KB_*`, `SW_4KB_*`)
- `xgameruntime/` — `xgameruntime.dll` 22-domain runtime, `XTaskQueue`/`XAsync`, `XGameSave`, `libHttpClient`/`XCurl`
- `audio_media/` — `xaudio2_9.dll`, `AcpHal.dll`, `GameInput.dll`, `Bink2Scarlett.dll`, `amd_fidelityfx_*_xbox.dll`
- `tests/` — Phase verification suites and environment integration tests
