#include "codegen_cpp.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace scarlett::xdl::compiler {

namespace {

struct CanonicalAbiPoint {
    const char* cpp_expr;
    VersionSpec ver;
    uint32_t flags;
};

const std::array<CanonicalAbiPoint, 10>& canonical_abi_points() {
    static const std::array<CanonicalAbiPoint, 10> kPoints = {{
        {"::scarlett::xdl::kGameOs2020",
         VersionSpec{10, 0, 19041, 0},
         kCompFlagAnaconda | kCompFlagRaytracing | kCompFlagMeshShaders},
        {"::scarlett::xdl::kGameOs2021",
         VersionSpec{10, 0, 20348, 0},
         kCompFlagAnaconda | kCompFlagRaytracing | kCompFlagMeshShaders},
        {"::scarlett::xdl::kGameOs2022",
         VersionSpec{10, 0, 22000, 0},
         kCompFlagAnaconda | kCompFlagRaytracing | kCompFlagMeshShaders},
        {"::scarlett::xdl::kGameOs2023PreGxdk",
         VersionSpec{10, 0, 22621, 1805},
         kCompFlagAnaconda | kCompFlagRaytracing | kCompFlagMeshShaders},
        {"::scarlett::xdl::kGameOs2023",
         VersionSpec{10, 0, 22621, 1806},
         kCompFlagAnaconda | kCompFlagRaytracing | kCompFlagMeshShaders},
        {"::scarlett::xdl::kGameOs2024",
         VersionSpec{10, 0, 25398, 0},
         kCompFlagAnaconda | kCompFlagRaytracing | kCompFlagMeshShaders},
        {"::scarlett::xdl::kGameOs2025",
         VersionSpec{10, 0, 26100, 0},
         kCompFlagAnaconda | kCompFlagRaytracing | kCompFlagMeshShaders},
        {"::scarlett::xdl::kGameOs2026",
         VersionSpec{10, 0, 26200, 0},
         kCompFlagAnaconda | kCompFlagRaytracing | kCompFlagMeshShaders},
        {"::scarlett::xdl::kGameOs2023.as_model(::scarlett::xdl::kAbiFlagDevKit)",
         VersionSpec{10, 0, 22621, 1806},
         kCompFlagDevKit | kCompFlagRaytracing | kCompFlagMeshShaders},
        {"::scarlett::xdl::kGameOs2026.as_model(::scarlett::xdl::kAbiFlagLockhart)",
         VersionSpec{10, 0, 26200, 0},
         kCompFlagLockhart | kCompFlagRaytracing | kCompFlagMeshShaders},
    }};
    return kPoints;
}

std::string map_base_type_cpp(const std::string& name, const SemanticContext& ctx,
                              const std::string& abi_arg) {
    if (name == "void") return "void";
    if (name == "char") return "char";
    if (name == "wchar_t") return "uint16_t";
    if (name == "u8" || name == "uint8_t") return "uint8_t";
    if (name == "i8" || name == "int8_t") return "int8_t";
    if (name == "u16" || name == "uint16_t") return "uint16_t";
    if (name == "i16" || name == "int16_t") return "int16_t";
    if (name == "u32" || name == "uint32_t" || name == "bool32") return "uint32_t";
    if (name == "i32" || name == "int32_t") return "int32_t";
    if (name == "u64" || name == "uint64_t" || name == "gpu_va") return "uint64_t";
    if (name == "i64" || name == "int64_t") return "int64_t";
    if (name == "f32" || name == "float") return "float";
    if (name == "f64" || name == "double") return "double";
    if (name == "hresult" || name == "HRESULT") return "scarlett_hresult_t";
    if (name == "guid" || name == "GUID") return "scarlett_guid_t";
    if (name == "handle" || name == "HANDLE") return "void*";

    if (ctx.is_struct(name) || ctx.is_bitfield(name)) {
        return name + "<" + abi_arg + ">";
    }
    if (ctx.is_interface(name)) {
        return name + "Proxy<" + abi_arg + ">";
    }
    return name;
}

std::string format_type_cpp(const TypeRef& t, const SemanticContext& ctx,
                            const std::string& abi_arg) {
    std::string out;
    if (t.is_const) {
        out += "const ";
    }
    out += map_base_type_cpp(t.base_name, ctx, abi_arg);
    for (int i = 0; i < t.pointer_depth; ++i) {
        out += "*";
        if (i < static_cast<int>(t.pointer_const.size()) && t.pointer_const[i]) {
            out += " const";
        }
    }
    return out;
}

std::string format_version_make_abi(VersionSpec v) {
    std::ostringstream oss;
    oss << "::scarlett::xdl::make_abi(" << v.major << "u, " << v.minor << "u, "
        << v.build << "u, " << v.revision << "u)";
    return oss.str();
}

std::string format_hex_u64(uint64_t val, int width = 0) {
    std::ostringstream oss;
    oss << "UINT64_C(0x" << std::hex << std::uppercase;
    if (width > 0) {
        oss << std::setw(width) << std::setfill('0');
    }
    oss << val << ")";
    return oss.str();
}

std::string format_hex_u32(uint32_t val, int width = 8) {
    std::ostringstream oss;
    oss << "UINT32_C(0x" << std::hex << std::uppercase
        << std::setw(width) << std::setfill('0') << val << ")";
    return oss.str();
}

std::string format_guid_initializer(const GuidValue& g) {
    std::ostringstream oss;
    oss << "{ " << format_hex_u32(g.data1, 8) << ", UINT16_C(0x"
        << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << g.data2
        << "), UINT16_C(0x" << std::setw(4) << std::setfill('0') << g.data3 << "), { ";
    for (size_t i = 0; i < 8; ++i) {
        if (i > 0) oss << ", ";
        oss << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<unsigned>(g.data4[i]) << "u";
    }
    oss << " } }";
    return oss.str();
}

std::string build_attr_active_expr_cpp(const AttributeSet& attrs, const std::string& abi_var) {
    std::vector<std::string> clauses;
    if (attrs.added.has_value()) {
        std::ostringstream c;
        c << abi_var << ".version_ge(" << attrs.added->major << "u, "
          << attrs.added->minor << "u, " << attrs.added->build << "u, "
          << attrs.added->revision << "u)";
        clauses.push_back(c.str());
    }
    if (attrs.removed.has_value()) {
        std::ostringstream c;
        c << abi_var << ".version_lt(" << attrs.removed->major << "u, "
          << attrs.removed->minor << "u, " << attrs.removed->build << "u, "
          << attrs.removed->revision << "u)";
        clauses.push_back(c.str());
    }
    if (attrs.conditional_expr) {
        clauses.push_back(emit_cond_expr_cpp(*attrs.conditional_expr, abi_var));
    }
    if (clauses.empty()) {
        return "true";
    }
    std::string combined = "(";
    for (size_t i = 0; i < clauses.size(); ++i) {
        if (i > 0) combined += " && ";
        combined += clauses[i];
    }
    combined += ")";
    return combined;
}

void emit_bitfield(std::ostringstream& out, const BitfieldDecl& bf, const SemanticContext& ctx) {
    const std::string utype = format_type_cpp(bf.underlying, ctx, "ABI");
    const bool is64 = (bf.total_bits == 64);

    out << "template<scarlett_abi_t ABI = kDefaultAbi>\n";
    out << "struct " << bf.name << " {\n";
    out << "    " << utype << " raw = 0;\n\n";

    for (const auto& m : bf.members) {
        uint64_t mask = (m.bit_width == 64) ? ~0ULL : ((1ULL << m.bit_width) - 1ULL);
        out << "    static constexpr uint32_t k" << m.name << "Shift = " << m.bit_offset << "u;\n";
        out << "    static constexpr uint32_t k" << m.name << "Bits = " << m.bit_width << "u;\n";
        if (is64) {
            out << "    static constexpr uint64_t k" << m.name << "Mask = "
                << format_hex_u64(mask) << ";\n";
        } else {
            out << "    static constexpr uint32_t k" << m.name << "Mask = "
                << format_hex_u32(static_cast<uint32_t>(mask), 0) << ";\n";
        }
    }
    out << "\n";

    for (const auto& m : bf.members) {
        out << "    [[nodiscard]] constexpr " << utype << " get_" << m.name
            << "() const noexcept {\n";
        out << "        return (raw >> k" << m.name << "Shift) & k" << m.name << "Mask;\n";
        out << "    }\n";
        out << "    constexpr void set_" << m.name << "(" << utype << " value) noexcept {\n";
        out << "        raw = static_cast<" << utype << ">(raw & ~(k" << m.name << "Mask << k"
            << m.name << "Shift)) |\n";
        out << "              static_cast<" << utype << ">((value & k" << m.name << "Mask) << k"
            << m.name << "Shift);\n";
        out << "    }\n";
    }

    out << "\n    [[nodiscard]] static constexpr " << bf.name << " pack(\n";
    for (size_t i = 0; i < bf.members.size(); ++i) {
        const auto& m = bf.members[i];
        out << "        " << utype << " " << m.name << "_val";
        if (i + 1 == bf.members.size() &&
            (m.name == "Reserved" || m.name == "reserved")) {
            out << " = 0";
        }
        out << (i + 1 < bf.members.size() ? ",\n" : ") noexcept {\n");
    }
    out << "        " << bf.name << " res{};\n";
    for (const auto& m : bf.members) {
        out << "        res.set_" << m.name << "(" << m.name << "_val);\n";
    }
    out << "        return res;\n";
    out << "    }\n";
    out << "};\n\n";

    const uint64_t expected_bytes = bf.total_bits / 8;
    for (const auto& pt : canonical_abi_points()) {
        out << "static_assert(sizeof(" << bf.name << "<" << pt.cpp_expr << ">) == "
            << expected_bytes << "u, \"" << bf.name << " size mismatch\");\n";
    }
    out << "static_assert(std::is_standard_layout_v<" << bf.name
        << "<kDefaultAbi>>, \"" << bf.name << " must be standard-layout\");\n\n";
}

void emit_struct(std::ostringstream& out, const StructDecl& st, const SemanticContext& ctx) {
    /* Collect unique field predicates */
    std::vector<std::string> unique_preds;
    std::unordered_map<std::string, uint32_t> pred_to_bit;
    std::vector<int> field_pred_bit(st.fields.size(), -1);

    for (size_t i = 0; i < st.fields.size(); ++i) {
        std::string expr = build_attr_active_expr_cpp(st.fields[i].attrs, "eff");
        if (expr == "true") {
            continue;
        }
        auto it = pred_to_bit.find(expr);
        if (it == pred_to_bit.end()) {
            uint32_t bit = static_cast<uint32_t>(unique_preds.size());
            pred_to_bit[expr] = bit;
            unique_preds.push_back(expr);
            field_pred_bit[i] = static_cast<int>(bit);
        } else {
            field_pred_bit[i] = static_cast<int>(it->second);
        }
    }

    if (st.attrs.force_abi.has_value()) {
        out << "inline constexpr scarlett_abi_t kForcedAbi_" << st.name << " = "
            << format_version_make_abi(*st.attrs.force_abi) << ";\n";
    }

    auto emit_fields = [&](uint32_t mask, const std::string& abi_for_nested) {
        if (st.attrs.explicit_align.has_value()) {
            /* handled on struct declaration */
        }
        for (size_t i = 0; i < st.fields.size(); ++i) {
            int pb = field_pred_bit[i];
            if (pb >= 0 && ((mask & (1u << pb)) == 0u)) {
                continue;
            }
            const auto& f = st.fields[i];
            out << "    ";
            if (f.attrs.explicit_align.has_value()) {
                out << "alignas(" << *f.attrs.explicit_align << ") ";
            }
            out << format_type_cpp(f.type, ctx, abi_for_nested) << " " << f.name;
            if (f.array_len > 0) {
                out << "[" << f.array_len << "]";
            }
            out << ";\n";
        }
    };

    const std::string nested_abi = st.attrs.force_abi.has_value()
                                       ? ("kForcedAbi_" + st.name)
                                       : "ABI";

    if (unique_preds.empty()) {
        out << "template<scarlett_abi_t ABI = kDefaultAbi>\n";
        out << "struct ";
        if (st.attrs.explicit_align.has_value()) {
            out << "alignas(" << *st.attrs.explicit_align << ") ";
        }
        out << st.name << " {\n";
        emit_fields(0u, nested_abi);
        out << "};\n\n";
    } else {
        out << "[[nodiscard]] constexpr uint32_t " << st.name
            << "_layout_epoch(scarlett_abi_t abi) noexcept {\n";
        if (st.attrs.force_abi.has_value()) {
            out << "    const scarlett_abi_t eff = kForcedAbi_" << st.name
                << ".with_flags(abi.flags);\n";
        } else {
            out << "    const scarlett_abi_t eff = abi;\n";
        }
        out << "    uint32_t mask = 0u;\n";
        for (size_t b = 0; b < unique_preds.size(); ++b) {
            out << "    if (" << unique_preds[b] << ") { mask |= (1u << " << b << "u); }\n";
        }
        out << "    return mask;\n";
        out << "}\n\n";

        out << "template<scarlett_abi_t ABI, uint32_t Epoch = " << st.name
            << "_layout_epoch(ABI)>\n";
        out << "struct " << st.name << "_layout;\n\n";

        const uint32_t combo_count = 1u << unique_preds.size();
        for (uint32_t mask = 0; mask < combo_count; ++mask) {
            out << "template<scarlett_abi_t ABI>\n";
            out << "struct ";
            if (st.attrs.explicit_align.has_value()) {
                out << "alignas(" << *st.attrs.explicit_align << ") ";
            }
            out << st.name << "_layout<ABI, " << mask << "u> {\n";
            emit_fields(mask, nested_abi);
            out << "};\n\n";
        }

        out << "template<scarlett_abi_t ABI = kDefaultAbi>\n";
        out << "using " << st.name << " = " << st.name << "_layout<ABI, "
            << st.name << "_layout_epoch(ABI)>;\n\n";
    }

    /* Emit compile-time sizeof & offsetof static_asserts across Game OS 2020..2026 */
    for (const auto& pt : canonical_abi_points()) {
        ComputedStructLayout csl{};
        std::string err;
        if (!ctx.compute_struct_layout(st, pt.ver, pt.flags, csl, err)) {
            continue;
        }
        out << "static_assert(std::is_standard_layout_v<" << st.name << "<" << pt.cpp_expr
            << ">>, \"" << st.name << " must be standard-layout\");\n";
        out << "static_assert(sizeof(" << st.name << "<" << pt.cpp_expr << ">) == "
            << csl.size << "u, \"sizeof(" << st.name << ") mismatch at " << pt.ver.to_string()
            << "\");\n";
        for (const auto& cf : csl.fields) {
            out << "static_assert(offsetof(" << st.name << "<" << pt.cpp_expr << ">, "
                << cf.name << ") == " << cf.offset << "u, \"offsetof(" << st.name << "::"
                << cf.name << ") mismatch at " << pt.ver.to_string() << "\");\n";
        }
    }
    out << "\n";
}

void emit_interface(std::ostringstream& out, const InterfaceDecl& iface, const SemanticContext& ctx) {
    const ResolvedInterface* ri = ctx.find_resolved_interface(iface.name);
    if (!ri) return;

    const bool has_uuid = iface.attrs.parsed_uuid.has_value() && !iface.attrs.no_uuid;
    out << "inline constexpr bool k" << iface.name << "HasUuid = "
        << (has_uuid ? "true" : "false") << ";\n";
    if (has_uuid) {
        out << "inline constexpr scarlett_guid_t kIID_" << iface.name << " = "
            << format_guid_initializer(*iface.attrs.parsed_uuid) << ";\n";
        out << "static_assert(sizeof(kIID_" << iface.name
            << ") == 16u, \"GUID must be 16 bytes\");\n";
    }
    if (iface.attrs.force_abi.has_value()) {
        out << "inline constexpr scarlett_abi_t kForcedAbi_" << iface.name << " = "
            << format_version_make_abi(*iface.attrs.force_abi) << ";\n";
    }
    out << "inline constexpr uint32_t k" << iface.name << "SlotCount = "
        << ri->total_slots << "u;\n\n";

    /* VTable struct */
    out << "template<scarlett_abi_t ABI = kDefaultAbi>\n";
    out << "struct " << iface.name << "VTable {\n";
    for (const auto& slot : ri->slots) {
        if (slot.is_reserved_padding || slot.method == nullptr) {
            out << "    scarlett_hresult_t (SCARLETT_MS_ABI *ReservedSlot_" << slot.slot_index
                << ")(void* self, uint64_t arg1, uint64_t arg2, uint64_t arg3);\n";
        } else {
            const MethodDecl& m = *slot.method;
            out << "    " << format_type_cpp(m.return_type, ctx, "ABI")
                << " (SCARLETT_MS_ABI *" << m.name << ")(void* self";
            for (const auto& p : m.params) {
                out << ", " << format_type_cpp(p.type, ctx, "ABI") << " " << p.name;
            }
            out << ");\n";
        }
    }
    out << "};\n\n";

    /* Per-slot diagnostic auto-stub descriptors & x86-64 Win64 naked trampolines */
    out << "template<scarlett_abi_t ABI = kDefaultAbi>\n";
    out << "struct " << iface.name << "Stubs {\n";
    for (const auto& slot : ri->slots) {
        const std::string slot_name = (slot.is_reserved_padding || !slot.method)
                                          ? ("ReservedSlot_" + std::to_string(slot.slot_index))
                                          : slot.method->name;
        const std::string sym_full = iface.name + "::" + slot_name;

        out << "    static constexpr uint32_t kFlags_" << slot_name << " =\n";
        out << "        static_cast<uint32_t>(SCARLETT_STUB_FLAG_XDL_VTABLE)";
        if (!slot.is_reserved_padding && slot.method && slot.method->attrs.removed.has_value()) {
            const VersionSpec& rv = *slot.method->attrs.removed;
            out << " |\n        (ABI.version_ge(" << rv.major << "u, " << rv.minor << "u, "
                << rv.build << "u, " << rv.revision
                << "u) ? static_cast<uint32_t>(SCARLETT_STUB_FLAG_REMOVED_IN_ABI) : 0u)";
        }
        out << ";\n";

        out << "    SCARLETT_XDL_HIDDEN static inline const scarlett_stub_descriptor_t kDesc_"
            << slot_name << " = {\n";
        out << "        SCARLETT_STUB_THUNK_ABI_VERSION,\n";
        out << "        kFlags_" << slot_name << ",\n";
        out << "        SCARLETT_STUB_ORDINAL_NONE,\n";
        out << "        " << slot.slot_index << "u,\n";
        out << "        SCARLETT_ERR_UNIMPLEMENTED_STUB,\n";
        out << "        0u,\n";
        out << "        kDllName,\n";
        out << "        \"" << sym_full << "\"\n";
        out << "    };\n";

        out << "    [[gnu::ms_abi, gnu::naked]] static scarlett_hresult_t thunk_" << slot_name
            << "() {\n";
        out << "        asm volatile(\n";
        out << "            \"lea %c0(%%rip), %%r10\\n\\t\"\n";
        out << "            SCARLETT_XDL_JMP_TRAMPOLINE\n";
        out << "            : : \"i\"(&kDesc_" << slot_name << ")\n";
        out << "        );\n";
        out << "    }\n";
    }
    out << "};\n\n";

    /* Binary-compatible COM Proxy struct */
    out << "template<scarlett_abi_t ABI = kDefaultAbi>\n";
    out << "struct " << iface.name << "Proxy {\n";
    out << "    const " << iface.name << "VTable<ABI>* lpVtbl = &default_vtable();\n\n";
    out << "    static constexpr uint32_t kSlotCount = k" << iface.name << "SlotCount;\n";
    out << "    static constexpr bool kHasUuid = k" << iface.name << "HasUuid;\n\n";

    out << "    [[nodiscard]] static const " << iface.name
        << "VTable<ABI>& default_vtable() noexcept {\n";
    out << "        static const " << iface.name << "VTable<ABI> kVtbl = {\n";
    for (size_t i = 0; i < ri->slots.size(); ++i) {
        const auto& slot = ri->slots[i];
        const std::string slot_name = (slot.is_reserved_padding || !slot.method)
                                          ? ("ReservedSlot_" + std::to_string(slot.slot_index))
                                          : slot.method->name;
        out << "            ::scarlett::xdl::cast_thunk<decltype(" << iface.name << "VTable<ABI>::"
            << slot_name << ")>(&" << iface.name << "Stubs<ABI>::thunk_" << slot_name << ")";
        out << (i + 1 < ri->slots.size() ? ",\n" : "\n");
    }
    out << "        };\n";
    out << "        return kVtbl;\n";
    out << "    }\n\n";

    out << "    [[nodiscard]] static bool verify_no_null_slots(const " << iface.name
        << "VTable<ABI>& vt) noexcept {\n";
    out << "        const void* const* raw_slots = reinterpret_cast<const void* const*>(&vt);\n";
    out << "        for (uint32_t i = 0; i < kSlotCount; ++i) {\n";
    out << "            if (raw_slots[i] == nullptr) {\n";
    out << "                return false;\n";
    out << "            }\n";
    out << "        }\n";
    out << "        return true;\n";
    out << "    }\n\n";

    for (const auto& slot : ri->slots) {
        if (slot.is_reserved_padding || !slot.method) continue;
        const MethodDecl& m = *slot.method;
        const std::string ret_cpp = format_type_cpp(m.return_type, ctx, "ABI");
        out << "    " << ret_cpp << " " << m.name << "(";
        for (size_t i = 0; i < m.params.size(); ++i) {
            if (i > 0) out << ", ";
            out << format_type_cpp(m.params[i].type, ctx, "ABI") << " " << m.params[i].name;
        }
        out << ") {\n";
        out << "        ";
        if (ret_cpp != "void") {
            out << "return ";
        }
        out << "lpVtbl->" << m.name << "(this";
        for (const auto& p : m.params) {
            out << ", " << p.name;
        }
        out << ");\n";
        out << "    }\n";
    }
    out << "};\n\n";

    /* VTable sizeof & offsetof static_asserts across Game OS 2020..2026 */
    for (const auto& pt : canonical_abi_points()) {
        out << "static_assert(sizeof(" << iface.name << "VTable<" << pt.cpp_expr << ">) == "
            << (static_cast<uint64_t>(ri->total_slots) * 8ULL)
            << "u, \"VTable size mismatch for " << iface.name << "\");\n";
        out << "static_assert(sizeof(" << iface.name << "Proxy<" << pt.cpp_expr
            << ">) == 8u, \"COM Proxy size must equal pointer size\");\n";
        out << "static_assert(offsetof(" << iface.name << "Proxy<" << pt.cpp_expr
            << ">, lpVtbl) == 0u, \"lpVtbl must be at offset 0\");\n";
    }
    for (const auto& slot : ri->slots) {
        const std::string slot_name = (slot.is_reserved_padding || !slot.method)
                                          ? ("ReservedSlot_" + std::to_string(slot.slot_index))
                                          : slot.method->name;
        out << "static_assert(offsetof(" << iface.name << "VTable<kDefaultAbi>, " << slot_name
            << ") == " << (static_cast<uint64_t>(slot.slot_index) * 8ULL)
            << "u, \"VTable slot offset mismatch\");\n";
    }
    out << "\n";
}

void emit_export(std::ostringstream& out, const ExportDecl& ex, const SemanticContext& ctx) {
    out << "template<scarlett_abi_t ABI = kDefaultAbi>\n";
    out << "using PFN_" << ex.name << "_T = " << format_type_cpp(ex.return_type, ctx, "ABI")
        << " (SCARLETT_MS_ABI *)(";
    for (size_t i = 0; i < ex.params.size(); ++i) {
        if (i > 0) out << ", ";
        out << format_type_cpp(ex.params[i].type, ctx, "ABI") << " " << ex.params[i].name;
    }
    out << ");\n";
    out << "using PFN_" << ex.name << " = PFN_" << ex.name << "_T<kDefaultAbi>;\n\n";

    const uint32_t ordinal = ex.attrs.ordinal.value_or(0u);
    const std::string active_expr = build_attr_active_expr_cpp(ex.attrs, "abi");

    out << "struct Export_" << ex.name << " {\n";
    out << "    static constexpr const char* kName = \"" << ex.name << "\";\n";
    out << "    static constexpr uint32_t kOrdinal = " << ordinal << "u;\n";
    out << "    static constexpr bool kHasOrdinal = "
        << (ex.attrs.ordinal.has_value() ? "true" : "false") << ";\n";
    out << "    static constexpr bool kUnimplemented = "
        << (ex.attrs.unimplemented ? "true" : "false") << ";\n";
    if (ex.attrs.register_abi.has_value()) {
        out << "    static constexpr const char* kRegisterAbi = \""
            << *ex.attrs.register_abi << "\";\n";
    }
    out << "    [[nodiscard]] static constexpr bool is_active(scarlett_abi_t abi) noexcept {\n";
    out << "        (void)abi;\n";
    out << "        return " << active_expr << ";\n";
    out << "    }\n";

    out << "    SCARLETT_XDL_HIDDEN static inline const scarlett_stub_descriptor_t kStubDescriptor = {\n";
    out << "        SCARLETT_STUB_THUNK_ABI_VERSION,\n";
    out << "        " << (ex.attrs.ordinal.has_value()
                              ? "static_cast<uint32_t>(SCARLETT_STUB_FLAG_ORDINAL_EXPORT)"
                              : "static_cast<uint32_t>(SCARLETT_STUB_FLAG_NONE)")
        << ",\n";
    out << "        " << ordinal << "u,\n";
    out << "        SCARLETT_STUB_SLOT_NONE,\n";
    out << "        SCARLETT_ERR_UNIMPLEMENTED_STUB,\n";
    out << "        0u,\n";
    out << "        kDllName,\n";
    out << "        \"" << ex.name << "\"\n";
    out << "    };\n";

    out << "    [[gnu::ms_abi, gnu::naked]] static scarlett_hresult_t stub_thunk() {\n";
    out << "        asm volatile(\n";
    out << "            \"lea %c0(%%rip), %%r10\\n\\t\"\n";
    out << "            SCARLETT_XDL_JMP_TRAMPOLINE\n";
    out << "            : : \"i\"(&kStubDescriptor)\n";
    out << "        );\n";
    out << "    }\n";
    out << "    [[nodiscard]] static PFN_" << ex.name << " stub_fn() noexcept {\n";
    out << "        return ::scarlett::xdl::cast_thunk<PFN_" << ex.name << ">(&stub_thunk);\n";
    out << "    }\n";
    out << "};\n\n";
}

} /* namespace */

