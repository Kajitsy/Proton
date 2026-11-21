# `xg_xs.dll` (`scarlett/xg_xs/`)

Xbox Graphics (`xg_xs.dll`) RDNA2 (`GFX10.3`) texture and surface layout computer.

## Responsibilities

- Computes 64-bit pitch, alignment, mipmap offsets, and swizzle modes (`SW_LINEAR`, `SW_4KB_S`, `SW_4KB_D`, `SW_64KB_S`, `SW_64KB_D`, `SW_64KB_R_X`) for 8..128 bpp formats including resources exceeding 4 GiB.
- Pure PE64 computation library with zero host OS dependencies.
