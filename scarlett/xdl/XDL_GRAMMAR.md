# `scarlett-xdl` Grammar & Codegen Specification (Game OS 2020..2026)

This document is the normative specification for the `.xdl` Interface Definition Language, the `scarlett-xdl` host compiler, generated C++20 headers, `.def` export files, and the Win64 diagnostic auto-stub trampoline runtime.

---

## 1. Lexical Elements

- **Line comments**: `// ...` until newline.
- **Block comments**: `/* ... */` (non-nested).
- **Identifiers**: `[A-Za-z_][A-Za-z0-9_]*`.
- **Integer literals**: decimal (`42`), hexadecimal (`0x585D070E`, `0x10`).
- **String literals**: `"..."` with standard escapes `\"`, `\\`, `\n`.
- **Version literals (`VersionSpec`)**:
  - Dotted 4-part: `Major.Minor.Build.Revision` (each component `0..65535`), e.g. `10.0.19041.0`, `10.0.22621.1806`.
  - Comma 4-part (inside attribute parens): `Major, Minor, Build, Revision`, e.g. `10, 0, 22621, 1806`.
  - Hex GXDK Build/Revision tag: `0x585D070E` — expands to `Major=10, Minor=0, Build=(val >> 16) & 0xFFFF, Revision=val & 0xFFFF` (`0x585D070E` == `10.0.22621.1806`).

### Canonical Game OS 2020..2026 ABI Constants (`scarlett_xdl_runtime.hpp`)

| Constant | Version (`M.m.B.R`) | Packed `gxdk_build_rev()` | Epoch Significance |
| :--- | :--- | :--- | :--- |
| `kGameOs2020` | `10.0.19041.0` | `0x4A610000` | Launch Xbox Series X\|S (2020) |
| `kGameOs2021` | `10.0.20348.0` | `0x4F7C0000` | 2021 GXDK |
| `kGameOs2022` | `10.0.22000.0` | `0x55F00000` | 2022 GXDK |
| `kGameOs2023PreGxdk` | `10.0.22621.1805` | `0x585D070D` | Boundary immediately before `0x585D070E` |
| `kGameOs2023` / `kGameOsGxdk585D070E` | `10.0.22621.1806` | `0x585D070E` | GXDK `>= 0x585D070E` boundary |
| `kGameOs2024` | `10.0.25398.0` | `0x63360000` | 2024 GXDK |
| `kGameOs2025` | `10.0.26100.0` | `0x65F40000` | 2025 GXDK |
| `kGameOs2026` | `10.0.26200.0` | `0x66580000` | 2026 GXDK (latest) |

---

## 2. EBNF Grammar

