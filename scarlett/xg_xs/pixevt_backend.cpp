#include "pixevt_backend.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace scarlett::xg_xs {

namespace {

struct PixStackFrame {
    std::string label;
    float color[4];
};

struct PixBackendState {
    std::mutex mutex;
    std::atomic<bool> enabled{true};
    PixDebugUtilsCallbacks callbacks{};
    std::unordered_map<uint64_t, std::vector<PixStackFrame>> stacks;
    std::unordered_map<uint64_t, uint32_t> underflow_counts;
    std::vector<ScarlettPixDebugUtilsRecord> records;
};

PixBackendState& get_pix_state() {
    static PixBackendState state;
    return state;
}

void copy_label_trunc(char* dst, size_t cap, const char* src) noexcept {
    if (dst == nullptr || cap == 0u) {
        return;
    }
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0u;
    while (i + 1u < cap && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

} // namespace

PixEventBackend& PixEventBackend::instance() {
    static PixEventBackend backend;
    return backend;
}

void PixEventBackend::set_enabled(bool enabled) noexcept {
    get_pix_state().enabled.store(enabled, std::memory_order_release);
}

bool PixEventBackend::is_enabled() const noexcept {
    return get_pix_state().enabled.load(std::memory_order_acquire);
}

void PixEventBackend::set_callbacks(const PixDebugUtilsCallbacks& callbacks) noexcept {
    auto& st = get_pix_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.callbacks = callbacks;
}

void PixEventBackend::clear_recorded_events() {
    auto& st = get_pix_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.records.clear();
    st.stacks.clear();
    st.underflow_counts.clear();
}

std::vector<ScarlettPixDebugUtilsRecord> PixEventBackend::get_recorded_events() const {
    auto& st = get_pix_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    return st.records;
}

void PixEventBackend::argb_to_rgba_float(uint64_t argb, float out_rgba[4]) noexcept {
    const uint32_t c = static_cast<uint32_t>(argb & 0xFFFFFFFFULL);
    out_rgba[0] = static_cast<float>((c >> 16u) & 0xFFu) / 255.0f; /* R */
    out_rgba[1] = static_cast<float>((c >>  8u) & 0xFFu) / 255.0f; /* G */
    out_rgba[2] = static_cast<float>((c >>  0u) & 0xFFu) / 255.0f; /* B */
    out_rgba[3] = static_cast<float>((c >> 24u) & 0xFFu) / 255.0f; /* A */
}

scarlett_hresult_t PixEventBackend::handle_event(scarlett_pix_event_params_t& params) {
    auto& st = get_pix_state();

    if (params.op_kind == 4u) {
        st.enabled.store(params.enabled_flag != 0u, std::memory_order_release);
        params.out_status = SCARLETT_OK;
        return SCARLETT_OK;
    }

    // Zero-overhead fast path when debug labels are disabled
    if (params.op_kind != 3u && !st.enabled.load(std::memory_order_acquire)) {
        params.out_depth = 0u;
        params.out_underflow_count = 0u;
        params.out_status = SCARLETT_OK;
        return SCARLETT_OK;
    }

    std::lock_guard<std::mutex> lock(st.mutex);
    auto& stack = st.stacks[params.command_list];
    auto& underflows = st.underflow_counts[params.command_list];

    if (params.op_kind == 3u) {
        params.out_depth = static_cast<uint32_t>(stack.size());
        params.out_underflow_count = underflows;
        params.out_status = SCARLETT_OK;
        return SCARLETT_OK;
    }

    const char* raw_label = (params.label_utf8_addr != 0ULL)
                                ? reinterpret_cast<const char*>(static_cast<uintptr_t>(params.label_utf8_addr))
                                : "";

    float rgba[4]{};
    argb_to_rgba_float(params.argb_color, rgba);

    if (params.op_kind == 0u) {
        if (stack.size() >= SCARLETT_PIX_MAX_STACK_DEPTH) {
            params.out_depth = static_cast<uint32_t>(stack.size());
            params.out_underflow_count = underflows;
            params.out_status = SCARLETT_E_OUTOFMEMORY;
            return params.out_status;
        }
        PixStackFrame frame{};
        frame.label = raw_label;
        std::memcpy(frame.color, rgba, sizeof(rgba));
        stack.push_back(frame);

        ScarlettPixDebugUtilsRecord rec{};
        rec.op_kind = 0u;
        rec.stack_depth = static_cast<uint32_t>(stack.size());
        rec.command_list = params.command_list;
        std::memcpy(rec.color, rgba, sizeof(rgba));
        copy_label_trunc(rec.label, sizeof(rec.label), raw_label);
        st.records.push_back(rec);

        if (st.callbacks.begin_label != nullptr) {
            ScarlettVkDebugUtilsLabelEXT vk_label{};
            vk_label.sType = SCARLETT_VK_STYPE_DEBUG_UTILS_LABEL_EXT;
            vk_label.pLabelNameAddr = reinterpret_cast<uint64_t>(rec.label);
            std::memcpy(vk_label.color, rgba, sizeof(rgba));
            st.callbacks.begin_label(params.command_list, &vk_label, st.callbacks.user_data);
        }

        params.out_depth = static_cast<uint32_t>(stack.size());
        params.out_underflow_count = underflows;
        params.out_status = SCARLETT_OK;
        return SCARLETT_OK;
    }

    if (params.op_kind == 1u) {
        if (stack.empty()) {
            underflows++;
            params.out_depth = 0u;
            params.out_underflow_count = underflows;
            params.out_status = SCARLETT_E_FAIL;
            return params.out_status;
        }
        PixStackFrame top = stack.back();
        stack.pop_back();

        ScarlettPixDebugUtilsRecord rec{};
        rec.op_kind = 1u;
        rec.stack_depth = static_cast<uint32_t>(stack.size());
        rec.command_list = params.command_list;
        std::memcpy(rec.color, top.color, sizeof(top.color));
        copy_label_trunc(rec.label, sizeof(rec.label), top.label.c_str());
        st.records.push_back(rec);

        if (st.callbacks.end_label != nullptr) {
            st.callbacks.end_label(params.command_list, st.callbacks.user_data);
        }

        params.out_depth = static_cast<uint32_t>(stack.size());
        params.out_underflow_count = underflows;
        params.out_status = SCARLETT_OK;
        return SCARLETT_OK;
    }

    if (params.op_kind == 2u) {
        ScarlettPixDebugUtilsRecord rec{};
        rec.op_kind = 2u;
        rec.stack_depth = static_cast<uint32_t>(stack.size());
        rec.command_list = params.command_list;
        std::memcpy(rec.color, rgba, sizeof(rgba));
        copy_label_trunc(rec.label, sizeof(rec.label), raw_label);
        st.records.push_back(rec);

        if (st.callbacks.insert_label != nullptr) {
            ScarlettVkDebugUtilsLabelEXT vk_label{};
            vk_label.sType = SCARLETT_VK_STYPE_DEBUG_UTILS_LABEL_EXT;
            vk_label.pLabelNameAddr = reinterpret_cast<uint64_t>(rec.label);
            std::memcpy(vk_label.color, rgba, sizeof(rgba));
            st.callbacks.insert_label(params.command_list, &vk_label, st.callbacks.user_data);
        }

        params.out_depth = static_cast<uint32_t>(stack.size());
        params.out_underflow_count = underflows;
        params.out_status = SCARLETT_OK;
        return SCARLETT_OK;
    }

    params.out_status = SCARLETT_E_INVALIDARG;
    return params.out_status;
}

} // namespace scarlett::xg_xs
