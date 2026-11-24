/*
 * Proton-Scarlett — d3d12_xs_unix.so Dense WINE_UNIX_CALL Dispatch Table
 *
 * Exports:
 *   - __wine_unix_call_funcs[] (exact opcode order 0..23, verified by SCARLETT_STATIC_ASSERT)
 *   - scarlett_d3d12xs_unix_call(uint32_t opcode, void *params)
 *   - scarlett_d3d12xs_set_stub_sink(...)
 */

#include "d3d12_xs_internal.hpp"

namespace scarlett::d3d12xs::unixlib {

namespace {

int32_t op_init(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().init_module(*static_cast<scarlett_unix_init_params_t *>(args));
}

int32_t op_create_device(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().create_device(
        *static_cast<scarlett_d3d12xs_create_device_params_t *>(args));
}

int32_t op_destroy_device(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().destroy_device(
        *static_cast<scarlett_d3d12xs_destroy_device_params_t *>(args));
}

int32_t op_query_caps(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().query_caps(
        *static_cast<scarlett_d3d12xs_query_caps_params_t *>(args));
}

int32_t op_set_amp_limits(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().set_amp_limits(
        *static_cast<scarlett_d3d12xs_set_amp_limits_params_t *>(args));
}

int32_t op_create_queue(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().create_queue(
        *static_cast<scarlett_d3d12xs_create_queue_params_t *>(args));
}

int32_t op_create_placed_resource(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().create_placed_resource(
        *static_cast<scarlett_d3d12xs_create_placed_resource_params_t *>(args));
}

int32_t op_destroy_resource(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().destroy_resource(
        *static_cast<scarlett_d3d12xs_destroy_resource_params_t *>(args));
}

int32_t op_create_desc_heap(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().create_desc_heap(
        *static_cast<scarlett_d3d12xs_create_desc_heap_params_t *>(args));
}

int32_t op_write_descriptor(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().write_descriptor(
        *static_cast<scarlett_d3d12xs_write_desc_params_t *>(args));
}

int32_t op_eval_desc_policy(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().eval_desc_policy(
        *static_cast<scarlett_d3d12xs_eval_desc_policy_params_t *>(args));
}

int32_t op_create_cmd_list(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().create_cmd_list(
        *static_cast<scarlett_d3d12xs_create_cmd_list_params_t *>(args));
}

int32_t op_record_barrier(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().record_barrier(
        *static_cast<scarlett_d3d12xs_record_barrier_params_t *>(args));
}

int32_t op_record_indirect(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().record_indirect(
        *static_cast<scarlett_d3d12xs_record_indirect_params_t *>(args));
}

int32_t op_execute_cmd_lists(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().execute_cmd_lists(
        *static_cast<scarlett_d3d12xs_execute_cmd_lists_params_t *>(args));
}

int32_t op_strip_dxil(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().strip_dxil(
        *static_cast<scarlett_d3d12xs_strip_dxil_params_t *>(args));
}

int32_t op_set_frame_interval(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().set_frame_interval(
        *static_cast<scarlett_d3d12xs_frame_interval_params_t *>(args));
}

int32_t op_schedule_frame_event(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().schedule_frame_event(
        *static_cast<scarlett_d3d12xs_schedule_frame_params_t *>(args));
}

int32_t op_wait_frame_event(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().wait_frame_event(
        *static_cast<scarlett_d3d12xs_wait_frame_params_t *>(args));
}

int32_t op_presentx(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().presentx(
        *static_cast<scarlett_d3d12xs_presentx_params_t *>(args));
}

int32_t op_resize_swapchain(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().resize_swapchain(
        *static_cast<scarlett_d3d12xs_resize_swapchain_params_t *>(args));
}

int32_t op_power_state(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().power_state(
        *static_cast<scarlett_d3d12xs_power_state_params_t *>(args));
}

int32_t op_dump_dred_fault(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().dump_dred_fault(
        *static_cast<scarlett_d3d12xs_dred_dump_params_t *>(args));
}

int32_t op_report_stub(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().report_stub(
        *static_cast<scarlett_d3d12xs_report_stub_params_t *>(args));
}

int32_t op_lift_shader(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().lift_shader(
        *static_cast<scarlett_d3d12xs_lift_shader_params_t *>(args));
}

int32_t op_check_mem(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return D3D12XsManager::instance().check_mem(
        *static_cast<scarlett_d3d12xs_check_mem_params_t *>(args));
}

} // namespace

} // namespace scarlett::d3d12xs::unixlib

