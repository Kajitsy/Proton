#ifndef SCARLETT_XDL_LAYOUT_HPP
#define SCARLETT_XDL_LAYOUT_HPP

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast.hpp"

namespace scarlett::xdl::compiler {

/* Flag bits matching scarlett_xdl_runtime.hpp */
inline constexpr uint32_t kCompFlagModelMask   = 0x00000003u;
inline constexpr uint32_t kCompFlagAnaconda    = 0x00000000u;
inline constexpr uint32_t kCompFlagLockhart    = 0x00000001u;
inline constexpr uint32_t kCompFlagDevKit      = 0x00000002u;
inline constexpr uint32_t kCompFlagPixEnabled  = (1u << 3);
inline constexpr uint32_t kCompFlagRaytracing  = (1u << 4);
inline constexpr uint32_t kCompFlagMeshShaders = (1u << 5);
inline constexpr uint32_t kCompFlagBcpack      = (1u << 6);

struct TypeLayout {
    uint64_t size = 0;
    uint64_t align = 1;
};

struct ComputedFieldLayout {
    size_t field_index = 0;
    std::string name;
    uint64_t offset = 0;
    uint64_t size = 0;
    uint64_t align = 1;
};

struct ComputedStructLayout {
    uint64_t size = 0;
    uint64_t align = 1;
    std::vector<ComputedFieldLayout> fields;
};

struct ResolvedSlot {
    uint32_t slot_index = 0;
    bool is_reserved_padding = false;
    std::string owner_interface;
    const MethodDecl* method = nullptr; /* nullptr if reserved padding */
};

struct ResolvedInterface {
    const InterfaceDecl* decl = nullptr;
    uint32_t total_slots = 0;
    std::vector<ResolvedSlot> slots;
};

bool eval_cond_expr(const CondExpr& expr, VersionSpec ver, uint32_t flags) noexcept;
std::string emit_cond_expr_cpp(const CondExpr& expr, const std::string& abi_var);
bool is_active_in_abi(const AttributeSet& attrs, VersionSpec ver, uint32_t flags) noexcept;

class SemanticContext {
public:
    explicit SemanticContext(Module& mod);

    bool validate(std::vector<Diagnostic>& diags);

    bool compute_type_layout(const TypeRef& type, VersionSpec ver, uint32_t flags,
                             TypeLayout& out, std::string& err) const;

    bool compute_struct_layout(const StructDecl& st, VersionSpec ver, uint32_t flags,
                               ComputedStructLayout& out, std::string& err) const;

    const ResolvedInterface* find_resolved_interface(const std::string& name) const;
    bool is_enum(const std::string& name) const;
    bool is_bitfield(const std::string& name) const;
    bool is_struct(const std::string& name) const;
    bool is_interface(const std::string& name) const;
    bool is_typedef(const std::string& name) const;

private:
    Module& mod_;
    std::unordered_map<std::string, size_t> typedef_map_;
    std::unordered_map<std::string, size_t> enum_map_;
    std::unordered_map<std::string, size_t> bitfield_map_;
    std::unordered_map<std::string, size_t> struct_map_;
    std::unordered_map<std::string, size_t> interface_map_;
    std::unordered_map<std::string, ResolvedInterface> resolved_interfaces_;
};

} /* namespace scarlett::xdl::compiler */

#endif /* SCARLETT_XDL_LAYOUT_HPP */
