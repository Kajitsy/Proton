# Scarlett Teamwork Status

Обновлено: 2026-11-24T21:08:00Z

## Что сделано (итерация 8: живая проверка M3 — реализация PSO, пересылка SetPipelineState/DrawInstanced и захват окна)
- Прогон R3 (`45s`, `PROTON_XBOX_SCARLETT=1`, `PROTON_XBOX_SHADER_MODE=adaptive`, `/tmp/gta`, без `+module`) завершился по таймауту `45s` (`exit=124`, `19717` строк / `18825` после дедупликации в `/tmp/gta_scarlett_loop.log`) без `Unhandled exception` (`0` падений, `0` новых `.crash.log`; при завершении по `SIGTERM` в конце лога зафиксирована одна строка `virtual_setup_exception nested exception` на потоке `0184`).
- **Реализация PSO (`[scarlett-pso]`, `78` залогированных вызовов до `seq=53248`)**:
  - `slot=86`: `12/12` `real != 0`; `slot=55`: `43/43` `real != 0`; `slot=57`: `10/11` `real != 0` (`1` — `seq=35`, `spirv=0, real=0`); `slot=67`: `11/12` `real != 0` (`1` — `seq=53248`, `lhr=0x80830007`). Всего `76/78` `real != 0`, `0` падений `vkCreateGraphicsPipelines`, `[scarlett-shader] lift_container failed`: `8` (`0xa05c0203` / `0xa05c0202`, дошли до `#3800`).
- **Пересылка `ID3D12GraphicsCommandListX` (`[scarlett-cl]`, `128` дедуп. строк)**:
  - `SetPipelineState` (`9`, до `seq=1536`) и `DrawInstanced` (`9`, до `seq=1536`) успешно пересланы в `clm->real_obj`, `no_real_pso`: `0`. Также пересланы `SetDescriptorHeaps` (`9`), `SetGraphicsRootSignature` (`9`), `OMSetRenderTargets` (`9`), `OMSetDepthBounds` (`9`), `IASetPrimitiveTopology` (`8`), `ClearRenderTargetView` (`7`), `OMSetBlendFactor` (`7`), `RSSetViewports` (`7`), `RSSetScissorRects` (`7`); `XboxSlot72/74/75` обработаны как `xbox_noop`.
  - **Выявленный пробел привязки**: все `13` залогированных вызовов `SetGraphicsRootDescriptorTable` (`slot=36`, `seq=1..1280`, `BaseDescriptor=0x0FA8EF00`..`0x1146DEA0`) отброшены с `(bad_gpu_desc)` в `d3d12_xs_pe.c:3625` (`d3d12xs_translate_gpu_desc_handle == 0`).
- **`PresentX` и захват окна**:
  - Окно `steam_proton` (`1490x802` при `at=[-907,-30]`, X11 ID `0x7200001`, `xwayland=true`) захвачено до завершения 45s в `/tmp/gta_iter8_grim.png` и `/tmp/gta_iter8_x11.png`: содержимое остаётся пустым (`x11` `583x772` полностью чёрный `min=0 max=0 mean=0 stddev=0`; `grim` показывает тёмную подложку/верхнюю панель поверх рабочего стола, слева белая область за границей экрана `x<0`).
  - `[scarlett-presentx]`: `frame=1..600` (`40` записей, `hr=0x00000000`); `[scarlett-readback]`: `18` раз (`ok`, `3840x2160 fmt=28 nz=0`); `[scarlett-presentx-wsi]`: `15` раз (`did_blit=1`, `nonzero_rgb=0/4224`).

## Динамическая трассировка `/tmp/gta_scarlett_loop.log` (дедуп.)
- `xmem.dll`: `17`; `dstorage_xs`: `193`; `AcpHal`: `4`; `XApu`: `1`; `xg_xs` / `xgameruntime` / `XCRDAPI` / `Bink2Scarlett` / `GameInput`: `0`.
- `d3d12_xs.dll`: `CreatePlacedResourceX` (`19`, включая `p0va=0x216A0000`/`0x23650000` с `created-va0`), `CreateRootSignature` (`41`), `CreateCommandList` (`15`), `WaitFrameEventX` (`18`), `FlushGpuLabels` (`17`), `[scarlett-sampler]` (`8`), `[scarlett-veh]` (`13`).
- Ошибки `vkd3d`/`d3d12`/`vulkan` (`err:`): `0`; `VK_ERROR_DEVICE_LOST`: `0`. Прочие `err:`: `8` (`winebth` `c0000142` x1, `openxr` `0x2` x6, финальный `nested exception` при `SIGTERM` x1).

## Активные блокеры M3 (почему `nz=0` при `real != 0` PSO)
1. **`SetGraphicsRootDescriptorTable` `(bad_gpu_desc)` (`13/13` вызовов в `d3d12_xs_pe.c:3625`)**: `d3d12xs_translate_gpu_desc_handle()` возвращает `0` для GPU-адресов дескрипторов (`0x0FA8EF00`..`0x1146DEA0`), из-за чего таблицы дескрипторов ресурсов/CBV не пересылаются перед `DrawInstanced`.
2. **Отсутствие вызовов `Dispatch` / `ResourceBarrier` / `IASetVertexBuffers` / `IASetIndexBuffer` / `Close` в `[scarlett-cl]` и `(created-va0)` на буферах `PresentX`** (`p0va=0x216A0000`/`0x23650000`): привязка GPU VA и барьеров/закрытия командных списков требует проверки перед следующим прогоном.
3. **Единичные пропуски шейдеров**: `8` ошибок `[scarlett-shader] lift_container failed` (`0xa05c0203` / `0xa05c0202`) и `2` `real=0` PSO (`slot=57 seq=35`, `slot=67 seq=53248`).
