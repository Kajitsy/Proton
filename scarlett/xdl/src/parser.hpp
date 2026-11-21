#ifndef SCARLETT_XDL_PARSER_HPP
#define SCARLETT_XDL_PARSER_HPP

#include <memory>
#include <string>
#include <vector>

#include "ast.hpp"
#include "lexer.hpp"

namespace scarlett::xdl::compiler {

bool parse_guid_string(const std::string& s, GuidValue& out);
bool parse_version_string(const std::string& s, VersionSpec& out);
std::shared_ptr<CondExpr> parse_conditional_expression(const std::string& expr,
                                                       std::string& out_error);

class Parser {
public:
    Parser(const std::vector<Token>& tokens, std::string filename);

    bool parse_module(Module& out_mod, std::vector<Diagnostic>& diags);

private:
    const Token& peek(size_t offset = 0) const noexcept;
    const Token& advance() noexcept;
    bool check(TokenKind kind) const noexcept;
    bool check_ident(const char* kw) const noexcept;
    bool match(TokenKind kind) noexcept;
    bool expect(TokenKind kind, const char* what, std::vector<Diagnostic>& diags);

    bool parse_module_header(Module& mod, std::vector<Diagnostic>& diags);
    bool parse_attributes(AttributeSet& attrs, std::vector<Diagnostic>& diags);
    bool parse_version_spec(VersionSpec& out, std::vector<Diagnostic>& diags);
    bool parse_type_ref(TypeRef& out, std::vector<Diagnostic>& diags);
    bool parse_param_list(std::vector<ParamDecl>& out, std::vector<Diagnostic>& diags);

    bool parse_const(AttributeSet attrs, Module& mod, std::vector<Diagnostic>& diags);
    bool parse_typedef(AttributeSet attrs, Module& mod, std::vector<Diagnostic>& diags);
    bool parse_enum(AttributeSet attrs, Module& mod, std::vector<Diagnostic>& diags);
    bool parse_bitfield(AttributeSet attrs, Module& mod, std::vector<Diagnostic>& diags);
    bool parse_struct(AttributeSet attrs, Module& mod, std::vector<Diagnostic>& diags);
    bool parse_interface(AttributeSet attrs, Module& mod, std::vector<Diagnostic>& diags);
    bool parse_export(AttributeSet attrs, Module& mod, std::vector<Diagnostic>& diags);

    void error(SourceLoc loc, const std::string& msg, std::vector<Diagnostic>& diags);

    const std::vector<Token>& tokens_;
    std::string filename_;
    size_t pos_ = 0;
};

} /* namespace scarlett::xdl::compiler */

#endif /* SCARLETT_XDL_PARSER_HPP */
