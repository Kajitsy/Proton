#include "arena_scenes.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

#include "d3d12_xs/d3d12_xs_api.h"
#include "d3d12_xs/d3d12_xs_vk_abi.h"
#include "loader/loader_unix_abi.h"

namespace scarlett::arena {

namespace {

struct PsmCounters {
    std::atomic<uint32_t> suspend_count{0u};
    std::atomic<uint32_t> resume_count{0u};
    std::atomic<uint8_t> last_quiesced{0u};
};

void SCARLETT_WIN64_API on_app_state_change(uint8_t quiesced, void* context) {
    auto* c = static_cast<PsmCounters*>(context);
    if (c == nullptr) return;
    c->last_quiesced.store(quiesced, std::memory_order_release);
    if (quiesced != 0u) {
        c->suspend_count.fetch_add(1u, std::memory_order_acq_rel);
    } else {
        c->resume_count.fetch_add(1u, std::memory_order_acq_rel);
    }
}

} // namespace

bool run_scene5_presentx_pacing(ArenaTelemetry& tel) {
    SceneChecker sc(tel, tel.s5.checks, "Scene5_PresentX_Pacing");

    void* dev_raw = nullptr;
    ARENA_CHECK_HR(sc, D3D12XboxCreateDevice(nullptr, nullptr, &dev_raw), SCARLETT_OK);
    auto* dev = static_cast<ID3D12DeviceX*>(dev_raw);
    if (dev == nullptr) {
        tel.s5.passed = false;
        return false;
    }

    /* 1. 100-frame 60 Hz Pacing + Dual-Plane HDR10 PQ + GameDVR PresentX */
    ARENA_CHECK_HR(sc, D3D12XboxSetFrameIntervalX(dev->unix_device_handle, 16667u, 0u, 0u, 0u),
                   SCARLETT_OK);

    constexpr int kFrames = 100;
    std::vector<double> abs_jitters_ms;
    abs_jitters_ms.reserve(kFrames);

    bool hdr10_ok = true;
    bool gamedvr_ok = true;
    uint64_t prev_token = 0u;

    for (int f = 0; f < kFrames; ++f) {
        uint64_t token = 0u;
        uint64_t target_ns = 0u;
        ARENA_CHECK_HR(sc,
                       D3D12XboxScheduleFrameEventX(dev->unix_device_handle,
                                                    SCARLETT_FRAME_EVENT_ORIGIN, 0u,
                                                    &token, &target_ns),
                       SCARLETT_OK);
        ARENA_CHECK(sc, token > prev_token);
        prev_token = token;

        scarlett_d3d12xs_wait_frame_params_t wait_info{};
        ARENA_CHECK_HR(sc,
                       D3D12XboxWaitFrameEventX(dev->unix_device_handle,
                                                SCARLETT_FRAME_EVENT_ORIGIN,
                                                1000u, token, &wait_info),
                       SCARLETT_OK);
        ARENA_CHECK(sc, wait_info.out_token == token);

        const double jitter_ms = std::abs(static_cast<double>(wait_info.out_jitter_ns)) / 1.0e6;
        abs_jitters_ms.push_back(jitter_ms);

        D3D12XBOX_PRESENT_PLANE_PARAMETERS planes[2]{};
        planes[0].ColorSpace = SCARLETT_COLOR_SPACE_G2084_P2020;
        planes[0].SourceRectRight = 3840u;
        planes[0].SourceRectBottom = 2160u;
        planes[1].ColorSpace = SCARLETT_COLOR_SPACE_G22_P709;
        planes[1].SourceRectRight = 1920u;
        planes[1].SourceRectBottom = 1080u;

        D3D12XBOX_PRESENT_PARAMETERS pparams{};
        pparams.Token = token;
        pparams.HdrMaxLuminance = 1000.0f;
        pparams.HdrMinLuminance = 0.005f;
        pparams.HdrMaxContentLightLevel = 1000.0f;
        pparams.HdrMaxFrameAverageLightLevel = 400.0f;

        scarlett_d3d12xs_presentx_params_t present_out{};
        const scarlett_hresult_t phr = D3D12XboxPresentX(dev->unix_device_handle, 2u, planes,
                                                         &pparams, &present_out);
        if (phr == static_cast<scarlett_hresult_t>(SCAR_VK_ERROR_DEVICE_LOST)) {
            ++tel.vk_error_device_lost_count;
        }
        ARENA_CHECK_HR(sc, phr, SCARLETT_OK);
        if (present_out.out_vk_color_space_plane0 != SCAR_VK_COLOR_SPACE_HDR10_ST2084_EXT ||
            present_out.out_vk_color_space_plane1 != SCAR_VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            hdr10_ok = false;
        }
        if (present_out.out_gamedvr_captured != 1u) {
            gamedvr_ok = false;
        }
    }

    ARENA_CHECK(sc, hdr10_ok && gamedvr_ok);
    tel.s5.hdr10_pq_colorspace_ok = hdr10_ok;
    tel.s5.gamedvr_plane1_captured = gamedvr_ok;
    tel.s5.frames_paced = static_cast<uint32_t>(kFrames);

    std::sort(abs_jitters_ms.begin(), abs_jitters_ms.end());
    const size_t idx_p50 = (abs_jitters_ms.size() * 50u) / 100u;
    const size_t idx_p95 = (abs_jitters_ms.size() * 95u) / 100u;
    const size_t idx_p99 = std::min(abs_jitters_ms.size() - 1u,
                                    static_cast<size_t>(std::ceil(abs_jitters_ms.size() * 0.99)) - 1u);
    tel.s5.frametime_p50_ms = abs_jitters_ms[idx_p50];
    tel.s5.frametime_p95_ms = abs_jitters_ms[idx_p95];
    tel.s5.frametime_p99_ms = abs_jitters_ms[idx_p99];
    tel.s5.frametime_max_ms = abs_jitters_ms.back();
    ARENA_CHECK(sc, tel.s5.frametime_p99_ms < 1.5);

    /* 2. 10 Cycles Alt+Enter / Resize + SuspendX / ResumeX + RegisterAppStateChangeNotification */
    std::array<uint64_t, 4> res_handles{};
    for (size_t i = 0u; i < res_handles.size(); ++i) {
        D3D12XBOX_RESOURCE_DESC d{};
        d.Dimension = 3u;
        d.Width = 512u;
        d.Height = 512u;
        d.DepthOrArraySize = 1u;
        d.MipLevels = 1u;
        d.Format = 28u;
        d.ByteSize = 1024u * 1024u;
        ARENA_CHECK_HR(sc,
                       D3D12XboxCreatePlacedResourceX(
                           dev->unix_device_handle,
                           SCARLETT_MEM_GRAPHICS_BASE + (i + 1u) * (2u * 1024u * 1024u),
                           &d, 0u, &res_handles[i], nullptr),
                       SCARLETT_OK);
    }

    scarlett_d3d12xs_create_queue_params_t q{};
    ARENA_CHECK_HR(sc,
                   D3D12XboxCreateCommandQueueX(dev->unix_device_handle,
                                                SCARLETT_D3D12_QUEUE_TYPE_DIRECT, 0u, &q),
                   SCARLETT_OK);
    uint64_t cl = 0u;
    ARENA_CHECK_HR(sc,
                   D3D12XboxCreateCommandListX(dev->unix_device_handle,
                                               SCARLETT_D3D12_QUEUE_TYPE_DIRECT, 0u, &cl),
                   SCARLETT_OK);

    PsmCounters psm_counters{};
    void* psm_reg = nullptr;
    ARENA_CHECK(sc, RegisterAppStateChangeNotification(&on_app_state_change, &psm_counters, &psm_reg) == 0u);
    ARENA_CHECK(sc, psm_reg != nullptr);

    std::atomic<bool> stop_poller{false};
    std::atomic<bool> poller_ok{true};
    std::thread poller([&]() {
        while (!stop_poller.load(std::memory_order_relaxed)) {
            scarlett_d3d12xs_query_caps_params_t c{};
            if (D3D12XboxQueryDeviceCapsX(dev->unix_device_handle, &c) != SCARLETT_OK) {
                poller_ok.store(false, std::memory_order_relaxed);
                break;
            }
        }
    });

    uint32_t expected_gen = 1u;
    bool all_preserved = true;
    for (int cycle = 0; cycle < 10; ++cycle) {
        const uint32_t w = (cycle % 2 == 0) ? 1920u : 3840u;
        const uint32_t h = (cycle % 2 == 0) ? 1080u : 2160u;
        const uint32_t fs = (cycle % 2 == 0) ? 0u : 1u;

        scarlett_d3d12xs_resize_swapchain_params_t rz{};
        ARENA_CHECK_HR(sc, D3D12XboxResizeSwapChainX(dev->unix_device_handle, w, h, fs, 1u, 3u, &rz),
                       SCARLETT_OK);
        ++expected_gen;
        ARENA_CHECK(sc, rz.out_generation == expected_gen);
        if (rz.out_preserved_resources != 4u) all_preserved = false;

        scarlett_d3d12xs_power_state_params_t pw{};
        ARENA_CHECK_HR(sc, D3D12XboxSuspendX(dev->unix_device_handle, &pw), SCARLETT_OK);
        ARENA_CHECK(sc, pw.out_is_suspended == 1u);
        if (pw.out_preserved_resources != 4u) all_preserved = false;

        ARENA_CHECK_HR(sc, ScarlettPsmDispatchEvent(SCARLETT_PSM_EVENT_SUSPEND), SCARLETT_OK);

        D3D12XBOX_PRESENT_PLANE_PARAMETERS plane{};
        plane.pResource = reinterpret_cast<void*>(static_cast<uintptr_t>(res_handles[0]));
        plane.ColorSpace = SCARLETT_COLOR_SPACE_G2084_P2020;
        ARENA_CHECK_HR(sc, D3D12XboxPresentX(dev->unix_device_handle, 1u, &plane, nullptr, nullptr),
                       SCARLETT_E_PENDING);

        ARENA_CHECK_HR(sc, D3D12XboxResumeX(dev->unix_device_handle, &pw), SCARLETT_OK);
        ++expected_gen;
        ARENA_CHECK(sc, pw.out_is_suspended == 0u && pw.out_swapchain_generation == expected_gen);
        if (pw.out_preserved_resources != 4u) all_preserved = false;

        ARENA_CHECK_HR(sc, ScarlettPsmDispatchEvent(SCARLETT_PSM_EVENT_RESUME), SCARLETT_OK);

        scarlett_d3d12xs_presentx_params_t po{};
        ARENA_CHECK_HR(sc, D3D12XboxPresentX(dev->unix_device_handle, 1u, &plane, nullptr, &po),
                       SCARLETT_OK);
        ARENA_CHECK(sc, po.out_swapchain_generation == expected_gen);
        ARENA_CHECK_HR(sc,
                       D3D12XboxExecuteCommandListsX(dev->unix_device_handle, q.out_queue_handle,
                                                     &cl, 1u, 0u, 0u, nullptr),
                       SCARLETT_OK);
    }

    stop_poller.store(true, std::memory_order_relaxed);
    poller.join();
    ARENA_CHECK(sc, poller_ok.load());

    UnregisterAppStateChangeNotification(psm_reg);

    tel.s5.alt_enter_suspend_cycles = 10u;
    tel.s5.psm_suspend_callbacks = psm_counters.suspend_count.load(std::memory_order_acquire);
    tel.s5.psm_resume_callbacks = psm_counters.resume_count.load(std::memory_order_acquire);
    tel.s5.preserved_resources_ok = all_preserved;

    ARENA_CHECK(sc, tel.s5.psm_suspend_callbacks == 10u);
    ARENA_CHECK(sc, tel.s5.psm_resume_callbacks == 10u);
    ARENA_CHECK(sc, all_preserved);

    scarlett_d3d12xs_query_caps_params_t final_caps{};
    ARENA_CHECK_HR(sc, D3D12XboxQueryDeviceCapsX(dev->unix_device_handle, &final_caps), SCARLETT_OK);
    ARENA_CHECK(sc, final_caps.out_validation_errors == 0u);
    ARENA_CHECK(sc, final_caps.out_uma_present_flushes == 0u);

    for (uint64_t rh : res_handles) {
        ARENA_CHECK_HR(sc, D3D12XboxDestroyResourceX(dev->unix_device_handle, rh), SCARLETT_OK);
    }
    ARENA_CHECK(sc, dev->lpVtbl->Release(dev) == 0u);

    tel.s5.passed = sc.ok();
    return tel.s5.passed;
}

} // namespace scarlett::arena
