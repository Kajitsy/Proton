# `xmem.dll` / `kernelx.dll` / `xmem_unix.so` (`scarlett/xmem/`)

Xbox Series X|S virtual/physical memory manager and 3-tier UMA Vulkan memory bridge.

## Memory Layout & Profiles

- `MEM_GRAPHICS`: `0x100000000..0xFFFFFFFFFF`
- `MEM_TITLE`: `0x40000000000..0x7FFFFFFFFFF`
- Profiles: Anaconda (`13.5 GiB`), Lockhart (`8.5 GiB`), DevKit (`40 GiB`).
- Separation: `xmem.dll` / `kernelx.dll` (PE64 `x86_64-windows`) invoke `xmem_unix.so` (`x86_64-unix`) exclusively via `WINE_UNIX_CALL`.