```ebnf
File         ::= ModuleHeader Declaration* EOF ;

ModuleHeader ::= "module" Ident "{" ModuleProp* "}" ";" ;
ModuleProp   ::= "dll" "=" StringLit ";"
               | "namespace" "=" StringLit ";"
               | "default_abi" "=" VersionSpec ";"
               | "abi_range" "=" IntLit ".." IntLit ";" ;

Declaration  ::= AttrList? ( ConstDecl
                           | TypedefDecl
                           | EnumDecl
                           | BitfieldDecl
                           | StructDecl
                           | InterfaceDecl
                           | ExportDecl ) ;

AttrList     ::= ( "[" Attr ( "," Attr )* "]" )+ ;

Attr         ::= "uuid" "(" StringLit ")"
               | "no_uuid"
               | "force_abi" "(" VersionSpec ")"
               | "added" "(" VersionSpec ")"
               | "removed" "(" VersionSpec ")"
               | "conditional" "(" StringLit ")"
               | "ordinal" "(" IntLit ")"
               | "unimplemented"
               | "slots" "(" IntLit ")"
               | "slot" "(" IntLit ")"
               | "size" "(" IntLit ")"
               | "align" "(" IntLit ")"
               | "offset" "(" IntLit ")"
               | "win64_abi"
               | "register_abi" "(" StringLit ")" ;

ConstDecl    ::= "const" TypeRef Ident "=" IntLit ";" ;

TypedefDecl  ::= "typedef" TypeRef Ident ";" ;

EnumDecl     ::= "enum" Ident ( ":" TypeRef )? "{" EnumMember* "}" ";" ;
EnumMember   ::= AttrList? Ident ( "=" IntLit )? ( "," | ";" )? ;

BitfieldDecl ::= "bitfield" Ident ":" TypeRef "{" BitfieldMember* "}" ";" ;
BitfieldMember ::= AttrList? Ident ":" IntLit ";" ;

StructDecl   ::= "struct" Ident "{" StructField* "}" ";" ;
StructField  ::= AttrList? TypeRef Ident ( "[" IntLit "]" )? ";" ;

InterfaceDecl ::= "interface" Ident ( ":" Ident )? "{" MethodDecl* "}" ";" ;
MethodDecl    ::= AttrList? TypeRef Ident "(" ParamList? ")" ";" ;

ExportDecl   ::= "export" TypeRef Ident "(" ParamList? ")" ";" ;

ParamList    ::= Param ( "," Param )* ;
Param        ::= TypeRef Ident? ;

TypeRef      ::= "const"? BaseType ( "*" "const"? )* ;
BaseType     ::= "void" | "char" | "wchar_t"
               | "u8" | "uint8_t" | "i8" | "int8_t"
               | "u16" | "uint16_t" | "i16" | "int16_t"
               | "u32" | "uint32_t" | "i32" | "int32_t"
               | "u64" | "uint64_t" | "i64" | "int64_t"
               | "f32" | "float" | "f64" | "double"
               | "bool32" | "hresult" | "HRESULT"
               | "guid" | "GUID" | "handle" | "HANDLE" | "gpu_va"
               | Ident ;
```

---

## 3. Attribute Semantics & Validation Rules

1. **`[uuid("XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX")]`**:
   - Validated as canonical 36-character GUID (`8-4-4-4-12` hex).
   - Mutually exclusive with `[no_uuid]`.
   - Emits `inline constexpr scarlett_guid_t kIID_<Interface>` and `static_assert(sizeof(kIID_<Interface>) == 16)`.
2. **`[no_uuid]`**:
   - Flat COM/dispatch interface without a `QueryInterface` GUID (`kHasUuid = false`).
3. **`[force_abi(VersionSpec)]`**:
   - Pins a struct, interface, method, or export to a fixed `scarlett_abi_t` snapshot (`kForcedAbi`).
   - Struct/interface templates with `[force_abi(V)]` always resolve their layout using `V` regardless of the template argument `ABI`.
4. **`[added(VersionSpec)]`**:
   - Symbol / struct field / vtable method / export is active only when `ABI >= VersionSpec`.
   - On a **struct field**: partitions the struct into standard-layout epoch specializations (`struct <Name>_layout<ABI, Epoch>`) so `sizeof` and `offsetof` remain 100% standard-layout with zero dummy padding members.
   - On a **vtable slot**: when `ABI < VersionSpec`, the slot stays at its fixed offset in the vtable (preserving binary layout) and is bound to a fail-loud diagnostic auto-stub thunk.
5. **`[removed(VersionSpec)]`**:
   - Symbol / struct field / vtable method / export is inactive when `ABI >= VersionSpec`.
   - On a **vtable slot**: preserves the exact slot index and fills it with a diagnostic auto-stub thunk flagged with `SCARLETT_STUB_FLAG_REMOVED_IN_ABI`.
6. **`[conditional("expr")]`**:
   - Compile-time predicate over `ABI` (`scarlett_abi_t`). Supported atoms:
     - Console models: `anaconda`, `lockhart`, `devkit`
     - Feature flags: `pix_enabled`, `raytracing`, `mesh_shaders`, `bcpack`
     - Version comparisons: `gxdk >= 0x585D070E`, `gxdk < 0x585D070E`, `abi >= 10.0.22621.1806`, `abi < 10.0.22621.1806`
     - Boolean operators: `!`, `&&`, `||`, `( ... )`
   - Invalid expressions fail compilation with a diagnostic error.
