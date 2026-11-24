/*
 * Proton-Scarlett — dstorage_xs_unix.so Dense WINE_UNIX_CALL Dispatch Table
 *
 * Exports:
 *   - __wine_unix_call_funcs[] (exact opcode order 0..22, verified by SCARLETT_STATIC_ASSERT)
 *   - scarlett_dstorage_unix_call(uint32_t opcode, void *params)
 *   - scarlett_dstorage_set_stub_sink(...)
 *   - scarlett_dstorage_reset_runtime()
 */

#include "dstorage_xs_core.hpp"
#include "dstorage_xs_api.h"

namespace scarlett::dstorage_xs::unixlib {

namespace {

int32_t op_init(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().init_module(
        *static_cast<scarlett_unix_init_params_t *>(args));
}

int32_t op_open_file(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().open_file(
        *static_cast<scarlett_dstorage_open_file_params_t *>(args));
}

int32_t op_close_file(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().close_file(
        *static_cast<scarlett_dstorage_close_file_params_t *>(args));
}

int32_t op_create_queue(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().create_queue(
        *static_cast<scarlett_dstorage_create_queue_params_t *>(args));
}

int32_t op_destroy_queue(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().destroy_queue(
        *static_cast<scarlett_dstorage_destroy_queue_params_t *>(args));
}

int32_t op_submit_request(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().submit_request_direct(
        *static_cast<scarlett_dstorage_submit_params_t *>(args));
}

int32_t op_poll_completion(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().poll_completion(
        *static_cast<scarlett_dstorage_poll_params_t *>(args));
}

int32_t op_load_bcpack_plugin(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().load_bcpack_plugin(
        *static_cast<scarlett_dstorage_bcpack_plugin_params_t *>(args));
}

int32_t op_enqueue_request(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().enqueue_request(
        *static_cast<scarlett_dstorage_enqueue_request_params_t *>(args));
}

int32_t op_enqueue_status(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().enqueue_status(
        *static_cast<scarlett_dstorage_enqueue_status_params_t *>(args));
}

int32_t op_enqueue_signal(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().enqueue_signal(
        *static_cast<scarlett_dstorage_enqueue_signal_params_t *>(args));
}

int32_t op_submit_queue(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().submit_queue(
        *static_cast<scarlett_dstorage_submit_queue_params_t *>(args));
}

int32_t op_cancel_tag(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().cancel_tag(
        *static_cast<scarlett_dstorage_cancel_tag_params_t *>(args));
}

int32_t op_query_queue(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().query_queue(
        *static_cast<scarlett_dstorage_query_queue_params_t *>(args));
}

int32_t op_create_status_array(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().create_status_array(
        *static_cast<scarlett_dstorage_create_status_params_t *>(args));
}

int32_t op_destroy_status_array(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().destroy_status_array(
        *static_cast<scarlett_dstorage_destroy_status_params_t *>(args));
}

int32_t op_query_status(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().query_status(
        *static_cast<scarlett_dstorage_query_status_params_t *>(args));
}

int32_t op_get_file_info(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().get_file_info(
        *static_cast<scarlett_dstorage_file_info_params_t *>(args));
}

int32_t op_retrieve_error_record(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().retrieve_error_record(
        *static_cast<scarlett_dstorage_error_record_params_t *>(args));
}

int32_t op_set_staging_size(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().set_staging_size(
        *static_cast<scarlett_dstorage_staging_size_params_t *>(args));
}

int32_t op_query_caps(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().query_caps(
        *static_cast<scarlett_dstorage_caps_params_t *>(args));
}

int32_t op_submit_batch(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().submit_batch(
        *static_cast<scarlett_dstorage_submit_batch_params_t *>(args));
}

int32_t op_report_stub(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().report_stub(
        *static_cast<scarlett_dstorage_report_stub_params_t *>(args));
}

int32_t op_wait_signals(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return DStorageManager::instance().wait_signals(
        *static_cast<scarlett_dstorage_wait_signals_params_t *>(args));
}

} // namespace

} // namespace scarlett::dstorage_xs::unixlib

