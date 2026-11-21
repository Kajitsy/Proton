#ifndef SCARLETT_XDL_LEXER_HPP
#define SCARLETT_XDL_LEXER_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "ast.hpp"

namespace scarlett::xdl::compiler {

enum class TokenKind {
    Eof,
    Ident,
    IntLit,
    StringLit,
    LBrace,     /* { */
    RBrace,     /* } */
    LBracket,   /* [ */
    RBracket,   /* ] */
    LParen,     /* ( */
    RParen,     /* ) */
    Semicolon,  /* ; */
    Colon,      /* : */
    Comma,      /* , */
    Dot,        /* . */
    DotDot,     /* .. */
    Equals,     /* = */
    Star,       /* * */
    Minus,      /* - */
};

struct Token {
    TokenKind kind = TokenKind::Eof;
    std::string text;
    uint64_t int_val = 0;
    bool is_hex = false;
    SourceLoc loc{};
};

class Lexer {
public:
    Lexer(std::string source, std::string filename);

    bool tokenize(std::vector<Token>& out_tokens, std::vector<Diagnostic>& diags);

private:
    char peek(size_t offset = 0) const noexcept;
    char advance() noexcept;
    void skip_whitespace_and_comments(std::vector<Diagnostic>& diags);

    std::string source_;
    std::string filename_;
    size_t pos_ = 0;
    uint32_t line_ = 1;
    uint32_t col_ = 1;
};

} /* namespace scarlett::xdl::compiler */

#endif /* SCARLETT_XDL_LEXER_HPP */
