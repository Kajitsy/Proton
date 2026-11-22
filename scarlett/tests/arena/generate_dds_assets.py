#!/usr/bin/env python3
"""Generate strict 148-byte DDS + DX10 test assets (BC7 and RGBA8) for ScarlettArena-Linux."""

import os
import struct
import sys

DDS_MAGIC = 0x20534444       # "DDS "
DDS_FOURCC_DX10 = 0x30315844 # "DX10"

DDSD_CAPS = 0x1
DDSD_HEIGHT = 0x2
DDSD_WIDTH = 0x4
DDSD_PITCH = 0x8
DDSD_PIXELFORMAT = 0x1000
DDSD_MIPMAPCOUNT = 0x20000
DDSD_LINEARSIZE = 0x80000
DDPF_FOURCC = 0x4
DDSCAPS_TEXTURE = 0x1000

DXGI_FORMAT_R8G8B8A8_UNORM = 28
DXGI_FORMAT_BC7_UNORM = 98
D3D10_RESOURCE_DIMENSION_TEXTURE2D = 3


def pack_dds_dx10(width: int, height: int, dxgi_format: int, payload: bytes) -> bytes:
    is_bc7 = dxgi_format == DXGI_FORMAT_BC7_UNORM
    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_MIPMAPCOUNT
    if is_bc7:
        flags |= DDSD_LINEARSIZE
        pitch_or_linear = ((width + 3) // 4) * ((height + 3) // 4) * 16
    else:
        flags |= DDSD_PITCH
        pitch_or_linear = width * 4

    # 4 bytes magic + 124 bytes DDS_HEADER + 20 bytes DDS_HEADER_DXT10 = 148 bytes
    hdr = struct.pack(
        "<8I11I8I5I5I",
        DDS_MAGIC,
        124,                # dwSize
        flags,              # dwFlags
        height,             # dwHeight
        width,              # dwWidth
        pitch_or_linear,    # dwPitchOrLinearSize
        1,                  # dwDepth
        1,                  # dwMipMapCount
        *([0] * 11),        # dwReserved1[11]
        32,                 # ddspf.dwSize
        DDPF_FOURCC,        # ddspf.dwFlags
        DDS_FOURCC_DX10,    # ddspf.dwFourCC
        0, 0, 0, 0, 0,      # RGBBitCount + masks
        DDSCAPS_TEXTURE,    # dwCaps
        0, 0, 0, 0,         # dwCaps2..4 + dwReserved2
        dxgi_format,        # dx10.dxgiFormat
        D3D10_RESOURCE_DIMENSION_TEXTURE2D,
        0,                  # miscFlag
        1,                  # arraySize
        0,                  # miscFlags2
    )
    assert len(hdr) == 148, f"Expected 148B DDS+DX10 header, got {len(hdr)}"
    return hdr + payload


def main() -> int:
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "package", "assets"
    )
    os.makedirs(out_dir, exist_ok=True)

    # 16x16 BC7: 4x4 blocks = 16 blocks * 16 bytes = 256 bytes
    bc7_blocks = bytearray()
    for by in range(4):
        for bx in range(4):
            block = bytes(((bx * 37 + by * 73 + k * 13) & 0xFF) for k in range(16))
            bc7_blocks.extend(block)
    bc7_dds = pack_dds_dx10(16, 16, DXGI_FORMAT_BC7_UNORM, bytes(bc7_blocks))
    with open(os.path.join(out_dir, "arena_albedo_bc7.dds"), "wb") as f:
        f.write(bc7_dds)

    # 16x16 RGBA8: 16*16*4 = 1024 bytes
    rgba_pixels = bytearray()
    for y in range(16):
        for x in range(16):
            rgba_pixels.extend((x * 16, y * 16, ((x ^ y) * 16) & 0xFF, 255))
    rgba_dds = pack_dds_dx10(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, bytes(rgba_pixels))
    with open(os.path.join(out_dir, "arena_ui_rgba8.dds"), "wb") as f:
        f.write(rgba_dds)

    print(f"Generated strict 148B DDS+DX10 assets in {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
