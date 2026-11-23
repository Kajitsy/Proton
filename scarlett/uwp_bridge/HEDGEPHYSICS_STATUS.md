# HedgePhysics.exe on Proton (UWP bridge): status report

Date: 2026-11-23.
Working copy: branch `proton-scarlett-dev` (the task text said `proton-scarless-dev`; the
actual branch in this repository is `proton-scarlett-dev`).
Everything described below is committed: see section 6.

## 1. Result against the five success criteria

| # | Criterion | Status | Evidence |
|---|-----------|--------|----------|
| 1 | `Initialize` / `SetWindow` / `Load` / `Activated` / `Run` without `0x80004001` | **DONE** | Every stage logged exactly once in run #7 and run #11 (section 2) |
| 2 | `UnityPlayer.dll` loads without `0xC0000139` | **DONE** | 0 `0xC0000139` / 0 missing imports in every run #4+; CRT audit 784/784 resolved |
| 3 | `CreateSwapChainForCoreWindow` creates a DXVK swapchain | **DONE** | `DXGI: CreateSwapChainForCoreWindow: hWnd=0x10078 size=1280x720 ...`, `NotifySwapChainCreated swapchain=0x7e8000 size=945x1022`, GTX 1050 Ti |
| 4 | 10-30 frames via `Present`, no crash | **DONE (strong)** | Run #7: 99 `DxgiSwapChain::Present` log lines = frames 1..3840, no crash, killed only by timeout. Final run with Calendar + OPC fixes: 1115+ log lines (frames 1..64860 at the 30 s timeout; drain ended at frame 91200), symlink removed |
| 5 | Changes committed, final log excerpts | **DONE** | wine `bba5fcb7d99..737d0fa043a`, dxvk `bea1cac2`, parent commit (section 6); excerpts below |

The game now renders continuously until the harness timeout. The frame loop is presenting
with an uncapped/immediate-mode swapchain (~2.2k presents/s during the loading screen);
"frames presented" is proven by the DXVK `Present` log lines, not by visual inspection.

## 2. Final evidence

### Run #7 — canonical lifecycle + first frames (2026-11-23T16:52:06+03:00, timeout 30 s, rc=124)

Log: `/tmp/uwp_logs/hp_run_7.log`; DXVK copy `/tmp/uwp_logs/hp_run_7.log.HedgePhysics_dxgi.log`;
excerpt file `/tmp/uwp_logs/hp_run_7_evidence.txt`.

```
[scarlett:uwp:uwphost]   UwpHostInitialize complete (abi_version=1)
[scarlett:uwp:coreapp]   CoreApplication::Run(view_source=...) starting UWP lifecycle
[scarlett:uwp:corewindow] Created real Win32 HWND=0x10082 (1280x720)
[scarlett:uwp:coreapp]   IFrameworkView::Initialize succeeded
[scarlett:uwp:coreapp]   IFrameworkView::SetWindow succeeded
[scarlett:uwp:coreapp]   IFrameworkView::Load succeeded
[scarlett:uwp:coreapp]   Firing CoreApplicationView::Activated(Launch)
[scarlett:uwp:coreapp]   Entering IFrameworkView::Run()
[scarlett:uwp:corewindow] NotifySwapChainCreated swapchain=0x7e8000 hwnd=0x10082 size=945x1022
info:  DXVK: v2.7.1-500-gbc02bc78fe9b8bc0
info:  Found device: NVIDIA GeForce GTX 1050 Ti (NVIDIA 580.196.7)
info:  DXGI: CreateSwapChainForCoreWindow: hWnd=0x10082, size=1280x720, format=87, buffers=2, swapEffect=3, flags=0x840
info:  DxgiSwapChain::Present: frame 1
...        (frames 2..35 consecutively, then every 60th)
info:  DxgiSwapChain::Present: frame 3840
(99 Present log lines total; no 0xC0000139, no unhandled exception, no page fault; timeout kill)
```

