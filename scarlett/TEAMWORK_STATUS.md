# Scarlett Teamwork Status

Обновлено: 2026-11-25T00:45:30Z

## Что сделано (итерация 11: проверка draw-flow M3 — CTest 8/8, последовательная сборка, прогон R3 45s и VKD3D_DEBUG=warn)
- **Сборка и тесты**:
  - Host CTest (`cmake -S scarlett -B /tmp/scarlett_host_tests -G Ninja -DSCARLETT_BUILD_HOST_TESTS=ON -DSCARLETT_BUILD_PE=OFF`, цели `test_d3d12_xs`, `test_rdna2_lifter`, `test_rdna2_corpus`): пройдено **`8/8`** (`rdna2_lifter_unit_tests`, `rdna2_lifter_corpus_2048_exit_gate`, `rdna2_lifter_dropin_check`, `rdna2_lifter_dropin_dry_run`, `d3d12_xs_unit_tests`, `d3d12_xs_pe64_freestanding`, `d3d12_xs_pe64_nm_isolation`, `d3d12_xs_def_dlltool`, `CTEST_EXIT=0`).
  - Последовательная инкрементальная сборка (`make -j$(nproc) -C build vkd3d-proton`, затем `scarlett`, затем `dist`) завершилась с кодом возврата **`0`** (при одновременном параллельном запуске `dist` и других целей возможна гонка установки Meson).
- **Прогон R3 (`45s`, `PROTON_XBOX_SCARLETT=1`, `PROTON_XBOX_SHADER_MODE=adaptive`, без `+module`)**:
  - Завершился по таймауту `45s` (`exit=124`, `25275` строк / `24135` после дедупликации `:warn:seh:dispatch_exception` в логе R3), **`Unhandled exception`: `0`**, обязательная очистка процессов подтвердила `CLEAN: 0 processes`.
- **Шейдеры, PSO и командные списки (`[scarlett-shader]`, `[scarlett-pso]`, `[scarlett-cl]`)**:
  - Ошибки декодирования ISA `0xa05c0202` полностью устранены (`4 -> 0`), пропуск `slot=57 seq=37` устранён (`slot=57`: `11/11` `real != 0`).
  - `[scarlett-pso]` (`77` записей): **`76/77` `real != 0`** (`slot=86`: `12/12`, `slot=55`: `43/43`, `slot=57`: `11/11`, `slot=67`: `10/11`, остаётся `1` `real==0` на `slot=67 seq=52224` с `lhr=0xA05C0203`).
  - `[scarlett-shader]` (`49` записей, до `lift #2200`): `8` ошибок `0xa05c0203` (`spirv-val` SSA dominance) и `1` `hr=0x80830007` на `lift #2200` (`wave=0`).
  - `[scarlett-cl]` (`117` записей): пересылаются `Close` (`8`), `SetDescriptorHeaps` (`7`), `SetPipelineState` (`7`), `SetGraphicsRootSignature` (`7`), `OMSetRenderTargets` (`7`), `OMSetDepthBounds` (`7`), `DrawInstanced` (`7`), `ClearRenderTargetView` (`6`), `IASetPrimitiveTopology` (`6`), `OMSetBlendFactor` (`6`), `RSSetScissorRects` (`6`), `RSSetViewports` (`6`), `SetGraphicsRootSRV` (`6`), `IASetVertexBuffers` (`6`), `SetGraphicsRootDescriptorTable` (`6`, `bad_gpu_desc=0`), `ExecuteCommandLists` (`9`), `XboxSlot72/74/75` (`7/6/6` `xbox_noop`); `ResourceBarrier`/`Dispatch`/`IASetIndexBuffer`/`ResetX` в логе `0`.
  - `CreatePlacedResourceX` (`22` записи): `15` `(created)`, `7` `(created-va0)` (включая оба буфера `PresentX` `0x217D0000` / `0x23780000`).
- **`PresentX` и захват окна**:
  - `[scarlett-presentx]`: `33` записи (`frame=1..180`, `hr=0x00000000`); `[scarlett-readback]`: `11` раз (`3840x2160 fmt=28 nz=0`, `min=0 max=0`); `[scarlett-presentx-wsi]`: `8` раз (`did_blit=1`, `nonzero_rgb=0/4224`).
  - Захват X11 (`583x772`) полностью чёрный (`Grayscale min=0 max=0 mean=0 stddev=0`); захват `grim` (`1490x802` при `at=[-907,-30]`) показывает тёмное окно и панель рабочего стола без отрисованного кадра.

## Диагностика `VKD3D_DEBUG=warn` (20s)
- Завершился с `exit=124` (`22409` сырых / `21397` дедуп. строк, `Unhandled exception=0`, `CLEAN: 0 processes`).
- Активные предупреждения `vkd3d-proton`: `d3d12_pipeline_state_init_graphics_create_info: DSV format is DXGI_FORMAT_UNKNOWN.` (`x4`), `vk_image_aspect_flags_from_d3d12: Invalid plane index 3 for format 37.` (`x2`), `d3d12_descriptor_heap_create: Unsupported descriptor count 1245184 for heap type 0 (max 1000000).` (`x1`), `d3d12_device_create_vkd3d_queues: Could not allocate an out of band queue for queue family 2.` (`x1`), `d3d12_device_caps_init_shader_model: Enabling SM 6.6 on pre-Turing NVIDIA despite lack of native compute shader derivatives.` (`x1`), `openvr_*` / `openxr_*`. `VK_ERROR_DEVICE_LOST` и `err:vkd3d` отсутствуют (`0`).

## Активные блокеры M3 (почему `nz=0` при `DrawInstanced` и `PresentX`)
1. **`CreatePlacedResourceX` `(created-va0)` (`7/22` ресурсов, включая оба буфера `PresentX` `0x217D0000` / `0x23780000`)**: целевые поверхности представления создаются по ветке `created-va0`, без ненулевого вывода в `readback` / `WSI`.
2. **Ограничение кучи дескрипторов (`1245184 -> 1000000`) и отсутствие пересылки `ResourceBarrier` / `Dispatch` / `IASetIndexBuffer` / `ResetX`** в `[scarlett-cl]`.
3. **Ошибки лифтинга шейдеров**: `8` `0xa05c0203` (`spirv-val` SSA dominance), `1` `real==0` PSO (`slot=67 seq=52224`) и `hr=0x80830007` на `lift #2200`.
