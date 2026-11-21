/*
 * Proton-Scarlett — XDL Parser, Validator & Codegen Integration Tests
 *
 * Tests `scarlett-xdl` CLI execution across all 6 canonical Game OS schemas,
 * verifies generated `.def` export files, tests negative schema validation
 * diagnostics, and verifies positive codegen of all 8 XDL attributes.
 */

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef SCARLETT_XDL_BIN
#error "SCARLETT_XDL_BIN compile definition is required"
#endif
#ifndef SCARLETT_XDL_SCHEMAS_DIR
#error "SCARLETT_XDL_SCHEMAS_DIR compile definition is required"
#endif
#ifndef SCARLETT_XDL_GEN_DIR
#error "SCARLETT_XDL_GEN_DIR compile definition is required"
#endif
#ifndef SCARLETT_XDL_TMP_DIR
#error "SCARLETT_XDL_TMP_DIR compile definition is required"
#endif

namespace {

int g_pass_count = 0;
int g_fail_count = 0;

void report_check(bool ok, const char* expr, const char* file, int line, const std::string& detail = {}) {
    if (ok) {
        ++g_pass_count;
    } else {
        ++g_fail_count;
        std::fprintf(stderr, "[FAIL] %s:%d: %s", file, line, expr);
        if (!detail.empty()) {
            std::fprintf(stderr, " (%s)", detail.c_str());
        }
        std::fprintf(stderr, "\n");
    }
}

#define CHECK(expr) report_check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
#define CHECK_MSG(expr, msg) report_check(static_cast<bool>(expr), #expr, __FILE__, __LINE__, (msg))
#define REQUIRE(expr)                                                           \
    do {                                                                        \
        const bool ok_ = static_cast<bool>(expr);                               \
        report_check(ok_, #expr, __FILE__, __LINE__);                           \
        if (!ok_) {                                                             \
            return false;                                                       \
        }                                                                       \
    } while (0)

struct ProcessResult {
    int exit_code = -1;
    bool exited_normally = false;
    std::string output;
};

ProcessResult run_xdl_cli(const std::vector<std::string>& args) {
    ProcessResult res;
    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) != 0) {
        res.output = std::string("pipe() failed: ") + std::strerror(errno);
        return res;
    }

    std::fflush(nullptr);
    const pid_t pid = fork();
    if (pid < 0) {
        res.output = std::string("fork() failed: ") + std::strerror(errno);
        close(pipefd[0]);
        close(pipefd[1]);
        return res;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        std::vector<char*> argv;
        argv.reserve(args.size() + 2);
        std::string bin_str = SCARLETT_XDL_BIN;
        argv.push_back(bin_str.data());
        std::vector<std::string> mutable_args = args;
        for (std::string& a : mutable_args) {
            argv.push_back(a.data());
        }
        argv.push_back(nullptr);

        execv(SCARLETT_XDL_BIN, argv.data());
        _exit(127);
    }

    close(pipefd[1]);
    char buf[4096];
    for (;;) {
        const ssize_t n = read(pipefd[0], buf, sizeof(buf));
        if (n > 0) {
            res.output.append(buf, static_cast<size_t>(n));
        } else if (n == 0) {
            break;
        } else if (errno != EINTR) {
            break;
        }
    }
    close(pipefd[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return res;
        }
    }

    if (WIFEXITED(status)) {
        res.exited_normally = true;
        res.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        res.exited_normally = false;
        res.exit_code = 128 + WTERMSIG(status);
    }
    return res;
}

bool read_text_file(const std::filesystem::path& path, std::string& out_text) {
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        return false;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    out_text = oss.str();
    return true;
}

bool write_text_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream ofs(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        return false;
    }
    ofs.write(text.data(), static_cast<std::streamsize>(text.size()));
    return ofs.good();
}

