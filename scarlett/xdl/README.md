# `scarlett-xdl` (`scarlett/xdl/`)

Host-side C++20 IDL compiler, code generator, and Win64 diagnostic auto-stub trampoline runtime for Xbox Series X|S Game OS (`2020..2026`, `scarlett_abi_t`, GXDK `>= 0x585D070E`).

## Contents

- `XDL_GRAMMAR.md` — Normative EBNF grammar, attribute semantics, ABI layout table, and CLI reference.
- `include/scarlett_xdl_runtime.hpp` — `scarlett_abi_t` C++20 NTTP, Game OS `2020..2026` milestones (`kGameOs2020`..`kGameOs2026`, `kGameOs2023PreGxdk` `0x585D070D`, `kGameOs2023` `0x585D070E`), `SCARLETT_MS_ABI`, `scarlett_xdl_query_api_impl`, and auto-stub runtime hooks.
- `src/` — `scarlett-xdl` lexer, recursive-descent parser (all 8 attributes: `[uuid]`, `[no_uuid]`, `[force_abi]`, `[added]`, `[removed]`, `[conditional]`, `[ordinal]`, `[unimplemented]`), Win64 layout engine & semantic validator, C++20 header generator (`template<scarlett_abi_t ABI>`), PE64 `.def` generator, and x86-64 Win64 ABI diagnostic auto-stub trampoline (`stub_trampoline_x86_64.S` + `stub_thunk_runtime.cpp`).
- `schemas/` — Starter `.xdl` schemas for `d3d12_xs`, `dstorage_xs`, `xg_xs`, `xgameruntime`, `xaudio2_9_xs`, and `gameinput_xs`.
- `tests/` — Comprehensive CTest suite verifying CLI/parser/validator/negative diagnostics, compilation & `static_assert(sizeof/offsetof)` of all 6 generated headers across Game OS `2020..2026` + `0x585D070E`, Win64 register ABI `QueryApiImpl` + 32-slot vtables, and diagnostic auto-stub thunks under `PROTON_XBOX_STRICT=0` and `PROTON_XBOX_STRICT=1`.
