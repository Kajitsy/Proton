/*
 * Proton-Scarlett — Exhaustive C++20 Unit Tests for XMem / KernelX / UMA Bridge
 *
 * Self-contained verification suite (zero external test frameworks).
 * Exercises the public C API (xmem_api.h -> PE/unixlib bridge -> xmem_unix.so)
 * across all 11 mandatory contract groups.
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#if defined(__has_include)
#  if __has_include("xmem_api.h")
#    include "xmem_api.h"
#    include "xmem_unix_abi.h"
#    include "scarlett_errors.h"
#  else
#    include "../xmem_api.h"
#    include "../xmem_unix_abi.h"
#    include "../../include/scarlett_errors.h"
#  endif
#else
#  include "xmem_api.h"
#  include "xmem_unix_abi.h"
#  include "scarlett_errors.h"
#endif

namespace {

int g_total_failures = 0;
int g_section_failures = 0;

void record_failure(const char* expr, const char* file, int line, const std::string& detail) {
    ++g_total_failures;
    ++g_section_failures;
    std::cerr << "  [FAIL] " << file << ":" << line << " check `" << expr << "` failed";
    if (!detail.empty()) {
        std::cerr << " (" << detail << ")";
    }
    std::cerr << "\n";
}

template <typename T>
std::string to_hex_str(T value) {
    std::ostringstream oss;
    oss << "0x" << std::hex << static_cast<uint64_t>(value);
    return oss.str();
}

std::string hr_detail(scarlett_hresult_t actual, scarlett_hresult_t expected) {
    std::ostringstream oss;
    oss << "actual=" << scarlett_error_name(actual) << " (" << to_hex_str(static_cast<uint32_t>(actual))
        << "), expected=" << scarlett_error_name(expected) << " (" << to_hex_str(static_cast<uint32_t>(expected)) << ")";
    return oss.str();
}

#define CHECK(expr)                                                              \
    do {                                                                         \
        if (!(expr)) {                                                           \
            record_failure(#expr, __FILE__, __LINE__, "");                       \
        }                                                                        \
    } while (false)

#define CHECK_MSG(expr, msg_expr)                                                \
    do {                                                                         \
        if (!(expr)) {                                                           \
            record_failure(#expr, __FILE__, __LINE__, (msg_expr));               \
        }                                                                        \
    } while (false)

#define CHECK_HR(actual_expr, expected_hr)                                       \
    do {                                                                         \
        const scarlett_hresult_t _act_hr = (actual_expr);                        \
        const scarlett_hresult_t _exp_hr = (expected_hr);                        \
        if (_act_hr != _exp_hr) {                                                \
            record_failure(#actual_expr " == " #expected_hr, __FILE__, __LINE__, \
                           hr_detail(_act_hr, _exp_hr));                         \
        }                                                                        \
    } while (false)

#define REQUIRE(expr)                                                            \
    do {                                                                         \
        if (!(expr)) {                                                           \
            record_failure(#expr, __FILE__, __LINE__, "fatal section precondition"); \
            return false;                                                        \
        }                                                                        \
    } while (false)

#define REQUIRE_HR(actual_expr, expected_hr)                                     \
    do {                                                                         \
        const scarlett_hresult_t _act_hr = (actual_expr);                        \
        const scarlett_hresult_t _exp_hr = (expected_hr);                        \
        if (_act_hr != _exp_hr) {                                                \
            record_failure(#actual_expr " == " #expected_hr, __FILE__, __LINE__, \
                           hr_detail(_act_hr, _exp_hr));                         \
            return false;                                                        \
        }                                                                        \
    } while (false)

template <typename Fn>
bool run_section(int index, const char* name, Fn&& fn) {
    g_section_failures = 0;
    const bool ok = fn();
    if (ok && g_section_failures == 0) {
        std::cout << "[PASS] Group " << index << ": " << name << "\n";
        return true;
    }
    std::cout << "[FAIL] Group " << index << ": " << name
              << " (failures=" << g_section_failures << ")\n";
    return false;
}

uint8_t pattern_byte(uint64_t page_index, uint64_t byte_offset, uint8_t salt) {
    return static_cast<uint8_t>(((page_index * 37u) + (byte_offset * 13u) + salt) & 0xFFu);
}

void fill_page_pattern(uint64_t va, uint64_t page_index, uint64_t page_size, uint8_t salt) {
    auto* ptr = reinterpret_cast<volatile uint8_t*>(static_cast<uintptr_t>(va));
    for (uint64_t i = 0; i < page_size; ++i) {
        ptr[i] = pattern_byte(page_index, i, salt);
    }
}

bool verify_page_pattern(uint64_t va, uint64_t page_index, uint64_t page_size, uint8_t salt,
                         uint64_t* out_bad_offset = nullptr) {
    const auto* ptr = reinterpret_cast<const volatile uint8_t*>(static_cast<uintptr_t>(va));
    for (uint64_t i = 0; i < page_size; ++i) {
        if (ptr[i] != pattern_byte(page_index, i, salt)) {
            if (out_bad_offset != nullptr) {
                *out_bad_offset = i;
            }
            return false;
        }
    }
    return true;
}

// ============================================================================
// Group 1: WINE_UNIX_CALL handshake
// ============================================================================
bool test_group_1_unix_handshake() {
    scarlett_xmem_set_unix_call_bridge(&scarlett_xmem_unix_call);
    CHECK(scarlett_xmem_get_unix_call_bridge() == &scarlett_xmem_unix_call);

    // Valid XMemInitialize
    REQUIRE_HR(XMemInitialize(SCARLETT_MODEL_ANACONDA, 0u), SCARLETT_OK);

    // Valid direct SCARLETT_XMEM_UNIX_INIT handshake
    scarlett_unix_init_params_t init_ok{};
    init_ok.abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;
    init_ok.module_id = SCARLETT_UNIX_MOD_XMEM;
    init_ok.console_model = SCARLETT_MODEL_ANACONDA;
    init_ok.strict_mode = 0u;
    init_ok.shader_mode = SCARLETT_SHADER_MODE_ADAPTIVE;
    init_ok.net_mode = SCARLETT_NET_MODE_OFFLINE;
    init_ok.out_status = SCARLETT_E_FAIL;
    const int32_t rc_ok = scarlett_xmem_unix_call(SCARLETT_XMEM_UNIX_INIT, &init_ok);
    CHECK(scarlett_succeeded(rc_ok));
    CHECK_HR(init_ok.out_status, SCARLETT_OK);
    CHECK(init_ok.out_unix_abi_version == SCARLETT_UNIX_CALL_ABI_VERSION);

    // Mismatched ABI version -> SCARLETT_ERR_ABI_VERSION_MISMATCH
    scarlett_unix_init_params_t init_bad{};
    init_bad.abi_version = SCARLETT_UNIX_CALL_ABI_VERSION + 77u;
    init_bad.module_id = SCARLETT_UNIX_MOD_XMEM;
    init_bad.console_model = SCARLETT_MODEL_ANACONDA;
    init_bad.strict_mode = 0u;
    init_bad.shader_mode = SCARLETT_SHADER_MODE_ADAPTIVE;
    init_bad.net_mode = SCARLETT_NET_MODE_OFFLINE;
    init_bad.out_status = SCARLETT_OK;
    const int32_t rc_bad = scarlett_xmem_unix_call(SCARLETT_XMEM_UNIX_INIT, &init_bad);
    CHECK_MSG((rc_bad == SCARLETT_ERR_ABI_VERSION_MISMATCH) ||
                  (init_bad.out_status == SCARLETT_ERR_ABI_VERSION_MISMATCH),
              hr_detail(rc_bad != 0 ? rc_bad : init_bad.out_status,
                        SCARLETT_ERR_ABI_VERSION_MISMATCH));
    CHECK(rc_bad == SCARLETT_ERR_ABI_VERSION_MISMATCH || rc_bad == SCARLETT_OK);
    CHECK(init_bad.out_status == SCARLETT_ERR_ABI_VERSION_MISMATCH ||
          init_bad.out_status == SCARLETT_OK);

    // Unknown opcode -> SCARLETT_ERR_UNIX_CALL_INVALID_OPCODE
    const int32_t rc_invalid_op =
        scarlett_xmem_unix_call(SCARLETT_XMEM_UNIX_EXT_OP_COUNT + 100u, nullptr);
    CHECK_HR(rc_invalid_op, SCARLETT_ERR_UNIX_CALL_INVALID_OPCODE);

    const int32_t rc_high_op = scarlett_xmem_unix_call(0xDEADFFFFu, nullptr);
    CHECK_HR(rc_high_op, SCARLETT_ERR_UNIX_CALL_INVALID_OPCODE);

    // Null payload on INIT must fail cleanly
    const int32_t rc_null = scarlett_xmem_unix_call(SCARLETT_XMEM_UNIX_INIT, nullptr);
    CHECK(scarlett_failed(rc_null));

    // Re-initialize cleanly
    REQUIRE_HR(XMemInitialize(SCARLETT_MODEL_ANACONDA, 0u), SCARLETT_OK);
    return true;
}

// ============================================================================
// Group 2: 64 KiB physical pages, virtual reservations, map/unmap, STILL_MAPPED
// ============================================================================
bool test_group_2_phys_pages_and_reservations() {
    REQUIRE_HR(XMemSetConsoleModel(SCARLETT_MODEL_ANACONDA, 1u), SCARLETT_OK);

    constexpr uint64_t kPageCount = 4;
    constexpr uint64_t kTotalSize = kPageCount * XMEM_PAGE_SIZE_64KB;
    std::array<uint64_t, kPageCount> pfns{};
    uint64_t alloc_id = 0;

    REQUIRE_HR(XMemAllocatePhysicalPages(
                   kPageCount, XMEM_PAGE_SIZE_64KB, SCARLETT_MEM_CACHE_WRITEBACK,
                   XMEM_PHYS_FLAG_GRAPHICS, pfns.data(), &alloc_id),
               SCARLETT_OK);
    CHECK(alloc_id != 0u);
    for (uint64_t i = 0; i < kPageCount; ++i) {
        for (uint64_t j = i + 1; j < kPageCount; ++j) {
            CHECK(pfns[i] != pfns[j]);
        }
    }

    uint64_t va = 0;
    REQUIRE_HR(XMemVirtualAlloc(
                   0u, kTotalSize, XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                   XMEM_PAGE_READWRITE, &va),
               SCARLETT_OK);
    CHECK(va != 0u);
    CHECK((va % XMEM_PAGE_SIZE_64KB) == 0u);
    CHECK(scarlett_addr_is_graphics(va, kTotalSize));

    REQUIRE_HR(XMemMapPhysicalPages(
                   va, kPageCount, XMEM_PAGE_SIZE_64KB, pfns.data(),
                   SCARLETT_MEM_ACCESS_READWRITE, SCARLETT_MEM_CACHE_WRITEBACK),
               SCARLETT_OK);

    for (uint64_t i = 0; i < kPageCount; ++i) {
        fill_page_pattern(va + i * XMEM_PAGE_SIZE_64KB, i, XMEM_PAGE_SIZE_64KB, 0x3Au);
    }
    for (uint64_t i = 0; i < kPageCount; ++i) {
        uint64_t bad_off = 0;
        CHECK(verify_page_pattern(va + i * XMEM_PAGE_SIZE_64KB, i, XMEM_PAGE_SIZE_64KB, 0x3Au, &bad_off));
    }

    // Must fail with SCARLETT_ERR_XMEM_PAGES_STILL_MAPPED while mapped
    CHECK_HR(XMemFreePhysicalPages(kPageCount, XMEM_PAGE_SIZE_64KB, pfns.data()),
             SCARLETT_ERR_XMEM_PAGES_STILL_MAPPED);
    CHECK_HR(XMemFreePhysicalPages(1u, XMEM_PAGE_SIZE_64KB, &pfns[0]),
             SCARLETT_ERR_XMEM_PAGES_STILL_MAPPED);

    // Data remains readable after rejected free
    CHECK(verify_page_pattern(va, 0u, XMEM_PAGE_SIZE_64KB, 0x3Au));

    // Clean unmap -> free physical pages -> release virtual range
    REQUIRE_HR(XMemUnmapPhysicalPages(va, kPageCount, XMEM_PAGE_SIZE_64KB), SCARLETT_OK);
    REQUIRE_HR(XMemFreePhysicalPages(kPageCount, XMEM_PAGE_SIZE_64KB, pfns.data()), SCARLETT_OK);
    REQUIRE_HR(XMemVirtualFree(va, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);

    // Also test standard reserve+commit -> decommit -> release cycle
    uint64_t commit_va = 0;
    REQUIRE_HR(XMemVirtualAlloc(
                   0u, XMEM_PAGE_SIZE_64KB * 2u,
                   XMEM_MEM_RESERVE | XMEM_MEM_COMMIT | XMEM_MEM_TITLE,
                   XMEM_PAGE_READWRITE, &commit_va),
               SCARLETT_OK);
    CHECK(scarlett_addr_is_title(commit_va, XMEM_PAGE_SIZE_64KB * 2u));
    auto* cptr = reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(commit_va));
    cptr[0] = 0xCAFEBABEu;
    CHECK(cptr[0] == 0xCAFEBABEu);
    CHECK_HR(XMemVirtualFree(commit_va, XMEM_PAGE_SIZE_64KB, XMEM_MEM_DECOMMIT), SCARLETT_OK);
    CHECK_HR(XMemVirtualFree(commit_va, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);

    return true;
}

// ============================================================================
// Group 3: 2 MiB groups (32 x 64 KiB) via Map & Scatter (2MB_GROUP + permutation)
// ============================================================================
bool test_group_3_2mb_groups_and_scatter() {
    REQUIRE_HR(XMemSetConsoleModel(SCARLETT_MODEL_ANACONDA, 1u), SCARLETT_OK);

    constexpr uint64_t kCount = XMEM_2MB_GROUP_PAGE_COUNT; // 32
    static_assert(kCount * XMEM_PAGE_SIZE_64KB == XMEM_PAGE_SIZE_2MB);

    std::array<uint64_t, kCount> pfns{};
    uint64_t alloc_id = 0;
    REQUIRE_HR(XMemAllocatePhysicalPages(
                   kCount, XMEM_PAGE_SIZE_64KB, SCARLETT_MEM_CACHE_WRITEBACK,
                   XMEM_PHYS_FLAG_GRAPHICS | XMEM_PHYS_FLAG_HUGEPAGE_HINT,
                   pfns.data(), &alloc_id),
               SCARLETT_OK);

    uint64_t va_seq = 0;
    REQUIRE_HR(XMemVirtualAlloc(
                   0u, XMEM_PAGE_SIZE_2MB, XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                   XMEM_PAGE_READWRITE, &va_seq),
               SCARLETT_OK);

    uint64_t va_scat = 0;
    REQUIRE_HR(XMemVirtualAlloc(
                   0u, XMEM_PAGE_SIZE_2MB, XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                   XMEM_PAGE_READWRITE, &va_scat),
               SCARLETT_OK);

    // 1) Sequential 2 MiB group mapping via XMemMapPhysicalPages
    REQUIRE_HR(XMemMapPhysicalPages(
                   va_seq, kCount, XMEM_PAGE_SIZE_64KB, pfns.data(),
                   SCARLETT_MEM_ACCESS_READWRITE, SCARLETT_MEM_CACHE_WRITEBACK),
               SCARLETT_OK);

    for (uint64_t p = 0; p < kCount; ++p) {
        fill_page_pattern(va_seq + p * XMEM_PAGE_SIZE_64KB, p, XMEM_PAGE_SIZE_64KB, 0x55u);
    }
    for (uint64_t p = 0; p < kCount; ++p) {
        uint64_t bad_off = 0;
        CHECK_MSG(verify_page_pattern(va_seq + p * XMEM_PAGE_SIZE_64KB, p,
                                      XMEM_PAGE_SIZE_64KB, 0x55u, &bad_off),
                  "seq page=" + std::to_string(p) + " off=" + std::to_string(bad_off));
    }

    REQUIRE_HR(XMemUnmapPhysicalPages(va_seq, kCount, XMEM_PAGE_SIZE_64KB), SCARLETT_OK);

    // 2) Shuffled/reversed scatter 2 MiB group via XMemMapPhysicalPagesScatter + XMEM_SCATTER_FLAG_2MB_GROUP
    // Bijection over 0..31: slot s maps original physical page orig = (31 - ((s * 17 + 5) % 32))
    std::array<uint64_t, kCount> perm_indices{};
    std::array<uint64_t, kCount> perm_pfns{};
    std::array<uint64_t, kCount> byte_offsets{};
    for (uint64_t s = 0; s < kCount; ++s) {
        const uint64_t orig = 31u - ((s * 17u + 5u) % kCount);
        perm_indices[s] = orig;
        perm_pfns[s] = pfns[orig];
        byte_offsets[s] = s * XMEM_PAGE_SIZE_64KB;
    }

    REQUIRE_HR(XMemMapPhysicalPagesScatter(
                   va_scat, byte_offsets.data(), perm_pfns.data(), kCount, XMEM_PAGE_SIZE_64KB,
                   SCARLETT_MEM_ACCESS_READWRITE, SCARLETT_MEM_CACHE_WRITEBACK,
                   XMEM_SCATTER_FLAG_2MB_GROUP),
               SCARLETT_OK);

    // Every permuted slot in va_scat must expose byte-for-byte data written via va_seq
    for (uint64_t s = 0; s < kCount; ++s) {
        uint64_t bad_off = 0;
        CHECK_MSG(verify_page_pattern(va_scat + byte_offsets[s], perm_indices[s],
                                      XMEM_PAGE_SIZE_64KB, 0x55u, &bad_off),
                  "scatter read slot=" + std::to_string(s) + " orig=" +
                      std::to_string(perm_indices[s]) + " off=" + std::to_string(bad_off));
    }

    // Write new byte-for-byte pattern through scatter mapping
    for (uint64_t s = 0; s < kCount; ++s) {
        fill_page_pattern(va_scat + byte_offsets[s], s, XMEM_PAGE_SIZE_64KB, 0x91u);
    }
    for (uint64_t s = 0; s < kCount; ++s) {
        CHECK(verify_page_pattern(va_scat + byte_offsets[s], s, XMEM_PAGE_SIZE_64KB, 0x91u));
    }

    // Clean unmap of scatter mapping, then remap sequentially and verify physical persistence
    REQUIRE_HR(XMemUnmapPhysicalPages(va_scat, kCount, XMEM_PAGE_SIZE_64KB), SCARLETT_OK);

    // Also test reversed byte_offsets with sequential pfns (nullptr or explicit reversed offsets)
    std::array<uint64_t, kCount> rev_offsets{};
    for (uint64_t i = 0; i < kCount; ++i) {
        rev_offsets[i] = (kCount - 1u - i) * XMEM_PAGE_SIZE_64KB;
    }
    REQUIRE_HR(XMemMapPhysicalPagesScatter(
                   va_seq, rev_offsets.data(), pfns.data(), kCount, XMEM_PAGE_SIZE_64KB,
                   SCARLETT_MEM_ACCESS_READWRITE, SCARLETT_MEM_CACHE_WRITEBACK,
                   XMEM_SCATTER_FLAG_2MB_GROUP),
               SCARLETT_OK);

    for (uint64_t s = 0; s < kCount; ++s) {
        const uint64_t orig = perm_indices[s];
        const uint64_t va_page = va_seq + rev_offsets[orig];
        CHECK(verify_page_pattern(va_page, s, XMEM_PAGE_SIZE_64KB, 0x91u));
    }

    REQUIRE_HR(XMemUnmapPhysicalPages(va_seq, kCount, XMEM_PAGE_SIZE_64KB), SCARLETT_OK);
    REQUIRE_HR(XMemFreePhysicalPages(kCount, XMEM_PAGE_SIZE_64KB, pfns.data()), SCARLETT_OK);
    REQUIRE_HR(XMemVirtualFree(va_seq, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);
    REQUIRE_HR(XMemVirtualFree(va_scat, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);
    return true;
}

// ============================================================================
// Group 4: RO / RW aliasing on the same physical pages
// ============================================================================
bool test_group_4_ro_rw_aliasing() {
    REQUIRE_HR(XMemSetConsoleModel(SCARLETT_MODEL_ANACONDA, 1u), SCARLETT_OK);

    constexpr uint64_t kPages = 2;
    constexpr uint64_t kSize = kPages * XMEM_PAGE_SIZE_64KB;
    std::array<uint64_t, kPages> pfns{};
    uint64_t alloc_id = 0;

    REQUIRE_HR(XMemAllocatePhysicalPages(
                   kPages, XMEM_PAGE_SIZE_64KB, SCARLETT_MEM_CACHE_WRITEBACK,
                   XMEM_PHYS_FLAG_GRAPHICS, pfns.data(), &alloc_id),
               SCARLETT_OK);

    uint64_t va_rw = 0;
    uint64_t va_ro = 0;
    REQUIRE_HR(XMemVirtualAlloc(0u, kSize, XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                                XMEM_PAGE_READWRITE, &va_rw),
               SCARLETT_OK);
    REQUIRE_HR(XMemVirtualAlloc(0u, kSize, XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                                XMEM_PAGE_READONLY, &va_ro),
               SCARLETT_OK);

    REQUIRE_HR(XMemMapPhysicalPages(
                   va_rw, kPages, XMEM_PAGE_SIZE_64KB, pfns.data(),
                   SCARLETT_MEM_ACCESS_READWRITE, SCARLETT_MEM_CACHE_WRITEBACK),
               SCARLETT_OK);
    REQUIRE_HR(XMemMapPhysicalPages(
                   va_ro, kPages, XMEM_PAGE_SIZE_64KB, pfns.data(),
                   SCARLETT_MEM_ACCESS_READONLY, SCARLETT_MEM_CACHE_WRITEBACK),
               SCARLETT_OK);

    XMEM_VIRTUAL_MEMORY_BASIC_INFORMATION qi_ro{};
    CHECK_HR(XMemVirtualQuery(va_ro, &qi_ro), SCARLETT_OK);
    CHECK(qi_ro.State == XMEM_MEM_COMMIT);
    CHECK(qi_ro.Protect == XMEM_PAGE_READONLY);

    auto* rw_u64 = reinterpret_cast<volatile uint64_t*>(static_cast<uintptr_t>(va_rw));
    const auto* ro_u64 = reinterpret_cast<const volatile uint64_t*>(static_cast<uintptr_t>(va_ro));

    // Mutation #1 across both pages
    for (uint64_t p = 0; p < kPages; ++p) {
        fill_page_pattern(va_rw + p * XMEM_PAGE_SIZE_64KB, p, XMEM_PAGE_SIZE_64KB, 0x11u);
    }
    for (uint64_t p = 0; p < kPages; ++p) {
        CHECK(verify_page_pattern(va_ro + p * XMEM_PAGE_SIZE_64KB, p, XMEM_PAGE_SIZE_64KB, 0x11u));
    }

    // Repeated mutations via VA_RW must immediately be visible in VA_RO without copying
    const uint64_t test_offsets_u64[] = {
        0u,
        512u,                              // 4 KiB offset
        (XMEM_PAGE_SIZE_64KB / 8u) - 1u,   // end of page 0
        (XMEM_PAGE_SIZE_64KB / 8u),        // start of page 1
        (kSize / 8u) - 1u                  // end of page 1
    };
    for (uint64_t round = 1; round <= 5; ++round) {
        for (uint64_t idx : test_offsets_u64) {
            const uint64_t val = (UINT64_C(0xA5A5000000000000) | (round << 24)) ^ idx;
            rw_u64[idx] = val;
            CHECK(ro_u64[idx] == val);
        }
    }

    // Unmapping VA_RO leaves VA_RW active, so physical pages are still mapped
    REQUIRE_HR(XMemUnmapPhysicalPages(va_ro, kPages, XMEM_PAGE_SIZE_64KB), SCARLETT_OK);
    CHECK_HR(XMemFreePhysicalPages(kPages, XMEM_PAGE_SIZE_64KB, pfns.data()),
             SCARLETT_ERR_XMEM_PAGES_STILL_MAPPED);

    REQUIRE_HR(XMemUnmapPhysicalPages(va_rw, kPages, XMEM_PAGE_SIZE_64KB), SCARLETT_OK);
    REQUIRE_HR(XMemFreePhysicalPages(kPages, XMEM_PAGE_SIZE_64KB, pfns.data()), SCARLETT_OK);
    REQUIRE_HR(XMemVirtualFree(va_ro, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);
    REQUIRE_HR(XMemVirtualFree(va_rw, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);
    return true;
}

// ============================================================================
// Group 5: Cache-attribute conflicts (all pairs WRITEBACK / WRITECOMBINE / UNCACHED)
// ============================================================================
bool test_group_5_cache_attr_conflicts() {
    REQUIRE_HR(XMemSetConsoleModel(SCARLETT_MODEL_ANACONDA, 1u), SCARLETT_OK);

    static_assert(SCARLETT_ERR_XMEM_CACHE_ATTR_CONFLICT == SCARLETT_ERR_XMEM_CACHE_CONFLICT,
                  "Cache conflict alias must match");

    constexpr std::array<scarlett_mem_cache_attr_t, 3> kCacheAttrs = {
        SCARLETT_MEM_CACHE_WRITEBACK,
        SCARLETT_MEM_CACHE_WRITECOMBINE,
        SCARLETT_MEM_CACHE_UNCACHED,
    };

    for (scarlett_mem_cache_attr_t alloc_attr : kCacheAttrs) {
        std::array<uint64_t, 2> pfns{};
        uint64_t alloc_id = 0;
        REQUIRE_HR(XMemAllocatePhysicalPages(
                       2u, XMEM_PAGE_SIZE_64KB, alloc_attr,
                       XMEM_PHYS_FLAG_GRAPHICS, pfns.data(), &alloc_id),
                   SCARLETT_OK);

        uint64_t va1 = 0;
        uint64_t va2 = 0;
        REQUIRE_HR(XMemVirtualAlloc(0u, 2u * XMEM_PAGE_SIZE_64KB,
                                    XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                                    XMEM_PAGE_READWRITE, &va1),
                   SCARLETT_OK);
        REQUIRE_HR(XMemVirtualAlloc(0u, 2u * XMEM_PAGE_SIZE_64KB,
                                    XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                                    XMEM_PAGE_READWRITE, &va2),
                   SCARLETT_OK);

        // Direct mismatch on unmapped va1 must fail
        for (scarlett_mem_cache_attr_t bad_attr : kCacheAttrs) {
            if (bad_attr == alloc_attr) {
                continue;
            }
            CHECK_HR(XMemMapPhysicalPages(va1, 2u, XMEM_PAGE_SIZE_64KB, pfns.data(),
                                          SCARLETT_MEM_ACCESS_READWRITE, bad_attr),
                     SCARLETT_ERR_XMEM_CACHE_ATTR_CONFLICT);
        }

        // Map va1 with matching cache attribute
        REQUIRE_HR(XMemMapPhysicalPages(va1, 2u, XMEM_PAGE_SIZE_64KB, pfns.data(),
                                        SCARLETT_MEM_ACCESS_READWRITE, alloc_attr),
                   SCARLETT_OK);

        // Aliasing va2 with any conflicting cache attribute must fail
        for (scarlett_mem_cache_attr_t bad_attr : kCacheAttrs) {
            if (bad_attr == alloc_attr) {
                continue;
            }
            CHECK_HR(XMemMapPhysicalPages(va2, 2u, XMEM_PAGE_SIZE_64KB, pfns.data(),
                                          SCARLETT_MEM_ACCESS_READONLY, bad_attr),
                     SCARLETT_ERR_XMEM_CACHE_ATTR_CONFLICT);
            CHECK_HR(XMemMapPhysicalPagesScatter(va2, nullptr, pfns.data(), 2u, XMEM_PAGE_SIZE_64KB,
                                                 SCARLETT_MEM_ACCESS_READWRITE, bad_attr,
                                                 XMEM_SCATTER_FLAG_NONE),
                     SCARLETT_ERR_XMEM_CACHE_ATTR_CONFLICT);
        }

        REQUIRE_HR(XMemUnmapPhysicalPages(va1, 2u, XMEM_PAGE_SIZE_64KB), SCARLETT_OK);
        REQUIRE_HR(XMemFreePhysicalPages(2u, XMEM_PAGE_SIZE_64KB, pfns.data()), SCARLETT_OK);
        REQUIRE_HR(XMemVirtualFree(va2, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);
        REQUIRE_HR(XMemVirtualFree(va1, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);
    }

    return true;
}

// ============================================================================
// Group 6: Cross-allocation map / unmap errors (zero partial mappings)
// ============================================================================
bool test_group_6_cross_allocation_spans() {
    REQUIRE_HR(XMemSetConsoleModel(SCARLETT_MODEL_ANACONDA, 1u), SCARLETT_OK);

    static_assert(SCARLETT_ERR_XMEM_CROSS_ALLOC_SPAN == SCARLETT_ERR_XMEM_CROSS_ALLOCATION,
                  "Cross-alloc error alias must match");

    // Create two strictly adjacent 2-page virtual reservations [va_a, va_b) and [va_b, va_b + 2*64KB)
    uint64_t va_a = 0;
    uint64_t va_b = 0;
    REQUIRE_HR(XMemVirtualAlloc(0u, 2u * XMEM_PAGE_SIZE_64KB,
                                XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                                XMEM_PAGE_READWRITE, &va_a),
               SCARLETT_OK);
    REQUIRE_HR(XMemVirtualAlloc(va_a + 2u * XMEM_PAGE_SIZE_64KB, 2u * XMEM_PAGE_SIZE_64KB,
                                XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                                XMEM_PAGE_READWRITE, &va_b),
               SCARLETT_OK);
    REQUIRE(va_b == va_a + 2u * XMEM_PAGE_SIZE_64KB);

    std::array<uint64_t, 2> phys_a{};
    std::array<uint64_t, 2> phys_b{};
    uint64_t id_a = 0;
    uint64_t id_b = 0;
    REQUIRE_HR(XMemAllocatePhysicalPages(2u, XMEM_PAGE_SIZE_64KB, SCARLETT_MEM_CACHE_WRITEBACK,
                                         XMEM_PHYS_FLAG_GRAPHICS, phys_a.data(), &id_a),
               SCARLETT_OK);
    REQUIRE_HR(XMemAllocatePhysicalPages(2u, XMEM_PAGE_SIZE_64KB, SCARLETT_MEM_CACHE_WRITEBACK,
                                         XMEM_PHYS_FLAG_GRAPHICS, phys_b.data(), &id_b),
               SCARLETT_OK);
    CHECK(id_a != id_b);

    // 6a) Map across two adjacent virtual reservations -> error + 0 partial mappings
    CHECK_HR(XMemMapPhysicalPages(va_a + XMEM_PAGE_SIZE_64KB, 2u, XMEM_PAGE_SIZE_64KB,
                                  phys_a.data(), SCARLETT_MEM_ACCESS_READWRITE,
                                  SCARLETT_MEM_CACHE_WRITEBACK),
             SCARLETT_ERR_XMEM_CROSS_ALLOC_SPAN);

    XMEM_VIRTUAL_MEMORY_BASIC_INFORMATION qi{};
    CHECK_HR(XMemVirtualQuery(va_a + XMEM_PAGE_SIZE_64KB, &qi), SCARLETT_OK);
    CHECK(qi.State == XMEM_MEM_RESERVE);
    CHECK_HR(XMemVirtualQuery(va_b, &qi), SCARLETT_OK);
    CHECK(qi.State == XMEM_MEM_RESERVE);

    XMEM_WORKING_SET_STATISTICS stats{};
    CHECK_HR(XMemGetWorkingSetStatistics(&stats), SCARLETT_OK);
    CHECK(stats.PhysicalMappedBytes == 0u);
    CHECK(stats.ActiveAliasMappings == 0u);

    // 6b) Map each reservation properly, then unmap across their boundary -> error
    REQUIRE_HR(XMemMapPhysicalPages(va_a, 2u, XMEM_PAGE_SIZE_64KB, phys_a.data(),
                                    SCARLETT_MEM_ACCESS_READWRITE, SCARLETT_MEM_CACHE_WRITEBACK),
               SCARLETT_OK);
    REQUIRE_HR(XMemMapPhysicalPages(va_b, 2u, XMEM_PAGE_SIZE_64KB, phys_b.data(),
                                    SCARLETT_MEM_ACCESS_READWRITE, SCARLETT_MEM_CACHE_WRITEBACK),
               SCARLETT_OK);

    CHECK_HR(XMemUnmapPhysicalPages(va_a + XMEM_PAGE_SIZE_64KB, 2u, XMEM_PAGE_SIZE_64KB),
             SCARLETT_ERR_XMEM_CROSS_ALLOC_SPAN);

    // Verify neither half of the cross-boundary unmap was unmapped
    CHECK_HR(XMemVirtualQuery(va_a + XMEM_PAGE_SIZE_64KB, &qi), SCARLETT_OK);
    CHECK(qi.State == XMEM_MEM_COMMIT);
    CHECK_HR(XMemVirtualQuery(va_b, &qi), SCARLETT_OK);
    CHECK(qi.State == XMEM_MEM_COMMIT);

    REQUIRE_HR(XMemUnmapPhysicalPages(va_a, 2u, XMEM_PAGE_SIZE_64KB), SCARLETT_OK);
    REQUIRE_HR(XMemUnmapPhysicalPages(va_b, 2u, XMEM_PAGE_SIZE_64KB), SCARLETT_OK);

    // 6c) Map with PFNs from two independent XMemAllocatePhysicalPages in a single call -> error
    uint64_t va_single = 0;
    REQUIRE_HR(XMemVirtualAlloc(0u, 4u * XMEM_PAGE_SIZE_64KB,
                                XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                                XMEM_PAGE_READWRITE, &va_single),
               SCARLETT_OK);

    const std::array<uint64_t, 2> mixed_pfns = { phys_a[0], phys_b[0] };
    CHECK_HR(XMemMapPhysicalPages(va_single, 2u, XMEM_PAGE_SIZE_64KB, mixed_pfns.data(),
                                  SCARLETT_MEM_ACCESS_READWRITE, SCARLETT_MEM_CACHE_WRITEBACK),
             SCARLETT_ERR_XMEM_CROSS_ALLOC_SPAN);
    CHECK_HR(XMemMapPhysicalPagesScatter(va_single, nullptr, mixed_pfns.data(), 2u, XMEM_PAGE_SIZE_64KB,
                                         SCARLETT_MEM_ACCESS_READWRITE, SCARLETT_MEM_CACHE_WRITEBACK,
                                         XMEM_SCATTER_FLAG_NONE),
             SCARLETT_ERR_XMEM_CROSS_ALLOC_SPAN);

    CHECK_HR(XMemVirtualQuery(va_single, &qi), SCARLETT_OK);
    CHECK(qi.State == XMEM_MEM_RESERVE);

    // 6d) Map phys_a and phys_b into adjacent halves of ONE reservation, then unmap across them -> error
    REQUIRE_HR(XMemMapPhysicalPages(va_single, 2u, XMEM_PAGE_SIZE_64KB, phys_a.data(),
                                    SCARLETT_MEM_ACCESS_READWRITE, SCARLETT_MEM_CACHE_WRITEBACK),
               SCARLETT_OK);
    REQUIRE_HR(XMemMapPhysicalPages(va_single + 2u * XMEM_PAGE_SIZE_64KB, 2u, XMEM_PAGE_SIZE_64KB,
                                    phys_b.data(), SCARLETT_MEM_ACCESS_READWRITE,
                                    SCARLETT_MEM_CACHE_WRITEBACK),
               SCARLETT_OK);

    CHECK_HR(XMemUnmapPhysicalPages(va_single + XMEM_PAGE_SIZE_64KB, 2u, XMEM_PAGE_SIZE_64KB),
             SCARLETT_ERR_XMEM_CROSS_ALLOC_SPAN);

    // Both pages must still be committed
    CHECK_HR(XMemVirtualQuery(va_single + XMEM_PAGE_SIZE_64KB, &qi), SCARLETT_OK);
    CHECK(qi.State == XMEM_MEM_COMMIT);
    CHECK_HR(XMemVirtualQuery(va_single + 2u * XMEM_PAGE_SIZE_64KB, &qi), SCARLETT_OK);
    CHECK(qi.State == XMEM_MEM_COMMIT);

    // Clean teardown
    REQUIRE_HR(XMemUnmapPhysicalPages(va_single, 2u, XMEM_PAGE_SIZE_64KB), SCARLETT_OK);
    REQUIRE_HR(XMemUnmapPhysicalPages(va_single + 2u * XMEM_PAGE_SIZE_64KB, 2u, XMEM_PAGE_SIZE_64KB),
               SCARLETT_OK);
    REQUIRE_HR(XMemFreePhysicalPages(2u, XMEM_PAGE_SIZE_64KB, phys_a.data()), SCARLETT_OK);
    REQUIRE_HR(XMemFreePhysicalPages(2u, XMEM_PAGE_SIZE_64KB, phys_b.data()), SCARLETT_OK);
    REQUIRE_HR(XMemVirtualFree(va_single, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);
    REQUIRE_HR(XMemVirtualFree(va_b, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);
    REQUIRE_HR(XMemVirtualFree(va_a, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);
    return true;
}

// ============================================================================
// Group 7: VirtualQuery, WorkingSetStatistics, MakeTag, AllocDefault/Size/Free
// ============================================================================
bool test_group_7_query_stats_and_default_heap() {
    REQUIRE_HR(XMemSetConsoleModel(SCARLETT_MODEL_ANACONDA, 1u), SCARLETT_OK);

    // Query free address in MEM_GRAPHICS
    XMEM_VIRTUAL_MEMORY_BASIC_INFORMATION qi{};
    REQUIRE_HR(XMemVirtualQuery(SCARLETT_MEM_GRAPHICS_BASE + 0x20000000ULL, &qi), SCARLETT_OK);
    CHECK(qi.State == XMEM_MEM_FREE);

    // Reserve 4 x 64 KiB, map first 2 pages, inspect VirtualQuery & WorkingSetStatistics
    uint64_t va = 0;
    REQUIRE_HR(XMemVirtualAlloc(0u, 4u * XMEM_PAGE_SIZE_64KB,
                                XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                                XMEM_PAGE_READWRITE, &va),
               SCARLETT_OK);

    REQUIRE_HR(XMemVirtualQuery(va + 4096u, &qi), SCARLETT_OK);
    CHECK(qi.State == XMEM_MEM_RESERVE);
    CHECK(qi.AllocationBase == va);
    CHECK(qi.Region == SCARLETT_MEM_REGION_GRAPHICS);
    CHECK(qi.AllocationId != 0u);

    std::array<uint64_t, 2> pfns{};
    uint64_t alloc_id = 0;
    REQUIRE_HR(XMemAllocatePhysicalPages(2u, XMEM_PAGE_SIZE_64KB, SCARLETT_MEM_CACHE_WRITEBACK,
                                         XMEM_PHYS_FLAG_GRAPHICS, pfns.data(), &alloc_id),
               SCARLETT_OK);
    REQUIRE_HR(XMemMapPhysicalPages(va, 2u, XMEM_PAGE_SIZE_64KB, pfns.data(),
                                    SCARLETT_MEM_ACCESS_READWRITE, SCARLETT_MEM_CACHE_WRITEBACK),
               SCARLETT_OK);

    REQUIRE_HR(XMemVirtualQuery(va, &qi), SCARLETT_OK);
    CHECK(qi.State == XMEM_MEM_COMMIT);
    CHECK(qi.AllocationBase == va);
    CHECK(qi.Protect == XMEM_PAGE_READWRITE);
    CHECK(qi.CacheAttr == SCARLETT_MEM_CACHE_WRITEBACK);
    CHECK(qi.Region == SCARLETT_MEM_REGION_GRAPHICS);
    CHECK(qi.RegionSize >= 2u * XMEM_PAGE_SIZE_64KB);

    REQUIRE_HR(XMemVirtualQuery(va + 2u * XMEM_PAGE_SIZE_64KB, &qi), SCARLETT_OK);
    CHECK(qi.State == XMEM_MEM_RESERVE);
    CHECK(qi.AllocationBase == va);

    XMEM_WORKING_SET_STATISTICS ws{};
    REQUIRE_HR(XMemGetWorkingSetStatistics(&ws), SCARLETT_OK);
    CHECK(ws.TotalBudgetBytes == SCARLETT_BUDGET_ANACONDA_BYTES);
    CHECK(ws.PhysicalAllocatedBytes == 2u * XMEM_PAGE_SIZE_64KB);
    CHECK(ws.PhysicalMappedBytes == 2u * XMEM_PAGE_SIZE_64KB);
    CHECK(ws.ReservedBytes >= 4u * XMEM_PAGE_SIZE_64KB);
    CHECK(ws.CommittedBytes >= 2u * XMEM_PAGE_SIZE_64KB);
    CHECK(ws.GraphicsCommittedBytes >= 2u * XMEM_PAGE_SIZE_64KB);
    CHECK(ws.PeakCommittedBytes >= ws.CommittedBytes);
    CHECK(ws.ActivePhysicalPages >= 2u);
    CHECK(ws.ActiveAliasMappings >= 1u);

    REQUIRE_HR(XMemUnmapPhysicalPages(va, 2u, XMEM_PAGE_SIZE_64KB), SCARLETT_OK);
    REQUIRE_HR(XMemFreePhysicalPages(2u, XMEM_PAGE_SIZE_64KB, pfns.data()), SCARLETT_OK);
    REQUIRE_HR(XMemVirtualFree(va, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);

    // XMemMakeTag & XMemAllocDefault / XMemSizeDefault / XMemFreeDefault
    const uint64_t tag_default = XMemMakeTag(
        1u, 2u, 0u, XMEM_TAG_PAGE_64KB,
        static_cast<uint8_t>(SCARLETT_MEM_CACHE_WRITEBACK), 0u, 0u);
    CHECK(tag_default == scarlett_xmem_make_tag_inline(
                             1u, 2u, 0u, XMEM_TAG_PAGE_64KB,
                             static_cast<uint8_t>(SCARLETT_MEM_CACHE_WRITEBACK), 0u, 0u));
    CHECK(scarlett_xmem_tag_alignment(tag_default) == XMEM_DEFAULT_HEAP_ALIGNMENT);
    CHECK(!scarlett_xmem_tag_zero_init(tag_default));

    void* p16 = XMemAllocDefault(77u, tag_default);
    REQUIRE(p16 != nullptr);
    CHECK((reinterpret_cast<uintptr_t>(p16) % 16u) == 0u);
    CHECK(XMemSizeDefault(p16, tag_default) >= 77u);
    std::memset(p16, 0xA7, 77u);

    REQUIRE_HR(XMemGetWorkingSetStatistics(&ws), SCARLETT_OK);
    CHECK(ws.HeapAllocatedBytes >= 77u);
    CHECK_HR(XMemFreeDefault(p16, tag_default), SCARLETT_OK);

    // 64-byte alignment + zero_init = 1
    const uint64_t tag_64 = XMemMakeTag(
        3u, 4u, 6u, XMEM_TAG_PAGE_64KB,
        static_cast<uint8_t>(SCARLETT_MEM_CACHE_WRITEBACK), 1u, 1u);
    CHECK(scarlett_xmem_tag_alignment(tag_64) == 64u);
    CHECK(scarlett_xmem_tag_zero_init(tag_64));
    CHECK(scarlett_xmem_tag_graphics_hint(tag_64));

    void* p64 = XMemAllocDefault(4096u, tag_64);
    REQUIRE(p64 != nullptr);
    CHECK((reinterpret_cast<uintptr_t>(p64) % 64u) == 0u);
    CHECK(XMemSizeDefault(p64, tag_64) >= 4096u);
    const auto* b64 = static_cast<const uint8_t*>(p64);
    bool all_zero_64 = true;
    for (size_t i = 0; i < 4096u; ++i) {
        if (b64[i] != 0u) {
            all_zero_64 = false;
            break;
        }
    }
    CHECK(all_zero_64);
    CHECK_HR(XMemFreeDefault(p64, tag_64), SCARLETT_OK);

    // 256-byte alignment + zero_init = 1
    const uint64_t tag_256 = XMemMakeTag(
        5u, 6u, 8u, XMEM_TAG_PAGE_4KB,
        static_cast<uint8_t>(SCARLETT_MEM_CACHE_WRITEBACK), 1u, 0u);
    CHECK(scarlett_xmem_tag_alignment(tag_256) == 256u);
    CHECK(scarlett_xmem_tag_zero_init(tag_256));

    void* p256 = XMemAllocDefault(1537u, tag_256);
    REQUIRE(p256 != nullptr);
    CHECK((reinterpret_cast<uintptr_t>(p256) % 256u) == 0u);
    CHECK(XMemSizeDefault(p256, tag_256) >= 1537u);
    const auto* b256 = static_cast<const uint8_t*>(p256);
    bool all_zero_256 = true;
    for (size_t i = 0; i < 1537u; ++i) {
        if (b256[i] != 0u) {
            all_zero_256 = false;
            break;
        }
    }
    CHECK(all_zero_256);
    CHECK_HR(XMemFreeDefault(p256, tag_256), SCARLETT_OK);

    REQUIRE_HR(XMemGetWorkingSetStatistics(&ws), SCARLETT_OK);
    CHECK(ws.HeapAllocatedBytes == 0u);
    CHECK(ws.PhysicalAllocatedBytes == 0u);
    CHECK(ws.CommittedBytes == 0u);
    return true;
}

// ============================================================================
// Group 8: MEM_GRAPHICS / MEM_TITLE ranges, console models, BUDGET_EXCEEDED
// ============================================================================
bool test_group_8_ranges_models_and_budgets() {
    REQUIRE_HR(XMemSetConsoleModel(SCARLETT_MODEL_ANACONDA, 1u), SCARLETT_OK);

    // MEM_GRAPHICS via KernelXVirtualAlloc / Query / Free
    void* gfx_ptr = KernelXVirtualAlloc(
        nullptr, 2u * XMEM_PAGE_SIZE_64KB,
        XMEM_MEM_RESERVE | XMEM_MEM_COMMIT | XMEM_MEM_GRAPHICS,
        XMEM_PAGE_READWRITE);
    REQUIRE(gfx_ptr != nullptr);
    CHECK(scarlett_addr_is_graphics(reinterpret_cast<uintptr_t>(gfx_ptr), 2u * XMEM_PAGE_SIZE_64KB));
    *static_cast<volatile uint64_t*>(gfx_ptr) = UINT64_C(0x123456789ABCDEF0);
    CHECK(*static_cast<volatile uint64_t*>(gfx_ptr) == UINT64_C(0x123456789ABCDEF0));

    XMEM_VIRTUAL_MEMORY_BASIC_INFORMATION kqi{};
    CHECK_HR(KernelXVirtualQuery(gfx_ptr, &kqi), SCARLETT_OK);
    CHECK(kqi.Region == SCARLETT_MEM_REGION_GRAPHICS);
    CHECK(kqi.State == XMEM_MEM_COMMIT);
    CHECK(KernelXVirtualFree(gfx_ptr, 0u, XMEM_MEM_RELEASE) != 0u);

    // MEM_TITLE via KernelXVirtualAlloc / Query / Free
    void* title_ptr = KernelXVirtualAlloc(
        nullptr, 2u * XMEM_PAGE_SIZE_64KB,
        XMEM_MEM_RESERVE | XMEM_MEM_COMMIT | XMEM_MEM_TITLE,
        XMEM_PAGE_READWRITE);
    REQUIRE(title_ptr != nullptr);
    CHECK(scarlett_addr_is_title(reinterpret_cast<uintptr_t>(title_ptr), 2u * XMEM_PAGE_SIZE_64KB));
    *static_cast<volatile uint64_t*>(title_ptr) = UINT64_C(0xFEDCBA9876543210);
    CHECK(*static_cast<volatile uint64_t*>(title_ptr) == UINT64_C(0xFEDCBA9876543210));

    CHECK_HR(KernelXVirtualQuery(title_ptr, &kqi), SCARLETT_OK);
    CHECK(kqi.Region == SCARLETT_MEM_REGION_TITLE);
    CHECK(kqi.State == XMEM_MEM_COMMIT);
    CHECK(KernelXVirtualFree(title_ptr, 0u, XMEM_MEM_RELEASE) != 0u);

    // AllocateTitlePhysicalPages / FreeTitlePhysicalPages
    std::array<uint64_t, 2> title_pfns{};
    REQUIRE_HR(AllocateTitlePhysicalPages(2u, XMEM_PAGE_SIZE_64KB, XMEM_PAGE_READWRITE, title_pfns.data()),
               SCARLETT_OK);
    REQUIRE_HR(FreeTitlePhysicalPages(2u, XMEM_PAGE_SIZE_64KB, title_pfns.data()), SCARLETT_OK);

    // Check TitleMemoryStatus, JobTitleMemoryStatus, and BUDGET_EXCEEDED across all 3 models
    struct ModelSpec {
        scarlett_console_model_t model;
        uint64_t total_bytes;
        uint64_t fast_bytes;
        uint64_t std_bytes;
    };
    const std::array<ModelSpec, 3> kModels = {{
        {SCARLETT_MODEL_ANACONDA, SCARLETT_BUDGET_ANACONDA_BYTES,
         SCARLETT_BUDGET_ANACONDA_FAST_BYTES, SCARLETT_BUDGET_ANACONDA_STD_BYTES},
        {SCARLETT_MODEL_LOCKHART, SCARLETT_BUDGET_LOCKHART_BYTES,
         SCARLETT_BUDGET_LOCKHART_FAST_BYTES, SCARLETT_BUDGET_LOCKHART_STD_BYTES},
        {SCARLETT_MODEL_DEVKIT, SCARLETT_BUDGET_DEVKIT_BYTES,
         SCARLETT_BUDGET_DEVKIT_FAST_BYTES, SCARLETT_BUDGET_DEVKIT_STD_BYTES},
    }};

    for (const auto& spec : kModels) {
        REQUIRE_HR(XMemSetConsoleModel(spec.model, 1u), SCARLETT_OK);

        XMEM_TITLE_MEMORY_STATUS tms{};
        tms.dwLength = sizeof(tms);
        REQUIRE_HR(TitleMemoryStatus(&tms), SCARLETT_OK);
        CHECK(tms.ConsoleModel == static_cast<uint32_t>(spec.model));
        CHECK(tms.ullTotalMem == spec.total_bytes);
        CHECK(tms.ullAvailMem <= spec.total_bytes);
        CHECK(tms.ullTitleLimit == spec.total_bytes);
        CHECK(tms.ullFastPoolBytes == spec.fast_bytes);
        CHECK(tms.ullStandardPoolBytes == spec.std_bytes);

        XMEM_JOB_TITLE_MEMORY_STATUS jtms{};
        jtms.dwLength = sizeof(jtms);
        REQUIRE_HR(JobTitleMemoryStatus(&jtms), SCARLETT_OK);
        CHECK(jtms.ConsoleModel == static_cast<uint32_t>(spec.model));
        CHECK(jtms.ullTotalMem == spec.total_bytes);
        CHECK(jtms.ullTitleLimit == spec.total_bytes);
        CHECK(jtms.ullFastPoolBytes == spec.fast_bytes);
        CHECK(jtms.ullStandardPoolBytes == spec.std_bytes);

        // Over-budget commit via XMemVirtualAlloc -> SCARLETT_ERR_XMEM_BUDGET_EXCEEDED
        uint64_t over_va = 0;
        CHECK_HR(XMemVirtualAlloc(0u, spec.total_bytes + XMEM_PAGE_SIZE_64KB,
                                  XMEM_MEM_RESERVE | XMEM_MEM_COMMIT | XMEM_MEM_TITLE,
                                  XMEM_PAGE_READWRITE, &over_va),
                 SCARLETT_ERR_XMEM_BUDGET_EXCEEDED);
        CHECK(over_va == 0u);

        // Over-budget physical allocation via XMemAllocatePhysicalPages -> SCARLETT_ERR_XMEM_BUDGET_EXCEEDED
        const uint64_t over_pages_4mb = (spec.total_bytes / XMEM_PAGE_SIZE_4MB) + 1u;
        std::vector<uint64_t> over_pfns(over_pages_4mb, 0u);
        uint64_t over_alloc_id = 0;
        CHECK_HR(XMemAllocatePhysicalPages(
                     over_pages_4mb, XMEM_PAGE_SIZE_4MB, SCARLETT_MEM_CACHE_WRITEBACK,
                     XMEM_PHYS_FLAG_NONE, over_pfns.data(), &over_alloc_id),
                 SCARLETT_ERR_XMEM_BUDGET_EXCEEDED);

        // KernelXVirtualAlloc over-budget must return nullptr
        CHECK(KernelXVirtualAlloc(
                  nullptr, spec.total_bytes + XMEM_PAGE_SIZE_64KB,
                  XMEM_MEM_RESERVE | XMEM_MEM_COMMIT | XMEM_MEM_TITLE,
                  XMEM_PAGE_READWRITE) == nullptr);
    }

    REQUIRE_HR(XMemSetConsoleModel(SCARLETT_MODEL_ANACONDA, 1u), SCARLETT_OK);
    return true;
}

// ============================================================================
// Group 9: 3-tier UMA bridge & zero 1-frame lag mid-frame write-watch
// ============================================================================
struct UmaUploadRecord {
    uint64_t command_list_id;
    uint64_t region_base_va;
    uint64_t byte_offset;
    uint64_t byte_length;
    std::vector<uint8_t> payload;
};

struct UmaUploadCapture {
    std::mutex mu;
    std::vector<UmaUploadRecord> records;
};

void uma_test_upload_cb(uint64_t command_list_id,
                        uint64_t region_base_va,
                        uint64_t byte_offset,
                        const void* cpu_data,
                        uint64_t byte_length,
                        void* user_data) {
    auto* cap = static_cast<UmaUploadCapture*>(user_data);
    if (cap == nullptr) {
        return;
    }
    UmaUploadRecord rec{};
    rec.command_list_id = command_list_id;
    rec.region_base_va = region_base_va;
    rec.byte_offset = byte_offset;
    rec.byte_length = byte_length;
    if (cpu_data != nullptr && byte_length > 0u) {
        const auto* bytes = static_cast<const uint8_t*>(cpu_data);
        rec.payload.assign(bytes, bytes + byte_length);
    }
    std::lock_guard<std::mutex> lock(cap->mu);
    cap->records.push_back(std::move(rec));
}

bool span_covers(const UmaUploadRecord& rec, uint64_t cmd_id, uint64_t base_va,
                 uint64_t target_off, uint64_t target_len) {
    return rec.command_list_id == cmd_id &&
           rec.region_base_va == base_va &&
           rec.byte_offset <= target_off &&
           (rec.byte_offset + rec.byte_length) >= (target_off + target_len);
}

bool test_group_9_uma_tiers_and_midframe_write_watch() {
    REQUIRE_HR(XMemSetConsoleModel(SCARLETT_MODEL_ANACONDA, 1u), SCARLETT_OK);

    // Helper RAII scope reset for UMA host probe & upload callback
    struct UmaCleanupGuard {
        ~UmaCleanupGuard() {
            scarlett_uma_set_gpu_upload_callback(nullptr, nullptr);
            XMEM_UMA_TIER_INFO info{};
            XMemUmaProbe(XMEM_UMA_PROBE_RESET_HOST, SCARLETT_UMA_TIER_AUTO, 0u, &info);
        }
    } uma_guard;

    XMEM_UMA_TIER_INFO uma_info{};

    // --- Tier A matrix check (ReBAR + VK_EXT_MEMORY_FD + DMA_HEAP/MEMFD_EXPORT) ---
    {
        const uint32_t tier_a_caps = XMEM_UMA_CAP_REBAR | XMEM_UMA_CAP_VK_EXT_MEMORY_FD |
                                     XMEM_UMA_CAP_VK_EXT_DMABUF | XMEM_UMA_CAP_DMA_HEAP |
                                     XMEM_UMA_CAP_MEMFD_EXPORT;
        REQUIRE_HR(XMemUmaProbe(XMEM_UMA_PROBE_SET_SIM_CAPS, SCARLETT_UMA_TIER_AUTO,
                                tier_a_caps, &uma_info),
                   SCARLETT_OK);
        CHECK(uma_info.SelectedTier == SCARLETT_UMA_TIER_A_REBAR_DMABUF);

        const uint32_t apu_caps = XMEM_UMA_CAP_APU | XMEM_UMA_CAP_VK_EXT_MEMORY_FD |
                                  XMEM_UMA_CAP_MEMFD_EXPORT;
        REQUIRE_HR(XMemUmaProbe(XMEM_UMA_PROBE_SET_SIM_CAPS, SCARLETT_UMA_TIER_AUTO,
                                apu_caps, &uma_info),
                   SCARLETT_OK);
        CHECK(uma_info.SelectedTier == SCARLETT_UMA_TIER_A_REBAR_DMABUF);

        constexpr uint64_t kPages = 2;
        constexpr uint64_t kSize = kPages * XMEM_PAGE_SIZE_64KB;
        std::array<uint64_t, kPages> pfns{};
        uint64_t alloc_id = 0;
        uint64_t va = 0;
        REQUIRE_HR(XMemAllocatePhysicalPages(kPages, XMEM_PAGE_SIZE_64KB, SCARLETT_MEM_CACHE_WRITEBACK,
                                             XMEM_PHYS_FLAG_GRAPHICS | XMEM_PHYS_FLAG_DMABUF_HINT,
                                             pfns.data(), &alloc_id),
                   SCARLETT_OK);
        REQUIRE_HR(XMemVirtualAlloc(0u, kSize, XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                                    XMEM_PAGE_READWRITE, &va),
                   SCARLETT_OK);
        REQUIRE_HR(XMemMapPhysicalPages(va, kPages, XMEM_PAGE_SIZE_64KB, pfns.data(),
                                        SCARLETT_MEM_ACCESS_READWRITE, SCARLETT_MEM_CACHE_WRITEBACK),
                   SCARLETT_OK);

        scarlett_uma_tier_t active_tier = SCARLETT_UMA_TIER_AUTO;
        int32_t dmabuf_fd = -1;
        uint64_t host_ptr = 0;
        REQUIRE_HR(XMemUmaExport(va, kSize, SCARLETT_UMA_TIER_AUTO, &active_tier, &dmabuf_fd, &host_ptr),
                   SCARLETT_OK);
        CHECK(active_tier == SCARLETT_UMA_TIER_A_REBAR_DMABUF);
        CHECK(dmabuf_fd >= 0);
        if (dmabuf_fd >= 0) {
            struct stat st{};
            CHECK(::fstat(dmabuf_fd, &st) == 0);
            ::close(dmabuf_fd);
        }

        REQUIRE_HR(XMemUnmapPhysicalPages(va, kPages, XMEM_PAGE_SIZE_64KB), SCARLETT_OK);
        REQUIRE_HR(XMemFreePhysicalPages(kPages, XMEM_PAGE_SIZE_64KB, pfns.data()), SCARLETT_OK);
        REQUIRE_HR(XMemVirtualFree(va, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);
    }

    // --- Tier B matrix check (VK_EXT_HOST_MEM + XMemUmaTrackHotResource migration) ---
    {
        const uint32_t tier_b_caps = XMEM_UMA_CAP_VK_EXT_HOST_MEM;
        REQUIRE_HR(XMemUmaProbe(XMEM_UMA_PROBE_SET_SIM_CAPS, SCARLETT_UMA_TIER_AUTO,
                                tier_b_caps, &uma_info),
                   SCARLETT_OK);
        CHECK(uma_info.SelectedTier == SCARLETT_UMA_TIER_B_EXT_HOST);

        constexpr uint64_t kPages = 2;
        constexpr uint64_t kSize = kPages * XMEM_PAGE_SIZE_64KB;
        std::array<uint64_t, kPages> pfns{};
        uint64_t alloc_id = 0;
        uint64_t va = 0;
        REQUIRE_HR(XMemAllocatePhysicalPages(kPages, XMEM_PAGE_SIZE_64KB, SCARLETT_MEM_CACHE_WRITEBACK,
                                             XMEM_PHYS_FLAG_GRAPHICS, pfns.data(), &alloc_id),
                   SCARLETT_OK);
        REQUIRE_HR(XMemVirtualAlloc(0u, kSize, XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                                    XMEM_PAGE_READWRITE, &va),
                   SCARLETT_OK);
        REQUIRE_HR(XMemMapPhysicalPages(va, kPages, XMEM_PAGE_SIZE_64KB, pfns.data(),
                                        SCARLETT_MEM_ACCESS_READWRITE, SCARLETT_MEM_CACHE_WRITEBACK),
                   SCARLETT_OK);

        // Seed data before export/migration
        auto* cpu = reinterpret_cast<volatile uint8_t*>(static_cast<uintptr_t>(va));
        for (uint64_t i = 0; i < 64u; ++i) {
            cpu[i] = static_cast<uint8_t>(0xB0u + i);
        }

        scarlett_uma_tier_t active_tier = SCARLETT_UMA_TIER_AUTO;
        int32_t dmabuf_fd = -1;
        uint64_t host_ptr = 0;
        REQUIRE_HR(XMemUmaExport(va, kSize, SCARLETT_UMA_TIER_AUTO, &active_tier, &dmabuf_fd, &host_ptr),
                   SCARLETT_OK);
        CHECK(active_tier == SCARLETT_UMA_TIER_B_EXT_HOST);
        CHECK(host_ptr == va);
        if (dmabuf_fd >= 0) {
            ::close(dmabuf_fd);
        }

        uint32_t is_migrated = 0;
        uint32_t migration_count = 0;
        for (int iter = 0; iter < 64 && is_migrated == 0u; ++iter) {
            REQUIRE_HR(XMemUmaTrackHotResource(va, kSize, XMEM_UMA_HOT_RECORD_GPU_READ, 16u,
                                               &is_migrated, &migration_count),
                       SCARLETT_OK);
        }
        if (is_migrated == 0u) {
            REQUIRE_HR(XMemUmaTrackHotResource(va, kSize, XMEM_UMA_HOT_FORCE_MIGRATE, 1u,
                                               &is_migrated, &migration_count),
                       SCARLETT_OK);
        }
        CHECK(is_migrated != 0u);
        CHECK(migration_count >= 1u);

        REQUIRE_HR(XMemUmaProbe(XMEM_UMA_PROBE_QUERY, SCARLETT_UMA_TIER_AUTO, 0u, &uma_info),
                   SCARLETT_OK);
        CHECK(uma_info.TotalMigratedHotRegions >= 1u);

        std::array<uint8_t, 64> shadow{};
        REQUIRE_HR(scarlett_uma_read_gpu_shadow(va, 0u, shadow.data(), shadow.size()), SCARLETT_OK);
        for (uint64_t i = 0; i < 64u; ++i) {
            CHECK(shadow[i] == static_cast<uint8_t>(0xB0u + i));
        }

        REQUIRE_HR(XMemUnmapPhysicalPages(va, kPages, XMEM_PAGE_SIZE_64KB), SCARLETT_OK);
        REQUIRE_HR(XMemFreePhysicalPages(kPages, XMEM_PAGE_SIZE_64KB, pfns.data()), SCARLETT_OK);
        REQUIRE_HR(XMemVirtualFree(va, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);
    }

    // --- Tier C matrix check & mid-frame write-watch zero-lag verification ---
    {
        const uint32_t tier_c_caps = XMEM_UMA_CAP_UFFD_WP | XMEM_UMA_CAP_MPROTECT_FALLBACK;
        REQUIRE_HR(XMemUmaProbe(XMEM_UMA_PROBE_SET_SIM_CAPS, SCARLETT_UMA_TIER_AUTO,
                                tier_c_caps, &uma_info),
                   SCARLETT_OK);
        CHECK(uma_info.SelectedTier == SCARLETT_UMA_TIER_C_UFFD_WP);
        CHECK(uma_info.WatchMode == XMEM_UMA_WATCH_UFFD_WP ||
              uma_info.WatchMode == XMEM_UMA_WATCH_MPROTECT_SIGSEGV);

        constexpr uint64_t kPages = 4;
        constexpr uint64_t kSize = kPages * XMEM_PAGE_SIZE_64KB;
        std::array<uint64_t, kPages> pfns{};
        uint64_t alloc_id = 0;
        uint64_t va = 0;
        REQUIRE_HR(XMemAllocatePhysicalPages(kPages, XMEM_PAGE_SIZE_64KB, SCARLETT_MEM_CACHE_WRITEBACK,
                                             XMEM_PHYS_FLAG_GRAPHICS, pfns.data(), &alloc_id),
                   SCARLETT_OK);
        REQUIRE_HR(XMemVirtualAlloc(0u, kSize, XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                                    XMEM_PAGE_READWRITE, &va),
                   SCARLETT_OK);
        REQUIRE_HR(XMemMapPhysicalPages(va, kPages, XMEM_PAGE_SIZE_64KB, pfns.data(),
                                        SCARLETT_MEM_ACCESS_READWRITE, SCARLETT_MEM_CACHE_WRITEBACK),
                   SCARLETT_OK);

        scarlett_uma_tier_t active_tier = SCARLETT_UMA_TIER_AUTO;
        int32_t dmabuf_fd = -1;
        uint64_t host_ptr = 0;
        REQUIRE_HR(XMemUmaExport(va, kSize, SCARLETT_UMA_TIER_AUTO, &active_tier, &dmabuf_fd, &host_ptr),
                   SCARLETT_OK);
        CHECK(active_tier == SCARLETT_UMA_TIER_C_UFFD_WP);
        if (dmabuf_fd >= 0) {
            ::close(dmabuf_fd);
        }

        UmaUploadCapture cap{};
        scarlett_uma_set_gpu_upload_callback(&uma_test_upload_cb, &cap);

        auto* cpu = reinterpret_cast<volatile uint8_t*>(static_cast<uintptr_t>(va));
        constexpr uint64_t kChunkLen = 256u;
        constexpr uint64_t kPage2Offset = 2u * XMEM_PAGE_SIZE_64KB;

        // Write #1 to page #0
        std::array<uint8_t, kChunkLen> pat0_a{};
        for (uint64_t i = 0; i < kChunkLen; ++i) {
            pat0_a[i] = static_cast<uint8_t>((0x21u + i * 3u) & 0xFFu);
            cpu[i] = pat0_a[i];
        }

        uint64_t flushed_pages = 0;
        uint64_t flushed_bytes = 0;
        REQUIRE_HR(scarlett_uma_flush_before_submit(1u, va, kSize, &flushed_pages, &flushed_bytes),
                   SCARLETT_OK);
        CHECK(flushed_pages >= 1u);
        CHECK(flushed_bytes >= kChunkLen);

        {
            std::lock_guard<std::mutex> lock(cap.mu);
            const bool saw_cmd1_page0 = std::any_of(
                cap.records.begin(), cap.records.end(),
                [&](const UmaUploadRecord& r) { return span_covers(r, 1u, va, 0u, kChunkLen); });
            CHECK(saw_cmd1_page0);
        }

        std::array<uint8_t, kChunkLen> shadow_read{};
        REQUIRE_HR(scarlett_uma_read_gpu_shadow(va, 0u, shadow_read.data(), kChunkLen), SCARLETT_OK);
        CHECK(shadow_read == pat0_a);

        REQUIRE_HR(XMemUmaProbe(XMEM_UMA_PROBE_QUERY, SCARLETT_UMA_TIER_AUTO, 0u, &uma_info),
                   SCARLETT_OK);
        CHECK(uma_info.TotalPresentFlushes == 0u);
        CHECK(uma_info.TotalSubmitFlushes >= 1u);

        // Write #2: touch page #2 AND re-overwrite page #0 (verifies write-protection re-arm!)
        std::array<uint8_t, kChunkLen> pat0_b{};
        std::array<uint8_t, kChunkLen> pat2_a{};
        for (uint64_t i = 0; i < kChunkLen; ++i) {
            pat0_b[i] = static_cast<uint8_t>((0xC3u + i * 5u) & 0xFFu);
            pat2_a[i] = static_cast<uint8_t>((0x7Du + i * 7u) & 0xFFu);
            cpu[i] = pat0_b[i];
            cpu[kPage2Offset + i] = pat2_a[i];
        }

        flushed_pages = 0;
        flushed_bytes = 0;
        REQUIRE_HR(scarlett_uma_flush_before_submit(2u, va, kSize, &flushed_pages, &flushed_bytes),
                   SCARLETT_OK);
        CHECK(flushed_pages >= 2u);
        CHECK(flushed_bytes >= 2u * kChunkLen);

        {
            std::lock_guard<std::mutex> lock(cap.mu);
            const bool saw_cmd2_p0 = std::any_of(
                cap.records.begin(), cap.records.end(),
                [&](const UmaUploadRecord& r) { return span_covers(r, 2u, va, 0u, kChunkLen); });
            const bool saw_cmd2_p2 = std::any_of(
                cap.records.begin(), cap.records.end(),
                [&](const UmaUploadRecord& r) {
                    return span_covers(r, 2u, va, kPage2Offset, kChunkLen);
                });
            CHECK(saw_cmd2_p0);
            CHECK(saw_cmd2_p2);
        }

        REQUIRE_HR(scarlett_uma_read_gpu_shadow(va, 0u, shadow_read.data(), kChunkLen), SCARLETT_OK);
        CHECK(shadow_read == pat0_b);
        REQUIRE_HR(scarlett_uma_read_gpu_shadow(va, kPage2Offset, shadow_read.data(), kChunkLen),
                   SCARLETT_OK);
        CHECK(shadow_read == pat2_a);

        REQUIRE_HR(XMemUmaProbe(XMEM_UMA_PROBE_QUERY, SCARLETT_UMA_TIER_AUTO, 0u, &uma_info),
                   SCARLETT_OK);
        CHECK(uma_info.TotalPresentFlushes == 0u);

        // Flush #3 with NO CPU writes -> flushed_pages == 0
        flushed_pages = 999u;
        flushed_bytes = 999u;
        REQUIRE_HR(scarlett_uma_flush_before_submit(3u, va, kSize, &flushed_pages, &flushed_bytes),
                   SCARLETT_OK);
        CHECK(flushed_pages == 0u);
        CHECK(flushed_bytes == 0u);
        {
            std::lock_guard<std::mutex> lock(cap.mu);
            const bool saw_cmd3 = std::any_of(
                cap.records.begin(), cap.records.end(),
                [](const UmaUploadRecord& r) { return r.command_list_id == 3u; });
            CHECK(!saw_cmd3);
        }

        REQUIRE_HR(XMemUnmapPhysicalPages(va, kPages, XMEM_PAGE_SIZE_64KB), SCARLETT_OK);
        REQUIRE_HR(XMemFreePhysicalPages(kPages, XMEM_PAGE_SIZE_64KB, pfns.data()), SCARLETT_OK);
        REQUIRE_HR(XMemVirtualFree(va, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);
    }

    scarlett_uma_set_gpu_upload_callback(nullptr, nullptr);
    REQUIRE_HR(XMemUmaProbe(XMEM_UMA_PROBE_RESET_HOST, SCARLETT_UMA_TIER_AUTO, 0u, &uma_info),
               SCARLETT_OK);
    return true;
}

// ============================================================================
// Group 10: Thread topology, TimeSensitive TLS isolation, Sync primitives
// ============================================================================
void check_sync_backend_honesty(const XMEM_SYNC_STATUS& st) {
    const bool valid_ntsync =
        (st.Backend == XMEM_SYNC_BACKEND_NTSYNC_KERNEL) && (st.NtsyncErrno == 0);
    const bool valid_fallback =
        (st.Backend == XMEM_SYNC_BACKEND_FUTEX_FALLBACK) && (st.NtsyncErrno != 0);
    CHECK_MSG(valid_ntsync || valid_fallback,
              "backend=" + std::to_string(st.Backend) +
                  " ntsync_errno=" + std::to_string(st.NtsyncErrno));
}

bool test_group_10_threads_and_sync() {
    REQUIRE_HR(XMemSetConsoleModel(SCARLETT_MODEL_ANACONDA, 1u), SCARLETT_OK);

    // 10a) Processor topology & Xbox core affinity
    XMEM_PROCESSOR_TOPOLOGY topo{};
    REQUIRE_HR(XThreadGetProcessorTopology(&topo), SCARLETT_OK);
    CHECK(topo.XboxCoreCount == XMEM_XBOX_TITLE_CORES);     // 7
    CHECK(topo.XboxThreadCount == XMEM_XBOX_TITLE_THREADS); // 14
    CHECK(topo.HostTotalCpus >= 1u);
    for (uint32_t t = 0; t < XMEM_XBOX_TITLE_THREADS; ++t) {
        CHECK(topo.HostCpuForXboxThread[t] < topo.HostTotalCpus);
    }

    std::atomic<bool> affinity_ok{false};
    std::thread affinity_thr([&]() {
        uint32_t applied_one = 0;
        const scarlett_hresult_t hr1 = XThreadSetXboxCoreAffinity(0x1u, &applied_one);
        uint32_t applied_all = 0;
        const scarlett_hresult_t hr2 = XThreadSetXboxCoreAffinity(0x3FFFu, &applied_all);
        const scarlett_hresult_t hr_zero = XThreadSetXboxCoreAffinity(0u, nullptr);
        affinity_ok.store(scarlett_succeeded(hr1) && scarlett_succeeded(hr2) &&
                          scarlett_failed(hr_zero));
    });
    affinity_thr.join();
    CHECK(affinity_ok.load());

    // 10b) Time-sensitive TLS isolation & SCARLETT_E_GS_ASYNC_FUNCTION_REQUIRED
    CHECK(XThreadIsTimeSensitive() == 0u);
    CHECK_HR(XThreadAssertNotTimeSensitive(), SCARLETT_OK);

    uint32_t prev_ts = 99u;
    uint32_t os_prio = 0u;
    REQUIRE_HR(XThreadSetTimeSensitive(1u, &prev_ts, &os_prio), SCARLETT_OK);
    CHECK(prev_ts == 0u);
    CHECK(XThreadIsTimeSensitive() == 1u);
    CHECK_HR(XThreadAssertNotTimeSensitive(), SCARLETT_E_GS_ASYNC_FUNCTION_REQUIRED);

    std::atomic<bool> tls_isolated{false};
    std::thread ts_child([&]() {
        const bool initially_false = (XThreadIsTimeSensitive() == 0u);
        const bool assert_ok = (XThreadAssertNotTimeSensitive() == SCARLETT_OK);
        uint32_t c_prev = 99u;
        const scarlett_hresult_t hr_on = XThreadSetTimeSensitive(1u, &c_prev, nullptr);
        const bool became_true = (XThreadIsTimeSensitive() == 1u);
        const bool assert_err =
            (XThreadAssertNotTimeSensitive() == SCARLETT_E_GS_ASYNC_FUNCTION_REQUIRED);
        const scarlett_hresult_t hr_off = XThreadSetTimeSensitive(0u, &c_prev, nullptr);
        const bool became_false = (XThreadIsTimeSensitive() == 0u);
        tls_isolated.store(initially_false && assert_ok && scarlett_succeeded(hr_on) &&
                           became_true && assert_err && scarlett_succeeded(hr_off) &&
                           became_false);
    });
    ts_child.join();
    CHECK(tls_isolated.load());
    // Main thread TLS state must remain true despite child disabling its own TLS
    CHECK(XThreadIsTimeSensitive() == 1u);
    CHECK_HR(XThreadAssertNotTimeSensitive(), SCARLETT_E_GS_ASYNC_FUNCTION_REQUIRED);

    REQUIRE_HR(XThreadSetTimeSensitive(0u, &prev_ts, &os_prio), SCARLETT_OK);
    CHECK(prev_ts == 1u);
    CHECK(XThreadIsTimeSensitive() == 0u);
    CHECK_HR(XThreadAssertNotTimeSensitive(), SCARLETT_OK);

    // 10c) Manual-reset Event (Set / Reset / Pulse)
    uint64_t ev_man = 0;
    XMEM_SYNC_STATUS st{};
    REQUIRE_HR(KernelXCreateEvent(1u, 0u, XMEM_SYNC_CREATE_FLAG_NONE, &ev_man, &st), SCARLETT_OK);
    REQUIRE(ev_man != 0u);
    check_sync_backend_honesty(st);

    uint32_t sig_idx = 0;
    uint32_t timed_out = 0;
    scarlett_hresult_t hr = KernelXWaitForMultipleObjects(1u, &ev_man, 0u, 0u, &sig_idx, &timed_out);
    CHECK(scarlett_succeeded(hr));
    CHECK(timed_out == 1u);
    CHECK(sig_idx == XMEM_SYNC_WAIT_TIMEOUT_INDEX);

    uint32_t prev_val = 99u;
    REQUIRE_HR(KernelXSetEvent(ev_man, &prev_val), SCARLETT_OK);
    CHECK(prev_val == 0u);

    // Manual-reset stays signaled across multiple waits
    for (int w = 0; w < 2; ++w) {
        hr = KernelXWaitForMultipleObjects(1u, &ev_man, 0u, 0u, &sig_idx, &timed_out);
        CHECK(scarlett_succeeded(hr));
        CHECK(timed_out == 0u);
        CHECK(sig_idx == 0u);
    }

    REQUIRE_HR(KernelXResetEvent(ev_man, &prev_val), SCARLETT_OK);
    CHECK(prev_val == 1u);
    hr = KernelXWaitForMultipleObjects(1u, &ev_man, 0u, 0u, &sig_idx, &timed_out);
    CHECK(scarlett_succeeded(hr));
    CHECK(timed_out == 1u);

    // PulseEvent wakes a waiter and resets back to unsignaled
    std::atomic<bool> pulse_woke{false};
    std::thread pulse_waiter([&]() {
        uint32_t idx = 0;
        uint32_t to = 1u;
        const scarlett_hresult_t whr = KernelXWaitForMultipleObjects(
            1u, &ev_man, 0u, UINT64_C(2000000000), &idx, &to);
        pulse_woke.store(scarlett_succeeded(whr) && to == 0u && idx == 0u);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    REQUIRE_HR(KernelXPulseEvent(ev_man, &prev_val), SCARLETT_OK);
    pulse_waiter.join();
    CHECK(pulse_woke.load());
    // After PulseEvent, event must be unsignaled
    hr = KernelXWaitForMultipleObjects(1u, &ev_man, 0u, 0u, &sig_idx, &timed_out);
    CHECK(scarlett_succeeded(hr));
    CHECK(timed_out == 1u);
    REQUIRE_HR(KernelXCloseSyncHandle(ev_man), SCARLETT_OK);

    // 10d) Auto-reset Event
    uint64_t ev_auto = 0;
    REQUIRE_HR(KernelXCreateEvent(0u, 1u, XMEM_SYNC_CREATE_FLAG_NONE, &ev_auto, &st), SCARLETT_OK);
    check_sync_backend_honesty(st);

    hr = KernelXWaitForMultipleObjects(1u, &ev_auto, 0u, 0u, &sig_idx, &timed_out);
    CHECK(scarlett_succeeded(hr));
    CHECK(timed_out == 0u);
    CHECK(sig_idx == 0u);

    // Consumed by the first wait -> second poll must time out
    hr = KernelXWaitForMultipleObjects(1u, &ev_auto, 0u, 0u, &sig_idx, &timed_out);
    CHECK(scarlett_succeeded(hr));
    CHECK(timed_out == 1u);

    REQUIRE_HR(KernelXSetEvent(ev_auto, &prev_val), SCARLETT_OK);
    CHECK(prev_val == 0u);
    hr = KernelXWaitForMultipleObjects(1u, &ev_auto, 0u, 0u, &sig_idx, &timed_out);
    CHECK(scarlett_succeeded(hr));
    CHECK(timed_out == 0u);
    REQUIRE_HR(KernelXCloseSyncHandle(ev_auto), SCARLETT_OK);

    // 10e) Semaphore (release + overflow error)
    uint64_t sem = 0;
    REQUIRE_HR(KernelXCreateSemaphore(1u, 3u, XMEM_SYNC_CREATE_FLAG_NONE, &sem, &st), SCARLETT_OK);
    check_sync_backend_honesty(st);

    hr = KernelXWaitForMultipleObjects(1u, &sem, 0u, 0u, &sig_idx, &timed_out);
    CHECK(scarlett_succeeded(hr));
    CHECK(timed_out == 0u);

    hr = KernelXWaitForMultipleObjects(1u, &sem, 0u, 0u, &sig_idx, &timed_out);
    CHECK(scarlett_succeeded(hr));
    CHECK(timed_out == 1u);

    REQUIRE_HR(KernelXReleaseSemaphore(sem, 2u, &prev_val), SCARLETT_OK);
    CHECK(prev_val == 0u);

    // Overflow (2 + 2 > 3) must fail
    CHECK(scarlett_failed(KernelXReleaseSemaphore(sem, 2u, &prev_val)));

    for (int i = 0; i < 2; ++i) {
        hr = KernelXWaitForMultipleObjects(1u, &sem, 0u, 0u, &sig_idx, &timed_out);
        CHECK(scarlett_succeeded(hr));
        CHECK(timed_out == 0u);
    }
    hr = KernelXWaitForMultipleObjects(1u, &sem, 0u, 0u, &sig_idx, &timed_out);
    CHECK(scarlett_succeeded(hr));
    CHECK(timed_out == 1u);
    REQUIRE_HR(KernelXCloseSyncHandle(sem), SCARLETT_OK);

    // 10f) Mutex (recursive lock + non-owner unlock error)
    uint64_t mtx = 0;
    REQUIRE_HR(KernelXCreateMutex(0u, XMEM_SYNC_CREATE_FLAG_NONE, &mtx, &st), SCARLETT_OK);
    check_sync_backend_honesty(st);

    hr = KernelXWaitForMultipleObjects(1u, &mtx, 0u, XMEM_SYNC_TIMEOUT_INFINITE, &sig_idx, &timed_out);
    CHECK(scarlett_succeeded(hr));
    CHECK(timed_out == 0u);

    // Recursive lock on same thread succeeds
    hr = KernelXWaitForMultipleObjects(1u, &mtx, 0u, 0u, &sig_idx, &timed_out);
    CHECK(scarlett_succeeded(hr));
    CHECK(timed_out == 0u);

    // Non-owner thread cannot unlock or immediately lock
    std::atomic<bool> non_owner_rejected{false};
    std::thread mtx_other([&]() {
        uint32_t pv = 0;
        const scarlett_hresult_t rel_hr = KernelXReleaseMutex(mtx, &pv);
        uint32_t idx = 0;
        uint32_t to = 0;
        const scarlett_hresult_t wait_hr = KernelXWaitForMultipleObjects(1u, &mtx, 0u, 0u, &idx, &to);
        non_owner_rejected.store(scarlett_failed(rel_hr) && scarlett_succeeded(wait_hr) && to == 1u);
    });
    mtx_other.join();
    CHECK(non_owner_rejected.load());

    // Release both recursive locks
    REQUIRE_HR(KernelXReleaseMutex(mtx, &prev_val), SCARLETT_OK);
    REQUIRE_HR(KernelXReleaseMutex(mtx, &prev_val), SCARLETT_OK);
    // Releasing unowned mutex must fail
    CHECK(scarlett_failed(KernelXReleaseMutex(mtx, &prev_val)));

    // Now another thread can acquire and release
    std::atomic<bool> other_acquired{false};
    std::thread mtx_acq([&]() {
        uint32_t idx = 0;
        uint32_t to = 1u;
        const scarlett_hresult_t whr = KernelXWaitForMultipleObjects(
            1u, &mtx, 0u, UINT64_C(1000000000), &idx, &to);
        const scarlett_hresult_t rhr = KernelXReleaseMutex(mtx, nullptr);
        other_acquired.store(scarlett_succeeded(whr) && to == 0u && scarlett_succeeded(rhr));
    });
    mtx_acq.join();
    CHECK(other_acquired.load());
    REQUIRE_HR(KernelXCloseSyncHandle(mtx), SCARLETT_OK);

    // 10g) WaitForMultipleObjects (WaitAny, WaitAll, timeout, multithreaded wakeup)
    uint64_t h_arr[2] = {0u, 0u};
    REQUIRE_HR(KernelXCreateEvent(1u, 0u, XMEM_SYNC_CREATE_FLAG_NONE, &h_arr[0], &st), SCARLETT_OK);
    REQUIRE_HR(KernelXCreateEvent(1u, 0u, XMEM_SYNC_CREATE_FLAG_NONE, &h_arr[1], &st), SCARLETT_OK);

    // WaitAny with second handle signaled -> signaled_index == 1
    REQUIRE_HR(KernelXSetEvent(h_arr[1], nullptr), SCARLETT_OK);
    hr = KernelXWaitForMultipleObjects(2u, h_arr, 0u /*WaitAny*/, 0u, &sig_idx, &timed_out);
    CHECK(scarlett_succeeded(hr));
    CHECK(timed_out == 0u);
    CHECK(sig_idx == 1u);

    // WaitAll with only one handle signaled -> timeout
    hr = KernelXWaitForMultipleObjects(2u, h_arr, 1u /*WaitAll*/, UINT64_C(5000000), &sig_idx, &timed_out);
    CHECK(scarlett_succeeded(hr));
    CHECK(timed_out == 1u);
    CHECK(sig_idx == XMEM_SYNC_WAIT_TIMEOUT_INDEX);

    // Multithreaded wakeup for WaitAll
    REQUIRE_HR(KernelXResetEvent(h_arr[0], nullptr), SCARLETT_OK);
    REQUIRE_HR(KernelXResetEvent(h_arr[1], nullptr), SCARLETT_OK);
    std::thread waker([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        KernelXSetEvent(h_arr[0], nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        KernelXSetEvent(h_arr[1], nullptr);
    });

    hr = KernelXWaitForMultipleObjects(2u, h_arr, 1u /*WaitAll*/, UINT64_C(2000000000), &sig_idx, &timed_out);
    waker.join();
    CHECK(scarlett_succeeded(hr));
    CHECK(timed_out == 0u);

    REQUIRE_HR(KernelXCloseSyncHandle(h_arr[0]), SCARLETT_OK);
    REQUIRE_HR(KernelXCloseSyncHandle(h_arr[1]), SCARLETT_OK);
    return true;
}

