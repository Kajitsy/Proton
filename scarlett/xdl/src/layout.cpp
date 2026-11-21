#include "layout.hpp"

#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>
#include <unordered_set>

namespace scarlett::xdl::compiler {

namespace {

uint64_t align_up(uint64_t value, uint64_t align) noexcept {
    if (align <= 1) return value;
    return (value + align - 1) & ~(align - 1);
}

bool builtin_type_layout(const std::string& name, TypeLayout& out) noexcept {
    if (name == "u8" || name == "uint8_t" || name == "i8" || name == "int8_t" || name == "char") {
        out = {1, 1};
        return true;
    }
    if (name == "u16" || name == "uint16_t" || name == "i16" || name == "int16_t" || name == "wchar_t") {
        out = {2, 2};
        return true;
    }
    if (name == "u32" || name == "uint32_t" || name == "i32" || name == "int32_t" ||
        name == "f32" || name == "float" || name == "bool32" ||
        name == "hresult" || name == "HRESULT") {
        out = {4, 4};
        return true;
    }
    if (name == "u64" || name == "uint64_t" || name == "i64" || name == "int64_t" ||
        name == "f64" || name == "double" || name == "gpu_va" ||
        name == "handle" || name == "HANDLE") {
        out = {8, 8};
        return true;
    }
    if (name == "guid" || name == "GUID") {
        out = {16, 4};
        return true;
    }
    return false;
}

bool is_power_of_two(uint64_t v) noexcept {
    return v != 0 && (v & (v - 1)) == 0;
}

} /* namespace */

bool eval_cond_expr(const CondExpr& expr, VersionSpec ver, uint32_t flags) noexcept {
    switch (expr.kind) {
        case CondExpr::Kind::AtomModel: {
            uint32_t model = flags & kCompFlagModelMask;
            if (expr.atom == "anaconda") return model == kCompFlagAnaconda;
            if (expr.atom == "lockhart") return model == kCompFlagLockhart;
            if (expr.atom == "devkit") return model == kCompFlagDevKit;
            return false;
        }
        case CondExpr::Kind::AtomFlag: {
            if (expr.atom == "pix_enabled") return (flags & kCompFlagPixEnabled) != 0;
            if (expr.atom == "raytracing") return (flags & kCompFlagRaytracing) != 0;
            if (expr.atom == "mesh_shaders") return (flags & kCompFlagMeshShaders) != 0;
            if (expr.atom == "bcpack") return (flags & kCompFlagBcpack) != 0;
            return false;
        }
        case CondExpr::Kind::VersionCmp: {
            uint64_t lhs = expr.is_gxdk_cmp ? static_cast<uint64_t>(ver.gxdk_build_rev())
                                            : ver.pack_u64();
            uint64_t rhs = expr.is_gxdk_cmp ? static_cast<uint64_t>(expr.cmp_gxdk)
                                            : expr.cmp_version.pack_u64();
            switch (expr.cmp_op) {
                case CondExpr::CmpOp::Ge: return lhs >= rhs;
                case CondExpr::CmpOp::Gt: return lhs > rhs;
                case CondExpr::CmpOp::Le: return lhs <= rhs;
                case CondExpr::CmpOp::Lt: return lhs < rhs;
                case CondExpr::CmpOp::Eq: return lhs == rhs;
                case CondExpr::CmpOp::Ne: return lhs != rhs;
            }
            return false;
        }
        case CondExpr::Kind::Not:
            return !eval_cond_expr(*expr.lhs, ver, flags);
        case CondExpr::Kind::And:
            return eval_cond_expr(*expr.lhs, ver, flags) &&
                   eval_cond_expr(*expr.rhs, ver, flags);
        case CondExpr::Kind::Or:
            return eval_cond_expr(*expr.lhs, ver, flags) ||
                   eval_cond_expr(*expr.rhs, ver, flags);
    }
    return false;
}

std::string emit_cond_expr_cpp(const CondExpr& expr, const std::string& abi_var) {
    switch (expr.kind) {
        case CondExpr::Kind::AtomModel:
            if (expr.atom == "anaconda") return abi_var + ".is_anaconda()";
            if (expr.atom == "lockhart") return abi_var + ".is_lockhart()";
            if (expr.atom == "devkit") return abi_var + ".is_devkit()";
            return "false";
        case CondExpr::Kind::AtomFlag:
            if (expr.atom == "pix_enabled") return abi_var + ".has_flag(::scarlett::xdl::kAbiFlagPixEnabled)";
            if (expr.atom == "raytracing") return abi_var + ".has_flag(::scarlett::xdl::kAbiFlagRaytracing)";
            if (expr.atom == "mesh_shaders") return abi_var + ".has_flag(::scarlett::xdl::kAbiFlagMeshShaders)";
            if (expr.atom == "bcpack") return abi_var + ".has_flag(::scarlett::xdl::kAbiFlagBcpack)";
            return "false";
        case CondExpr::Kind::VersionCmp: {
            const char* op_str = ">=";
            switch (expr.cmp_op) {
                case CondExpr::CmpOp::Ge: op_str = ">="; break;
                case CondExpr::CmpOp::Gt: op_str = ">"; break;
                case CondExpr::CmpOp::Le: op_str = "<="; break;
                case CondExpr::CmpOp::Lt: op_str = "<"; break;
                case CondExpr::CmpOp::Eq: op_str = "=="; break;
                case CondExpr::CmpOp::Ne: op_str = "!="; break;
            }
            std::ostringstream oss;
            if (expr.is_gxdk_cmp) {
                oss << "(" << abi_var << ".gxdk_build_rev() " << op_str << " 0x"
                    << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
                    << expr.cmp_gxdk << "u)";
            } else {
                oss << "(" << abi_var << ".pack_u64() " << op_str << " ::scarlett::xdl::make_abi("
                    << expr.cmp_version.major << "u, " << expr.cmp_version.minor << "u, "
                    << expr.cmp_version.build << "u, " << expr.cmp_version.revision
                    << "u).pack_u64())";
            }
            return oss.str();
        }
        case CondExpr::Kind::Not:
            return "(!" + emit_cond_expr_cpp(*expr.lhs, abi_var) + ")";
        case CondExpr::Kind::And:
            return "(" + emit_cond_expr_cpp(*expr.lhs, abi_var) + " && " +
                   emit_cond_expr_cpp(*expr.rhs, abi_var) + ")";
        case CondExpr::Kind::Or:
            return "(" + emit_cond_expr_cpp(*expr.lhs, abi_var) + " || " +
                   emit_cond_expr_cpp(*expr.rhs, abi_var) + ")";
    }
    return "false";
}

bool is_active_in_abi(const AttributeSet& attrs, VersionSpec ver, uint32_t flags) noexcept {
    if (attrs.added.has_value() && ver < *attrs.added) {
        return false;
    }
    if (attrs.removed.has_value() && ver >= *attrs.removed) {
        return false;
    }
    if (attrs.conditional_expr && !eval_cond_expr(*attrs.conditional_expr, ver, flags)) {
        return false;
    }
    return true;
}

SemanticContext::SemanticContext(Module& mod) : mod_(mod) {}

bool SemanticContext::is_enum(const std::string& name) const {
    return enum_map_.find(name) != enum_map_.end();
}

bool SemanticContext::is_bitfield(const std::string& name) const {
    return bitfield_map_.find(name) != bitfield_map_.end();
}

bool SemanticContext::is_struct(const std::string& name) const {
    return struct_map_.find(name) != struct_map_.end();
}

bool SemanticContext::is_interface(const std::string& name) const {
    return interface_map_.find(name) != interface_map_.end();
}

bool SemanticContext::is_typedef(const std::string& name) const {
    return typedef_map_.find(name) != typedef_map_.end();
}

const ResolvedInterface* SemanticContext::find_resolved_interface(const std::string& name) const {
    auto it = resolved_interfaces_.find(name);
    if (it == resolved_interfaces_.end()) return nullptr;
    return &it->second;
}

bool SemanticContext::compute_type_layout(const TypeRef& type, VersionSpec ver, uint32_t flags,
                                          TypeLayout& out, std::string& err) const {
    if (type.pointer_depth > 0) {
        out = {8, 8};
        return true;
    }
    if (builtin_type_layout(type.base_name, out)) {
        return true;
    }
    auto td_it = typedef_map_.find(type.base_name);
    if (td_it != typedef_map_.end()) {
        return compute_type_layout(mod_.typedefs[td_it->second].target, ver, flags, out, err);
    }
    auto en_it = enum_map_.find(type.base_name);
    if (en_it != enum_map_.end()) {
        return compute_type_layout(mod_.enums[en_it->second].underlying, ver, flags, out, err);
    }
    auto bf_it = bitfield_map_.find(type.base_name);
    if (bf_it != bitfield_map_.end()) {
        return compute_type_layout(mod_.bitfields[bf_it->second].underlying, ver, flags, out, err);
    }
    auto st_it = struct_map_.find(type.base_name);
    if (st_it != struct_map_.end()) {
        ComputedStructLayout csl{};
        if (!compute_struct_layout(mod_.structs[st_it->second], ver, flags, csl, err)) {
            return false;
        }
        out = {csl.size, csl.align};
        return true;
    }
    if (interface_map_.find(type.base_name) != interface_map_.end()) {
        out = {8, 8};
        return true;
    }

    err = "unknown type '" + type.base_name + "'";
    return false;
}

bool SemanticContext::compute_struct_layout(const StructDecl& st, VersionSpec ver, uint32_t flags,
                                            ComputedStructLayout& out, std::string& err) const {
    out = ComputedStructLayout{};
    const VersionSpec eff_ver = st.attrs.force_abi.value_or(ver);
    uint64_t offset = 0;
    uint64_t max_align = 1;
    if (st.attrs.explicit_align.has_value()) {
        max_align = std::max(max_align, *st.attrs.explicit_align);
    }

    for (size_t i = 0; i < st.fields.size(); ++i) {
        const StructField& f = st.fields[i];
        if (!is_active_in_abi(f.attrs, eff_ver, flags)) {
            continue;
        }
        TypeLayout fl{};
        if (!compute_type_layout(f.type, eff_ver, flags, fl, err)) {
            return false;
        }
        uint64_t field_align = fl.align;
        if (f.attrs.explicit_align.has_value()) {
            field_align = std::max(field_align, *f.attrs.explicit_align);
        }
        offset = align_up(offset, field_align);
        uint64_t total_size = fl.size * (f.array_len > 0 ? f.array_len : 1);
        out.fields.push_back(ComputedFieldLayout{i, f.name, offset, total_size, field_align});
        offset += total_size;
        max_align = std::max(max_align, field_align);
    }

    out.align = max_align;
    out.size = (offset == 0) ? 1 : align_up(offset, max_align);
    return true;
}

bool SemanticContext::validate(std::vector<Diagnostic>& diags) {
    const size_t start_diags = diags.size();
    auto err = [&](SourceLoc loc, const std::string& msg) {
        diags.push_back(Diagnostic{mod_.source_path, loc, msg});
    };

    if (mod_.abi_year_min < 2020 || mod_.abi_year_max > 2026 ||
        mod_.abi_year_min > mod_.abi_year_max) {
        err(SourceLoc{1, 1}, "module abi_range must lie within 2020..2026");
    }

    /* Register symbol tables & reject duplicate type names */
    std::unordered_set<std::string> all_types;
    auto reg_type = [&](const std::string& name, SourceLoc loc) {
        if (!all_types.insert(name).second) {
            err(loc, "duplicate type declaration '" + name + "'");
        }
    };

    for (size_t i = 0; i < mod_.typedefs.size(); ++i) {
        reg_type(mod_.typedefs[i].alias, mod_.typedefs[i].loc);
        typedef_map_[mod_.typedefs[i].alias] = i;
    }
    for (size_t i = 0; i < mod_.enums.size(); ++i) {
        reg_type(mod_.enums[i].name, mod_.enums[i].loc);
        enum_map_[mod_.enums[i].name] = i;
    }
    for (size_t i = 0; i < mod_.bitfields.size(); ++i) {
        reg_type(mod_.bitfields[i].name, mod_.bitfields[i].loc);
        bitfield_map_[mod_.bitfields[i].name] = i;
    }
    for (size_t i = 0; i < mod_.structs.size(); ++i) {
        reg_type(mod_.structs[i].name, mod_.structs[i].loc);
        struct_map_[mod_.structs[i].name] = i;
    }
    for (size_t i = 0; i < mod_.interfaces.size(); ++i) {
        reg_type(mod_.interfaces[i].name, mod_.interfaces[i].loc);
        interface_map_[mod_.interfaces[i].name] = i;
    }

    /* Validate bitfields */
    for (auto& bf : mod_.bitfields) {
        if (bf.attrs.uuid_str.has_value() || bf.attrs.no_uuid) {
            err(bf.loc, "[uuid]/[no_uuid] is only valid on interface declarations");
        }
        TypeLayout ul{};
        std::string terr;
        if (!compute_type_layout(bf.underlying, mod_.default_abi, kCompFlagAnaconda, ul, terr) ||
            (ul.size != 4 && ul.size != 8)) {
            err(bf.loc, "bitfield '" + bf.name + "' underlying type must be 32-bit or 64-bit integer");
            continue;
        }
        const uint32_t expected_bits = static_cast<uint32_t>(ul.size * 8);
        uint32_t bit_cursor = 0;
        std::unordered_set<std::string> field_names;
        for (auto& m : bf.members) {
            if (!field_names.insert(m.name).second) {
                err(m.loc, "duplicate bitfield member '" + m.name + "' in '" + bf.name + "'");
            }
            if (m.bit_width == 0 || m.bit_width > expected_bits) {
                err(m.loc, "invalid bit width " + std::to_string(m.bit_width) + " for '" + m.name + "'");
            }
            m.bit_offset = bit_cursor;
            bit_cursor += m.bit_width;
        }
        bf.total_bits = bit_cursor;
        if (bit_cursor != expected_bits) {
            err(bf.loc, "bitfield '" + bf.name + "' total bit width is " +
                        std::to_string(bit_cursor) + ", expected exactly " +
                        std::to_string(expected_bits));
        }
    }

    /* Validate structs and explicit [size]/[align]/[offset] assertions */
    for (const auto& st : mod_.structs) {
        if (st.attrs.uuid_str.has_value() || st.attrs.no_uuid) {
            err(st.loc, "[uuid]/[no_uuid] is only valid on interface declarations");
        }
        if (st.attrs.ordinal.has_value()) {
            err(st.loc, "[ordinal] is only valid on export declarations");
        }
        if (st.attrs.explicit_align.has_value() && !is_power_of_two(*st.attrs.explicit_align)) {
            err(st.loc, "[align] on struct '" + st.name + "' must be a power of two");
        }

        std::unordered_set<std::string> seen_fields;
        for (const auto& f : st.fields) {
            if (!seen_fields.insert(f.name).second) {
                err(f.loc, "duplicate field '" + f.name + "' in struct '" + st.name + "'");
            }
            if (f.attrs.added.has_value() && f.attrs.removed.has_value() &&
                *f.attrs.removed <= *f.attrs.added) {
                err(f.loc, "field '" + f.name + "' has [removed] <= [added]");
            }
            TypeLayout fl{};
            std::string terr;
            if (!compute_type_layout(f.type, mod_.default_abi, kCompFlagAnaconda, fl, terr)) {
                err(f.loc, "in struct '" + st.name + "' field '" + f.name + "': " + terr);
            }
        }

        ComputedStructLayout csl{};
        std::string lerr;
        if (compute_struct_layout(st, mod_.default_abi, kCompFlagAnaconda, csl, lerr)) {
            if (st.attrs.explicit_size.has_value() && csl.size != *st.attrs.explicit_size) {
                err(st.loc, "struct '" + st.name + "' computed sizeof is " +
                            std::to_string(csl.size) + ", expected [size(" +
                            std::to_string(*st.attrs.explicit_size) + ")]");
            }
            for (const auto& cf : csl.fields) {
                const StructField& f = st.fields[cf.field_index];
                if (f.attrs.explicit_offset.has_value() &&
                    cf.offset != *f.attrs.explicit_offset) {
                    err(f.loc, "field '" + st.name + "::" + f.name + "' computed offset is " +
                               std::to_string(cf.offset) + ", expected [offset(" +
                               std::to_string(*f.attrs.explicit_offset) + ")]");
                }
            }
        } else {
            err(st.loc, "failed to compute layout for struct '" + st.name + "': " + lerr);
        }
    }

    /* Validate interfaces & resolve vtable slots (including inheritance) */
    for (auto& iface : mod_.interfaces) {
        if (iface.attrs.uuid_str.has_value() && iface.attrs.no_uuid) {
            err(iface.loc, "interface '" + iface.name + "' cannot specify both [uuid] and [no_uuid]");
        }
        if (iface.attrs.ordinal.has_value()) {
            err(iface.loc, "[ordinal] is only valid on export declarations");
        }

        ResolvedInterface ri{};
        ri.decl = &iface;

        std::unordered_map<uint32_t, ResolvedSlot> slot_map;
        uint32_t next_slot = 0;

        if (iface.base_interface.has_value()) {
            auto base_it = resolved_interfaces_.find(*iface.base_interface);
            if (base_it == resolved_interfaces_.end()) {
                err(iface.loc, "interface '" + iface.name + "' inherits from unknown or later-declared interface '" +
                               *iface.base_interface + "'");
            } else {
                for (const auto& bs : base_it->second.slots) {
                    slot_map[bs.slot_index] = bs;
                    next_slot = std::max(next_slot, bs.slot_index + 1);
                }
            }
        }

        std::unordered_set<std::string> method_names;
        for (auto& m : iface.methods) {
            if (!method_names.insert(m.name).second) {
                err(m.loc, "duplicate method '" + m.name + "' in interface '" + iface.name + "'");
            }
            if (m.attrs.added.has_value() && m.attrs.removed.has_value() &&
                *m.attrs.removed <= *m.attrs.added) {
                err(m.loc, "method '" + m.name + "' has [removed] <= [added]");
            }
            uint32_t assigned = m.attrs.slot.value_or(next_slot);
            if (slot_map.find(assigned) != slot_map.end()) {
                err(m.loc, "vtable slot " + std::to_string(assigned) + " conflict on method '" +
                           iface.name + "::" + m.name + "'");
                continue;
            }
            if (iface.attrs.slots.has_value() && assigned >= *iface.attrs.slots) {
                err(m.loc, "vtable slot " + std::to_string(assigned) + " on '" + iface.name + "::" +
                           m.name + "' exceeds [slots(" + std::to_string(*iface.attrs.slots) + ")]");
                continue;
            }
            m.resolved_slot = assigned;
            slot_map[assigned] = ResolvedSlot{assigned, false, iface.name, &m};
            next_slot = assigned + 1;
        }

        uint32_t max_slot_count = next_slot;
        for (const auto& [idx, _] : slot_map) {
            max_slot_count = std::max(max_slot_count, idx + 1);
        }
        if (iface.attrs.slots.has_value()) {
            if (*iface.attrs.slots < max_slot_count) {
                err(iface.loc, "interface '" + iface.name + "' has more slots than [slots(" +
                               std::to_string(*iface.attrs.slots) + ")]");
            } else {
                max_slot_count = *iface.attrs.slots;
            }
        }

        ri.total_slots = max_slot_count;
        ri.slots.reserve(max_slot_count);
        for (uint32_t s = 0; s < max_slot_count; ++s) {
            auto it = slot_map.find(s);
            if (it != slot_map.end()) {
                ri.slots.push_back(it->second);
            } else {
                ri.slots.push_back(ResolvedSlot{s, true, iface.name, nullptr});
            }
        }
        resolved_interfaces_[iface.name] = std::move(ri);
    }

    /* Validate exports & ordinals */
    std::unordered_set<std::string> export_names;
    std::unordered_map<uint32_t, std::string> ordinal_owners;
    for (const auto& ex : mod_.exports) {
        if (ex.attrs.uuid_str.has_value() || ex.attrs.no_uuid) {
            err(ex.loc, "[uuid]/[no_uuid] is only valid on interface declarations");
        }
        if (!export_names.insert(ex.name).second) {
            err(ex.loc, "duplicate export '" + ex.name + "'");
        }
        if (ex.attrs.ordinal.has_value()) {
            auto it = ordinal_owners.find(*ex.attrs.ordinal);
            if (it != ordinal_owners.end()) {
                err(ex.loc, "duplicate [ordinal(" + std::to_string(*ex.attrs.ordinal) +
                            ")] on export '" + ex.name + "' (already assigned to '" +
                            it->second + "')");
            } else {
                ordinal_owners[*ex.attrs.ordinal] = ex.name;
            }
        }
        if (ex.attrs.added.has_value() && ex.attrs.removed.has_value() &&
            *ex.attrs.removed <= *ex.attrs.added) {
            err(ex.loc, "export '" + ex.name + "' has [removed] <= [added]");
        }
    }

    return diags.size() == start_diags;
}

} /* namespace scarlett::xdl::compiler */
