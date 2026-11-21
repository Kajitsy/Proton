#ifndef SCARLETT_XDL_CODEGEN_DEF_HPP
#define SCARLETT_XDL_CODEGEN_DEF_HPP

#include <cstdint>
#include <string>

#include "ast.hpp"

namespace scarlett::xdl::compiler {

std::string generate_def_file(const Module& mod, VersionSpec target_abi, uint32_t target_flags);

} /* namespace scarlett::xdl::compiler */

#endif /* SCARLETT_XDL_CODEGEN_DEF_HPP */