extern "C" {

using wine_unixlib_entry_t = int32_t (*)(void *);

#define SCARLETT_DSTORAGE_UNIX_FUNC_LIST                                           \
    scarlett::dstorage_xs::unixlib::op_init,                  /*  0: INIT */       \
    scarlett::dstorage_xs::unixlib::op_open_file,             /*  1: OPEN_FILE */  \
    scarlett::dstorage_xs::unixlib::op_close_file,            /*  2: CLOSE_FILE */ \
    scarlett::dstorage_xs::unixlib::op_create_queue,          /*  3: CREATE_QUEUE */ \
    scarlett::dstorage_xs::unixlib::op_destroy_queue,         /*  4: DESTROY_QUEUE */ \
    scarlett::dstorage_xs::unixlib::op_submit_request,        /*  5: SUBMIT_REQUEST */ \
    scarlett::dstorage_xs::unixlib::op_poll_completion,       /*  6: POLL_COMPLETION */ \
    scarlett::dstorage_xs::unixlib::op_load_bcpack_plugin,    /*  7: LOAD_BCPACK_PLUGIN */ \
    scarlett::dstorage_xs::unixlib::op_enqueue_request,       /*  8: ENQUEUE_REQUEST */ \
    scarlett::dstorage_xs::unixlib::op_enqueue_status,        /*  9: ENQUEUE_STATUS */ \
    scarlett::dstorage_xs::unixlib::op_enqueue_signal,        /* 10: ENQUEUE_SIGNAL */ \
    scarlett::dstorage_xs::unixlib::op_submit_queue,          /* 11: SUBMIT_QUEUE */ \
    scarlett::dstorage_xs::unixlib::op_cancel_tag,            /* 12: CANCEL_TAG */ \
    scarlett::dstorage_xs::unixlib::op_query_queue,           /* 13: QUERY_QUEUE */ \
    scarlett::dstorage_xs::unixlib::op_create_status_array,   /* 14: CREATE_STATUS_ARRAY */ \
    scarlett::dstorage_xs::unixlib::op_destroy_status_array,  /* 15: DESTROY_STATUS_ARRAY */ \
    scarlett::dstorage_xs::unixlib::op_query_status,          /* 16: QUERY_STATUS */ \
    scarlett::dstorage_xs::unixlib::op_get_file_info,         /* 17: GET_FILE_INFO */ \
    scarlett::dstorage_xs::unixlib::op_retrieve_error_record, /* 18: RETRIEVE_ERROR_RECORD */ \
    scarlett::dstorage_xs::unixlib::op_set_staging_size,      /* 19: SET_STAGING_SIZE */ \
    scarlett::dstorage_xs::unixlib::op_query_caps,            /* 20: QUERY_CAPS */ \
    scarlett::dstorage_xs::unixlib::op_submit_batch,          /* 21: SUBMIT_BATCH */ \
    scarlett::dstorage_xs::unixlib::op_report_stub,           /* 22: REPORT_STUB */ \
    scarlett::dstorage_xs::unixlib::op_wait_signals           /* 23: WAIT_SIGNALS */

static const wine_unixlib_entry_t k_dstorage_local_unix_funcs[] = {
    SCARLETT_DSTORAGE_UNIX_FUNC_LIST
};

#if !defined(SCARLETT_DSTORAGEXS_STATIC_HOST_LINK)
extern const wine_unixlib_entry_t __wine_unix_call_funcs[] SCARLETT_DSTORAGEXS_API;
const wine_unixlib_entry_t __wine_unix_call_funcs[] = {
    SCARLETT_DSTORAGE_UNIX_FUNC_LIST
};

SCARLETT_STATIC_ASSERT(
    sizeof(__wine_unix_call_funcs) / sizeof(__wine_unix_call_funcs[0]) ==
        SCARLETT_DSTORAGE_UNIX_EXT_OP_COUNT,
    "__wine_unix_call_funcs table length must match SCARLETT_DSTORAGE_UNIX_EXT_OP_COUNT");

SCARLETT_STATIC_ASSERT(
    sizeof(k_dstorage_local_unix_funcs) == sizeof(__wine_unix_call_funcs),
    "local and exported unix call tables must match");
#else
SCARLETT_STATIC_ASSERT(
    sizeof(k_dstorage_local_unix_funcs) / sizeof(k_dstorage_local_unix_funcs[0]) ==
        SCARLETT_DSTORAGE_UNIX_EXT_OP_COUNT,
    "k_dstorage_local_unix_funcs table length must match SCARLETT_DSTORAGE_UNIX_EXT_OP_COUNT");
#endif

SCARLETT_DSTORAGEXS_API int32_t scarlett_dstorage_unix_call(uint32_t opcode, void *params) {
    if (opcode >= SCARLETT_DSTORAGE_UNIX_EXT_OP_COUNT) {
        return SCARLETT_ERR_UNIX_CALL_INVALID_OPCODE;
    }
    return k_dstorage_local_unix_funcs[opcode](params);
}

SCARLETT_DSTORAGEXS_API void scarlett_dstorage_set_stub_sink(
    scarlett_stub_sink_fn_t sink,
    void *user_data) {
    scarlett::dstorage_xs::unixlib::DStorageManager::instance().set_stub_sink(sink, user_data);
}

SCARLETT_DSTORAGEXS_API void scarlett_dstorage_reset_runtime(void) {
    scarlett::dstorage_xs::unixlib::DStorageManager::instance().reset_all();
}

} /* extern "C" */
