#include "parser.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace scarlett::xdl::compiler {

namespace {

int hex_nibble(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

bool parse_hex_u64(const std::string& s, size_t offset, size_t len, uint64_t& out) noexcept {
    if (offset + len > s.size()) return false;
    uint64_t val = 0;
    for (size_t i = 0; i < len; ++i) {
        int n = hex_nibble(s[offset + i]);
        if (n < 0) return false;
        val = (val << 4) | static_cast<uint64_t>(n);
    }
    out = val;
    return true;
}

class CondParser {
public:
    explicit CondParser(const std::string& src) : src_(src) {}

    std::shared_ptr<CondExpr> parse(std::string& err) {
        auto node = parse_or(err);
        if (!node) return nullptr;
        skip_ws();
        if (pos_ != src_.size()) {
            err = "unexpected trailing characters in conditional expression: '" +
                  src_.substr(pos_) + "'";
            return nullptr;
        }
        return node;
    }

private:
    void skip_ws() {
        while (pos_ < src_.size() &&
               std::isspace(static_cast<unsigned char>(src_[pos_]))) {
            ++pos_;
        }
    }

    bool starts_with(const char* s) const {
        size_t i = 0;
        while (s[i] != '\0') {
            if (pos_ + i >= src_.size() || src_[pos_ + i] != s[i]) {
                return false;
            }
            ++i;
        }
        return true;
    }

    std::shared_ptr<CondExpr> parse_or(std::string& err) {
        auto lhs = parse_and(err);
        if (!lhs) return nullptr;
        while (true) {
            skip_ws();
            if (starts_with("||")) {
                pos_ += 2;
                auto rhs = parse_and(err);
                if (!rhs) return nullptr;
                auto node = std::make_shared<CondExpr>();
                node->kind = CondExpr::Kind::Or;
                node->lhs = std::move(lhs);
                node->rhs = std::move(rhs);
                lhs = std::move(node);
            } else {
                break;
            }
        }
        return lhs;
    }

    std::shared_ptr<CondExpr> parse_and(std::string& err) {
        auto lhs = parse_unary(err);
        if (!lhs) return nullptr;
        while (true) {
            skip_ws();
            if (starts_with("&&")) {
                pos_ += 2;
                auto rhs = parse_unary(err);
                if (!rhs) return nullptr;
                auto node = std::make_shared<CondExpr>();
                node->kind = CondExpr::Kind::And;
                node->lhs = std::move(lhs);
                node->rhs = std::move(rhs);
                lhs = std::move(node);
            } else {
                break;
            }
        }
        return lhs;
    }

    std::shared_ptr<CondExpr> parse_unary(std::string& err) {
        skip_ws();
        if (pos_ < src_.size() && src_[pos_] == '!') {
            ++pos_;
            auto inner = parse_unary(err);
            if (!inner) return nullptr;
            auto node = std::make_shared<CondExpr>();
            node->kind = CondExpr::Kind::Not;
            node->lhs = std::move(inner);
            return node;
        }
        return parse_primary(err);
    }

    std::string read_ident() {
        skip_ws();
        std::string out;
        while (pos_ < src_.size() &&
               (std::isalnum(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '_')) {
            out.push_back(src_[pos_++]);
        }
        return out;
    }

    std::shared_ptr<CondExpr> parse_primary(std::string& err) {
        skip_ws();
        if (pos_ < src_.size() && src_[pos_] == '(') {
            ++pos_;
            auto inner = parse_or(err);
            if (!inner) return nullptr;
            skip_ws();
            if (pos_ >= src_.size() || src_[pos_] != ')') {
                err = "missing ')' in conditional expression";
                return nullptr;
            }
            ++pos_;
            return inner;
        }

        std::string id = read_ident();
        if (id.empty()) {
            err = "expected identifier in conditional expression";
            return nullptr;
        }

        if (id == "anaconda" || id == "lockhart" || id == "devkit") {
            auto node = std::make_shared<CondExpr>();
            node->kind = CondExpr::Kind::AtomModel;
            node->atom = id;
            return node;
        }
        if (id == "pix_enabled" || id == "raytracing" || id == "mesh_shaders" || id == "bcpack") {
            auto node = std::make_shared<CondExpr>();
            node->kind = CondExpr::Kind::AtomFlag;
            node->atom = id;
            return node;
        }
        if (id == "gxdk" || id == "abi") {
            skip_ws();
            CondExpr::CmpOp op = CondExpr::CmpOp::Ge;
            if (starts_with(">=")) { op = CondExpr::CmpOp::Ge; pos_ += 2; }
            else if (starts_with("<=")) { op = CondExpr::CmpOp::Le; pos_ += 2; }
            else if (starts_with("==")) { op = CondExpr::CmpOp::Eq; pos_ += 2; }
            else if (starts_with("!=")) { op = CondExpr::CmpOp::Ne; pos_ += 2; }
            else if (starts_with(">")) { op = CondExpr::CmpOp::Gt; pos_ += 1; }
            else if (starts_with("<")) { op = CondExpr::CmpOp::Lt; pos_ += 1; }
            else {
                err = "expected comparison operator after '" + id + "' in conditional expression";
                return nullptr;
            }
            skip_ws();
            std::string rhs;
            while (pos_ < src_.size() &&
                   (std::isalnum(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '.')) {
                rhs.push_back(src_[pos_++]);
            }
            if (rhs.empty()) {
                err = "expected version or hex literal after comparison operator";
                return nullptr;
            }
            VersionSpec v{};
            if (!parse_version_string(rhs, v)) {
                err = "invalid version literal in conditional expression: '" + rhs + "'";
                return nullptr;
            }
            auto node = std::make_shared<CondExpr>();
            node->kind = CondExpr::Kind::VersionCmp;
            node->is_gxdk_cmp = (id == "gxdk");
            node->cmp_op = op;
            node->cmp_version = v;
            node->cmp_gxdk = v.gxdk_build_rev();
            return node;
        }

        err = "unknown identifier '" + id + "' in conditional expression";
        return nullptr;
    }

    const std::string& src_;
    size_t pos_ = 0;
};

} /* namespace */

bool parse_guid_string(const std::string& raw, GuidValue& out) {
    std::string s = raw;
    if (s.size() == 38 && s.front() == '{' && s.back() == '}') {
        s = s.substr(1, 36);
    }
    if (s.size() != 36) return false;
    if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') {
        return false;
    }
    uint64_t d1 = 0, d2 = 0, d3 = 0, d4a = 0, d4b = 0;
    if (!parse_hex_u64(s, 0, 8, d1)) return false;
    if (!parse_hex_u64(s, 9, 4, d2)) return false;
    if (!parse_hex_u64(s, 14, 4, d3)) return false;
    if (!parse_hex_u64(s, 19, 4, d4a)) return false;
    if (!parse_hex_u64(s, 24, 12, d4b)) return false;

    out.data1 = static_cast<uint32_t>(d1);
    out.data2 = static_cast<uint16_t>(d2);
    out.data3 = static_cast<uint16_t>(d3);
    out.data4[0] = static_cast<uint8_t>((d4a >> 8) & 0xFFu);
    out.data4[1] = static_cast<uint8_t>(d4a & 0xFFu);
    out.data4[2] = static_cast<uint8_t>((d4b >> 40) & 0xFFu);
    out.data4[3] = static_cast<uint8_t>((d4b >> 32) & 0xFFu);
    out.data4[4] = static_cast<uint8_t>((d4b >> 24) & 0xFFu);
    out.data4[5] = static_cast<uint8_t>((d4b >> 16) & 0xFFu);
    out.data4[6] = static_cast<uint8_t>((d4b >> 8) & 0xFFu);
    out.data4[7] = static_cast<uint8_t>(d4b & 0xFFu);

    std::string upper = s;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    out.canonical = std::move(upper);
    return true;
}

bool parse_version_string(const std::string& s, VersionSpec& out) {
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        char* end = nullptr;
        unsigned long long val = std::strtoull(s.c_str(), &end, 16);
        if (end == s.c_str() || *end != '\0' || val > 0xFFFFFFFFULL) {
            return false;
        }
        out.major = 10u;
        out.minor = 0u;
        out.build = static_cast<uint16_t>((val >> 16) & 0xFFFFu);
        out.revision = static_cast<uint16_t>(val & 0xFFFFu);
        return true;
    }

    std::vector<uint16_t> parts;
    size_t start = 0;
    while (start <= s.size()) {
        size_t dot = s.find('.', start);
        std::string piece = (dot == std::string::npos) ? s.substr(start)
                                                       : s.substr(start, dot - start);
        if (piece.empty()) return false;
        for (char c : piece) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        }
        unsigned long long v = std::strtoull(piece.c_str(), nullptr, 10);
        if (v > 65535ULL) return false;
        parts.push_back(static_cast<uint16_t>(v));
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    if (parts.size() != 4) return false;
    out.major = parts[0];
    out.minor = parts[1];
    out.build = parts[2];
    out.revision = parts[3];
    return true;
}

std::shared_ptr<CondExpr> parse_conditional_expression(const std::string& expr,
                                                       std::string& out_error) {
    CondParser cp(expr);
    return cp.parse(out_error);
}

Parser::Parser(const std::vector<Token>& tokens, std::string filename)
    : tokens_(tokens), filename_(std::move(filename)) {}

const Token& Parser::peek(size_t offset) const noexcept {
    if (pos_ + offset >= tokens_.size()) {
        return tokens_.back();
    }
    return tokens_[pos_ + offset];
}

const Token& Parser::advance() noexcept {
    const Token& t = peek();
    if (pos_ + 1 < tokens_.size()) {
        ++pos_;
    }
    return t;
}

bool Parser::check(TokenKind kind) const noexcept {
    return peek().kind == kind;
}

bool Parser::check_ident(const char* kw) const noexcept {
    return peek().kind == TokenKind::Ident && peek().text == kw;
}

bool Parser::match(TokenKind kind) noexcept {
    if (check(kind)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::expect(TokenKind kind, const char* what, std::vector<Diagnostic>& diags) {
    if (check(kind)) {
        advance();
        return true;
    }
    error(peek().loc, std::string("expected ") + what + ", got '" + peek().text + "'", diags);
    return false;
}

void Parser::error(SourceLoc loc, const std::string& msg, std::vector<Diagnostic>& diags) {
    diags.push_back(Diagnostic{filename_, loc, msg});
}

bool Parser::parse_version_spec(VersionSpec& out, std::vector<Diagnostic>& diags) {
    const Token& first = peek();
    if (first.kind != TokenKind::IntLit) {
        error(first.loc, "expected version number (Major.Minor.Build.Revision or 0xHEX)", diags);
        return false;
    }
    if (first.is_hex) {
        if (first.int_val > 0xFFFFFFFFULL) {
            error(first.loc, "hex GXDK version exceeds 32-bit range", diags);
            return false;
        }
        advance();
        out.major = 10u;
        out.minor = 0u;
        out.build = static_cast<uint16_t>((first.int_val >> 16) & 0xFFFFu);
        out.revision = static_cast<uint16_t>(first.int_val & 0xFFFFu);
        return true;
    }

    uint16_t parts[4] = {};
    for (int i = 0; i < 4; ++i) {
        const Token& t = peek();
        if (t.kind != TokenKind::IntLit || t.int_val > 65535ULL) {
            error(t.loc, "expected uint16 version component (0..65535)", diags);
            return false;
        }
        parts[i] = static_cast<uint16_t>(t.int_val);
        advance();
        if (i < 3) {
            if (!match(TokenKind::Dot) && !match(TokenKind::Comma)) {
                error(peek().loc, "expected '.' or ',' between version components", diags);
                return false;
            }
        }
    }
    out.major = parts[0];
    out.minor = parts[1];
    out.build = parts[2];
    out.revision = parts[3];
    return true;
}

bool Parser::parse_attributes(AttributeSet& attrs, std::vector<Diagnostic>& diags) {
    bool any = false;
    while (check(TokenKind::LBracket)) {
        if (!any) {
            attrs.loc = peek().loc;
            any = true;
        }
        advance(); /* '[' */
        while (!check(TokenKind::RBracket) && !check(TokenKind::Eof)) {
            const Token& name_tok = peek();
            if (name_tok.kind != TokenKind::Ident) {
                error(name_tok.loc, "expected attribute name", diags);
                return false;
            }
            const std::string attr_name = name_tok.text;
            advance();

            if (attr_name == "uuid") {
                if (!expect(TokenKind::LParen, "'('", diags)) return false;
                if (!check(TokenKind::StringLit)) {
                    error(peek().loc, "expected quoted GUID string in [uuid(...)]", diags);
                    return false;
                }
                std::string gstr = advance().text;
                if (!expect(TokenKind::RParen, "')'", diags)) return false;
                GuidValue gv{};
                if (!parse_guid_string(gstr, gv)) {
                    error(name_tok.loc, "invalid GUID format in [uuid(\"" + gstr + "\")]", diags);
                    return false;
                }
                attrs.uuid_str = gstr;
                attrs.parsed_uuid = gv;
            } else if (attr_name == "no_uuid") {
                attrs.no_uuid = true;
            } else if (attr_name == "unimplemented") {
                attrs.unimplemented = true;
            } else if (attr_name == "win64_abi") {
                attrs.win64_abi = true;
            } else if (attr_name == "force_abi") {
                if (!expect(TokenKind::LParen, "'('", diags)) return false;
                VersionSpec v{};
                if (!parse_version_spec(v, diags)) return false;
                if (!expect(TokenKind::RParen, "')'", diags)) return false;
                attrs.force_abi = v;
            } else if (attr_name == "added") {
                if (!expect(TokenKind::LParen, "'('", diags)) return false;
                VersionSpec v{};
                if (!parse_version_spec(v, diags)) return false;
                if (!expect(TokenKind::RParen, "')'", diags)) return false;
                attrs.added = v;
            } else if (attr_name == "removed") {
                if (!expect(TokenKind::LParen, "'('", diags)) return false;
                VersionSpec v{};
                if (!parse_version_spec(v, diags)) return false;
                if (!expect(TokenKind::RParen, "')'", diags)) return false;
                attrs.removed = v;
            } else if (attr_name == "conditional") {
                if (!expect(TokenKind::LParen, "'('", diags)) return false;
                if (!check(TokenKind::StringLit)) {
                    error(peek().loc, "expected quoted expression string in [conditional(...)]", diags);
                    return false;
                }
                std::string cstr = advance().text;
                if (!expect(TokenKind::RParen, "')'", diags)) return false;
                std::string cerr;
                auto expr = parse_conditional_expression(cstr, cerr);
                if (!expr) {
                    error(name_tok.loc, "invalid [conditional(\"" + cstr + "\")]: " + cerr, diags);
                    return false;
                }
                attrs.conditional_str = cstr;
                attrs.conditional_expr = std::move(expr);
            } else if (attr_name == "ordinal") {
                if (!expect(TokenKind::LParen, "'('", diags)) return false;
                if (!check(TokenKind::IntLit)) {
                    error(peek().loc, "expected integer ordinal in [ordinal(N)]", diags);
                    return false;
                }
                uint64_t ord = advance().int_val;
                if (!expect(TokenKind::RParen, "')'", diags)) return false;
                if (ord == 0 || ord > 65535ULL) {
                    error(name_tok.loc, "ordinal must be in range 1..65535", diags);
                    return false;
                }
                attrs.ordinal = static_cast<uint32_t>(ord);
            } else if (attr_name == "slots") {
                if (!expect(TokenKind::LParen, "'('", diags)) return false;
                if (!check(TokenKind::IntLit)) {
                    error(peek().loc, "expected integer slot count in [slots(N)]", diags);
                    return false;
                }
                uint64_t n = advance().int_val;
                if (!expect(TokenKind::RParen, "')'", diags)) return false;
                if (n == 0 || n > 4096ULL) {
                    error(name_tok.loc, "slots count must be in range 1..4096", diags);
                    return false;
                }
                attrs.slots = static_cast<uint32_t>(n);
            } else if (attr_name == "slot") {
                if (!expect(TokenKind::LParen, "'('", diags)) return false;
                if (!check(TokenKind::IntLit)) {
                    error(peek().loc, "expected integer slot index in [slot(K)]", diags);
                    return false;
                }
                uint64_t k = advance().int_val;
                if (!expect(TokenKind::RParen, "')'", diags)) return false;
                if (k > 4095ULL) {
                    error(name_tok.loc, "slot index must be in range 0..4095", diags);
                    return false;
                }
                attrs.slot = static_cast<uint32_t>(k);
            } else if (attr_name == "size") {
                if (!expect(TokenKind::LParen, "'('", diags)) return false;
                if (!check(TokenKind::IntLit)) {
                    error(peek().loc, "expected integer size in [size(N)]", diags);
                    return false;
                }
                attrs.explicit_size = advance().int_val;
                if (!expect(TokenKind::RParen, "')'", diags)) return false;
            } else if (attr_name == "align") {
                if (!expect(TokenKind::LParen, "'('", diags)) return false;
                if (!check(TokenKind::IntLit)) {
                    error(peek().loc, "expected integer alignment in [align(N)]", diags);
                    return false;
                }
                attrs.explicit_align = advance().int_val;
                if (!expect(TokenKind::RParen, "')'", diags)) return false;
            } else if (attr_name == "offset") {
                if (!expect(TokenKind::LParen, "'('", diags)) return false;
                if (!check(TokenKind::IntLit)) {
                    error(peek().loc, "expected integer offset in [offset(N)]", diags);
                    return false;
                }
                attrs.explicit_offset = advance().int_val;
                if (!expect(TokenKind::RParen, "')'", diags)) return false;
            } else if (attr_name == "register_abi") {
                if (!expect(TokenKind::LParen, "'('", diags)) return false;
                if (!check(TokenKind::StringLit)) {
                    error(peek().loc, "expected quoted string in [register_abi(...)]", diags);
                    return false;
                }
                attrs.register_abi = advance().text;
                if (!expect(TokenKind::RParen, "')'", diags)) return false;
            } else {
                error(name_tok.loc, "unknown attribute '[" + attr_name + "]'", diags);
                return false;
            }

            if (match(TokenKind::Comma)) {
                continue;
            }
            break;
        }
        if (!expect(TokenKind::RBracket, "']'", diags)) return false;
    }
    return true;
}

bool Parser::parse_type_ref(TypeRef& out, std::vector<Diagnostic>& diags) {
    out = TypeRef{};
    out.loc = peek().loc;
    if (check_ident("const")) {
        out.is_const = true;
        advance();
    }
    if (!check(TokenKind::Ident)) {
        error(peek().loc, "expected type name, got '" + peek().text + "'", diags);
        return false;
    }
    out.base_name = advance().text;
    while (match(TokenKind::Star)) {
        ++out.pointer_depth;
        bool pconst = false;
        if (check_ident("const")) {
            pconst = true;
            advance();
        }
        out.pointer_const.push_back(pconst);
    }
    return true;
}

bool Parser::parse_param_list(std::vector<ParamDecl>& out, std::vector<Diagnostic>& diags) {
    out.clear();
    if (check(TokenKind::RParen)) {
        return true;
    }
    /* Support `(void)` parameter list */
    if (check_ident("void") && peek(1).kind == TokenKind::RParen) {
        advance();
        return true;
    }
    while (true) {
        ParamDecl p{};
        p.loc = peek().loc;
        if (!parse_type_ref(p.type, diags)) return false;
        if (check(TokenKind::Ident)) {
            p.name = advance().text;
        } else {
            p.name = "arg" + std::to_string(out.size());
        }
        out.push_back(std::move(p));
        if (!match(TokenKind::Comma)) {
            break;
        }
    }
    return true;
}

bool Parser::parse_module_header(Module& mod, std::vector<Diagnostic>& diags) {
    if (!check_ident("module")) {
        error(peek().loc, "expected 'module' header at start of .xdl file", diags);
        return false;
    }
    advance();
    if (!check(TokenKind::Ident)) {
        error(peek().loc, "expected module identifier", diags);
        return false;
    }
    mod.name = advance().text;
    if (!expect(TokenKind::LBrace, "'{'", diags)) return false;

    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        const Token& prop = peek();
        if (prop.kind != TokenKind::Ident) {
            error(prop.loc, "expected module property name", diags);
            return false;
        }
        std::string key = advance().text;
        if (!expect(TokenKind::Equals, "'='", diags)) return false;

        if (key == "dll") {
            if (!check(TokenKind::StringLit)) {
                error(peek().loc, "expected string literal for 'dll'", diags);
                return false;
            }
            mod.dll_name = advance().text;
        } else if (key == "namespace") {
            if (!check(TokenKind::StringLit)) {
                error(peek().loc, "expected string literal for 'namespace'", diags);
                return false;
            }
            mod.cpp_namespace = advance().text;
        } else if (key == "default_abi") {
            if (!parse_version_spec(mod.default_abi, diags)) return false;
        } else if (key == "abi_range") {
            if (!check(TokenKind::IntLit)) {
                error(peek().loc, "expected start year for 'abi_range'", diags);
                return false;
            }
            mod.abi_year_min = static_cast<uint32_t>(advance().int_val);
            if (!expect(TokenKind::DotDot, "'..'", diags)) return false;
            if (!check(TokenKind::IntLit)) {
                error(peek().loc, "expected end year for 'abi_range'", diags);
                return false;
            }
            mod.abi_year_max = static_cast<uint32_t>(advance().int_val);
        } else {
            error(prop.loc, "unknown module property '" + key + "'", diags);
            return false;
        }
        if (!expect(TokenKind::Semicolon, "';'", diags)) return false;
    }

    if (!expect(TokenKind::RBrace, "'}'", diags)) return false;
    match(TokenKind::Semicolon);

    if (mod.dll_name.empty()) {
        mod.dll_name = mod.name + ".dll";
    }
    if (mod.cpp_namespace.empty()) {
        mod.cpp_namespace = "scarlett::xdl::" + mod.name;
    }
    return true;
}

bool Parser::parse_const(AttributeSet attrs, Module& mod, std::vector<Diagnostic>& diags) {
    ConstDecl d{};
    d.loc = peek().loc;
    d.attrs = std::move(attrs);
    advance(); /* 'const' */
    if (!parse_type_ref(d.type, diags)) return false;
    if (!check(TokenKind::Ident)) {
        error(peek().loc, "expected constant name", diags);
        return false;
    }
    d.name = advance().text;
    if (!expect(TokenKind::Equals, "'='", diags)) return false;
    if (!check(TokenKind::IntLit)) {
        error(peek().loc, "expected integer constant value", diags);
        return false;
    }
    const Token& vt = advance();
    d.value = vt.int_val;
    d.is_hex = vt.is_hex;
    if (!expect(TokenKind::Semicolon, "';'", diags)) return false;

    mod.decl_order.push_back(DeclItem{DeclKind::Const, mod.consts.size()});
    mod.consts.push_back(std::move(d));
    return true;
}

bool Parser::parse_typedef(AttributeSet attrs, Module& mod, std::vector<Diagnostic>& diags) {
    TypedefDecl d{};
    d.loc = peek().loc;
    d.attrs = std::move(attrs);
    advance(); /* 'typedef' */
    if (!parse_type_ref(d.target, diags)) return false;
    if (!check(TokenKind::Ident)) {
        error(peek().loc, "expected typedef alias name", diags);
        return false;
    }
    d.alias = advance().text;
    if (!expect(TokenKind::Semicolon, "';'", diags)) return false;

    mod.decl_order.push_back(DeclItem{DeclKind::Typedef, mod.typedefs.size()});
    mod.typedefs.push_back(std::move(d));
    return true;
}

bool Parser::parse_enum(AttributeSet attrs, Module& mod, std::vector<Diagnostic>& diags) {
    EnumDecl d{};
    d.loc = peek().loc;
    d.attrs = std::move(attrs);
    d.underlying.base_name = "uint32_t";
    advance(); /* 'enum' */
    if (!check(TokenKind::Ident)) {
        error(peek().loc, "expected enum name", diags);
        return false;
    }
    d.name = advance().text;
    if (match(TokenKind::Colon)) {
        if (!parse_type_ref(d.underlying, diags)) return false;
    }
    if (!expect(TokenKind::LBrace, "'{'", diags)) return false;

    int64_t next_val = 0;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        EnumMember m{};
        if (!parse_attributes(m.attrs, diags)) return false;
        if (!check(TokenKind::Ident)) {
            error(peek().loc, "expected enum member identifier", diags);
            return false;
        }
        m.loc = peek().loc;
        m.name = advance().text;
        if (match(TokenKind::Equals)) {
            bool neg = match(TokenKind::Minus);
            if (!check(TokenKind::IntLit)) {
                error(peek().loc, "expected integer enum value", diags);
                return false;
            }
            uint64_t v = advance().int_val;
            m.value = neg ? -static_cast<int64_t>(v) : static_cast<int64_t>(v);
            m.has_explicit_value = true;
            next_val = m.value + 1;
        } else {
            m.value = next_val++;
        }
        d.members.push_back(std::move(m));
        if (!match(TokenKind::Comma)) {
            match(TokenKind::Semicolon);
        }
    }

    if (!expect(TokenKind::RBrace, "'}'", diags)) return false;
    match(TokenKind::Semicolon);

    mod.decl_order.push_back(DeclItem{DeclKind::Enum, mod.enums.size()});
    mod.enums.push_back(std::move(d));
    return true;
}

bool Parser::parse_bitfield(AttributeSet attrs, Module& mod, std::vector<Diagnostic>& diags) {
    BitfieldDecl d{};
    d.loc = peek().loc;
    d.attrs = std::move(attrs);
    advance(); /* 'bitfield' */
    if (!check(TokenKind::Ident)) {
        error(peek().loc, "expected bitfield name", diags);
        return false;
    }
    d.name = advance().text;
    if (!expect(TokenKind::Colon, "':'", diags)) return false;
    if (!parse_type_ref(d.underlying, diags)) return false;
    if (!expect(TokenKind::LBrace, "'{'", diags)) return false;

    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        BitfieldMember m{};
        if (!parse_attributes(m.attrs, diags)) return false;
        if (!check(TokenKind::Ident)) {
            error(peek().loc, "expected bitfield member name", diags);
            return false;
        }
        m.loc = peek().loc;
        m.name = advance().text;
        if (!expect(TokenKind::Colon, "':'", diags)) return false;
        if (!check(TokenKind::IntLit)) {
            error(peek().loc, "expected integer bit width", diags);
            return false;
        }
        m.bit_width = static_cast<uint32_t>(advance().int_val);
        if (!expect(TokenKind::Semicolon, "';'", diags)) return false;
        d.members.push_back(std::move(m));
    }

