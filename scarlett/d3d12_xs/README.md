# Proton-Scarlett `d3d12_xs` Subsystem (`d3d12_xs.dll` + `d3d12_xs_unix.so`)

Clean-room Xbox Series X|S Direct3D 12 extension layer (`d3d12_xs.dll` PE64 + `d3d12_xs_unix.so` Vulkan 1.4 unixlib) and `vkd3d-proton` (`proton-scarlett-3.1`) drop-in overlay.

## Key Capabilities
- **Dynamic Vulkan 1.4 Backend (`dlopen("libvulkan.so.1")`)**: probes `VK_EXT_mesh_shader`, `VK_KHR_fragment_shading_rate`, `VK_EXT_shader_image_atomic_int64` (Nanite), `VK_EXT_device_generated_commands`, `VK_KHR_ray_query`, `VK_KHR_opacity_micromap`, `VK_EXT_device_fault` + DRED `PROTON_LOG` dumps, and optional `VK_LAYER_KHRONOS_validation` with zero-error verification.
- **GXDK >= `0x585D070E` Device Creation**: `AmplificationShaderIndirectArgsBufferSize`, `AmplificationShaderPayloadBufferSize`, `SetAmplificationShaderLimits`.
- **0-Frame Lag UMA Pre-Submit Flush**: `CreatePlacedResourceX` over `MEM_GRAPHICS` virtual/physical UMA regions (`xmem`), invoking `scarlett_uma_flush_before_submit()` synchronously before every `ExecuteCommandLists` (never deferred to `PresentX`).
- **Separated Queue Families, Enhanced Barriers & ExecuteIndirect**: Xbox queues `0x0` (Direct), `0x2` (DMA/Copy), and `0x10` (Async Compute XS) mapped to dedicated Vulkan queue families; `VkDependencyInfo`/`VkMemoryBarrier2` barriers; `VK_EXT_device_generated_commands` with clean CPU-parsed `vkCmdDrawIndexedIndirect`/`vkCmdDispatchIndirect` fallback.
- **Descriptor Heap Policy & NVIDIA 610.xx / 1M Guard**: `T#`/`V#`/`S#` heaps via `VK_EXT_descriptor_heap` with mandatory dynamic fallback to `VK_EXT_descriptor_buffer` (and `VK_EXT_descriptor_indexing`) on NVIDIA 610.xx drivers or `> 1,000,000` descriptors.
- **DXIL Fast-Path (Path A)**: DXBC container parser stripping private `XBOX` and `XS2M` chunks, recalculating chunk offsets/sizes/digest, and validating standard `DXIL`/`ILDB` presence for `dxil-spirv`.
- **PresentX & Sub-ms Frame Pacing**: `SetFrameIntervalX`, `ScheduleFrameEventX`, blocking `WaitFrameEventX` (monotonic 64-bit `D3D12XBOX_FRAME_PIPELINE_TOKEN`, hybrid `CLOCK_MONOTONIC` pacer), dual-plane HDR10 PQ (`G2084_P2020` / `VK_COLOR_SPACE_HDR10_ST2084_EXT`) + GameDVR (`G22_P709`), safe `Alt+Enter`/resize swapchain rebuild, and `SuspendX`/`ResumeX`.
- **Strict PE64 Isolation & Zero Silent Stubs**: freestanding C11 `d3d12_xs_pe.c` (`__wine_init_unix_call`, dense `__wine_unix_call_funcs` table), diagnostic auto-stub trampoline for all unimplemented exports and removed/unimplemented COM vtable slots.
- **Drop-In `vkd3d-proton` Overlay**: `vkd3d_proton_dropin/meson.build` and `vkd3d_proton_dropin/apply_to_vkd3d_proton.sh`.
