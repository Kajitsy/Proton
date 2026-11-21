#ifndef SCARLETT_XDL_AST_HPP
#define SCARLETT_XDL_AST_HPP

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace scarlett::xdl::compiler {

struct SourceLoc {
    uint32_t line = 1;
    uint32_t col = 1;
};

struct VersionSpec {
    uint16_t major = 10;
    uint16_t minor = 0;
    uint16_t build = 19041;
    uint16_t revision = 0;

    [[nodiscard]] constexpr uint64_t pack_u64() const noexcept {
        return (static_cast<uint64_t>(major) << 48) |
               (static_cast<uint64_t>(minor) << 32) |
               (static_cast<uint64_t>(build) << 16) |
               static_cast<uint64_t>(revision);
    }

    [[nodiscard]] constexpr uint32_t gxdk_build_rev() const noexcept {
        return (static_cast<uint32_t>(build) << 16) |
               static_cast<uint32_t>(revision);
    }

    [[nodiscard]] constexpr bool operator==(const VersionSpec& o) const noexcept {
        return pack_u64() == o.pack_u64();
    }
    [[nodiscard]] constexpr bool operator<(const VersionSpec& o) const noexcept {
        return pack_u64() < o.pack_u64();
    }
    [[nodiscard]] constexpr bool operator<=(const VersionSpec& o) const noexcept {
        return pack_u64() <= o.pack_u64();
    }
    [[nodiscard]] constexpr bool operator>=(const VersionSpec& o) const noexcept {
        return pack_u64() >= o.pack_u64();
    }

    [[nodiscard]] std::string to_string() const {
        return std::to_string(major) + "." + std::to_string(minor) + "." +
               std::to_string(build) + "." + std::to_string(revision);
    }
};

struct GuidValue {
    uint32_t data1 = 0;
    uint16_t data2 = 0;
    uint16_t data3 = 0;
    std::array<uint8_t, 8> data4{};
    std::string canonical;
};

/* Conditional expression AST for [conditional("...")] */
struct CondExpr {
    enum class Kind {
        AtomModel,     /* anaconda, lockhart, devkit */
        AtomFlag,      /* pix_enabled, raytracing, mesh_shaders, bcpack */
        VersionCmp,    /* abi >= V, abi < V, gxdk >= H, gxdk < H, etc. */
        Not,
        And,
        Or,
    };

    enum class CmpOp {
        Ge,
        Gt,
        Le,
        Lt,
        Eq,
        Ne,
    };

    Kind kind = Kind::AtomModel;
    std::string atom;
    bool is_gxdk_cmp = false;
    CmpOp cmp_op = CmpOp::Ge;
    VersionSpec cmp_version{};
    uint32_t cmp_gxdk = 0;
    std::shared_ptr<CondExpr> lhs;
    std::shared_ptr<CondExpr> rhs;
};

struct AttributeSet {
    std::optional<std::string> uuid_str;
    std::optional<GuidValue> parsed_uuid;
    bool no_uuid = false;
    std::optional<VersionSpec> force_abi;
    std::optional<VersionSpec> added;
    std::optional<VersionSpec> removed;
    std::optional<std::string> conditional_str;
    std::shared_ptr<CondExpr> conditional_expr;
    std::optional<uint32_t> ordinal;
    bool unimplemented = false;
    std::optional<uint32_t> slots;
    std::optional<uint32_t> slot;
    std::optional<uint64_t> explicit_size;
    std::optional<uint64_t> explicit_align;
    std::optional<uint64_t> explicit_offset;
    bool win64_abi = false;
    std::optional<std::string> register_abi;
    SourceLoc loc{};
};

struct TypeRef {
    bool is_const = false;
    std::string base_name;
    int pointer_depth = 0;
    std::vector<bool> pointer_const; /* constness of each pointer level */
    SourceLoc loc{};
};

struct ConstDecl {
    AttributeSet attrs;
    TypeRef type;
    std::string name;
    uint64_t value = 0;
    bool is_hex = false;
    SourceLoc loc{};
};

struct TypedefDecl {
    AttributeSet attrs;
    TypeRef target;
    std::string alias;
    SourceLoc loc{};
};

struct EnumMember {
    AttributeSet attrs;
    std::string name;
    int64_t value = 0;
    bool has_explicit_value = false;
    SourceLoc loc{};
};

struct EnumDecl {
    AttributeSet attrs;
    std::string name;
    TypeRef underlying;
    std::vector<EnumMember> members;
    SourceLoc loc{};
};

struct BitfieldMember {
    AttributeSet attrs;
    std::string name;
    uint32_t bit_width = 0;
    uint32_t bit_offset = 0; /* computed by validator */
    SourceLoc loc{};
};

struct BitfieldDecl {
    AttributeSet attrs;
    std::string name;
    TypeRef underlying;
    std::vector<BitfieldMember> members;
    uint32_t total_bits = 0;
    SourceLoc loc{};
};

struct StructField {
    AttributeSet attrs;
    TypeRef type;
    std::string name;
    uint64_t array_len = 0; /* 0 = scalar, >0 = fixed array [N] */
    SourceLoc loc{};
};

struct StructDecl {
    AttributeSet attrs;
    std::string name;
    std::vector<StructField> fields;
    SourceLoc loc{};
};

struct ParamDecl {
    TypeRef type;
    std::string name;
    SourceLoc loc{};
};

struct MethodDecl {
    AttributeSet attrs;
    TypeRef return_type;
    std::string name;
    std::vector<ParamDecl> params;
    uint32_t resolved_slot = 0;
    SourceLoc loc{};
};

struct InterfaceDecl {
    AttributeSet attrs;
    std::string name;
    std::optional<std::string> base_interface;
    std::vector<MethodDecl> methods;
    SourceLoc loc{};
};

struct ExportDecl {
    AttributeSet attrs;
    TypeRef return_type;
    std::string name;
    std::vector<ParamDecl> params;
    SourceLoc loc{};
};

enum class DeclKind {
    Const,
    Typedef,
    Enum,
    Bitfield,
    Struct,
    Interface,
    Export,
};

struct DeclItem {
    DeclKind kind;
    size_t index = 0; /* index into corresponding vector in Module */
};

struct Module {
    std::string source_path;
    std::string name;
    std::string dll_name;
    std::string cpp_namespace;
    VersionSpec default_abi{10, 0, 26200, 0};
    uint32_t abi_year_min = 2020;
    uint32_t abi_year_max = 2026;

    std::vector<ConstDecl> consts;
    std::vector<TypedefDecl> typedefs;
    std::vector<EnumDecl> enums;
    std::vector<BitfieldDecl> bitfields;
    std::vector<StructDecl> structs;
    std::vector<InterfaceDecl> interfaces;
    std::vector<ExportDecl> exports;
    std::vector<DeclItem> decl_order;
};

struct Diagnostic {
    std::string file;
    SourceLoc loc;
    std::string message;

    [[nodiscard]] std::string format() const {
        return file + ":" + std::to_string(loc.line) + ":" +
               std::to_string(loc.col) + ": error: " + message;
    }
};

} /* namespace scarlett::xdl::compiler */

#endif /* SCARLETT_XDL_AST_HPP */
