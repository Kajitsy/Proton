#include "lexer.hpp"

#include <cctype>
#include <cstdlib>

namespace scarlett::xdl::compiler {

Lexer::Lexer(std::string source, std::string filename)
    : source_(std::move(source)), filename_(std::move(filename)) {}

char Lexer::peek(size_t offset) const noexcept {
    if (pos_ + offset >= source_.size()) {
        return '\0';
    }
    return source_[pos_ + offset];
}

char Lexer::advance() noexcept {
    if (pos_ >= source_.size()) {
        return '\0';
    }
    char c = source_[pos_++];
    if (c == '\n') {
        ++line_;
        col_ = 1;
    } else {
        ++col_;
    }
    return c;
}

void Lexer::skip_whitespace_and_comments(std::vector<Diagnostic>& diags) {
    while (pos_ < source_.size()) {
        char c = peek();
        if (std::isspace(static_cast<unsigned char>(c))) {
            advance();
            continue;
        }
        if (c == '/' && peek(1) == '/') {
            while (pos_ < source_.size() && peek() != '\n') {
                advance();
            }
            continue;
        }
        if (c == '/' && peek(1) == '*') {
            SourceLoc start{line_, col_};
            advance();
            advance();
            bool closed = false;
            while (pos_ < source_.size()) {
                if (peek() == '*' && peek(1) == '/') {
                    advance();
                    advance();
                    closed = true;
                    break;
                }
                advance();
            }
            if (!closed) {
                diags.push_back(Diagnostic{filename_, start, "unterminated block comment"});
            }
            continue;
        }
        break;
    }
}

bool Lexer::tokenize(std::vector<Token>& out_tokens, std::vector<Diagnostic>& diags) {
    out_tokens.clear();
    const size_t initial_diags = diags.size();

    while (true) {
        skip_whitespace_and_comments(diags);
        if (pos_ >= source_.size()) {
            out_tokens.push_back(Token{TokenKind::Eof, "", 0, false, SourceLoc{line_, col_}});
            break;
        }

        SourceLoc start{line_, col_};
        char c = peek();

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::string ident;
            while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
                ident.push_back(advance());
            }
            out_tokens.push_back(Token{TokenKind::Ident, std::move(ident), 0, false, start});
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::string num;
            bool is_hex = false;
            if (c == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
                is_hex = true;
                num.push_back(advance());
                num.push_back(advance());
                while (std::isxdigit(static_cast<unsigned char>(peek()))) {
                    num.push_back(advance());
                }
                if (num.size() <= 2) {
                    diags.push_back(Diagnostic{filename_, start, "invalid hexadecimal literal"});
                    continue;
                }
            } else {
                while (std::isdigit(static_cast<unsigned char>(peek()))) {
                    num.push_back(advance());
                }
            }
            /* Skip optional integer suffixes like u / U / ul / ull */
            while (peek() == 'u' || peek() == 'U' || peek() == 'l' || peek() == 'L') {
                advance();
            }
            char* endptr = nullptr;
            unsigned long long parsed = std::strtoull(num.c_str(), &endptr, is_hex ? 16 : 10);
            if (endptr == num.c_str()) {
                diags.push_back(Diagnostic{filename_, start, "failed to parse integer literal: " + num});
                continue;
            }
            out_tokens.push_back(Token{TokenKind::IntLit, std::move(num),
                                       static_cast<uint64_t>(parsed), is_hex, start});
            continue;
        }

        if (c == '"') {
            advance();
            std::string str;
            bool closed = false;
            while (pos_ < source_.size()) {
                char sc = advance();
                if (sc == '"') {
                    closed = true;
                    break;
                }
                if (sc == '\\') {
                    if (pos_ >= source_.size()) {
                        break;
                    }
                    char esc = advance();
                    switch (esc) {
                        case '"':  str.push_back('"'); break;
                        case '\\': str.push_back('\\'); break;
                        case 'n':  str.push_back('\n'); break;
                        case 't':  str.push_back('\t'); break;
                        default:   str.push_back(esc); break;
                    }
                } else {
                    str.push_back(sc);
                }
            }
            if (!closed) {
                diags.push_back(Diagnostic{filename_, start, "unterminated string literal"});
                break;
            }
            out_tokens.push_back(Token{TokenKind::StringLit, std::move(str), 0, false, start});
            continue;
        }

        advance();
        switch (c) {
            case '{': out_tokens.push_back(Token{TokenKind::LBrace, "{", 0, false, start}); break;
            case '}': out_tokens.push_back(Token{TokenKind::RBrace, "}", 0, false, start}); break;
            case '[': out_tokens.push_back(Token{TokenKind::LBracket, "[", 0, false, start}); break;
            case ']': out_tokens.push_back(Token{TokenKind::RBracket, "]", 0, false, start}); break;
            case '(': out_tokens.push_back(Token{TokenKind::LParen, "(", 0, false, start}); break;
            case ')': out_tokens.push_back(Token{TokenKind::RParen, ")", 0, false, start}); break;
            case ';': out_tokens.push_back(Token{TokenKind::Semicolon, ";", 0, false, start}); break;
            case ':': out_tokens.push_back(Token{TokenKind::Colon, ":", 0, false, start}); break;
            case ',': out_tokens.push_back(Token{TokenKind::Comma, ",", 0, false, start}); break;
            case '=': out_tokens.push_back(Token{TokenKind::Equals, "=", 0, false, start}); break;
            case '*': out_tokens.push_back(Token{TokenKind::Star, "*", 0, false, start}); break;
            case '-': out_tokens.push_back(Token{TokenKind::Minus, "-", 0, false, start}); break;
            case '.':
                if (peek() == '.') {
                    advance();
                    out_tokens.push_back(Token{TokenKind::DotDot, "..", 0, false, start});
                } else {
                    out_tokens.push_back(Token{TokenKind::Dot, ".", 0, false, start});
                }
                break;
            default:
                diags.push_back(Diagnostic{
                    filename_, start,
                    std::string("unexpected character '") + c + "'"});
                break;
        }
    }

    return diags.size() == initial_diags;
}

} /* namespace scarlett::xdl::compiler */