// ============================================================================
// Group 11: Zero Silent Stubs (MapTitleEsramPages diagnostic stub sink)
// ============================================================================
struct StubSinkCapture {
    uint32_t calls = 0;
    scarlett_stub_context_t last_ctx{};
};

scarlett_stub_action_t test_stub_sink(const scarlett_stub_context_t* ctx, void* user_data) {
    auto* cap = static_cast<StubSinkCapture*>(user_data);
    if (cap != nullptr) {
        ++cap->calls;
        if (ctx != nullptr) {
            cap->last_ctx = *ctx;
        }
    }
    return SCARLETT_STUB_ACTION_LOG_AND_FAIL;
}

bool test_group_11_zero_silent_stubs() {
    REQUIRE_HR(XMemSetConsoleModel(SCARLETT_MODEL_ANACONDA, 1u), SCARLETT_OK);

    StubSinkCapture cap{};
    scarlett_xmem_set_stub_sink(&test_stub_sink, &cap);

    const std::array<uint32_t, 4> esram_pages = {1u, 2u, 3u, 4u};
    const scarlett_hresult_t hr = MapTitleEsramPages(
        reinterpret_cast<void*>(static_cast<uintptr_t>(SCARLETT_MEM_GRAPHICS_BASE)),
        esram_pages.size(), 0u, esram_pages.data());

    scarlett_xmem_set_stub_sink(nullptr, nullptr);

    CHECK(scarlett_failed(hr));
    CHECK(cap.calls == 1u);
    CHECK(scarlett_failed(cap.last_ctx.return_hr));
    CHECK(cap.last_ctx.hit_count >= 1u);
    CHECK(std::string_view(cap.last_ctx.dll_name).size() > 0u);
    CHECK(std::string_view(cap.last_ctx.symbol_name).find("MapTitleEsramPages") !=
          std::string_view::npos);
    return true;
}

} // namespace