std::string generate_cpp_header(const Module& mod, const SemanticContext& ctx) {
    std::ostringstream out;
    out << "/*\n"
        << " * Auto-generated by scarlett-xdl from " << mod.source_path << "\n"
        << " * Target DLL : " << mod.dll_name << "\n"
        << " * Game OS ABI: " << mod.abi_year_min << ".." << mod.abi_year_max << "\n"
        << " * DO NOT EDIT MANUALLY.\n"
        << " */\n\n";
    out << "#pragma once\n\n";
    out << "#include <cstddef>\n";
    out << "#include <cstdint>\n";
    out << "#include <type_traits>\n\n";
    out << "#include \"scarlett_xdl_runtime.hpp\"\n\n";
    out << "namespace " << mod.cpp_namespace << " {\n\n";
    out << "inline constexpr const char* kDllName = \"" << mod.dll_name << "\";\n";
    out << "inline constexpr scarlett_abi_t kDefaultAbi = "
        << format_version_make_abi(mod.default_abi) << ";\n\n";

    /* Forward declarations for mutually referencing types */
    for (const auto& bf : mod.bitfields) {
        out << "template<scarlett_abi_t ABI>\nstruct " << bf.name << ";\n";
    }
    for (const auto& iface : mod.interfaces) {
        out << "template<scarlett_abi_t ABI>\nstruct " << iface.name << "VTable;\n";
        out << "template<scarlett_abi_t ABI>\nstruct " << iface.name << "Proxy;\n";
    }
    if (!mod.bitfields.empty() || !mod.interfaces.empty()) {
        out << "\n";
    }

    for (const auto& item : mod.decl_order) {
        switch (item.kind) {
            case DeclKind::Const: {
                const auto& c = mod.consts[item.index];
                out << "inline constexpr " << format_type_cpp(c.type, ctx, "kDefaultAbi")
                    << " " << c.name << " = ";
                if (c.is_hex) {
                    out << "0x" << std::hex << std::uppercase << c.value << std::dec << "u";
                } else {
                    out << c.value << "u";
                }
                out << ";\n";
                break;
            }
            case DeclKind::Typedef: {
                const auto& td = mod.typedefs[item.index];
                out << "using " << td.alias << " = "
                    << format_type_cpp(td.target, ctx, "kDefaultAbi") << ";\n";
                break;
            }
            case DeclKind::Enum: {
                const auto& en = mod.enums[item.index];
                out << "enum " << en.name << " : "
                    << format_type_cpp(en.underlying, ctx, "kDefaultAbi") << " {\n";
                for (size_t i = 0; i < en.members.size(); ++i) {
                    const auto& m = en.members[i];
                    out << "    " << m.name << " = " << m.value
                        << (i + 1 < en.members.size() ? ",\n" : "\n");
                }
                out << "};\n\n";
                break;
            }
            case DeclKind::Bitfield:
                emit_bitfield(out, mod.bitfields[item.index], ctx);
                break;
            case DeclKind::Struct:
                emit_struct(out, mod.structs[item.index], ctx);
                break;
            case DeclKind::Interface:
                emit_interface(out, mod.interfaces[item.index], ctx);
                break;
            case DeclKind::Export:
                emit_export(out, mod.exports[item.index], ctx);
                break;
        }
    }

    if (!mod.exports.empty()) {
        out << "inline const scarlett_stub_descriptor_t* const kExportStubDescriptors[] = {\n";
        for (size_t i = 0; i < mod.exports.size(); ++i) {
            out << "    &Export_" << mod.exports[i].name << "::kStubDescriptor"
                << (i + 1 < mod.exports.size() ? ",\n" : "\n");
        }
        out << "};\n";
        out << "inline constexpr size_t kExportCount = " << mod.exports.size() << "u;\n\n";
    }

    out << "} /* namespace " << mod.cpp_namespace << " */\n";
    return out.str();
}

} /* namespace scarlett::xdl::compiler */
