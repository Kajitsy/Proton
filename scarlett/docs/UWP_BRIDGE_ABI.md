# Proton-Scarlett UWP Bridge ABI Contract (`uwp_bridge`)

**Status:** FROZEN (Phase 0 Baseline on `proton-scarlett-dev`)  
**Schema Version:** `1.0.0`  
**Owner:** Gemini 4 Neon (System & Graphics Architect)  
**Shared C/C++ Header:** `scarlett/uwp_bridge/include/uwp_bridge_abi.h`

---

## 1. Scope & Clean-Room Invariants

The `uwp_bridge` layer extends `Proton-Scarlett` (`proton_11.0`, Wine 11.0+, DXVK, `vkd3d-proton`, `steamrt4`) to run UWP PE64 applications (`gzdoom.exe`, `HedgePhysics.exe`, and generic UWP/XAML/CoreWindow titles) without rewriting existing `scarlett/` subsystems.

### Hard Rules
1. **Clean-Room Only:** Zero binaries, headers, or code copied from Windows SDK, `WindowsApps`, or Xbox GDK. All definitions derive cleanly from Wine/MinGW headers and documented COM/WinRT ABI layouts.
2. **Zero Silent Stubs:** No `return S_OK;` placeholders for window creation, swapchain creation, or WinRT class activation. Every unimplemented `*_APP.dll` export or unknown `activatableClassId` MUST emit a structured diagnostic containing:
   - DLL name or `activatableClassId`
   - Symbol name / ordinal or requested IID
   - Caller return address & module RVA
   - Captured Win64 argument registers (`RCX`, `RDX`, `R8`, `R9`)
   - Under `PROTON_XBOX_STRICT=1`, aborts with `SCARLETT_ERR_STRICT_STUB_ABORT`.
3. **Wine 11.0 PE vs Unix Separation:**
   - All `x86_64-windows` PE64 DLLs (`UWPHOST.dll`, `*_APP.dll`) compile with MinGW/LLVM (`-ffreestanding` or standard Wine/MinGW Win32 APIs ONLY — zero direct Linux syscalls or libc/POSIX host calls).
   - Host/Linux access goes exclusively through `x86_64-unix` `.so` modules via `WINE_UNIX_CALL` (`scarlett/include/scarlett_unix_call.h`) or standard Win32/Wine APIs.
4. **Zero PC Regressions:** All Wine hooks (`combase`, `kernelbase`) and `proton` UWP overrides activate ONLY when UWP mode is active (`PROTON_UWP_BRIDGE=1` or explicit UWP autodetect).

---

## 2. Canonical Directory Layout

```text
scarlett/uwp_bridge/
  include/
    uwp_bridge_abi.h          # Frozen shared ABI header (IIDs, structs, internal hooks)
  forwarders/
    gen_app_forwarders.py     # Generator emitting .def + diagnostic thunk C files
    sys/                      # Win32/System *_APP.dll + XINPUTUAP.dll
    crt/                      # MSVC UWP *_APP.dll (VCRUNTIME140_APP, MSVCP140_APP, vccorlib140_app, ...)
  uwphost/                    # UWPHOST.dll core: DllMain, ExecuteAssembly, AppxManifest, CoreApplication/CoreWindow/CoreDispatcher
  winrt_ui/                   # DisplayInformation, ApplicationView, SystemNavigationManager, CoreCursor, MouseDevice, DisplayRequest, ApiInformation
  winrt_sys/                  # ApplicationData, Package, Gamepad (bridged to GameInput/XInput), AnalyticsInfo
  tests/                      # Host unit tests + synthetic PE64 uwp_corewindow_dx11_test.exe
  meson.build
  CMakeLists.txt
```

---

## 3. Target PE64 Forwarder Matrix (`*_APP.dll`)

### 3.1 System / Win32 UWP Forwarders (`scarlett/uwp_bridge/forwarders/sys/`)

| Forwarder DLL | Primary Target(s) in Wine 11.0 | Notes |
|---|---|---|
| `KERNEL32_APP.dll` | `kernel32.dll`, `kernelbase.dll` | Plus package identity & AppPolicy exports |
| `USER32_APP.dll` | `user32.dll` | Win32 windowing/input exports |
| `GDI32_APP.dll` | `gdi32.dll` | GDI exports |
| `ADVAPI32_APP.dll` | `advapi32.dll` | Registry/crypto/eventing exports |
| `OLE32_APP.dll` | `ole32.dll`, `combase.dll` | COM core |
| `COMBASE_APP.dll` | `combase.dll` | WinRT & COM activation |
| `RPCRT4_APP.dll` | `rpcrt4.dll` | RPC runtime |
| `WS2_32_APP.dll` | `ws2_32.dll` | Winsock2 |
| `WINHTTP_APP.dll` | `winhttp.dll` | HTTP client |
| `CRYPT32_APP.dll` | `crypt32.dll` | Crypto API |
| `BCRYPT_APP.dll` | `bcrypt.dll` | CNG crypto |
| `MMDEVAPI_APP.dll` | `mmdevapi.dll` | WASAPI audio device enumeration |
| `XINPUT1_4_APP.dll` | `xinput1_4.dll` | XInput 1.4 |
| `XINPUTUAP.dll` | `xinput1_4.dll` / `xinputuap` | Directly imported by `UnityPlayer.dll` (`HedgePhysics.exe`) |