    if (!expect(TokenKind::RBrace, "'}'", diags)) return false;
    match(TokenKind::Semicolon);

    mod.decl_order.push_back(DeclItem{DeclKind::Bitfield, mod.bitfields.size()});
    mod.bitfields.push_back(std::move(d));
    return true;
}

bool Parser::parse_struct(AttributeSet attrs, Module& mod, std::vector<Diagnostic>& diags) {
    StructDecl d{};
    d.loc = peek().loc;
    d.attrs = std::move(attrs);
    advance(); /* 'struct' */
    if (!check(TokenKind::Ident)) {
        error(peek().loc, "expected struct name", diags);
        return false;
    }
    d.name = advance().text;
    if (!expect(TokenKind::LBrace, "'{'", diags)) return false;

    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        StructField f{};
        if (!parse_attributes(f.attrs, diags)) return false;
        f.loc = peek().loc;
        if (!parse_type_ref(f.type, diags)) return false;
        if (!check(TokenKind::Ident)) {
            error(peek().loc, "expected struct field name", diags);
            return false;
        }
        f.name = advance().text;
        if (match(TokenKind::LBracket)) {
            if (!check(TokenKind::IntLit) || peek().int_val == 0) {
                error(peek().loc, "expected positive integer array length", diags);
                return false;
            }
            f.array_len = advance().int_val;
            if (!expect(TokenKind::RBracket, "']'", diags)) return false;
        }
        if (!expect(TokenKind::Semicolon, "';'", diags)) return false;
        d.fields.push_back(std::move(f));
    }

    if (!expect(TokenKind::RBrace, "'}'", diags)) return false;
    match(TokenKind::Semicolon);

    mod.decl_order.push_back(DeclItem{DeclKind::Struct, mod.structs.size()});
    mod.structs.push_back(std::move(d));
    return true;
}