7. **`[ordinal(N)]`**:
   - Allowed on `export` declarations (`N >= 1`, `N <= 65535`). Duplicate ordinals in the same module are rejected.
   - Emits `ExportName @N` in the generated `.def` file and sets `ordinal = N` + `SCARLETT_STUB_FLAG_ORDINAL_EXPORT` in `scarlett_stub_descriptor_t`.
8. **`[unimplemented]`**:
   - Allowed on `export` and `interface` methods.
   - Mandates generation of a `scarlett_stub_descriptor_t` (with `default_error = SCARLETT_ERR_UNIMPLEMENTED_STUB`, never `SCARLETT_OK`) and an individual x86-64 Win64 ABI diagnostic trampoline jumping into `scarlett_stub_trampoline_win64`.
9. **`[slots(N)]` and `[slot(K)]`**:
   - `[slots(N)]` on an interface fixes exact vtable length to `N` slots (`N * 8` bytes). Any unspecified slot indices `0 .. N-1` are automatically populated with individual fail-loud auto-stub thunks (`ReservedSlot_<K>`), guaranteeing **zero NULL slots**.
   - `[slot(K)]` pins a method to 0-based index `K`. Overlapping or out-of-range slots are rejected.
10. **`[size(N)]`, `[align(N)]`, `[offset(N)]`**:
    - Optional explicit layout assertions checked by `scarlett-xdl` at compile time AND emitted as C++20 `static_assert`s.
11. **`[win64_abi]` and `[register_abi("...")]`**:
    - Documents and enforces explicit Microsoft x64 calling convention (`SCARLETT_MS_ABI`: `RCX`, `RDX`, `R8`, `R9`, 32-byte shadow space).

---

## 4. Win64 x86-64 ABI & Type Mapping

All layouts follow Microsoft x64 ABI (identical across PE64 and SysV AMD64 because fixed-width types are used exclusively):

| XDL Type | C++20 Type | Size | Align |
| :--- | :--- | :---: | :---: |
| `u8` / `uint8_t` / `char` | `uint8_t` / `char` | 1 | 1 |
| `i8` / `int8_t` | `int8_t` | 1 | 1 |
| `u16` / `uint16_t` / `wchar_t` | `uint16_t` | 2 | 2 |
| `i16` / `int16_t` | `int16_t` | 2 | 2 |
| `u32` / `uint32_t` / `bool32` | `uint32_t` | 4 | 4 |
| `i32` / `int32_t` | `int32_t` | 4 | 4 |
| `hresult` / `HRESULT` | `scarlett_hresult_t` | 4 | 4 |
| `f32` / `float` | `float` | 4 | 4 |
| `u64` / `uint64_t` / `gpu_va` | `uint64_t` | 8 | 8 |
| `i64` / `int64_t` | `int64_t` | 8 | 8 |
| `f64` / `double` | `double` | 8 | 8 |
| `handle` / `HANDLE` | `void*` | 8 | 8 |
| `T*` (any pointer) | `T*` | 8 | 8 |
| `guid` / `GUID` | `scarlett_guid_t` | 16 | 4 |

---

## 5. Generated C++20 API Naming Conventions

Given a module with `namespace = "scarlett::xdl::<mod>"` and `dll = "<name>.dll"`:

1. **Header file**: `<module>_xdl.hpp`, wrapped in `namespace <ns> { ... }`.
   - Includes `"scarlett_xdl_runtime.hpp"`.
   - Constants: `inline constexpr const char* kDllName = "<name>.dll";`
2. **Bitfields (`bitfield Name : uint64_t`)**:
   - `template<scarlett_abi_t ABI = kDefaultAbi> struct Name { uint64_t raw; ... };`
   - Shift & mask constants: `k<Field>Shift`, `k<Field>Mask`, `k<Field>Bits`.
   - Accessors: `constexpr uint64_t get_<Field>() const noexcept`, `constexpr void set_<Field>(uint64_t v) noexcept`.
   - Builder: `static constexpr Name pack(...) noexcept`.
   - `static_assert(sizeof(Name<...>) == sizeof(underlying))` for canonical Game OS `2020..2026` ABIs.
