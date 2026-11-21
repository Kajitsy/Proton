#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ast.hpp"
#include "codegen_cpp.hpp"
#include "codegen_def.hpp"
#include "layout.hpp"
#include "lexer.hpp"
#include "parser.hpp"

namespace {

using namespace scarlett::xdl::compiler;

void print_usage(std::ostream& os) {
    os << "scarlett-xdl — Proton-Scarlett Game OS (2020..2026) IDL Compiler\n"
       << "Usage:\n"
       << "  scarlett-xdl <input.xdl> [options]\n\n"
       << "Options:\n"
       << "  --out-dir <dir>              Output directory (default: .)\n"
       << "  --emit-header <path>         Explicit C++20 header output path\n"
       << "  --emit-def <path>            Explicit PE64 .def output path\n"
       << "  --target-abi <M.m.B.R|hex>   Target ABI version for .def filtering\n"
       << "  --console-model <model>      anaconda | lockhart | devkit (default: anaconda)\n"
       << "  --check-only                 Parse and validate without writing outputs\n"
       << "  --dump-ast                   Print parsed module summary\n"
       << "  --version                    Print scarlett-xdl version\n"
       << "  --help                       Show this help message\n";
}

bool read_file_text(const std::filesystem::path& path, std::string& out) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool write_file_text(const std::filesystem::path& path, const std::string& content) {
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            std::cerr << "scarlett-xdl: failed to create directory "
                      << path.parent_path() << ": " << ec.message() << "\n";
            return false;
        }
    }
    std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "scarlett-xdl: failed to open output file " << path << "\n";
        return false;
    }
    out << content;
    return out.good();
}

std::string strip_dll_extension(const std::string& dll_name) {
    if (dll_name.size() > 4) {
        std::string tail = dll_name.substr(dll_name.size() - 4);
        if (tail == ".dll" || tail == ".DLL") {
            return dll_name.substr(0, dll_name.size() - 4);
        }
    }
    return dll_name;
}

void dump_ast_summary(const Module& mod, const SemanticContext& ctx) {
    std::cout << "module " << mod.name << "\n"
              << "  dll: " << mod.dll_name << "\n"
              << "  namespace: " << mod.cpp_namespace << "\n"
              << "  default_abi: " << mod.default_abi.to_string() << "\n"
              << "  abi_range: " << mod.abi_year_min << ".." << mod.abi_year_max << "\n"
              << "  consts: " << mod.consts.size() << "\n"
              << "  typedefs: " << mod.typedefs.size() << "\n"
              << "  enums: " << mod.enums.size() << "\n"
              << "  bitfields: " << mod.bitfields.size() << "\n"
              << "  structs: " << mod.structs.size() << "\n"
              << "  interfaces: " << mod.interfaces.size() << "\n"
              << "  exports: " << mod.exports.size() << "\n";
    for (const auto& iface : mod.interfaces) {
        const auto* ri = ctx.find_resolved_interface(iface.name);
        std::cout << "    interface " << iface.name
                  << " slots=" << (ri ? ri->total_slots : 0u);
        if (iface.attrs.parsed_uuid.has_value()) {
            std::cout << " uuid={" << iface.attrs.parsed_uuid->canonical << "}";
        } else if (iface.attrs.no_uuid) {
            std::cout << " [no_uuid]";
        }
        std::cout << "\n";
    }
    for (const auto& ex : mod.exports) {
        std::cout << "    export " << ex.name;
        if (ex.attrs.ordinal.has_value()) {
            std::cout << " @" << *ex.attrs.ordinal;
        }
        if (ex.attrs.unimplemented) {
            std::cout << " [unimplemented]";
        }
        std::cout << "\n";
    }
}

} /* namespace */

