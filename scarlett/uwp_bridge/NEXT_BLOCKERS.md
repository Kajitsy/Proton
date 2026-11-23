# NEXT_BLOCKERS — static analysis after run #7

Date: 2026-11-23. Read-only analysis; sources at `proton-scarlett-dev` working-tree state.
Evidence logs: `/tmp/uwp_logs/hp_run_7.log` (run #7), `/tmp/uwp_logs/hp_run_latest.log` (run #6),
`/tmp/uwp_logs/hp_run_file.log` (run #4, `+file` trace), `/tmp/uwp_logs/hp_run_dxvk.log` (run #5).

**Run #7 outcome (baseline for this doc).** Rebuilt wine + UWPHOST: 99 `DxgiSwapChain::Present`
lines, frames 1..3840 → criterion 4 reached. The abort after that is
`Windows.Globalization.Calendar` CLASSNOTREG on the render thread
(`hp_run_7.log:1235-1241`, `tid=328`, caller `vccorlib140.dll+0x6e25`,
requested IID = `IActivationFactory {00000035-0000-0000-C000-000000000046}`).
`Windows.UI.ViewManagement.StatusBar` (`:417`) and
`Windows.Devices.Sensors.SimpleOrientationSensor` (`:894`) are thrown and caught; non-fatal.
Run #7 also confirms the rebuilt enumeration DLL works: no
`device_watcher_add_Removed` FIXME, and `MediaDevice` is now served in-process by
scarlett (`winrt_sys_devices.c`), no longer by wine's `windows.media.devices.dll`.
`DeviceInformation` is still served by wine's combase registry (expected).

---

## 1. DeviceWatcher / enumeration audit

### 1.1 `IDeviceWatcher` (`wine/dlls/windows.devices.enumeration/main.c`)

| Member | State | Where | Chance hit next |
|---|---|---|---|
| `Start` | real: builds `DEVPROP_FILTER_EXPRESSION[]`, `DevCreateObjectQuery` | `main.c:391-439` | hit already (run #7) |
| `Stop` | real: `DevCloseObjectQuery` | `main.c:441-466` | med (shutdown/pause) |
| `get_Status` | real, returns cached status | `main.c:301-312` | low |
| `add/remove_Added` | real handler list | `main.c:225-238` | hit (registration) |
| `add/remove_Updated` | registration real, **dispatch hole** (see 1.5) | `main.c:240-254` | registration hit; callback never |
| `add/remove_Removed` | registration real (new), **dispatch hole** | `main.c:256-270` | registration hit; callback never |
| `add/remove_EnumerationCompleted` | real; fires on `DevQueryStateEnumCompleted` | `:272-285`, dispatch `:362-368` | **fires in run #7** |
| `add/remove_Stopped` | real; fires on `DevQueryStateClosed` | `:287-299`, dispatch `:347-353` | low |
| `GetIids` / `GetRuntimeClassName` / `GetTrustLevel` | real (new) | `:199-222` | low |

Watcher creation paths: `CreateWatcher` `:938`, `CreateWatcher(DeviceClass)` `:944`,
`CreateWatcher(AQS)` `:956`, `CreateWatcher(AQS+props)` `:962`;
`DeviceInformationKind`-aware variants `:1012`/`:1060` region, AQS-for-DeviceClass
`aqs_filter_for_device_class` `:736-767`.

### 1.2 `IDeviceInformation` (`information.c`)

| Member | State | Where |
|---|---|---|
| `GetIids` / `GetTrustLevel` | real (new) | `:86-115` |
| `get_Id` | real | `:128` |
| `get_Name` | real: probes `System.ItemNameDisplay`, `System.Devices.FriendlyName`, `System.Devices.DeviceInstanceId`, falls back to Id | `:138-160` |
| `get_IsEnabled` | real (`System.Devices.InterfaceEnabled`) | `:163-180` |
| `IsDefault` | real (`System.Devices.IsDefault`) | `:182-199` |
| `get_EnclosureLocation` | returns `NULL, S_OK` | `:201-207` |
| `get_Properties` | real (`IMapView` over `PropertySet`) | `:209-214`, builder `:263` |
| `Update` | **no-op `S_OK` stub** | `:216-220` |
| `GetThumbnailAsync` / `GetGlyphThumbnailAsync` | `E_NOTIMPL` | `:222-233` |

`device_information_create` `:403`; `lookup_property_value` `:111`.

### 1.3 `IDeviceInformationCollection` (vector) and async ops

- `vector.c`: `get_Size`/`GetAt`/`IndexOf`/`GetMany`/`IIterator` iteration are real;
  `IInspectable` methods on the collection/iterator/iterable are `E_NOTIMPL` stubs at
  `vector.c:86-102` (iterator), `:223-241` (vector), `:418-436` (iterable).
- `async.c`: `put/get_Completed`, `GetResults`, `IAsyncInfo` status are real;
  `GetIids`/`GetTrustLevel` are `E_NOTIMPL` at `async.c:435-449` (operation) and
  `:583-597` (action).
- **`IDeviceInformationUpdate` has no implementation at all** (only the vtable slot
  `information.c:216`); a `device_information_update_create()` object is missing.
  Needed only once 1.5 is fixed.

### 1.4 What Unity actually calls (static + log evidence)

`UnityPlayer.dll` UTF-16 strings include: `TypedEventHandler<DeviceWatcher,DeviceInformation>`,
`<DeviceWatcher,DeviceInformationUpdate>`, `<DeviceWatcher,Object>`,
`DeviceInformationCollection`, `DeviceAccessInformation`,
`JoystickHID.JoystickDeviceWatcher` and Unity's HID joystick manager strings
("Watcher started.", "OnDeviceEnumerationComplete.", "AddDeviceToList", "Total devices connected: %d").
Run #6/#7 order on the AppThread was: `MediaDevice.add_DefaultAudioRenderDeviceChanged` →
`add_DefaultAudioCaptureDeviceChanged` → `GetAudioRenderSelector` → `DeviceInformation.CreateWatcher(AQS)`
→ add Added/…/Removed → `Start` (run #7, no abort). So a **filtered** watcher is the real-world path.

### 1.5 Holes found (loud — these are still broken even after the run #7 fix)

1. **A filtered watcher never enumerates anything (100% certain).**
   `device_watcher_Start` passes the parsed AQS filters to
   `DevCreateObjectQuery` (`main.c:424`). In
   `wine/dlls/cfgmgr32/main.c:1370-1372` that function does
   `FIXME("Query filters are not supported!")`, sets `ctx->filters = !!filters`
   (`:1379`), and the async enumeration is gated by
   `if (!ctx->filters) hr = enum_dev_objects(...)` (`:1277`). Result: state goes
   straight to `DevQueryStateEnumCompleted`, **zero `DevQueryResultAdd` callbacks**.
   Unity therefore gets an immediately-completed watcher with an empty device list;
   `OnDeviceEnumerationComplete` fires but no Added/Updated events ever will.
   Fix sketch (cfgmgr32, ~10-15 lines): drop the blanket FIXME and call
   `enum_dev_objects(ctx->type, ..., filters, filters + filters_len, ...)` in the
   query path; keep `ctx->filters` only for deciding whether to register
   `CM_Register_Notification` for updates. `enum_dev_objects` already evaluates
   filters (`:841-855`) — the query path just doesn't use them.
2. **Updated/Removed events are never dispatched (100% certain).**
   `device_object_query_callback` (`main.c:323-389`) handles only
   `DevQueryResultStateChange` (`:344`) and `DevQueryResultAdd` (`:374`);
   `DevQueryResultUpdate` / `DevQueryResultRemove` fall into
   `FIXME("Unhandled DEV_QUERY_RESULT_ACTION value")` `:383-385`. So the new
   `add_Removed`/`add_Updated` (`main.c:240-270`) succeed but are write-only.
   Fix sketch (~60-80 lines): `information.c` gains
   `device_information_update_create(const DEVPROPERTY *props, ULONG count, HSTRING id)`
   implementing `IDeviceInformationUpdate` (`get_Id`, `get_Properties`), and the
   callback notifies `removed_handlers` / `updated_handlers` with it. Also implement
   `device_information_Update` to merge props instead of returning `S_OK` (`:216`).
3. `activation_factory_*` remains `E_NOTIMPL` for `GetIids`/`GetRuntimeClassName`/
   `GetTrustLevel`/`ActivateInstance` (`main.c:697-718`). Low risk: C++/CX uses
   `RoGetActivationFactory` + direct vtable calls (see `vccorlib` caller addresses in
   the logs), it does not call `IInspectable::GetIids`. Same argument for the
   async/vector `IInspectable` stubs. Fix opportunistically (~6 lines each).
4. `device_watcher_get_Status` does not NULL-check `status` (`main.c:301`); cosmetic.

### 1.6 `windows.media.devices` audit

Wine `dlls/windows.media.devices/main.c`:
`GetAudioCaptureSelector` `:261-271`, `GetAudioRenderSelector` `:273-283`,
`GetVideoCaptureSelector` `:285-295` are real AQS strings;
`GetDefaultAudioCaptureId`/`RenderId` `:297-309` → `get_default_device_id` `:156-219`
builds a real `\\?\SWD#MMDEVAPI#{0.0.x...}#{class-guid}` id from
`IMMDeviceEnumerator`; `add/remove_DefaultAudio*DeviceChanged` are `S_OK` no-ops with a
token (`:311-349`); `DllGetClassObject` is a stub (`:376-380`, unused — activation goes
through `DllGetActivationFactory` `:382-387`).
scarlett's in-process `MediaDevice` (`winrt_sys/winrt_sys_devices.c:2487-2629`, new file)
now wins the dispatch (`winrt_sys_common.c:109`): selectors `:2545-2564`, events are
`pnp_w_add_noop` `:2622-2623`, and `GetDefaultAudioRenderId`
cross-calls wine's DLL via `LoadLibraryA("windows.media.devices.dll")` `:2566-2606`,
falling back to the fake string `{0.0.0.00000000}.{default}` `:2603-2605`.
Risk: wine's `ActivateAudioInterfaceAsync` accepts only a bare interface GUID or a
`\\?\SWD#MMDEVAPI#` path (`wine/dlls/mmdevapi/main.c:573-604`); the fake fallback would
be rejected with `ERROR_FILE_NOT_FOUND` if wine has no default endpoint. Prefer returning
the bare render GUID `{E6327CAD-DCEC-4949-AE8A-991E976A79D2}` (accepted as "default
render endpoint", `mmdevapi/main.c:583-586`) or propagating the failure.

### 1.7 `MMDEVAPI_APP` forwarders — audit result: nothing left to do for this title

`scarlett/uwp_bridge/forwarders/sys/MMDEVAPI_APP.def` exports exactly
`ActivateAudioInterfaceAsync = mmdevapi.ActivateAudioInterfaceAsync @17` (plus inert
`Ordinal_N` aliases). **UnityPlayer.dll imports `MMDevAPI.DLL` by ordinal 17
(`llvm-objdump -p`), not `MMDEVAPI_APP.dll`, and no shipped binary imports any
`MMDEVAPI_APP` name** — the forwarder is inert here. The real code path is wine's
`mmdevapi.dll`: `ActivateAudioInterfaceAsync` at `mmdevapi/main.c:653-701`, which
resolves bare class GUIDs or `\\?\SWD#MMDEVAPI#` paths (`:573-630`) and activates
`IAudioClient` (with `IAudioClient2/3` QI, `client.c:740-760`). No stubs remain in the
audio-open path; host audio is available (Pulse/PipeWire sockets present, winepulse built).

---

## 2. Next activation-factory gaps (prioritized, after Calendar)

Note: types **produced** by implemented factories do not need activation
(`DeviceWatcher`, `DeviceInformationCollection`, `DeviceInformationUpdate`, all
`*EventArgs`); only runtime classes that Unity `ref new`s / `RoGetActivationFactory`s
matter. "Missing" below = neither scarlett dispatch (`winrt_sys_common.c:1-118`,
`winrt_ui_common.c`, `uwphost` core) nor the wine prefix
`HKLM\Software\Microsoft\WindowsRuntime\ActivatableClassId` registry (71 classes;
relevant ones: `DeviceInformation`, `DeviceAccessInformation`, `MediaDevice`,
`Gamepad`, `Geolocator`, `DataWriter`, `PropertySet`, `PropertyValue`,
`ApplicationData`, `Language`, `AdvertisingManager`, `ThreadPool`).

| # | Class | Evidence (why now) | Implemented? | Minimal fix sketch |
|---|---|---|---|---|
| 1 | `Windows.Globalization.Calendar` | **ACTIVE blocker**, `hp_run_7.log:1235-1241`; `UnityPlayer.dll` string in the sensor-input cluster next to `Geolocator`/`Accelerometer` | no (wine dll has only `Language`/`GeographicRegion`/`ApplicationLanguages`; `windows.globalization/classes.idl`) | `wine/dlls/windows.globalization`: add `runtimeclass Calendar;` to `classes.idl`, add a `calendar.c` with `IActivationFactory`/`ActivateInstance` (IID requested is `IActivationFactory`) + `ICalendar` vtable (real `SetToNow`/`GetDateTime`/`AddSeconds`/`SetToMinute`; remaining methods safe `E_NOTIMPL`), switch in `main.c:1114 DllGetActivationFactory`. ~250-400 lines. |
| 2 | `Windows.Devices.Sensors.{Accelerometer,Gyrometer,Compass,OrientationSensor}` | Same `UnityPlayer.dll` cluster: `MTSensorInput<Accelerometer,…>`, `AccelerationInput`, `GyrometerInput`, `CompassInput`, `OrientationInput` (UTF-16 dump lines ~632-648); `SimpleOrientationSensor` already tried in run #7 and **caught** (`hp_run_7.log:894`) | no | `winrt_sys/winrt_sys_sensors.c` + dispatch entry in `winrt_sys_common.c`: statics `GetDefault`/`GetCurrentReading`, reading-event add/remove no-ops; ~250 lines. Non-fatal if missing (Unity catches ClassNotRegistered), so MEDIUM priority. |
| 3 | `Windows.Devices.Geolocation.Geolocator` (+ access/position args) | Same sensor cluster; class **is** registered (wine `geolocation.dll`) | partial (wine stub; probe for `E_NOTIMPL`) | Only needs to not throw on activation; Unity catches failures. LOW. |
| 4 | `Windows.Storage.KnownFolders` | UTF-16 cluster with `Launcher`/`LauncherOptions`/`FileOpenPicker` (`MetroUtils`/`MetroLauncher`) | no | Return static folder objects or `E_NOTIMPL`; only on OpenURL/file dialog. LOW. |
| 5 | `Windows.ApplicationModel.DataTransfer.{Clipboard,DataPackage,StandardDataFormats}` | `metro.ClipboardWatcher` string cluster; `MetroCopyPaste.cpp` is in the binary | no | Only on Ctrl+C/V. LOW. |
| 6 | `Windows.UI.Notifications.*` + `Windows.Data.Xml.Dom.XmlDocument` | Toast/Tile cluster next to each other; `XmlDocument` used to build badge/tile XML | no | Only if game schedules notifications. LOW. |
| 7 | `Windows.Storage.Streams.DataReader` | `DataWriter` is registered in wine, `DataReader` is not; string sits in the MediaDevice/FMOD/`StreamSocketListener` cluster | no | Implement alongside `DataWriter` in `windows.storage` if a managed bridge path needs it. LOW-MED. |
| 8 | `Windows.Security.Cryptography.Core.HashAlgorithmProvider` | `HardwareIdentification`/`AdvertisingManager` cluster | no | Only for hashing/machine-id features. LOW. |
| 9 | `Windows.Media.Capture.MediaCapture` + `MediaCaptureInitializationSettings` | `MetroWebCamTexture.cpp` cluster | no | WebCamTexture only; HedgePhysics does not appear to use webcam. LOW. |
| 10 | `Windows.UI.ViewManagement.StatusBar` | `hp_run_7.log:417`, thrown+caught | no | **No action** (proven non-fatal). |

---

## 3. Percent-decode fix plan (`unity%20default%20resources`)

### 3.1 Evidence

- Game tree contains **exactly one** percent-encoded path
  (`find /mnt/files/Downloads/HedgePhysics_1.2.3.0/... -name '*%*'` → 1):
  `Data/Resources/unity%20default%20resources`. Current workaround is a symlink
  `Data/Resources/unity default resources -> unity%20default%20resources`
  (created outside the repo, 2026-11-23).
- Run #4 `+file` trace (`/tmp/uwp_logs/hp_run_file.log:16597-16622`) shows Unity's first
  probes are **`GetFileAttributesExW("...\Data\Resources\unity default resources")`**
  (6+ times, `NtQueryFullAttributesFile` → `C0000034`), i.e. the decode must cover the
  attribute APIs, not only `CreateFileW`.
- `UnityPlayer.dll` imports the file APIs through API sets
  (`api-ms-win-core-file-l1-2-1.dll`, `-file-l2-1-1.dll`; see `llvm-objdump -p`), and
  **no game binary imports `KERNEL32_APP.dll`**: import lists are api-ms sets plus
  `VCRUNTIME140_APP`, `MSVCP140_APP`, `vccorlib140_app`, `XINPUTUAP`, `MMDevAPI.DLL`,
  `d3d11/d3d12/dxgi`, `OLEAUT32`, `WS2_32`, `UWPHOST`.

### 3.2 Layer decision

| Candidate | Verdict | Why |
|---|---|---|
| `forwarders/sys/KERNEL32_APP.def` | **No** | It is a forwarder DLL; nothing imports `KERNEL32_APP.dll`, and the app's api-ms file imports never resolve there. Editing it is inert (and would only help future titles). |
| `uwphost/uwphost_main.c:643` `CreateFileW` | **No** | That call is UWPHOST's own PE/CLR inspector (`uwp_pe_inspect_clr`); it never sees Unity's `Data/Resources` paths. |
| `wine/dlls/wintypes/storage.c` | **No** | Windows.Storage WinRT shims are used by the managed bridge only; UnityPlayer opens files through Win32. |
| **`wine/dlls/kernelbase/file.c`** | **Yes** | This is where the app's api-ms file imports land in Wine (apiset schema → kernelbase), and it already has a UWP hook precedent: `kernelbase_is_uwp_bridge_enabled()` (`kernelbase/main.c:98`, declared `kernelbase.h:41`) used by `GetCurrentPackagePath` etc. (`kernelbase/version.c:2100-2260`). No `.def`/`.spec`/`.idl` change is needed: the exported functions already exist; the retry helper stays `static`. |

### 3.3 Concrete patch (minimal)

1. **Add helper** in `wine/dlls/kernelbase/file.c` (near the file-API section, before
   `CreateFileW` main.c:~790; or a new `uwp_path.c` added to `dlls/kernelbase/Makefile.in`):
   ```c
   static BOOL uwp_percent_decode_path( LPCWSTR in, LPWSTR out, SIZE_T out_chars );
   ```
   Algorithm:
   - return FALSE unless `kernelbase_is_uwp_bridge_enabled()`, `in` non-NULL,
     `wcslen(in) < MAX_PATH`, and `in` contains at least one `%`;
   - convert only if needed: walk the wide string; a `%` followed by **two hex digits**
     contributes those two bytes, everything else contributes its wide char; collect a
     byte buffer, then `MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ...)`. If the
     bytes are not valid UTF-8, return FALSE (leave path untouched);
   - invalid `%` (not two hex digits) is copied literally; output `%` bytes are never
     re-examined (no double decode);
   - reject if decoded length ≥ `MAX_PATH`; return TRUE only if the result differs.
2. **`CreateFileW`** (`file.c:796`): rename the current body to
   `static HANDLE create_file_w( LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE )`
   and make the exported function a thin retry wrapper:
   ```c
   HANDLE WINAPI CreateFileW(...) {
       HANDLE h = create_file_w(...);
       DWORD err = GetLastError();
       WCHAR decoded[MAX_PATH];
       if (h == INVALID_HANDLE_VALUE &&
           (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) &&
           uwp_percent_decode_path(filename, decoded, ARRAY_SIZE(decoded))) {
           h = create_file_w(decoded, ...);
           if (h == INVALID_HANDLE_VALUE) SetLastError(err); /* preserve original error */
       }
       return h;
   }
   ```
   (Keep the `vxd_name`/`get_nt_file_options` logic inside `create_file_w`.)
3. **`GetFileAttributesExW`** (`file.c:1812`): after `if (!set_ntstatus(status))`, retry
   once — decode, `RtlDosPathNameToNtPathName_U` + `NtQueryFullAttributesFile` — when
   `GetLastError()` is `ERROR_FILE_NOT_FOUND`/`ERROR_PATH_NOT_FOUND`. This is the API the
   game actually fails on first (run #4 trace).
4. **`GetFileAttributesW`** (`file.c:1768`): same inline retry after
   `NtQueryAttributesFile` (cheap, same helper).
5. **Optional / defer:** `FindFirstFileExW` (`file.c:1272`) + `FindFirstFileW`
   (`file.c:1458`) — decode only the directory portion or skip: the title only does exact
   name probes today; wildcard decoding is a risk, so defer.
6. **Do not** touch `CreateDirectoryW`, NT-path APIs, or add any new export.

### 3.4 Guards / risks

- Gated by `PROTON_UWP_BRIDGE=1`; ordinary Wine apps are unaffected.
- Retry **only** after not-found, so a real file legitimately named `a%20b` still opens
  on the first lookup; exactly one retry, so `%2520` is not decoded twice.
- Restore the original `GetLastError()` when the retry also fails, so error semantics for
  the non-decoded case are unchanged.
- `MAX_PATH` enforced on the decoded string; NT-prefixed paths are not decoded (only
  DOS paths enter `RtlDosPathNameToNtPathName_U`).
- Other percent-looking names: only one such path exists in this package (3.1), so blast
  radius is one file; the env gate keeps it scoped anyway.
- Forwarder export ordinals are untouched (no `.def` edits anywhere).

### 3.5 Test approach

1. **Unit/integration in Wine tests:** add to `wine/dlls/kernelbase/tests/file.c`
   (existing file): create temp dir + file `"a b"`, `SetEnvironmentVariableW(L"PROTON_UWP_BRIDGE", L"1")`,
   assert `GetFileAttributesExW(L"...\\a%20b")` and `CreateFileW(L"...\\a%20b")` succeed;
   assert no decode when env var is unset; assert `a%2` (invalid) and `a%2520b` do not
   produce wrong opens. Run: `make -C wine/dlls/kernelbase test` (or the repo's usual
   `make -C build wine-x86_64` + `wine dlls/kernelbase/tests/kernelbase_test.exe file`).
2. **End-to-end:** remove only the symlink
   (`rm "<game>/Data/Resources/unity default resources"`), run the control command
   (`HEDGEPHYSICS_STATUS.md` §4) with `WINEDEBUG=+file,+err`, and check that
   `unity default resources` no longer ends in `C0000034` and that the resources load
   (no `Internal-ErrorShader`-class failure); then re-run with the env var unset once to
   confirm only the UWP path changed.
3. **Regression:** `make -C build wine-x86_64` must keep kernelbase tests green; also run
   the existing forwarder host tests (`test_sys_forwarders.py`, unchanged).

---

## 4. Recommended next 3 actions

1. **Calendar** (active abort): implement `Windows.Globalization.Calendar` in
   `wine/dlls/windows.globalization` (`classes.idl` + `main.c:1114` switch + new
   `calendar.c` with `IActivationFactory`/`ActivateInstance` and an `ICalendar` with real
   `SetToNow`/`GetDateTime`); then re-run and grep for
   `Failed to find library for L"Windows.Globalization.Calendar"`.
2. **Land the kernelbase percent-decode fallback** (§3.3: helper + retries in
   `CreateFileW:796`, `GetFileAttributesExW:1812`, `GetFileAttributesW:1768`), delete the
   game-folder symlink, and verify the `+file` trace shows a successful decoded probe.
   This is the only fix that removes an out-of-repo workaround.
3. **Close the two enumeration holes cheaply** (§1.5): (a) make
   `DevCreateObjectQueryEx` use `enum_dev_objects` with filters
   (`wine/dlls/cfgmgr32/main.c:1370-1379` + `:1277`), so device watchers finally deliver
   `Added`; (b) handle `DevQueryResultUpdate`/`DevQueryResultRemove` in
   `windows.devices.enumeration/main.c:323-389` with a new
   `device_information_update_create()` so the just-added `add_Removed`/`add_Updated`
   are not write-only. Without (a)+(b) the run #7 fix is only half of the DeviceWatcher
   contract; neither is currently fatal because Unity tolerates an empty device list.
