/*
 * Proton-Scarlett — PresentX & High-Precision Frame Pacing (d3d12_xs_unix.so)
 *
 * Implements:
 *   - SetFrameIntervalX / ScheduleFrameEventX / blocking WaitFrameEventX with
 *     monotonic 64-bit D3D12XBOX_FRAME_PIPELINE_TOKEN and hybrid CLOCK_MONOTONIC
 *     TIMER_ABSTIME + micro-spin pacing for deterministic p99 jitter < 1.5 ms.
 *   - PresentX with 1 or 2 independent planes:
 *       * Plane 0: Primary HDR10 PQ (G2084_P2020 -> VK_COLOR_SPACE_HDR10_ST2084_EXT
 *         + SMPTE ST.2086 VkHdrMetadataEXT for Wayland / Gamescope) or SDR.
 *       * Plane 1: Optional GameDVR / HUD plane (G22_P709 -> VK_COLOR_SPACE_SRGB_NONLINEAR_KHR).
 *   - Strict invariant: UMA dirty flush is NEVER deferred to PresentX.
 */

#include "d3d12_xs_internal.hpp"

#include <cerrno>
#include <sched.h>
#include <time.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace scarlett::d3d12xs::unixlib {

namespace {

constexpr uint64_t kNsPerUs = UINT64_C(1000);
constexpr uint64_t kNsPerMs = UINT64_C(1000000);
constexpr uint64_t kNsPerSec = UINT64_C(1000000000);
/* Wake 400us early via TIMER_ABSTIME, then precision-spin to absorb Linux hrtimer slack
 * while keeping the thread interactive under Linux EEVDF scheduler. */
constexpr uint64_t kPrecisionSpinMarginNs = UINT64_C(400000);

void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    _mm_pause();
#else
    ::sched_yield();
#endif
}

uint32_t map_color_space_to_vk(uint32_t xbox_cs) noexcept {
    switch (xbox_cs) {
        case SCARLETT_COLOR_SPACE_G2084_P2020:
            return SCAR_VK_COLOR_SPACE_HDR10_ST2084_EXT;
        case SCARLETT_COLOR_SPACE_G22_P709:
            return SCAR_VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        case SCARLETT_COLOR_SPACE_G10_P709:
            return SCAR_VK_COLOR_SPACE_BT709_NONLINEAR_EXT;
        default:
            return SCAR_VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    }
}

void sleep_until_monotonic_ns(uint64_t target_ns) noexcept {
    uint64_t now = monotonic_now_ns();
    if (now >= target_ns) {
        return;
    }

    if (target_ns - now > kPrecisionSpinMarginNs) {
        const uint64_t wake_ns = target_ns - kPrecisionSpinMarginNs;
        struct timespec ts{};
        ts.tv_sec = static_cast<time_t>(wake_ns / kNsPerSec);
        ts.tv_nsec = static_cast<long>(wake_ns % kNsPerSec);
        while (::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {
            if (monotonic_now_ns() >= wake_ns) {
                break;
            }
        }
    }

    while (monotonic_now_ns() < target_ns) {
        cpu_relax();
    }
}

} // namespace

uint64_t monotonic_now_ns() noexcept {
    struct timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * kNsPerSec + static_cast<uint64_t>(ts.tv_nsec);
}

scarlett_hresult_t pacing_set_frame_interval(
    SwapchainState &sc,
    const scarlett_d3d12xs_frame_interval_params_t &in) noexcept {
    if (in.period_us < 1000u || in.period_us > 1000000u) {
        return SCARLETT_E_INVALIDARG;
    }
    sc.frame_period_us = in.period_us;
    sc.origin_offset_us = in.origin_offset_us;
    sc.vrr_enabled = (in.vrr_enabled != 0u);
    return SCARLETT_OK;
}