int main(int argc, char** argv) {
    std::string input_path;
    std::filesystem::path out_dir = ".";
    std::filesystem::path emit_header_path;
    std::filesystem::path emit_def_path;
    bool has_target_abi = false;
    VersionSpec target_abi{10, 0, 26200, 0};
    uint32_t target_flags = kCompFlagAnaconda | kCompFlagRaytracing | kCompFlagMeshShaders;
    bool check_only = false;
    bool dump_ast = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(std::cout);
            return EXIT_SUCCESS;
        }
        if (arg == "--version") {
            std::cout << "scarlett-xdl 11.0-100 (Game OS 2020..2026)\n";
            return EXIT_SUCCESS;
        }
        if (arg == "--check-only") {
            check_only = true;
            continue;
        }
        if (arg == "--dump-ast") {
            dump_ast = true;
            continue;
        }
        if (arg == "--out-dir") {
            if (i + 1 >= argc) {
                std::cerr << "scarlett-xdl: missing value for --out-dir\n";
                return EXIT_FAILURE;
            }
            out_dir = argv[++i];
            continue;
        }
        if (arg == "--emit-header") {
            if (i + 1 >= argc) {
                std::cerr << "scarlett-xdl: missing value for --emit-header\n";
                return EXIT_FAILURE;
            }
            emit_header_path = argv[++i];
            continue;
        }
        if (arg == "--emit-def") {
            if (i + 1 >= argc) {
                std::cerr << "scarlett-xdl: missing value for --emit-def\n";
                return EXIT_FAILURE;
            }
            emit_def_path = argv[++i];
            continue;
        }
        if (arg == "--target-abi") {
            if (i + 1 >= argc) {
                std::cerr << "scarlett-xdl: missing value for --target-abi\n";
                return EXIT_FAILURE;
            }
            std::string vstr = argv[++i];
            if (!parse_version_string(vstr, target_abi)) {
                std::cerr << "scarlett-xdl: invalid --target-abi value: " << vstr << "\n";
                return EXIT_FAILURE;
            }
            has_target_abi = true;
            continue;
        }
        if (arg == "--console-model") {
            if (i + 1 >= argc) {
                std::cerr << "scarlett-xdl: missing value for --console-model\n";
                return EXIT_FAILURE;
            }
            std::string m = argv[++i];
            target_flags &= ~kCompFlagModelMask;
            if (m == "anaconda") {
                target_flags |= kCompFlagAnaconda;
            } else if (m == "lockhart") {
                target_flags |= kCompFlagLockhart;
            } else if (m == "devkit") {
                target_flags |= kCompFlagDevKit;
            } else {
                std::cerr << "scarlett-xdl: unknown --console-model '" << m
                          << "' (expected anaconda|lockhart|devkit)\n";
                return EXIT_FAILURE;
            }
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "scarlett-xdl: unknown option '" << arg << "'\n";
            return EXIT_FAILURE;
        }
        if (input_path.empty()) {
            input_path = arg;
        } else {
            std::cerr << "scarlett-xdl: multiple input files are not supported: " << arg << "\n";
            return EXIT_FAILURE;
        }
    }

    if (input_path.empty()) {
        print_usage(std::cerr);
        return EXIT_FAILURE;
    }

    std::string source;
    if (!read_file_text(input_path, source)) {
        std::cerr << "scarlett-xdl: failed to read input file '" << input_path << "'\n";
        return EXIT_FAILURE;
    }

    std::vector<Diagnostic> diags;
    std::vector<Token> tokens;
    Lexer lexer(std::move(source), input_path);
    if (!lexer.tokenize(tokens, diags)) {
        for (const auto& d : diags) {
            std::cerr << d.format() << "\n";
        }
        return EXIT_FAILURE;
    }

    Module mod{};
    Parser parser(tokens, input_path);
    if (!parser.parse_module(mod, diags)) {
        for (const auto& d : diags) {
            std::cerr << d.format() << "\n";
        }
        return EXIT_FAILURE;
    }

    SemanticContext sem(mod);
    if (!sem.validate(diags)) {
        for (const auto& d : diags) {
            std::cerr << d.format() << "\n";
        }
        return EXIT_FAILURE;
    }

    if (dump_ast) {
        dump_ast_summary(mod, sem);
    }

    if (check_only) {
        return EXIT_SUCCESS;
    }

    if (!has_target_abi) {
        target_abi = mod.default_abi;
    }

    if (emit_header_path.empty()) {
        emit_header_path = out_dir / (mod.name + "_xdl.hpp");
    }
    if (emit_def_path.empty()) {
        emit_def_path = out_dir / (strip_dll_extension(mod.dll_name) + ".def");
    }

    const std::string header_src = generate_cpp_header(mod, sem);
    const std::string def_src = generate_def_file(mod, target_abi, target_flags);

    if (!write_file_text(emit_header_path, header_src)) {
        return EXIT_FAILURE;
    }
    if (!write_file_text(emit_def_path, def_src)) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