extern "C" {

using wine_unixlib_entry_t = int32_t (*)(void *);

#define SCARLETT_D3D12XS_UNIX_FUNC_LIST                                        \
    scarlett::d3d12xs::unixlib::op_init,                   /*  0: INIT */      \
    scarlett::d3d12xs::unixlib::op_create_device,          /*  1: CREATE_DEVICE */ \
    scarlett::d3d12xs::unixlib::op_destroy_device,         /*  2: DESTROY_DEVICE */ \
    scarlett::d3d12xs::unixlib::op_query_caps,             /*  3: QUERY_CAPS */ \
    scarlett::d3d12xs::unixlib::op_set_amp_limits,         /*  4: SET_AMP_SHADER_LIMITS */ \
    scarlett::d3d12xs::unixlib::op_create_queue,           /*  5: CREATE_QUEUE */ \
    scarlett::d3d12xs::unixlib::op_create_placed_resource, /*  6: CREATE_PLACED_RESOURCE */ \
    scarlett::d3d12xs::unixlib::op_destroy_resource,       /*  7: DESTROY_RESOURCE */ \
    scarlett::d3d12xs::unixlib::op_create_desc_heap,       /*  8: CREATE_DESC_HEAP */ \
    scarlett::d3d12xs::unixlib::op_write_descriptor,       /*  9: WRITE_DESCRIPTOR */ \
    scarlett::d3d12xs::unixlib::op_eval_desc_policy,       /* 10: EVAL_DESC_POLICY */ \
    scarlett::d3d12xs::unixlib::op_create_cmd_list,        /* 11: CREATE_CMD_LIST */ \
    scarlett::d3d12xs::unixlib::op_record_barrier,         /* 12: RECORD_BARRIER */ \
    scarlett::d3d12xs::unixlib::op_record_indirect,        /* 13: RECORD_INDIRECT */ \
    scarlett::d3d12xs::unixlib::op_execute_cmd_lists,      /* 14: EXECUTE_CMD_LISTS */ \
    scarlett::d3d12xs::unixlib::op_strip_dxil,             /* 15: STRIP_DXIL */ \
    scarlett::d3d12xs::unixlib::op_set_frame_interval,     /* 16: SET_FRAME_INTERVAL */ \
    scarlett::d3d12xs::unixlib::op_schedule_frame_event,   /* 17: SCHEDULE_FRAME_EVENT */ \
    scarlett::d3d12xs::unixlib::op_wait_frame_event,       /* 18: WAIT_FRAME_EVENT */ \
    scarlett::d3d12xs::unixlib::op_presentx,               /* 19: PRESENTX */ \
    scarlett::d3d12xs::unixlib::op_resize_swapchain,       /* 20: RESIZE_SWAPCHAIN */ \
    scarlett::d3d12xs::unixlib::op_power_state,            /* 21: POWER_STATE */ \
    scarlett::d3d12xs::unixlib::op_dump_dred_fault,        /* 22: DUMP_DRED_FAULT */ \
    scarlett::d3d12xs::unixlib::op_report_stub,            /* 23: REPORT_STUB */ \
    scarlett::d3d12xs::unixlib::op_lift_shader,            /* 24: LIFT_SHADER */ \
    scarlett::d3d12xs::unixlib::op_check_mem               /* 25: CHECK_MEM */

static const wine_unixlib_entry_t k_d3d12xs_local_unix_funcs[] = {
    SCARLETT_D3D12XS_UNIX_FUNC_LIST
};

#if !defined(SCARLETT_D3D12XS_STATIC_HOST_LINK)
extern const wine_unixlib_entry_t __wine_unix_call_funcs[] SCARLETT_D3D12XS_API;
const wine_unixlib_entry_t __wine_unix_call_funcs[] = {
    SCARLETT_D3D12XS_UNIX_FUNC_LIST
};

SCARLETT_STATIC_ASSERT(
    sizeof(__wine_unix_call_funcs) / sizeof(__wine_unix_call_funcs[0]) ==
        SCARLETT_D3D12XS_UNIX_OP_COUNT,
    "__wine_unix_call_funcs table length must match SCARLETT_D3D12XS_UNIX_OP_COUNT");

SCARLETT_STATIC_ASSERT(
    sizeof(k_d3d12xs_local_unix_funcs) == sizeof(__wine_unix_call_funcs),
    "local and exported unix call tables must match");
#else
SCARLETT_STATIC_ASSERT(
    sizeof(k_d3d12xs_local_unix_funcs) / sizeof(k_d3d12xs_local_unix_funcs[0]) ==
        SCARLETT_D3D12XS_UNIX_OP_COUNT,
    "k_d3d12xs_local_unix_funcs table length must match SCARLETT_D3D12XS_UNIX_OP_COUNT");
#endif

SCARLETT_D3D12XS_API int32_t scarlett_d3d12xs_unix_call(uint32_t opcode, void *params) {
    if (opcode >= SCARLETT_D3D12XS_UNIX_OP_COUNT) {
        return SCARLETT_ERR_UNIX_CALL_INVALID_OPCODE;
    }
    return k_d3d12xs_local_unix_funcs[opcode](params);
}

SCARLETT_D3D12XS_API void scarlett_d3d12xs_set_stub_sink(
    scarlett_stub_sink_fn_t sink,
    void *user_data) {
    scarlett::d3d12xs::unixlib::D3D12XsManager::instance().set_stub_sink(sink, user_data);
}

} /* extern "C" */
