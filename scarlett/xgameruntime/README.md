# `xgameruntime.dll` / `xgameplatform.dll` / `libHttpClient.dll` / `XCurl.dll` (`scarlett/xgameruntime/`)

Xbox Game Runtime (`xgameruntime.dll`) across all 22 `XGameRuntimeFeature` domains.

## Subsystems

- `XTaskQueue` and `XAsync` asynchronous completion engine + `XThreadIsTimeSensitive()`.
- `XGameSave` transactional atomic container storage (`0x80830001..0x8083000F`).
- `libHttpClient` / `XCurl` gated by `PROTON_XBOX_NET_MODE=offline|lan|passthrough`.