### 3.2 MSVC UWP Runtime Forwarders (`scarlett/uwp_bridge/forwarders/crt/`)
Directly imported by `gzdoom.exe`, `SDL2.dll`, `OpenAL32.dll`, `libgallium_wgl.dll`, `zmusic.dll`, `HedgePhysics.exe`, `UnityPlayer.dll`, `UnityEngineDelegates.dll`, `BridgeInterface.dll`:

| Forwarder DLL | Primary Target in Wine 11.0 |
|---|---|
| `VCRUNTIME140_APP.dll` | `vcruntime140.dll` |
| `VCRUNTIME140_1_APP.dll` | `vcruntime140_1.dll` |
| `MSVCP140_APP.dll` | `msvcp140.dll` |
| `MSVCP140_1_APP.dll` | `msvcp140_1.dll` |
| `MSVCP140_2_APP.dll` | `msvcp140_2.dll` |
| `CONCRT140_APP.dll` | `concrt140.dll` |
| `vccorlib140_app.dll` | `vccorlib140.dll` |

Any symbol imported by UWP binaries that does not exist in the target Wine `.spec` MUST be emitted as a real safe implementation or a diagnostic auto-stub trampoline (logging DLL, symbol/ordinal, caller RVA, `RCX/RDX/R8/R9` and returning a failure status/HRESULT — never causing `0xC0000139` loader failure).

---

## 4. Package Identity & `AppxManifest.xml` Contract

### 4.1 Environment Variables Set by `proton` (UWP Mode)
- `PROTON_UWP_BRIDGE=1`
- `PROTON_UWP_MANIFEST=<windows or unix path to AppxManifest.xml>`
- `PROTON_UWP_PACKAGE_DIR=<windows path to package root, e.g. Z:\...\>`
- `PROTON_UWP_PACKAGE_NAME=<Identity/@Name>`
- `PROTON_UWP_PACKAGE_PUBLISHER=<Identity/@Publisher>`
- `PROTON_UWP_PACKAGE_VERSION=<Identity/@Version>`
- `PROTON_UWP_PACKAGE_FULL_NAME=<Name>_<Version>_x64__<PublisherId>`
- `PROTON_UWP_PACKAGE_FAMILY_NAME=<Name>_<PublisherId>`
- `PROTON_UWP_AUMID=<PackageFamilyName>!<Application/@Id>`
- `PROTON_UWP_LOCAL_STATE=<drive_c/users/steamuser/AppData/Local/Packages/<FamilyName>/LocalState>`
- `PROTON_UWP_ROAMING_STATE=<.../RoamingState>`
- `PROTON_UWP_TEMP_STATE=<.../TempState>`

### 4.2 Required Package & AppPolicy APIs (`kernelbase.dll`, `kernelx.dll`, `KERNEL32_APP.dll`)
When `PROTON_UWP_BRIDGE=1` (or manifest is present alongside executable):
- `GetCurrentPackageFullName` -> `ERROR_SUCCESS` (or `ERROR_INSUFFICIENT_BUFFER` with required WCHAR count including NUL)
- `GetCurrentPackageFamilyName` -> `ERROR_SUCCESS` / `ERROR_INSUFFICIENT_BUFFER`
- `GetCurrentPackageId` -> populates `PACKAGE_ID` (`version`, `processorArchitecture=9`, `name`, `publisher`, `publisherId`)
- `GetCurrentApplicationUserModelId` -> returns `<FamilyName>!<AppId>`
- `GetCurrentPackagePath` / `GetPackagePathByFullName` -> returns package install directory
- `AppPolicyGetProcessTerminationMethod` -> `AppPolicyProcessTerminationMethod_TerminateProcess` (2)
- `AppPolicyGetWindowingModel` -> `AppPolicyWindowingModel_Universal` (2)

---

## 5. `UWPHOST.dll` & WinRT Activation Contract

### 5.1 Required Exports of `UWPHOST.dll`
- `DllMain`
- `DllGetActivationFactory(HSTRING activatableClassId, IActivationFactory **factory)`
- `ExecuteAssembly(LPCWSTR entryPoint, ...)` — directly imported by `HedgePhysics.exe` (`UWPShim.exe`)! Parses `AppxManifest.xml`, initializes `UWPHOST`, locates/hosts the managed/native entry point or `UnityPlayer.dll` activation path, and logs exact diagnostics.
- `UwpHostInitialize(const uwp_host_config_t *cfg)`
- `UwpHostRun(IUnknown *viewSource)`
- `UwpHostTryGetActivationFactory(HSTRING classId, REFIID iid, void **factory)` — called by `wine/dlls/combase/roapi.c` when `PROTON_UWP_BRIDGE=1`
- `UwpHostLogUnknownClass(HSTRING classId, REFIID iid, void *caller_ret_addr, uint64_t rcx, uint64_t rdx, uint64_t r8, uint64_t r9)`
- WinRT runtime exports / hooks: `RoInitialize`, `RoUninitialize`, `RoGetActivationFactory`, `RoActivateInstance`, `WindowsCreateString`, `WindowsCreateStringReference`, `WindowsGetStringRawBuffer`, `WindowsDeleteString`.