bool contains_substr(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool test_canonical_schemas_cli() {
    const std::filesystem::path schemas_dir(SCARLETT_XDL_SCHEMAS_DIR);
    const std::vector<std::string> schema_files = {
        "d3d12_xs.xdl",
        "dstorage_xs.xdl",
        "xg_xs.xdl",
        "xgameruntime.xdl",
        "xaudio2_9_xs.xdl",
        "gameinput_xs.xdl",
    };

    for (const std::string& name : schema_files) {
        const std::filesystem::path schema_path = schemas_dir / name;
        REQUIRE(std::filesystem::exists(schema_path));

        const ProcessResult check_res = run_xdl_cli({schema_path.string(), "--check-only"});
        CHECK_MSG(check_res.exited_normally && check_res.exit_code == 0,
                  name + " --check-only failed: " + check_res.output);

        const ProcessResult dump_res = run_xdl_cli({schema_path.string(), "--check-only", "--dump-ast"});
        CHECK_MSG(dump_res.exited_normally && dump_res.exit_code == 0,
                  name + " --dump-ast failed: " + dump_res.output);
        CHECK_MSG(!dump_res.output.empty(), name + " --dump-ast produced empty output");
    }

    std::printf("[PASS] canonical_schemas_cli (6/6 schemas --check-only & --dump-ast)\n");
    return true;
}

bool test_generated_def_files() {
    const std::filesystem::path gen_dir(SCARLETT_XDL_GEN_DIR);

    struct DefExpectation {
        const char* filename;
        std::vector<std::string> required_tokens;
    };

    const std::vector<DefExpectation> expectations = {
        {"xaudio2_9.def", {"LIBRARY \"xaudio2_9.dll\"", "EXPORTS", "XAudio2Create @1"}},
        {"GameInput.def", {"LIBRARY \"GameInput.dll\"", "EXPORTS", "GameInputCreate @1"}},
        {"d3d12_xs.def", {"LIBRARY \"d3d12_xs.dll\"", "EXPORTS", "D3D12XboxCreateDevice @1"}},
        {"xgameruntime.def", {"LIBRARY \"xgameruntime.dll\"", "EXPORTS", "QueryApiImpl"}},
        {"dstorage_xs.def", {"LIBRARY \"dstorage_xs.dll\"", "EXPORTS", "DStorageGetFactory"}},
        {"xg_xs.def", {"LIBRARY \"xg_xs.dll\"", "EXPORTS", "XGCreateTexture2DComputer"}},
    };

    for (const DefExpectation& exp : expectations) {
        const std::filesystem::path def_path = gen_dir / exp.filename;
        std::string content;
        REQUIRE(read_text_file(def_path, content));
        CHECK_MSG(!content.empty(), std::string(exp.filename) + " is empty");

        for (const std::string& tok : exp.required_tokens) {
            CHECK_MSG(contains_substr(content, tok),
                      std::string(exp.filename) + " missing token: " + tok);
        }
    }

    std::printf("[PASS] generated_def_files (6/6 .def files & ordinal exports)\n");
    return true;
}

bool test_negative_validation() {
    const std::filesystem::path tmp_dir(SCARLETT_XDL_TMP_DIR);
    std::error_code ec;
    std::filesystem::create_directories(tmp_dir, ec);
    REQUIRE(!ec);

    struct NegativeCase {
        const char* filename;
        const char* source;
    };

    const std::vector<NegativeCase> cases = {
        {
            "neg_invalid_uuid.xdl",
            R"(module neg_invalid_uuid {
    dll = "neg_invalid_uuid.dll";
    namespace = "scarlett::xdl::neg_invalid_uuid";
    default_abi = 10.0.26200.0;
    abi_range = 2020..2026;
};
[uuid("not-a-guid")]
interface IBadUuid {
    hresult Ping();
};
)"
        },
        {
            "neg_uuid_and_no_uuid.xdl",
            R"(module neg_uuid_and_no_uuid {
    dll = "neg_both.dll";
    namespace = "scarlett::xdl::neg_both";
    default_abi = 10.0.26200.0;
    abi_range = 2020..2026;
};
[uuid("11111111-2222-3333-4444-555555555555"), no_uuid]
interface IConflictingUuid {
    hresult Ping();
};
)"
        },
        {
            "neg_duplicate_ordinal.xdl",
            R"(module neg_duplicate_ordinal {
    dll = "neg_ord.dll";
    namespace = "scarlett::xdl::neg_ord";
    default_abi = 10.0.26200.0;
    abi_range = 2020..2026;
};
[ordinal(1)] export hresult AlphaFn();
[ordinal(1)] export hresult BetaFn();
)"
        },
        {
            "neg_bitfield_63_bits.xdl",
            R"(module neg_bitfield_63_bits {
    dll = "neg_bf.dll";
    namespace = "scarlett::xdl::neg_bf";
    default_abi = 10.0.26200.0;
    abi_range = 2020..2026;
};
bitfield BadBitSum : uint64_t {
    PartA : 31;
    PartB : 32;
};
)"
        },
        {
            "neg_slot_conflict.xdl",
            R"(module neg_slot_conflict {
    dll = "neg_slot_conflict.dll";
    namespace = "scarlett::xdl::neg_slot_conflict";
    default_abi = 10.0.26200.0;
    abi_range = 2020..2026;
};
[no_uuid, slots(4)]
interface ISlotConflict {
    hresult M0();
    hresult M1();
    [slot(2)] hresult M2First();
    [slot(2)] hresult M2Second();
};
)"
        },
        {
            "neg_slot_oob.xdl",
            R"(module neg_slot_oob {
    dll = "neg_slot_oob.dll";
    namespace = "scarlett::xdl::neg_slot_oob";
    default_abi = 10.0.26200.0;
    abi_range = 2020..2026;
};
[no_uuid, slots(4)]
interface ISlotOob {
    hresult M0();
    [slot(4)] hresult M4OutOfRange();
};
)"
        },
        {
            "neg_bad_size_assert.xdl",
            R"(module neg_bad_size_assert {
    dll = "neg_size.dll";
    namespace = "scarlett::xdl::neg_size";
    default_abi = 10.0.26200.0;
    abi_range = 2020..2026;
};
[size(12)]
struct MismatchedSizeStruct {
    uint32_t FieldA;
    uint32_t FieldB;
};
)"
        },
    };

    for (const NegativeCase& nc : cases) {
        const std::filesystem::path xdl_path = tmp_dir / nc.filename;
        REQUIRE(write_text_file(xdl_path, nc.source));

        const ProcessResult res = run_xdl_cli({xdl_path.string(), "--check-only"});
        CHECK_MSG(res.exited_normally && res.exit_code != 0,
                  std::string(nc.filename) + " unexpectedly succeeded (exit=" +
                      std::to_string(res.exit_code) + ")");
        CHECK_MSG(!res.output.empty(),
                  std::string(nc.filename) + " failed without diagnostic message");
    }

    std::printf("[PASS] negative_validation (7/7 invalid schemas rejected)\n");
    return true;
}

