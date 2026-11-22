# Proton-Scarlett — RDNA2 (`GFX10.3`) -> SPIR-V 1.5/1.6 Adaptive Lifter

Clean-room binary shader lifter translating Xbox Series X|S RDNA2 (`GFX10.3`)
shaders to Vulkan 1.4-compliant SPIR-V 1.5/1.6, plus a drop-in overlay for
`vkd3d-proton/subprojects/dxil-spirv/rdna2-lifter/` (branch `proton-scarlett-3.1`).

## Architecture
- **Path A (DXIL Fast-Path)**: Strips proprietary `XBOX` / `XS2M` chunks from
  DXBC/DXIL containers, recalculates the DXBC chunk table & MD5 digest, extracts
  Xbox resource binding remaps, and hands clean DXIL to `dxil-spirv`.
- **Path B (Native RDNA2 Containers)**: Unpacks `XS2`, `FSSM`, `PS\x00\x09`, and
  `SGD5` binary shader containers across all 9 stage families (`VS`, `PS`, `CS`,
  `NGG Mesh`, `Task/Amplification`, `GS`, `HS`, `DS`, `DXR`).
- **Full GFX10.3 ISA Decoder**: Decodes all 12+ hardware encoding classes
  (`SOP1`, `SOP2`, `SOPC`, `SOPK`, `SOPP`, `SMEM`, `VOP1`, `VOP2`, `VOP3A`,
  `VOP3B`, `VOP3P`, `VOPC`, `VINTRP`, `DS` + `ds_bpermute_b32`/`ds_swizzle_b32`,
  `MUBUF`/`MTBUF`, `MIMG` + `image_bvh*_intersect_ray`, `FLAT`/`GLOBAL`/`SCRATCH`, `EXP`).
- **SSA IR & CFG Structurizer**: Uniform (`SGPR`) vs divergent (`VGPR`) typing,
  `EXEC`/`WQM` structurization, `SPV_KHR_maximal_reconvergence`, `SPV_KHR_quad_control`.
- **4-Stage Adaptive Emitter (`exact` / `adaptive` / `fast_f32`)**:
  1. Live Vulkan 1.4 compute microprobes (reading non-constant operands from
     `VkBuffer`) folded into a SHA-256 pipeline cache key.
  2. Forward + backward `computeKnownFPClass` dataflow analysis.
  3. Adaptive Wave64 (`NativeWave64` on AMD RADV, `NarrowedWave32` on Wave32-safe
     NVIDIA shaders, `PairedWave64LdsEmulation` when cross-half-wave deps exist).
  4. BVH4 RT (`VK_KHR_ray_query` primary + `PhysicalStorageBuffer64` 128B/64B BDA fallback).