3. **Structs (`struct Name`)**:
   - `template<scarlett_abi_t ABI = kDefaultAbi> struct Name_layout<ABI, Epoch>` + alias `template<scarlett_abi_t ABI = kDefaultAbi> using Name = Name_layout<ABI, Name_layout_epoch(ABI)>;` (or direct `template<scarlett_abi_t ABI = kDefaultAbi> struct Name` if layout-invariant).
   - Automatic `static_assert(sizeof(Name<V>) == ...)` and `static_assert(offsetof(Name<V>, field) == ...)` emitted for every canonical Game OS `2020..2026` version (including `kGameOs2020`, `kGameOs2023PreGxdk`, `kGameOs2023`, `kGameOs2026`, and DevKit profile where `[conditional]` applies).
4. **Interfaces (`interface IFoo`)**:
   - `inline constexpr bool kIFooHasUuid = true|false;`
   - If `[uuid]`: `inline constexpr scarlett_guid_t kIID_IFoo = { ... };`
   - `inline constexpr uint32_t kIFooSlotCount = N;`
   - `template<scarlett_abi_t ABI = kDefaultAbi> struct IFooVTable` with `SCARLETT_MS_ABI` function pointer member per slot (`slot_0..slot_(N-1)` named after each method or `ReservedSlot_<k>`).
   - `template<scarlett_abi_t ABI = kDefaultAbi> struct IFooStubs` containing:
     - Per-slot `[[gnu::visibility("hidden")]] static inline const scarlett_stub_descriptor_t kDesc_<SlotName>`
     - Per-slot `[[gnu::ms_abi, gnu::naked]] static scarlett_hresult_t thunk_<SlotName>()`
   - `template<scarlett_abi_t ABI = kDefaultAbi> struct IFooProxy`:
     - `const IFooVTable<ABI>* lpVtbl;` (`sizeof == 8`, `offsetof(lpVtbl) == 0`)
     - `static const IFooVTable<ABI>& default_vtable() noexcept` (every slot non-null; unimplemented/removed/reserved slots point to their exact diagnostic auto-stub trampoline)
     - `static bool verify_no_null_slots(const IFooVTable<ABI>& vt) noexcept`
     - Typed forwarding methods invoking `lpVtbl-><Method>(this, ...)`
   - Emits `static_assert(sizeof(IFooVTable<V>) == N * 8)` and `static_assert(offsetof(IFooVTable<V>, <Method>) == slot * 8)` across Game OS `2020..2026`.
5. **Exports (`export Ret Name(Params...)`)**:
   - Typed `SCARLETT_MS_ABI` function pointer alias `using PFN_<Name> = Ret (SCARLETT_MS_ABI *)(Params...);`
   - `struct Export_<Name>` containing:
     - `static constexpr const char* kName = "<Name>";`
     - `static constexpr uint32_t kOrdinal = <N or 0>;`
     - `static constexpr bool kUnimplemented = <true|false>;`
     - `static inline const scarlett_stub_descriptor_t kStubDescriptor = { ... };`
     - `[[gnu::ms_abi, gnu::naked]] static scarlett_hresult_t stub_thunk()`
   - Module export descriptor table `inline constexpr const scarlett_stub_descriptor_t* kExportStubDescriptors[]`.
6. **`.def` file**:
   - Standard Win64 module-definition format:
     ```def
     ; Generated by scarlett-xdl
     LIBRARY "<dll_name>"
     EXPORTS
         XAudio2Create @1
         OtherExport
     ```

---

## 6. `scarlett-xdl` CLI Reference

```
scarlett-xdl <input.xdl> [options]
  --out-dir <dir>              Output directory (default: current directory)
  --emit-header <path>         Output C++20 header path
  --emit-def <path>            Output PE64 .def path
  --target-abi <M.m.B.R|hex>   Target ABI for .def export filtering (default: 10.0.26200.0)
  --console-model <model>      anaconda | lockhart | devkit (default: anaconda)
  --check-only                 Validate schema & layout without emitting files
  --dump-ast                   Print parsed module summary to stdout
  --version                    Print tool version
  --help                       Print usage help
```