scarlett_hresult_t pacing_schedule_frame_event(
    SwapchainState &sc,
    scarlett_d3d12xs_schedule_frame_params_t &inout) noexcept {
    if (inout.event_type != SCARLETT_FRAME_EVENT_ORIGIN &&
        inout.event_type != SCARLETT_FRAME_EVENT_PRESENT) {
        inout.out_status = SCARLETT_E_INVALIDARG;
        return inout.out_status;
    }

    const uint64_t now = monotonic_now_ns();
    const uint64_t period_ns = static_cast<uint64_t>(sc.frame_period_us) * kNsPerUs;
    const uint64_t delay_ns = static_cast<uint64_t>(inout.delay_us) * kNsPerUs;
    const uint64_t origin_off_ns = static_cast<uint64_t>(sc.origin_offset_us) * kNsPerUs;

    uint64_t target_ns = 0u;
    if (sc.last_origin_ns == 0u) {
        target_ns = now + period_ns + delay_ns + origin_off_ns;
    } else {
        target_ns = sc.last_origin_ns + period_ns + delay_ns;
        /* Re-anchor cleanly if caller missed more than a full frame interval */
        if (target_ns + period_ns < now) {
            target_ns = now + period_ns + delay_ns;
        }
    }

    const uint64_t token = sc.next_token++;
    sc.scheduled_token = token;
    sc.scheduled_target_ns = target_ns;

    inout.out_token = token;
    inout.out_target_ns = target_ns;
    inout.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t pacing_wait_frame_event(
    std::mutex &device_mutex,
    SwapchainState &sc,
    scarlett_d3d12xs_wait_frame_params_t &inout) noexcept {
    if (inout.event_type != SCARLETT_FRAME_EVENT_ORIGIN &&
        inout.event_type != SCARLETT_FRAME_EVENT_PRESENT) {
        inout.out_status = SCARLETT_E_INVALIDARG;
        return inout.out_status;
    }

    uint64_t target_ns = 0u;
    uint64_t token = 0u;
    uint64_t deadline_ns = UINT64_MAX;
    const uint64_t start_now = monotonic_now_ns();

    if (inout.timeout_ms > 0u && inout.timeout_ms < (UINT64_MAX / kNsPerMs)) {
        deadline_ns = start_now + inout.timeout_ms * kNsPerMs;
    } else if (inout.timeout_ms == 0u) {
        deadline_ns = start_now;
    }

    {
        std::lock_guard<std::mutex> lock(device_mutex);
        if (sc.is_suspended) {
            inout.out_status = SCARLETT_E_PENDING;
            return inout.out_status;
        }
        const uint64_t period_ns = static_cast<uint64_t>(sc.frame_period_us) * kNsPerUs;
        if (sc.scheduled_target_ns != 0u &&
            (inout.expected_token == 0u || inout.expected_token == sc.scheduled_token)) {
            target_ns = sc.scheduled_target_ns;
            token = sc.scheduled_token;
        } else {
            token = sc.next_token++;
            target_ns = (sc.last_origin_ns == 0u) ? (start_now + period_ns)
                                                  : (sc.last_origin_ns + period_ns);
            if (target_ns + period_ns < start_now) {
                target_ns = start_now + period_ns;
            }
        }
    }

    const uint64_t effective_wake_ns = (target_ns < deadline_ns) ? target_ns : deadline_ns;
    sleep_until_monotonic_ns(effective_wake_ns);
    const uint64_t actual_ns = monotonic_now_ns();

    const bool timed_out = (actual_ns < target_ns && actual_ns >= deadline_ns);
    {
        std::lock_guard<std::mutex> lock(device_mutex);
        if (!timed_out) {
            sc.last_origin_ns = target_ns;
            if (sc.scheduled_token == token) {
                sc.scheduled_target_ns = 0u;
            }
        }
    }

    inout.out_token = token;
    inout.out_event_timestamp_ns = actual_ns;
    inout.out_target_timestamp_ns = target_ns;
    inout.out_jitter_ns = static_cast<int64_t>(actual_ns) - static_cast<int64_t>(target_ns);
    inout.out_status = timed_out ? SCARLETT_S_FALSE : SCARLETT_OK;
    return inout.out_status;
}

scarlett_hresult_t presentx_submit(
    SwapchainState &sc,
    uint64_t /*device_caps_mask*/,
    scarlett_d3d12xs_presentx_params_t &inout) noexcept {
    if (sc.is_suspended) {
        inout.out_status = SCARLETT_E_PENDING;
        return inout.out_status;
    }

    if (inout.plane_count != 1u && inout.plane_count != 2u) {
        inout.out_status = SCARLETT_E_INVALIDARG;
        return inout.out_status;
    }

    const uint32_t plane0_vk_cs = map_color_space_to_vk(inout.planes[0].color_space);
    sc.last_plane0_vk_colorspace = plane0_vk_cs;

    /* Populate SMPTE ST.2086 HDR10 metadata when primary plane is G2084_P2020 */
    if (inout.planes[0].color_space == SCARLETT_COLOR_SPACE_G2084_P2020) {
        sc.hdr_enabled = true;
        sc.last_hdr_metadata.sType = SCAR_VK_STRUCTURE_TYPE_HDR_METADATA_EXT;
        sc.last_hdr_metadata.pNext = nullptr;
        /* Rec.2020 mastering primaries + D65 white point */
        sc.last_hdr_metadata.displayPrimaryRed = {0.708f, 0.292f};
        sc.last_hdr_metadata.displayPrimaryGreen = {0.170f, 0.797f};
        sc.last_hdr_metadata.displayPrimaryBlue = {0.131f, 0.046f};
        sc.last_hdr_metadata.whitePoint = {0.3127f, 0.3290f};
        sc.last_hdr_metadata.maxLuminance =
            inout.hdr_max_luminance > 0.0f ? inout.hdr_max_luminance : 1000.0f;
        sc.last_hdr_metadata.minLuminance =
            inout.hdr_min_luminance > 0.0f ? inout.hdr_min_luminance : 0.005f;
        sc.last_hdr_metadata.maxContentLightLevel =
            inout.hdr_max_cll > 0.0f ? inout.hdr_max_cll : 1000.0f;
        sc.last_hdr_metadata.maxFrameAverageLightLevel =
            inout.hdr_max_fall > 0.0f ? inout.hdr_max_fall : 400.0f;
    }

    uint32_t plane1_vk_cs = SCAR_VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    uint32_t gamedvr_captured = 0u;
    if (inout.plane_count == 2u) {
        plane1_vk_cs = map_color_space_to_vk(inout.planes[1].color_space);
        sc.last_plane1_vk_colorspace = plane1_vk_cs;
        gamedvr_captured = 1u;
        sc.gamedvr_captures++;
    }

    sc.present_serial++;

    inout.out_vk_color_space_plane0 = plane0_vk_cs;
    inout.out_vk_color_space_plane1 = plane1_vk_cs;
    inout.out_gamedvr_captured = gamedvr_captured;
    inout.out_swapchain_generation = sc.generation;
    inout.out_present_serial = sc.present_serial;
    inout.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

} // namespace scarlett::d3d12xs::unixlib
