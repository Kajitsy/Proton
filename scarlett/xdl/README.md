# `scarlett-xdl` (`scarlett/xdl/`)

Host-side IDL compiler and code generator for Xbox Series X|S Game OS (`2020..2026`, `abi_t`) interfaces.

## Responsibilities

- Parses `.xdl` definitions and attributes (`[uuid]`, `[no_uuid]`, `[force_abi]`, `[added]`, `[removed]`, `[conditional]`, `[ordinal]`, `[unimplemented]`).
- Emits C++20 and C11 headers, 32-slot vtable dispatchers (`{073B7DCB-1FCF-4030-94BE-E3C9EB623428}` and `{37E56907-2F10-41E8-B72F-36EDB185331A}`), Win64 register ABI `QueryApiImpl` (`RCX=api_id`, `RDX=riid`, `R8=out_impl`), `.def` export definitions, and fail-loud auto-stub thunks.
