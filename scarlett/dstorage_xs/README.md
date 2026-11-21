# `dstorage_xs.dll` / `dstorage_xs.so` (`scarlett/dstorage_xs/`)

DirectStorage for Xbox Series X|S hardware queue and decompression pipeline.

## Architecture

- PE64 frontend (`dstorage_xs.dll`) validates 64-bit `DSTORAGE_REQUEST_OPTIONS` (`ZlibDecompress:1`, `BcpackMode:4`, `SwizzleMode:16`, `DestinationIsPhysicalPages:1`, `SourceType:1`, `SourceIsPhysicalPages:1`, `Reserved:40`) and dispatches batches via `WINE_UNIX_CALL`.
- Unix backend (`dstorage_xs.so`) executes asynchronous I/O via `io_uring`, GPU `vkdeflate` / `VK_EXT_memory_decompression`, CPU `zlib-ng`/`libdeflate`, and external `IBcpackDecoder` plugins (`PROTON_XBOX_BCPACK_PLUGIN`).