bool test_positive_all_8_attributes() {
    const std::filesystem::path tmp_dir(SCARLETT_XDL_TMP_DIR);
    std::error_code ec;
    std::filesystem::create_directories(tmp_dir, ec);
    REQUIRE(!ec);

    const std::filesystem::path xdl_path = tmp_dir / "pos_all8.xdl";
    const std::filesystem::path hpp_path = tmp_dir / "pos_all8_xdl.hpp";
    const std::filesystem::path def_path = tmp_dir / "pos_all8.def";

    const char* synthetic_schema = R"(module pos_all8 {
    dll = "pos_all8.dll";
    namespace = "scarlett::xdl::pos_all8";
    default_abi = 10.0.26200.0;
    abi_range = 2020..2026;
};

[force_abi(10.0.19041.0), size(8)]
struct FixedSnapshot {
    uint32_t Magic;
    uint32_t Flags;
};

struct VersionedRecord {
    uint32_t BaseHeader;
    [added(10.0.22621.1806)] uint32_t GxdkExtension;
    [removed(10.0.25398.0)] uint32_t DeprecatedField;
    [conditional("devkit")] uint64_t DevKitScratch;
};

[uuid("AABBCCDD-1122-3344-5566-778899AABBCC"), slots(6)]
interface IPosUuid {
    hresult QueryInterface(const guid* riid, void** ppv);
    uint32_t AddRef();
    uint32_t Release();
    [added(10.0.22621.1806)] hresult GxdkMethod(uint32_t val);
    [removed(10.0.25398.0)] hresult LegacyMethod();
    [unimplemented] hresult StubbedMethod(uint64_t token);
};