int main() {
    std::cout << "=== Proton-Scarlett XMem / KernelX / UMA Bridge Unit Tests ===\n";

    run_section(1, "WINE_UNIX_CALL handshake & ABI version validation",
                test_group_1_unix_handshake);
    run_section(2, "64 KiB physical pages, reservations, map/unmap & PAGES_STILL_MAPPED",
                test_group_2_phys_pages_and_reservations);
    run_section(3, "2 MiB groups (32x64 KiB) & scattered/permuted 2 MiB byte-for-byte",
                test_group_3_2mb_groups_and_scatter);
    run_section(4, "RO/RW aliasing zero-copy visibility across repeated mutations",
                test_group_4_ro_rw_aliasing);
    run_section(5, "Cache-attribute conflicts (WB vs WC vs UC)",
                test_group_5_cache_attr_conflicts);
    run_section(6, "Cross-allocation map/unmap boundary errors & zero partial maps",
                test_group_6_cross_allocation_spans);
    run_section(7, "VirtualQuery, WorkingSetStatistics, MakeTag & Default heap",
                test_group_7_query_stats_and_default_heap);
    run_section(8, "MEM_GRAPHICS / MEM_TITLE, 3 console budgets & BUDGET_EXCEEDED",
                test_group_8_ranges_models_and_budgets);
    run_section(9, "3-tier UMA bridge & Tier C mid-frame write-watch zero 1-frame lag",
                test_group_9_uma_tiers_and_midframe_write_watch);
    run_section(10, "7C/14T topology, TimeSensitive TLS & KernelX sync primitives",
                test_group_10_threads_and_sync);
    run_section(11, "Zero Silent Stubs (MapTitleEsramPages stub sink)",
                test_group_11_zero_silent_stubs);

    if (g_total_failures == 0) {
        std::cout << "[PASS] All 11 XMem/UMA test groups passed.\n";
        return 0;
    }

    std::cerr << "[FAIL] Total failed checks: " << g_total_failures << "\n";
    return 1;
}
