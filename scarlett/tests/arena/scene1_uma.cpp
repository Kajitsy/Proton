#include "arena_scenes.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <vector>

#include "d3d12_xs/d3d12_xs_api.h"
#include "xmem/xmem_api.h"
#include "xmem/xmem_unix_abi.h"

namespace scarlett::arena {

bool run_scene1_uma_aliasing(ArenaTelemetry& tel) {
    SceneChecker sc(tel, tel.s1.checks, "Scene1_UMA");

    /* 1. Initialize XMem for the active console model and verify budget */
    ARENA_CHECK_HR(sc, XMemInitialize(tel.model, 0u), SCARLETT_OK);
    ARENA_CHECK_HR(sc, XMemSetConsoleModel(tel.model, 1u), SCARLETT_OK);

    scarlett_xmem_title_mem_status_params_t tms_params{};
    ARENA_CHECK_HR(sc, scarlett_xmem_unix_call(SCARLETT_XMEM_UNIX_TITLE_MEM_STATUS, &tms_params), SCARLETT_OK);
    ARENA_CHECK_HR(sc, tms_params.out_status, SCARLETT_OK);
    ARENA_CHECK(sc, tms_params.out_console_model == static_cast<uint32_t>(tel.model));
    ARENA_CHECK(sc, tms_params.out_total_mem == tel.memory_budget_bytes);

    XMEM_WORKING_SET_STATISTICS ws{};
    ARENA_CHECK_HR(sc, XMemGetWorkingSetStatistics(&ws), SCARLETT_OK);
    ARENA_CHECK(sc, ws.TotalBudgetBytes == tel.memory_budget_bytes);
    tel.s1.model_budget_bytes = ws.TotalBudgetBytes;

    /* 2. Budget enforcement: exceed model budget -> SCARLETT_ERR_XMEM_BUDGET_EXCEEDED */
    uint64_t over_va = 0u;
    ARENA_CHECK_HR(sc,
                   XMemVirtualAlloc(0u, tel.memory_budget_bytes + XMEM_PAGE_SIZE_64KB,
                                    XMEM_MEM_RESERVE | XMEM_MEM_COMMIT | XMEM_MEM_TITLE,
                                    XMEM_PAGE_READWRITE, &over_va),
                   SCARLETT_ERR_XMEM_BUDGET_EXCEEDED);
    ARENA_CHECK(sc, over_va == 0u);

    const uint64_t over_pages_4mb = (tel.memory_budget_bytes / XMEM_PAGE_SIZE_4MB) + 1u;
    std::vector<uint64_t> over_pfns(over_pages_4mb, 0u);
    uint64_t over_alloc_id = 0u;
    ARENA_CHECK_HR(sc,
                   XMemAllocatePhysicalPages(over_pages_4mb, XMEM_PAGE_SIZE_4MB,
                                             SCARLETT_MEM_CACHE_WRITEBACK,
                                             XMEM_PHYS_FLAG_NONE,
                                             over_pfns.data(), &over_alloc_id),
                   SCARLETT_ERR_XMEM_BUDGET_EXCEEDED);
    tel.s1.budget_exceeded_enforced = true;

    /* 3. RO/RW zero-copy physical aliasing in MEM_GRAPHICS */
    constexpr uint64_t kAliasPages = 2u;
    constexpr uint64_t kAliasBytes = kAliasPages * XMEM_PAGE_SIZE_64KB;
    std::array<uint64_t, kAliasPages> alias_pfns{};
    uint64_t alias_alloc_id = 0u;
    ARENA_CHECK_HR(sc,
                   XMemAllocatePhysicalPages(kAliasPages, XMEM_PAGE_SIZE_64KB,
                                             SCARLETT_MEM_CACHE_WRITEBACK,
                                             XMEM_PHYS_FLAG_GRAPHICS,
                                             alias_pfns.data(), &alias_alloc_id),
                   SCARLETT_OK);

    uint64_t va_rw = 0u;
    uint64_t va_ro = 0u;
    uint64_t va_conflict = 0u;
    ARENA_CHECK_HR(sc,
                   XMemVirtualAlloc(0u, kAliasBytes, XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                                    XMEM_PAGE_READWRITE, &va_rw),
                   SCARLETT_OK);
    ARENA_CHECK_HR(sc,
                   XMemVirtualAlloc(0u, kAliasBytes, XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                                    XMEM_PAGE_READONLY, &va_ro),
                   SCARLETT_OK);
    ARENA_CHECK_HR(sc,
                   XMemVirtualAlloc(0u, kAliasBytes, XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                                    XMEM_PAGE_READONLY, &va_conflict),
                   SCARLETT_OK);

    ARENA_CHECK_HR(sc,
                   XMemMapPhysicalPages(va_rw, kAliasPages, XMEM_PAGE_SIZE_64KB,
                                        alias_pfns.data(), SCARLETT_MEM_ACCESS_READWRITE,
                                        SCARLETT_MEM_CACHE_WRITEBACK),
                   SCARLETT_OK);
    ARENA_CHECK_HR(sc,
                   XMemMapPhysicalPages(va_ro, kAliasPages, XMEM_PAGE_SIZE_64KB,
                                        alias_pfns.data(), SCARLETT_MEM_ACCESS_READONLY,
                                        SCARLETT_MEM_CACHE_WRITEBACK),
                   SCARLETT_OK);

    auto* ptr_rw = reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(va_rw));
    const auto* ptr_ro = reinterpret_cast<const volatile uint32_t*>(static_cast<uintptr_t>(va_ro));
    bool alias_match = (ptr_rw != nullptr && ptr_ro != nullptr);
    if (alias_match) {
        for (uint32_t i = 0u; i < 64u; ++i) {
            const uint32_t val = 0xA5000000u ^ (i * 0x01010101u);
            ptr_rw[i] = val;
            ptr_rw[(XMEM_PAGE_SIZE_64KB / sizeof(uint32_t)) + i] = ~val;
        }
        for (uint32_t i = 0u; i < 64u; ++i) {
            const uint32_t val = 0xA5000000u ^ (i * 0x01010101u);
            if (ptr_ro[i] != val ||
                ptr_ro[(XMEM_PAGE_SIZE_64KB / sizeof(uint32_t)) + i] != ~val) {
                alias_match = false;
                break;
            }
        }
    }
    ARENA_CHECK(sc, alias_match);
    tel.s1.ro_rw_aliasing_ok = alias_match;