### 5.2 Sub-Module Registration Protocol (`winrt_ui` & `winrt_sys` -> `uwphost`)
To allow parallel development with zero merge conflicts, `uwphost` dispatches `DllGetActivationFactory` in order:
1. Core classes (`uwphost`): `Windows.ApplicationModel.Core.CoreApplication`, `Windows.UI.Core.CoreWindow`, `Windows.UI.Core.CoreDispatcher`
2. UI classes (`winrt_ui`): calls `uwp_winrt_ui_get_activation_factory(const wchar_t *class_name, REFIID iid, void **out_factory)`
3. System classes (`winrt_sys`): calls `uwp_winrt_sys_get_activation_factory(const wchar_t *class_name, REFIID iid, void **out_factory)`
4. `AppxManifest.xml` `<Extension Category="windows.activatableClass.inProcessServer">`: loads `<Path>` DLL/EXE via `LoadLibraryW` and forwards to its `DllGetActivationFactory`!
5. Unknown class: logs `activatableClassId`, `IID`, caller RVA, and registers (`RCX/RDX/R8/R9`), then returns `CLASS_E_CLASSNOTAVAILABLE` (or aborts under `PROTON_XBOX_STRICT=1`).

### 5.3 CoreWindow <-> DXVK/vkd3d-proton Interop Bridge
Every `CoreWindow` object implemented in `UWPHOST.dll` MUST respond to `QueryInterface` for BOTH:
1. Standard Windows `IID_ICoreWindowInterop` (`{45D64A29-A63E-4CB6-B498-5781D298CB4F}`):
   - Slot 3: `HRESULT get_WindowHandle(HWND *hwnd)`
   - Slot 4: `HRESULT put_MessageHandled(unsigned char value)`
2. Proton-Scarlett `IID_IProtonScarlettCoreWindowBridge` (`{7A53F1D2-8C4B-4E91-9A2D-5B6E0C3F8A10}`):
   - Slot 3: `HRESULT GetHwnd(HWND *out_hwnd)`
   - Slot 4: `HRESULT GetClientSize(uint32_t *out_width, uint32_t *out_height)`
   - Slot 5: `HRESULT IsFullscreen(int32_t *out_is_fullscreen)`
   - Slot 6: `HRESULT NotifySwapChainCreated(IUnknown *swapchain)`

---

## 6. DXVK & `vkd3d-proton` `CreateSwapChainForCoreWindow` Contract

In `dxvk/src/dxgi/dxgi_factory.cpp`:
1. Validate `pDevice`, `pWindow`, `pDesc`, `ppSwapChain` (else `DXGI_ERROR_INVALID_CALL`).
2. Extract `HWND` from `pWindow` via `QueryInterface(IID_IProtonScarlettCoreWindowBridge)` first, falling back to `QueryInterface(IID_ICoreWindowInterop)`. If neither succeeds or `hwnd == NULL`, log error and return `DXGI_ERROR_INVALID_CALL`.
3. Normalize `DXGI_SWAP_CHAIN_DESC1`:
   - If `Width == 0 || Height == 0`: populate from `IProtonScarlettCoreWindowBridge::GetClientSize` or `GetClientRect(hwnd)` (fallback `1280x720` only if window rect is 0x0 and log warning).
   - Map `DXGI_SCALING_ASPECT_RATIO_STRETCH` (2) -> `DXGI_SCALING_STRETCH` (0) for HWND swapchain compatibility while preserving aspect info in logs.
   - Normalize `AlphaMode` (`DXGI_ALPHA_MODE_PREMULTIPLIED`/`STRAIGHT` -> `DXGI_ALPHA_MODE_IGNORE` if unsupported by HWND path).
   - Ensure `BufferCount >= 2` when `SwapEffect` is `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL` (3) or `DXGI_SWAP_EFFECT_FLIP_DISCARD` (4).
4. Construct `DXGI_SWAP_CHAIN_FULLSCREEN_DESC` (`Windowed = TRUE`, zero refresh rate) and delegate to `CreateSwapChainBase(pDevice, hwnd, &normDesc, &fsDesc, pRestrictToOutput, ppSwapChain)`.
5. Store reference to `pWindow` on `DxgiSwapChain` so `IDXGISwapChain1::GetCoreWindow(REFIID, void**)` returns the real `CoreWindow` instead of `DXGI_ERROR_INVALID_CALL`.
6. Ensure `vkd3d-proton` (`IDXGIVkSwapChainFactory::CreateSwapChain`) handles normalized `DXGI_SWAP_CHAIN_DESC1` cleanly for D3D12 UWP titles (`gzdoom.exe` GLon12/Mesa D3D12 & `UnityPlayer.dll` D3D12).