bool Parser::parse_interface(AttributeSet attrs, Module& mod, std::vector<Diagnostic>& diags) {
    InterfaceDecl d{};
    d.loc = peek().loc;
    d.attrs = std::move(attrs);
    advance(); /* 'interface' */
    if (!check(TokenKind::Ident)) {
        error(peek().loc, "expected interface name", diags);
        return false;
    }
    d.name = advance().text;
    if (match(TokenKind::Colon)) {
        if (!check(TokenKind::Ident)) {
            error(peek().loc, "expected base interface name after ':'", diags);
            return false;
        }
        d.base_interface = advance().text;
    }
    if (!expect(TokenKind::LBrace, "'{'", diags)) return false;

    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        MethodDecl m{};
        if (!parse_attributes(m.attrs, diags)) return false;
        m.loc = peek().loc;
        if (!parse_type_ref(m.return_type, diags)) return false;
        if (!check(TokenKind::Ident)) {
            error(peek().loc, "expected method name", diags);
            return false;
        }
        m.name = advance().text;
        if (!expect(TokenKind::LParen, "'('", diags)) return false;
        if (!parse_param_list(m.params, diags)) return false;
        if (!expect(TokenKind::RParen, "')'", diags)) return false;
        if (!expect(TokenKind::Semicolon, "';'", diags)) return false;
        d.methods.push_back(std::move(m));
    }

    if (!expect(TokenKind::RBrace, "'}'", diags)) return false;
    match(TokenKind::Semicolon);

    mod.decl_order.push_back(DeclItem{DeclKind::Interface, mod.interfaces.size()});
    mod.interfaces.push_back(std::move(d));
    return true;
}