[no_uuid]
interface IPosFlat {
    hresult Execute(uint32_t op);
    [unimplemented] hresult UnimplementedFlat();
};

[ordinal(1), win64_abi]
export hresult PosPrimaryEntry(uint32_t flags);

[ordinal(2), unimplemented]
export hresult PosStubExport(void* ctx);

[added(10.0.22621.1806)]
export hresult PosGxdkExport(uint64_t handle);
)";

    REQUIRE(write_text_file(xdl_path, synthetic_schema));

    const ProcessResult gen_res = run_xdl_cli({
        xdl_path.string(),
        "--emit-header", hpp_path.string(),
        "--emit-def", def_path.string(),
        "--target-abi", "10.0.26200.0",
        "--console-model", "anaconda",
    });
    CHECK_MSG(gen_res.exited_normally && gen_res.exit_code == 0,
              "pos_all8 codegen failed: " + gen_res.output);

    const ProcessResult devkit_res = run_xdl_cli({
        xdl_path.string(),
        "--check-only",
        "--console-model", "devkit",
    });
    CHECK_MSG(devkit_res.exited_normally && devkit_res.exit_code == 0,
              "pos_all8 devkit check failed: " + devkit_res.output);

    std::string hpp_text;
    std::string def_text;
    REQUIRE(read_text_file(hpp_path, hpp_text));
    REQUIRE(read_text_file(def_path, def_text));

    CHECK(contains_substr(def_text, "LIBRARY \"pos_all8.dll\""));
    CHECK(contains_substr(def_text, "EXPORTS"));
    CHECK(contains_substr(def_text, "PosPrimaryEntry @1"));
    CHECK(contains_substr(def_text, "PosStubExport @2"));
    CHECK(contains_substr(def_text, "PosGxdkExport"));

    CHECK(contains_substr(hpp_text, "namespace scarlett::xdl::pos_all8"));
    CHECK(contains_substr(hpp_text, "kIID_IPosUuid"));
    CHECK(contains_substr(hpp_text, "kIPosFlatHasUuid"));
    CHECK(contains_substr(hpp_text, "IPosUuidVTable"));
    CHECK(contains_substr(hpp_text, "IPosUuidProxy"));
    CHECK(contains_substr(hpp_text, "Export_PosPrimaryEntry"));
    CHECK(contains_substr(hpp_text, "Export_PosStubExport"));
    CHECK(contains_substr(hpp_text, "SCARLETT_STUB_FLAG_REMOVED_IN_ABI"));
    CHECK(contains_substr(hpp_text, "SCARLETT_STUB_FLAG_ORDINAL_EXPORT"));
    CHECK(contains_substr(hpp_text, "static_assert"));

    std::printf("[PASS] positive_all_8_attributes (header & .def generated)\n");
    return true;
}

} /* namespace */

int main() {
    test_canonical_schemas_cli();
    test_generated_def_files();
    test_negative_validation();
    test_positive_all_8_attributes();

    std::printf("test_xdl_parser_codegen summary: %d passed, %d failed\n",
                g_pass_count, g_fail_count);
    return g_fail_count == 0 ? 0 : 1;
}
