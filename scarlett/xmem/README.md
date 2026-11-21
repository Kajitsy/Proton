# `xmem.dll` / `kernelx.dll` / `xmem_unix.so` (`scarlett/xmem/`)

Xbox Series X|S virtual/physical memory manager, 3-tier UMA bridge, P-core thread topology mapper, and `/dev/ntsync` synchronization layer.

## Architecture & Strict PE64 / Unixlib Separation

| Component | Target | Source Files | Boundary Rule |
| :--- | :--- | :--- | :--- |
| **`xmem.dll`** | PE64 (`x86_64-windows`) | `xmem_pe.c`, `xmem.def` | Zero libc/POSIX/Vulkan symbols; crosses strictly via `WINE_UNIX_CALL` |
| **`kernelx.dll`** | PE64 (`x86_64-windows`) | `kernelx_pe.c`, `kernelx.def` | Zero libc/POSIX/Vulkan symbols; crosses strictly via `WINE_UNIX_CALL` |
| **`xmem_unix.so`** | ELF64 (`x86_64-unix`) | `xmem_phys_virt.cpp`, `xmem_uma_bridge.cpp`, `xmem_thread_sync.cpp`, `xmem_unix_dispatch.cpp` | Native Linux `memfd_create`/`DMA-BUF`/`mmap(MAP_FIXED)`/`userfaultfd`/`mprotect`/`/dev/ntsync`/futex |
| **Shared ABI** | Freestanding C11/C++20 | `xmem_unix_abi.h`, `xmem_api.h`, `xmem_pe_bridge.h` | Fixed-width structs with `SCARLETT_STATIC_ASSERT` |

## Memory Layout, Invariants & Budgets

- **`MEM_GRAPHICS`**: `0x0000000100000000..0x000000FFFFFFFFFF`
- **`MEM_TITLE`**: `0x0000040000000000..0x000007FFFFFFFFFF`
- **Console Profiles (`PROTON_XBOX_MODEL`)**:
  - `anaconda`: `13.5 GiB` (`14,495,514,624` bytes; `10.0 GiB` fast + `3.5 GiB` standard)
  - `lockhart`: `8.5 GiB` (`9,126,805,504` bytes; `8.0 GiB` fast + `0.5 GiB` standard)
  - `devkit`: `40.0 GiB` (`42,949,672,960` bytes; `30.0 GiB` fast + `10.0 GiB` standard)
- **Strict Invariants**:
  - RO/RW aliasing of the same physical pages -> allowed (zero-copy hardware-coherent).
  - Cache-attribute mismatch (`WRITEBACK` vs `WRITECOMBINE` vs `UNCACHED`) -> `SCARLETT_ERR_XMEM_CACHE_CONFLICT` (`SCARLETT_ERR_XMEM_CACHE_ATTR_CONFLICT`).
  - Cross-allocation virtual or physical map/unmap -> `SCARLETT_ERR_XMEM_CROSS_ALLOCATION` (`SCARLETT_ERR_XMEM_CROSS_ALLOC_SPAN`).
  - Freeing still-mapped physical pages -> `SCARLETT_ERR_XMEM_PAGES_STILL_MAPPED`.

## 3-Tier UMA Bridge (Zero 1-Frame Lag)

1. **Tier A**: ReBAR / APU zero-copy via `VK_KHR_external_memory_fd` (DMA-BUF / `memfd`).
2. **Tier B**: `VK_EXT_external_memory_host` + GPU-read hot-resource tracking and device-local shadow migration.
3. **Tier C**: `userfaultfd` (`UFFDIO_WRITEPROTECT`) with automatic async-signal-safe `mprotect(PROT_READ)` + `SIGSEGV` write-watch fallback when unprivileged `userfaultfd` is restricted. Dirty CPU pages are flushed synchronously before every `ExecuteCommandLists` via `scarlett_uma_flush_before_submit` (never deferred to `PresentX`).

## Threading & Synchronization

- 7-core / 14-thread Xbox title topology mapped onto host P-cores via `/sys/devices/system/cpu`.
- Per-thread TLS `XThreadSetTimeSensitive` / `XThreadIsTimeSensitive` / `XThreadAssertNotTimeSensitive`.
- Native `/dev/ntsync` (`NTSYNC_IOC_*`) with explicitly diagnosable Linux `futex` + `eventfd` fallback.