    /* 4. Guaranteed refusal on cache-attribute conflict */
    ARENA_CHECK_HR(sc,
                   XMemMapPhysicalPages(va_conflict, kAliasPages, XMEM_PAGE_SIZE_64KB,
                                        alias_pfns.data(), SCARLETT_MEM_ACCESS_READONLY,
                                        SCARLETT_MEM_CACHE_WRITECOMBINE),
                   SCARLETT_ERR_XMEM_CACHE_CONFLICT);
    ARENA_CHECK_HR(sc,
                   XMemMapPhysicalPages(va_conflict, kAliasPages, XMEM_PAGE_SIZE_64KB,
                                        alias_pfns.data(), SCARLETT_MEM_ACCESS_READONLY,
                                        SCARLETT_MEM_CACHE_UNCACHED),
                   SCARLETT_ERR_XMEM_CACHE_CONFLICT);
    tel.s1.cache_conflict_rejected = true;

    /* 5. Guaranteed refusal on cross-allocation map/unmap */
    uint64_t va_adj_a = 0u;
    uint64_t va_adj_b = 0u;
    ARENA_CHECK_HR(sc,
                   XMemVirtualAlloc(0u, kAliasBytes, XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                                    XMEM_PAGE_READWRITE, &va_adj_a),
                   SCARLETT_OK);
    ARENA_CHECK_HR(sc,
                   XMemVirtualAlloc(va_adj_a + kAliasBytes, kAliasBytes,
                                    XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                                    XMEM_PAGE_READWRITE, &va_adj_b),
                   SCARLETT_OK);
    ARENA_CHECK(sc, va_adj_b == va_adj_a + kAliasBytes);
    ARENA_CHECK_HR(sc,
                   XMemMapPhysicalPages(va_adj_a + XMEM_PAGE_SIZE_64KB, kAliasPages,
                                        XMEM_PAGE_SIZE_64KB, alias_pfns.data(),
                                        SCARLETT_MEM_ACCESS_READWRITE,
                                        SCARLETT_MEM_CACHE_WRITEBACK),
                   SCARLETT_ERR_XMEM_CROSS_ALLOCATION);
    tel.s1.cross_alloc_rejected = true;

