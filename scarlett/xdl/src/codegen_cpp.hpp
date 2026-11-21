#ifndef SCARLETT_XDL_CODEGEN_CPP_HPP
#define SCARLETT_XDL_CODEGEN_CPP_HPP

#include <string>

#include "ast.hpp"
#include "layout.hpp"

namespace scarlett::xdl::compiler {

std::string generate_cpp_header(const Module& mod, const SemanticContext& ctx);

} /* namespace scarlett::xdl::compiler */

#endif /* SCARLETT_XDL_CODEGEN_CPP_HPP */