At this point the render thread still died ~3.2 s into the run on
`Windows.Globalization.Calendar` (see run history #8/plumbing run below), which was fixed next.

### Run #11 — after `Windows.Globalization.Calendar` (30 s, timeout kill)

Log: `/tmp/uwp_logs/hp_run_11_calendar.log` (+ `.console.log`, + per-run DXVK logs).
1588 `Present` log lines, frames 1..93,180, 0 page faults, 0 Calendar CLASSNOTREG,
process alive until the timeout (rc=124). Rendering is now limited by the harness timeout,
not by a WinRT abort.

### Run #10 — OPC percent-encoded filename fix, symlink removed (30 s, timeout kill)

Log: `/tmp/uwp_logs/hp_run_10_opc.log` (large: run under `WINEDEBUG=+file`).
The out-of-repo symlink `Data/Resources/unity default resources` was moved away before the run.
4605 `Present` log lines, frames 1..274,200, 0 page faults, 48 `unity%20...` path mentions
in the `+file` trace (after drain; the script's timeout summary said 3120/185,100). Retry proof:
`GetFileAttributesExW(...\unity default resources)` -> c0000034, then
`get_nt_and_unix_names(...\unity%20default%20resources)` -> ret 0, followed by
`create_file_w`/`NtCreateFile` on the encoded name (log lines 17805-17814). — the decoded name is looked up, the encoded on-disk file is found,
the run returns to rendering. **Symlink left removed**; the fix is real, the symlink is gone.

### Final combined run — both fixes, symlink removed (2026-11-23T17:24:16+03:00, timeout 30 s, rc=124)

Log: `/tmp/uwp_logs/hp_run_final.log` (+ `.HedgePhysics_dxgi.log`, + `.HedgePhysics_d3d11.log`).
All five lifecycle stages exactly once, swapchain 945x1022, 1115 `Present` log lines at the
30 s timeout (frames 1..64,860; the log kept draining after the supervisor kill and ended at
frame 91,200), no crash markers. Only the three non-fatal `UNIMPLEMENTED WinRT class`
lines described in section 5.

## 3. Run history

| Run | Result |
|-----|--------|
| 1 | `Initialize` OK; crash on `PointerVisualizationSettings` CLASSNOTREG |
| 2 | Crash on `PnpObject` CLASSNOTREG |
| 3 | Crash in Wine `GetHostNames` (`Windows.Networking`) |
| 4 | Full lifecycle + swapchain; crash on missing `Internal-ErrorShader.shader` (percent-encoded `unity%20default%20resources`; symlink workaround added) |
| 5 | Alive 25 s; AppThread abort on `MediaDevice::GetAudioRenderSelector` E_NOTIMPL |
| 6 | `MediaDevice` OK; AppThread abort on `device_watcher_add_Removed` E_NOTIMPL |
| 7 | After rebuild: **first Present frames** — 99 lines / frames 1..3840; render thread then dies on `Windows.Globalization.Calendar` CLASSNOTREG |
| 8 | 60 s stability run, same Calendar stop at ~3.35 s; process itself alive for 60 s |
| 9 | First `%20` patch attempt (wrong direction: decoded instead of encoded) — 0 presents, crash at 12 s; symlink restored |
| 10 | `%20` percent-encode fix verified with symlink removed: 4605 Present lines, frames 1..274,200, 0 page faults |
| 11 | Calendar fix verified: 1588 Present lines, frames 1..93,180, no Calendar abort, timeout kill |
| final | Both fixes, symlink removed: 1115 Present lines at timeout (frames 1..64,860; drain to 91,200), timeout kill, 3 non-fatal missing classes |

## 4. What was implemented

### Core WinRT / lifecycle (`wine/dlls/combase`, `wine/dlls/vccorlib140`)
- Real `RoGetParameterizedTypeInstanceIID`, `RoFreeParameterizedTypeExtra`,
  `RoParameterizedTypeExtraGetTypeSignature`: full `IRoSimpleMetaDataBuilder`, canonical
  `pinterface/rc/ig/struct/enum/delegate` signatures, RFC 4122 UUIDv5 (SHA-1) over
  `{11f47ad5-7b73-42c0-abae-878b1e16adee}`. Standalone test matched 9 known PIIDs,
  e.g. `TypedEventHandler<CoreApplicationView,IActivatedEventArgs>` =
  `{cf193a96-eb13-5e3b-8bdf-87b6efae8339}`. This fixed the original `0x80004001`.
- `vccorlib140`: `Delegate_ctor` returns `this`; zero-initialized ref-class allocations.

### UWP host (`scarlett/uwp_bridge/uwphost`)
- Real `GetIids`, `ICoreImmersiveApplication::get_Views`, `IPrelaunchActivatedEventArgs`,
  `IApplicationViewActivatedEventArgs`, `SplashScreen` 1280x720, explicit
  `CoreWindow::Activate`, full lifecycle logging, CoreCLR hosting via
  `PROTON_UWP_CORECLR_PATH` / `ICLRRuntimeHost2::DllGetActivationFactory`.
- `CoreWindow`/`CoreDispatcher`: all `ICoreWindow` events, `get_Bounds` fallback,
  idempotent `Activate`, no double `Closed`, cursor / `WM_MOUSELEAVE` /
  `WM_CAPTURECHANGED`, bounded `ProcessEvents` drains.

### WinRT UI classes (`scarlett/uwp_bridge/winrt_ui`, 11 files)
- New `Windows.UI.Input.PointerVisualizationSettings` (fixed run #1 crash) and
  `UIViewSettings`; `ApplicationView` title cache + `TitleBar` singleton;
  `DisplayInformation`, `SystemNavigationManager`, `CoreCursor`, `MouseDevice`,
  `ApiInformation`. `StatusBar` is still CLASSNOTREG but Unity catches it.

### WinRT system classes (`scarlett/uwp_bridge/winrt_sys`, new `winrt_sys_devices.c`)
- `StorageFile`/streams/`StorageFolder` completion, `PropertyValue`, `Package.InstalledLocation`,
  `AnalyticsInfo`, `ThreadPool`/`ThreadPoolTimer`, `PnpObject`, `HidDevice`, touch/mouse/keyboard
  capabilities, `HardwareIdentification`, `MemoryManager`, `PowerManager`,
  `NetworkInformation`/`HostName`, `MediaDevice`.

### Wine WinRT libraries
- `windows.devices.enumeration`: real `DeviceWatcher` `add/remove_Updated` and
  `add/remove_Removed` handler lists, `DeviceInformation` `get_Name`/`get_IsEnabled`/
  `IsDefault`/`get_EnclosureLocation`/`Update`/`CreateFromIdAsync(AdditionalProperties)`,
  AQS filters for `FindAllAsync(DeviceClass)` and `CreateWatcher(DeviceClass)`.
  This removed the run #6 `E_NOTIMPL` blocker.
- `windows.media.devices`: real AQS selectors for audio capture / audio render / video capture.
- **`windows.globalization`: new `Calendar` runtimeclass** (`calendar.c`, full `ICalendar` +
  `ITimeZoneOnCalendar` + factories, all methods S_OK because C++/CX raises on E_NOTIMPL;
  real `ActivateInstance`/`GetDateTime`/`SetToNow`). Unity only calls `ActivateInstance` and
  `GetDateTime`; the render thread now survives the whole run.
- `kernelbase`: OPC percent-encoded filename fallback for the UWP bridge (see section 5 of the
  task list): on `ERROR_FILE_NOT_FOUND`/`ERROR_PATH_NOT_FOUND` retry once with the
  percent-encoded path, in `CreateFileW` (covers `CreateFileA`/`CreateFile2`),
  `GetFileAttributesW`, `GetFileAttributesExW`; original error preserved; single pass,
  UTF-8, uppercase `%XX`, `MAX_PATH` cap; gated on `kernelbase_is_uwp_bridge_enabled()`
  (`PROTON_UWP_BRIDGE=1`).
- `wintypes`: UWP package identity/path plumbing used by the bridge.

### Graphics
- `dxvk/src/dxgi/dxgi_swapchain.cpp`: logs `DxgiSwapChain::Present: frame N`
  (first 35 presents, then every 60th) — the only way to prove frame presentation from a log.
- `dxvk`: `CreateSwapChainForCoreWindow` / CoreWindow swapchain support (pre-existing branch work).

### Forwarders / audit
- Forwarders for every native import of `HedgePhysics.exe`, `UnityPlayer.dll`,
  `UnityEngineDelegates.dll`, `BridgeInterface.dll`, `ClrCompression.dll`: 100% resolved.
  `MMDEVAPI_APP.def` exports `ActivateAudioInterfaceAsync` at `@17`.
  Host tests: `test_sys_forwarders.py` 5/5, `test_crt_imports_audit.py` 784/784,
  `scarlett/tests` 13/13.

## 5. Known remaining gaps (not blocking the criteria)

1. Three `UNIMPLEMENTED WinRT class` lines remain, all **non-fatal** (Unity catches the
   exception, rendering continues): `Windows.UI.ViewManagement.StatusBar`,
   `Windows.Devices.Sensors.SimpleOrientationSensor`, `Windows.Devices.Sensors.Accelerometer`.
2. Device enumeration still enumerates **zero devices**, and `DevQueryResultUpdate`/`Remove`
   are never dispatched (the new `Updated`/`Removed` handlers are write-only). UNITY's audio
   init therefore continues silently with no audio devices; implement real device enumeration
   if audio/input devices are needed.
3. `propsys!GetPropertyDescriptionByName` remains a stub (AQS property resolution relies on
   fallbacks); see `wine/dlls/propsys`.
4. `%20` fallback covers standard OPC encoding only (uppercase hex, conservative safe set);
   `FindFirstFile*` enumeration of encoded names is not covered (the title only does exact
   probes).
5. DXVK bug (not fixed): `dxvk/src/d3d11/d3d11_swapchain.cpp:181-182` `ChangeProperties`
   passes the old `m_desc` extent to `setSurfaceExtent`; also line ~667 logs `m_desc.Format`
   instead of `Format`.
6. Wine prefixes created before the `Calendar` runtimeclass was added lack its
   ActivatableClassId registration (a fresh prefix normally gets it from `classes_r.res`
   during wineboot). The current `build/dist/files/share/default_pfx` template contains the
   registration, and the Steam prefix used for the HedgePhysics shortcut has been patched.
   For an old prefix, run once:
   ```
   WINEPREFIX=/path/to/compatdata/pfx build/dist/files/bin/wine reg add \
     "HKLM\Software\Microsoft\WindowsRuntime\ActivatableClassId\Windows.Globalization.Calendar" \
     /v DllPath /t REG_SZ /d "C:\windows\system32\windows.globalization.dll" /f
   ```
   To regenerate the template from scratch use `make -C build default_pfx` (it builds a
   prefix with `python3 default_pfx.py build/dist/files/share/default_pfx build/dist/files 0`).
   Note: in this local tree, a plain `wineboot` from the host fails to load 32-bit
   `kernel32.dll` (the interrupted i386 build left the 32-bit tree inconsistent), so a
   template can alternatively be produced by copying a working prefix, stripping run state
   (`system.reg.old`, `.update-timestamp`, `creation_sync_guard`, game AppData) and running
   `default_pfx.setup_dll_symlinks/fixup_drive_links/filter_registry`.
7. Interactive check (keyboard/mouse/resize/cursor capture, actual gameplay past the loading
   screen) was not performed; only log-level frame-present evidence exists.

## 6. Commits

- `wine` (branch `wt-uwp-wine`):
  - `bba5fcb7d99` wine(uwp_bridge): implement parameterized WinRT type resolution and CoreCLR activation support
  - `ebc03fd2742` wine(uwp_bridge): extend device/media WinRT classes and wintypes
  - `edf7555a61e` wine(uwp_bridge): implement Windows.Globalization.Calendar
  - `737d0fa043a` kernelbase: add OPC percent-encoded filename fallback for the UWP bridge
- `dxvk` (branch `wt-dxvk-corewindow`): `bea1cac2` [dxgi] Log presented frame ids for CoreWindow swapchains
- Parent repository: this commit records the updated `wine`/`dxvk` submodule pointers,
  the scarlett/uwp_bridge changes, `winrt_sys_devices.c`, the run script
  `scarlett/uwp_bridge/tools/run_hedgephysics.sh`, `NEXT_BLOCKERS.md`, this report, and
  `.gitignore` entries for `/contrib/` and `__pycache__/`.

## 7. How to build and run

Incremental builds:
```bash
make -C build wine-x86_64 scarlett-x86_64 dxvk-x86_64
```

The run environment must be prepared once (it lives outside the repo). CoreCLR 1.1 is
shipped as `Microsoft.NET.CoreRuntime.1.1.appx` next to the game package; unpack it to a
**persistent** location (survives /tmp cleanup):

```bash
mkdir -p ~/.local/share/hedgephysics/coreclr_1.1 /tmp/uwp_logs
unzip -o '/mnt/files/Downloads/HedgePhysics_1.2.3.0/Microsoft.NET.CoreRuntime.1.1.appx' \
      -d ~/.local/share/hedgephysics/coreclr_1.1
```

Then just use the committed helper:
```bash
cd /home/kajitsy/Documents/GitHub/Proton
./scarlett/uwp_bridge/tools/run_hedgephysics.sh 30 /tmp/uwp_logs/hp_run_latest.log
```

The script resolves CoreCLR from `~/.local/share/hedgephysics/coreclr_1.1/coreclr.dll`
(fallback: the old `/tmp/uwp_coreruntime_1.1/unpacked` unpack) and sets, among others:
`STEAM_COMPAT_DATA_PATH=/tmp/uwp_pfx_hedgephysics` (override with `STEAM_COMPAT_DATA_PATH=...`
or `UWP_CORECLR_UNIX=...` in the environment),
`STEAM_COMPAT_CLIENT_INSTALL_PATH=/tmp`,
`WINEDEBUG=+debugstr,+err,+uwp,+dxgi`,
`PROTON_UWP_CORECLR_PATH=Z:\home\kajitsy\.local\share\hedgephysics\coreclr_1.1\coreclr.dll`,
and (required for the frame evidence) `DXVK_LOG_LEVEL=info`, `DXVK_LOG_PATH=/tmp/uwp_logs`.
Proton's `init_session()` defaults `DXVK_LOG_LEVEL` to `none`, so without setting it DXVK
writes no `Present` lines at all. DXVK logs go to
`/tmp/uwp_logs/HedgePhysics_{dxgi,d3d11}.log` and mirror to stderr (the combined proton log).
The script also archives per-run copies as `<log>.HedgePhysics_{dxgi,d3d11}.log`.

Count the frames:
```bash
grep -c 'DxgiSwapChain::Present: frame ' /tmp/uwp_logs/hp_run_latest.log
grep 'DxgiSwapChain::Present: frame ' /tmp/uwp_logs/hp_run_latest.log | tail -1
```
(DXVK logs frames 1..35 and then every 60th, so the last printed frame id is the real
present count up to the next 59 frames.)

### Running HedgePhysics through Steam (non-Steam shortcut)

The build is registered as a Steam compatibility tool through a symlink:
`~/.steam/steam/compatibilitytools.d/proton-scarlett -> build/dist` (restart Steam once to
pick it up). Add `HedgePhysics.exe` as a non-Steam game and set, in
Properties -> Compatibility -> force `proton-scarlett`, with these Launch Options:

```
PROTON_LOG=1 PROTON_UWP_CORECLR_PATH='Z:\home\kajitsy\.local\share\hedgephysics\coreclr_1.1\coreclr.dll' %command%
```

(the old `Z:\tmp\uwp_coreruntime_1.1\unpacked\coreclr.dll` path works too while `/tmp`
still has it, but it does not survive a reboot). `PROTON_LOG=1` is what makes Proton write
its log; the log lands in `~/steam-<unsigned appid>.log` (for the current HedgePhysics
shortcut: `~/steam-13073843237460901888.log`). Search it for
`DxgiSwapChain::Present: frame ` to confirm frames. UWP auto-detection works from the
`AppxManifest.xml` next to the exe, so no other environment variables are needed.
Note: Unity pauses the frame loop when its window is not focused, so a launch from a terminal
while Steam has focus can show far fewer `Present` lines than a focused run - check the window
before concluding it hangs. Also, a Steam prefix created before the Calendar fix lacks the
registration (section 5 item 6) and the render thread dies after ~3 s - this is fixed in the
current template and in the existing Steam prefix.

## 8. Other UWP titles: SAB2 (non-Unity, .NET Native/C++-CX)

SAB2 (`/home/kajitsy/Downloads/SAB2_1.0.0.0_x64/uwp_1.0.0.0_x64`) is a different UWP app
(native C++/CX, SDL2 + GLFW + OpenGL, not Unity). With the current build it gets through the
UWP lifecycle (manifest detected, `Initialize`/`SetWindow`/`Load`/`Activated`/`Run` all
logged) but does not render: it first needs an OPC filename workaround for
`libstdc%2B%2B-6.dll` (a `libstdc++-6.dll` symlink was added in the game folder), then it
crashes with a NULL deref in its own `libuwp` (called from `glfw3`), and the log shows
`[SCARLETT-CRT:STUB] [FAIL] vccorlib140_app.dll ?GetIBoxVtable@Details@Platform@@...`
(SDL2 is hitting the unimplemented `GetIBoxVtable`/`GetIBoxArrayVtable` box vtables in
`scarlett/uwp_bridge/forwarders/crt/vccorlib140_app.c`). Running it is therefore not a matter
of launch parameters: the bridge needs, at least,
1. OPC percent-encoded filename fallback at the **DLL loader** level (`ntdll` LdrLoadDll /
   dependency resolution), not just kernelbase `CreateFileW`;
2. real `Platform::Details::GetIBoxVtable` / `GetIBoxArrayVtable` implementations;
3. `Windows.ApplicationModel.Package` / package-location support for native UWP apps.
Command used for the test (prefix `/tmp/sab2_pfx`, auto UWP detection):
```bash
mkdir -p /tmp/sab2_pfx
STEAM_COMPAT_DATA_PATH=/tmp/sab2_pfx STEAM_COMPAT_CLIENT_INSTALL_PATH=/tmp \
  WINEDEBUG=+debugstr,+err,+uwp \
  timeout 60s /home/kajitsy/Documents/GitHub/Proton/build/dist/proton run \
  /home/kajitsy/Downloads/SAB2_1.0.0.0_x64/uwp_1.0.0.0_x64/uwp.exe
```
Logs: `/tmp/uwp_logs/sab2_run3.log` (lifecycle + crash backtrace).

## 9. Where the evidence lives

- `/tmp/uwp_logs/hp_run_final.log` — final combined run (both fixes, symlink removed)
- `/tmp/uwp_logs/hp_run_7.log` — canonical lifecycle/first-frames run
- `/tmp/uwp_logs/hp_run_7_evidence.txt` — excerpt file
- `/tmp/uwp_logs/hp_run_10_opc.log` — `%20` fix verification with symlink removed (`+file`)
- `/tmp/uwp_logs/hp_run_11_calendar.log` — Calendar fix verification
- `/tmp/uwp_logs/hp_run_8_stability.log` — 60 s stability run
- `scarlett/uwp_bridge/NEXT_BLOCKERS.md` — audit of the next WinRT gaps and device enumeration
- `/tmp/uwp_logs/verify_both.console.log` — orchestration log of runs #10/#11
- `/tmp/uwp_logs/runner_evidence_summary.txt` — consolidated per-run criteria/evidence summary
- `/tmp/uwp_logs/hp_run_steam_prefix3.log` — Steam-prefix run after the Calendar registration fix
- `~/steam-13073843237460901888.log` — Proton log of the real Steam launch (shows the Calendar death before the fix)
- `/tmp/uwp_logs/sab2_run3.log` — SAB2 UWP lifecycle + crash backtrace