    ARENA_CHECK_HR(sc, XMemVirtualFree(va_adj_b, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);
    ARENA_CHECK_HR(sc, XMemVirtualFree(va_adj_a, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);
    ARENA_CHECK_HR(sc, XMemVirtualFree(va_conflict, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);
    ARENA_CHECK_HR(sc, XMemUnmapPhysicalPages(va_ro, kAliasPages, XMEM_PAGE_SIZE_64KB), SCARLETT_OK);
    ARENA_CHECK_HR(sc, XMemUnmapPhysicalPages(va_rw, kAliasPages, XMEM_PAGE_SIZE_64KB), SCARLETT_OK);
    ARENA_CHECK_HR(sc, XMemVirtualFree(va_ro, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);
    ARENA_CHECK_HR(sc, XMemVirtualFree(va_rw, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);
    ARENA_CHECK_HR(sc, XMemFreePhysicalPages(kAliasPages, XMEM_PAGE_SIZE_64KB, alias_pfns.data()),
                   SCARLETT_OK);

    /* 6. Mid-frame CPU->GPU UMA + CreatePlacedResourceX + ExecuteCommandLists (0-frame lag) */
    struct UmaProbeGuard {
        ~UmaProbeGuard() {
            scarlett_uma_set_gpu_upload_callback(nullptr, nullptr);
            XMEM_UMA_TIER_INFO info{};
            (void)XMemUmaProbe(XMEM_UMA_PROBE_RESET_HOST, SCARLETT_UMA_TIER_AUTO, 0u, &info);
        }
    } uma_guard;

    XMEM_UMA_TIER_INFO uma_info{};
    const uint32_t tier_c_caps = XMEM_UMA_CAP_UFFD_WP | XMEM_UMA_CAP_MPROTECT_FALLBACK;
    ARENA_CHECK_HR(sc,
                   XMemUmaProbe(XMEM_UMA_PROBE_SET_SIM_CAPS, SCARLETT_UMA_TIER_AUTO,
                                tier_c_caps, &uma_info),
                   SCARLETT_OK);
    ARENA_CHECK(sc, uma_info.SelectedTier == SCARLETT_UMA_TIER_C_UFFD_WP);

    constexpr uint64_t kTexPages = 4u;
    constexpr uint64_t kTexBytes = kTexPages * XMEM_PAGE_SIZE_64KB;
    std::array<uint64_t, kTexPages> tex_pfns{};
    uint64_t tex_alloc_id = 0u;
    uint64_t gpu_va = 0u;
    ARENA_CHECK_HR(sc,
                   XMemAllocatePhysicalPages(kTexPages, XMEM_PAGE_SIZE_64KB,
                                             SCARLETT_MEM_CACHE_WRITEBACK,
                                             XMEM_PHYS_FLAG_GRAPHICS,
                                             tex_pfns.data(), &tex_alloc_id),
                   SCARLETT_OK);
    ARENA_CHECK_HR(sc,
                   XMemVirtualAlloc(0u, kTexBytes, XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                                    XMEM_PAGE_READWRITE, &gpu_va),
                   SCARLETT_OK);
    ARENA_CHECK_HR(sc,
                   XMemMapPhysicalPages(gpu_va, kTexPages, XMEM_PAGE_SIZE_64KB,
                                        tex_pfns.data(), SCARLETT_MEM_ACCESS_READWRITE,
                                        SCARLETT_MEM_CACHE_WRITEBACK),
                   SCARLETT_OK);

    auto* cpu_tex = reinterpret_cast<volatile uint8_t*>(static_cast<uintptr_t>(gpu_va));
    for (uint64_t i = 0u; i < 64u; ++i) {
        cpu_tex[i] = static_cast<uint8_t>(0x10u + i);
    }

    scarlett_uma_tier_t active_tier = SCARLETT_UMA_TIER_AUTO;
    int32_t dmabuf_fd = -1;
    uint64_t host_ptr = 0u;
    ARENA_CHECK_HR(sc,
                   XMemUmaExport(gpu_va, kTexBytes, SCARLETT_UMA_TIER_AUTO,
                                 &active_tier, &dmabuf_fd, &host_ptr),
                   SCARLETT_OK);
    if (dmabuf_fd >= 0) {
        ::close(dmabuf_fd);
    }
    tel.s1.uma_tier = static_cast<uint32_t>(active_tier);

    void* dev_raw = nullptr;
    ARENA_CHECK_HR(sc, D3D12XboxCreateDevice(nullptr, nullptr, &dev_raw), SCARLETT_OK);
    auto* dev = static_cast<ID3D12DeviceX*>(dev_raw);
    if (dev != nullptr) {
        D3D12XBOX_RESOURCE_DESC tex_desc{};
        tex_desc.Dimension = 3u;
        tex_desc.Width = 256u;
        tex_desc.Height = 256u;
        tex_desc.DepthOrArraySize = 1u;
        tex_desc.MipLevels = 1u;
        tex_desc.Format = 28u;
        tex_desc.ByteSize = kTexBytes;

        uint64_t bad_res = 0u;
        ARENA_CHECK_HR(sc,
                       D3D12XboxCreatePlacedResourceX(dev->unix_device_handle,
                                                      SCARLETT_MEM_TITLE_BASE, &tex_desc,
                                                      0u, &bad_res, nullptr),
                       SCARLETT_ERR_XMEM_OUT_OF_RANGE);

        uint64_t res_handle = 0u;
        uint32_t res_tier = 0u;
        ARENA_CHECK_HR(sc,
                       D3D12XboxCreatePlacedResourceX(dev->unix_device_handle, gpu_va,
                                                      &tex_desc, 0u, &res_handle, &res_tier),
                       SCARLETT_OK);
        ARENA_CHECK(sc, res_handle != 0u);

        scarlett_d3d12xs_create_queue_params_t q_direct{};
        ARENA_CHECK_HR(sc,
                       D3D12XboxCreateCommandQueueX(dev->unix_device_handle,
                                                    SCARLETT_D3D12_QUEUE_TYPE_DIRECT,
                                                    0u, &q_direct),
                       SCARLETT_OK);

        uint64_t cmd_list = 0u;
        ARENA_CHECK_HR(sc,
                       D3D12XboxCreateCommandListX(dev->unix_device_handle,
                                                   SCARLETT_D3D12_QUEUE_TYPE_DIRECT,
                                                   0u, &cmd_list),
                       SCARLETT_OK);

        /* Mid-frame CPU mutation across two 64K pages */
        constexpr uint64_t kOff0 = 128u;
        constexpr uint64_t kOff2 = 2u * XMEM_PAGE_SIZE_64KB + 64u;
        for (uint64_t i = 0u; i < 32u; ++i) {
            cpu_tex[kOff0 + i] = static_cast<uint8_t>(0xC0u + i);
            cpu_tex[kOff2 + i] = static_cast<uint8_t>(0xE0u + i);
        }

        std::array<uint8_t, 32> shadow_before{};
        ARENA_CHECK_HR(sc,
                       scarlett_uma_read_gpu_shadow(gpu_va, kOff0, shadow_before.data(),
                                                    shadow_before.size()),
                       SCARLETT_OK);
        ARENA_CHECK(sc, shadow_before[0] != 0xC0u);

        scarlett_d3d12xs_execute_cmd_lists_params_t exec_info{};
        ARENA_CHECK_HR(sc,
                       D3D12XboxExecuteCommandListsX(dev->unix_device_handle,
                                                     q_direct.out_queue_handle,
                                                     &cmd_list, 1u, gpu_va, kTexBytes,
                                                     &exec_info),
                       SCARLETT_OK);
        ARENA_CHECK(sc, exec_info.out_uma_flushed_pre_submit == 1u);
        ARENA_CHECK(sc, exec_info.out_flushed_pages >= 2u);

        std::array<uint8_t, 32> shadow_after0{};
        std::array<uint8_t, 32> shadow_after2{};
        ARENA_CHECK_HR(sc,
                       scarlett_uma_read_gpu_shadow(gpu_va, kOff0, shadow_after0.data(),
                                                    shadow_after0.size()),
                       SCARLETT_OK);
        ARENA_CHECK_HR(sc,
                       scarlett_uma_read_gpu_shadow(gpu_va, kOff2, shadow_after2.data(),
                                                    shadow_after2.size()),
                       SCARLETT_OK);
        bool lag_zero = true;
        for (uint64_t i = 0u; i < 32u; ++i) {
            if (shadow_after0[i] != static_cast<uint8_t>(0xC0u + i) ||
                shadow_after2[i] != static_cast<uint8_t>(0xE0u + i)) {
                lag_zero = false;
                break;
            }
        }
        ARENA_CHECK(sc, lag_zero);

        /* Second write proves write-watch was re-armed */
        cpu_tex[kOff0] = 0x5Au;
        scarlett_d3d12xs_execute_cmd_lists_params_t exec_info2{};
        ARENA_CHECK_HR(sc,
                       D3D12XboxExecuteCommandListsX(dev->unix_device_handle,
                                                     q_direct.out_queue_handle,
                                                     &cmd_list, 1u, gpu_va, kTexBytes,
                                                     &exec_info2),
                       SCARLETT_OK);
        uint8_t b0 = 0u;
        ARENA_CHECK_HR(sc, scarlett_uma_read_gpu_shadow(gpu_va, kOff0, &b0, 1u), SCARLETT_OK);
        ARENA_CHECK(sc, b0 == 0x5Au);

        scarlett_d3d12xs_query_caps_params_t caps{};
        ARENA_CHECK_HR(sc, D3D12XboxQueryDeviceCapsX(dev->unix_device_handle, &caps), SCARLETT_OK);
        ARENA_CHECK(sc, caps.out_uma_pre_submit_flushes >= 2u);
        ARENA_CHECK(sc, caps.out_uma_present_flushes == 0u);
        ARENA_CHECK(sc, caps.out_validation_errors == 0u);

        tel.s1.uma_pre_submit_flushes = caps.out_uma_pre_submit_flushes;
        tel.s1.uma_present_flushes = caps.out_uma_present_flushes;
        tel.s1.zero_frame_lag_verified = lag_zero && (b0 == 0x5Au) && (caps.out_uma_present_flushes == 0u);

        ARENA_CHECK_HR(sc, D3D12XboxDestroyResourceX(dev->unix_device_handle, res_handle), SCARLETT_OK);
        ARENA_CHECK(sc, dev->lpVtbl->Release(dev) == 0u);
    }

    ARENA_CHECK_HR(sc, XMemUnmapPhysicalPages(gpu_va, kTexPages, XMEM_PAGE_SIZE_64KB), SCARLETT_OK);
    ARENA_CHECK_HR(sc, XMemFreePhysicalPages(kTexPages, XMEM_PAGE_SIZE_64KB, tex_pfns.data()),
                   SCARLETT_OK);
    ARENA_CHECK_HR(sc, XMemVirtualFree(gpu_va, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);

    tel.s1.passed = sc.ok();
    return tel.s1.passed;
}

} // namespace scarlett::arena
