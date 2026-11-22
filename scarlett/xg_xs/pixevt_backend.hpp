#ifndef SCARLETT_PIXEVT_BACKEND_HPP
#define SCARLETT_PIXEVT_BACKEND_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "xg_xs_unix_abi.h"

namespace scarlett::xg_xs {

using PixBeginLabelFn = void (*)(uint64_t cmd_buf, const ScarlettVkDebugUtilsLabelEXT* label, void* user_data);
using PixEndLabelFn = void (*)(uint64_t cmd_buf, void* user_data);
using PixInsertLabelFn = void (*)(uint64_t cmd_buf, const ScarlettVkDebugUtilsLabelEXT* label, void* user_data);

struct PixDebugUtilsCallbacks {
    PixBeginLabelFn begin_label = nullptr;
    PixEndLabelFn end_label = nullptr;
    PixInsertLabelFn insert_label = nullptr;
    void* user_data = nullptr;
};

class PixEventBackend {
public:
    static PixEventBackend& instance();

    void set_enabled(bool enabled) noexcept;
    bool is_enabled() const noexcept;

    void set_callbacks(const PixDebugUtilsCallbacks& callbacks) noexcept;
    void clear_recorded_events();
    std::vector<ScarlettPixDebugUtilsRecord> get_recorded_events() const;

    scarlett_hresult_t handle_event(scarlett_pix_event_params_t& params);

    static void argb_to_rgba_float(uint64_t argb, float out_rgba[4]) noexcept;

private:
    PixEventBackend() = default;
};

} // namespace scarlett::xg_xs

#endif // SCARLETT_PIXEVT_BACKEND_HPP