bool Parser::parse_export(AttributeSet attrs, Module& mod, std::vector<Diagnostic>& diags) {
    ExportDecl d{};
    d.loc = peek().loc;
    d.attrs = std::move(attrs);
    advance(); /* 'export' */
    if (!parse_type_ref(d.return_type, diags)) return false;
    if (!check(TokenKind::Ident)) {
        error(peek().loc, "expected export function name", diags);
        return false;
    }
    d.name = advance().text;
    if (!expect(TokenKind::LParen, "'('", diags)) return false;
    if (!parse_param_list(d.params, diags)) return false;
    if (!expect(TokenKind::RParen, "')'", diags)) return false;
    if (!expect(TokenKind::Semicolon, "';'", diags)) return false;

    mod.decl_order.push_back(DeclItem{DeclKind::Export, mod.exports.size()});
    mod.exports.push_back(std::move(d));
    return true;
}

bool Parser::parse_module(Module& out_mod, std::vector<Diagnostic>& diags) {
    out_mod.source_path = filename_;
    if (!parse_module_header(out_mod, diags)) {
        return false;
    }

    while (!check(TokenKind::Eof)) {
        AttributeSet attrs{};
        if (!parse_attributes(attrs, diags)) {
            return false;
        }
        if (check(TokenKind::Eof)) {
            error(peek().loc, "dangling attributes at end of file", diags);
            return false;
        }

        if (check_ident("const")) {
            if (!parse_const(std::move(attrs), out_mod, diags)) return false;
        } else if (check_ident("typedef")) {
            if (!parse_typedef(std::move(attrs), out_mod, diags)) return false;
        } else if (check_ident("enum")) {
            if (!parse_enum(std::move(attrs), out_mod, diags)) return false;
        } else if (check_ident("bitfield")) {
            if (!parse_bitfield(std::move(attrs), out_mod, diags)) return false;
        } else if (check_ident("struct")) {
            if (!parse_struct(std::move(attrs), out_mod, diags)) return false;
        } else if (check_ident("interface")) {
            if (!parse_interface(std::move(attrs), out_mod, diags)) return false;
        } else if (check_ident("export")) {
            if (!parse_export(std::move(attrs), out_mod, diags)) return false;
        } else {
            error(peek().loc, "unexpected declaration keyword '" + peek().text + "'", diags);
            return false;
        }
    }

    return true;
}

} /* namespace scarlett::xdl::compiler */
