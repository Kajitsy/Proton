#include "xg_xs_tiling.hpp"
#include "xbox_bvh_builder.hpp"
#include "xg_sfs_coalescer.hpp"
#include "pixevt_backend.hpp"

#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace scarlett::xg_xs::unixlib {

namespace {

std::mutex g_stub_mutex;
scarlett_stub_sink_fn_t g_stub_sink = nullptr;
void* g_stub_sink_user_data = nullptr;
std::atomic<uint32_t> g_strict_mode{0u};
std::atomic<uint32_t> g_stub_hit_count{0u};

uint32_t resolve_effective_strict_mode(uint32_t caller_strict) noexcept {
    if (caller_strict != 0u || g_strict_mode.load(std::memory_order_acquire) != 0u) {
        return 1u;
    }
    const char* env_strict = std::getenv(SCARLETT_ENV_STRICT);
    if (env_strict != nullptr && std::strcmp(env_strict, "1") == 0) {
        return 1u;
    }
    return 0u;
}

int32_t op_init(void* args) {
    if (args == nullptr) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_unix_init_params_t*>(args);
    p->out_unix_abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;

    if (p->abi_version != SCARLETT_UNIX_CALL_ABI_VERSION ||
        p->module_id != SCARLETT_XG_XS_UNIX_MODULE_ID) {
        p->out_status = SCARLETT_ERR_ABI_VERSION_MISMATCH;
        return p->out_status;
    }

    g_strict_mode.store(p->strict_mode != 0u ? 1u : 0u, std::memory_order_release);
    p->out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

int32_t op_create_tex2d_computer(void* args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    auto* p = static_cast<scarlett_xg_create_computer_params_t*>(args);
    p->out_status = TextureComputerRegistry::instance().create_computer(
        p->desc, p->out_computer_handle);
    return p->out_status;
}

int32_t op_destroy_computer(void* args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    auto* p = static_cast<scarlett_xg_destroy_computer_params_t*>(args);
    p->out_status = TextureComputerRegistry::instance().destroy_computer(p->computer_handle);
    return p->out_status;
}

int32_t op_get_resource_layout(void* args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    auto* p = static_cast<scarlett_xg_layout_params_t*>(args);
    p->out_status = TextureComputerRegistry::instance().get_resource_layout(
        p->computer_handle, p->resource_layout);
    return p->out_status;
}

int32_t op_get_subresource_layout(void* args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    auto* p = static_cast<scarlett_xg_layout_params_t*>(args);
    p->out_status = TextureComputerRegistry::instance().get_subresource_layout(
        p->computer_handle, p->subresource, p->subresource_layout);
    return p->out_status;
}

int32_t op_get_texel_offset(void* args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    auto* p = static_cast<scarlett_xg_texel_offset_params_t*>(args);
    p->out_status = TextureComputerRegistry::instance().get_texel_offset(
        p->computer_handle,
        p->plane,
        p->mip_level,
        p->x,
        p->y,
        p->z_or_slice,
        p->sample,
        p->out_offset_bytes);
    return p->out_status;
}

int32_t op_copy_into_subresource(void* args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    auto* p = static_cast<scarlett_xg_copy_subresource_params_t*>(args);
    p->out_status = TextureComputerRegistry::instance().copy_into_subresource(
        p->computer_handle,
        reinterpret_cast<void*>(static_cast<uintptr_t>(p->dst_addr)),
        p->pitch_bytes,
        p->subresource,
        reinterpret_cast<const void*>(static_cast<uintptr_t>(p->src_addr)));
    return p->out_status;
}

int32_t op_copy_from_subresource(void* args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    auto* p = static_cast<scarlett_xg_copy_subresource_params_t*>(args);
    p->out_status = TextureComputerRegistry::instance().copy_from_subresource(
        p->computer_handle,
        reinterpret_cast<void*>(static_cast<uintptr_t>(p->dst_addr)),
        p->pitch_bytes,
        p->subresource,
        reinterpret_cast<const void*>(static_cast<uintptr_t>(p->src_addr)));
    return p->out_status;
}

int32_t op_compute_optimal_swizzle(void* args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    auto* p = static_cast<scarlett_xg_optimal_swizzle_params_t*>(args);
    p->out_swizzle_mode = compute_optimal_swizzle_mode(
        p->resource_type,
        p->format,
        p->width,
        p->height,
        p->depth_or_array,
        p->sample_count,
        p->bind_flags);
    return SCARLETT_OK;
}

int32_t op_select_transfer_path(void* args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    auto* p = static_cast<scarlett_xg_transfer_path_params_t*>(args);
    p->out_status = select_transfer_path(
        p->caps, p->swizzle_mode, p->format, p->out_decision);
    return p->out_status;
}

int32_t op_bvh_prebuild_info(void* args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    auto* p = static_cast<scarlett_xbvh_prebuild_params_t*>(args);
    p->out_status = bvh_get_prebuild_info(p->inputs, p->out_info);
    return p->out_status;
}

int32_t op_bvh_build(void* args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    auto* p = static_cast<scarlett_xbvh_build_params_t*>(args);
    p->out_status = bvh_build_acceleration_structure(
        p->inputs,
        reinterpret_cast<void*>(static_cast<uintptr_t>(p->dst_bvh_addr)),
        p->dst_bvh_capacity,
        reinterpret_cast<const void*>(static_cast<uintptr_t>(p->src_bvh_addr)),
        reinterpret_cast<void*>(static_cast<uintptr_t>(p->scratch_addr)),
        p->scratch_capacity,
        p->out_written_bytes);
    return p->out_status;
}

int32_t op_bvh_intersect_ray(void* args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    auto* p = static_cast<scarlett_xbvh_ray_params_t*>(args);
    p->out_status = bvh_intersect_ray(
        reinterpret_cast<const void*>(static_cast<uintptr_t>(p->bvh_addr)),
        p->ray,
        p->out_hit);
    return p->out_status;
}

int32_t op_bvh_map_vulkan(void* args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    auto* p = static_cast<scarlett_xbvh_map_vk_params_t*>(args);
    p->out_status = bvh_map_to_vulkan_build_info(
        p->inputs,
        p->has_vk_khr_accel_struct,
        p->dst_as_handle,
        p->src_as_handle,
        p->scratch_bda,
        reinterpret_cast<ScarlettVkAccelTrianglesDataKHR*>(
            static_cast<uintptr_t>(p->out_vk_geometries_addr)),
        p->vk_geometries_capacity,
        reinterpret_cast<ScarlettVkAccelBuildRangeInfoKHR*>(
            static_cast<uintptr_t>(p->out_vk_ranges_addr)),
        p->out_build_info);
    return p->out_status;
}

int32_t op_sfs_coalesce_binds(void* args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    auto* p = static_cast<scarlett_sfs_coalesce_params_t*>(args);

    std::vector<ScarlettVkSparseImageMemoryBind> img_binds;
    std::vector<ScarlettVkSparseMemoryBind> mem_binds;
    p->out_status = ScarlettSparseBindCoalescer::coalesce_binds(
        reinterpret_cast<const ScarlettSparseTileBindRequest*>(
            static_cast<uintptr_t>(p->requests_addr)),
        p->request_count,
        p->aspect_mask,
        img_binds,
        mem_binds,
        p->out_stats);
    if (scarlett_failed(p->out_status)) {
        return p->out_status;
    }

    p->out_image_bind_count = static_cast<uint32_t>(img_binds.size());
    p->out_memory_bind_count = static_cast<uint32_t>(mem_binds.size());

    if (p->out_image_binds_addr != 0ULL) {
        const uint32_t copy_img = std::min(p->image_binds_capacity, p->out_image_bind_count);
        if (copy_img > 0u) {
            std::memcpy(
                reinterpret_cast<void*>(static_cast<uintptr_t>(p->out_image_binds_addr)),
                img_binds.data(),
                static_cast<size_t>(copy_img) * sizeof(ScarlettVkSparseImageMemoryBind));
        }
    }
    if (p->out_memory_binds_addr != 0ULL) {
        const uint32_t copy_mem = std::min(p->memory_binds_capacity, p->out_memory_bind_count);
        if (copy_mem > 0u) {
            std::memcpy(
                reinterpret_cast<void*>(static_cast<uintptr_t>(p->out_memory_binds_addr)),
                mem_binds.data(),
                static_cast<size_t>(copy_mem) * sizeof(ScarlettVkSparseMemoryBind));
        }
    }

    return p->out_status;
}

int32_t op_pix_event(void* args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return PixEventBackend::instance().handle_event(
        *static_cast<scarlett_pix_event_params_t*>(args));
}

int32_t op_report_stub(void* args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    auto* p = static_cast<scarlett_xg_report_stub_params_t*>(args);
    p->ctx.return_hr = scarlett_stub_enforce_error(p->ctx.return_hr);
    p->ctx.strict_mode = resolve_effective_strict_mode(p->ctx.strict_mode);
    p->ctx.hit_count = g_stub_hit_count.fetch_add(1u, std::memory_order_relaxed) + 1u;

    scarlett_stub_action_t action = scarlett_stub_resolve_action(&p->ctx);

    scarlett_stub_sink_fn_t sink = nullptr;
    void* ud = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_stub_mutex);
        sink = g_stub_sink;
        ud = g_stub_sink_user_data;
    }

    std::fprintf(stderr,
                 "[scarlett-stub] dll=%s sym=%s ord=%u slot=%u rva=0x%" PRIx64
                 " rcx=0x%" PRIx64 " rdx=0x%" PRIx64 " r8=0x%" PRIx64
                 " r9=0x%" PRIx64 " strict=%u hr=0x%08x\n",
                 p->ctx.dll_name,
                 p->ctx.symbol_name,
                 p->ctx.ordinal,
                 p->ctx.vtable_slot,
                 p->ctx.caller_rva,
                 p->ctx.rcx,
                 p->ctx.rdx,
                 p->ctx.r8,
                 p->ctx.r9,
                 p->ctx.strict_mode,
                 static_cast<uint32_t>(p->ctx.return_hr));

    if (sink != nullptr) {
        action = sink(&p->ctx, ud);
    }

    p->out_action = static_cast<uint32_t>(action);
    p->out_status = (action == SCARLETT_STUB_ACTION_LOG_AND_ABORT)
                        ? SCARLETT_ERR_STRICT_STUB_ABORT
                        : p->ctx.return_hr;
    return p->out_status;
}

} // namespace

} // namespace scarlett::xg_xs::unixlib

extern "C" {

using wine_unixlib_entry_t = int32_t (*)(void*);

extern const wine_unixlib_entry_t __wine_unix_call_funcs[] SCARLETT_XG_API;
const wine_unixlib_entry_t __wine_unix_call_funcs[] = {
    scarlett::xg_xs::unixlib::op_init,                    /*  0: SCARLETT_XG_XS_UNIX_INIT */
    scarlett::xg_xs::unixlib::op_create_tex2d_computer,   /*  1: SCARLETT_XG_XS_UNIX_CREATE_TEX2D_COMPUTER */
    scarlett::xg_xs::unixlib::op_destroy_computer,        /*  2: SCARLETT_XG_XS_UNIX_DESTROY_COMPUTER */
    scarlett::xg_xs::unixlib::op_get_resource_layout,     /*  3: SCARLETT_XG_XS_UNIX_GET_RESOURCE_LAYOUT */
    scarlett::xg_xs::unixlib::op_get_subresource_layout,  /*  4: SCARLETT_XG_XS_UNIX_GET_SUBRESOURCE_LAYOUT */
    scarlett::xg_xs::unixlib::op_get_texel_offset,        /*  5: SCARLETT_XG_XS_UNIX_GET_TEXEL_OFFSET */
    scarlett::xg_xs::unixlib::op_copy_into_subresource,   /*  6: SCARLETT_XG_XS_UNIX_COPY_INTO_SUBRESOURCE */
    scarlett::xg_xs::unixlib::op_copy_from_subresource,   /*  7: SCARLETT_XG_XS_UNIX_COPY_FROM_SUBRESOURCE */
    scarlett::xg_xs::unixlib::op_compute_optimal_swizzle, /*  8: SCARLETT_XG_XS_UNIX_COMPUTE_OPTIMAL_SWIZZLE */
    scarlett::xg_xs::unixlib::op_select_transfer_path,    /*  9: SCARLETT_XG_XS_UNIX_SELECT_TRANSFER_PATH */
    scarlett::xg_xs::unixlib::op_bvh_prebuild_info,       /* 10: SCARLETT_XG_XS_UNIX_BVH_PREBUILD_INFO */
    scarlett::xg_xs::unixlib::op_bvh_build,               /* 11: SCARLETT_XG_XS_UNIX_BVH_BUILD */
    scarlett::xg_xs::unixlib::op_bvh_intersect_ray,       /* 12: SCARLETT_XG_XS_UNIX_BVH_INTERSECT_RAY */
    scarlett::xg_xs::unixlib::op_bvh_map_vulkan,          /* 13: SCARLETT_XG_XS_UNIX_BVH_MAP_VULKAN */
    scarlett::xg_xs::unixlib::op_sfs_coalesce_binds,      /* 14: SCARLETT_XG_XS_UNIX_SFS_COALESCE_BINDS */
    scarlett::xg_xs::unixlib::op_pix_event,               /* 15: SCARLETT_XG_XS_UNIX_PIX_EVENT */
    scarlett::xg_xs::unixlib::op_report_stub,             /* 16: SCARLETT_XG_XS_UNIX_REPORT_STUB */
};

SCARLETT_STATIC_ASSERT(
    sizeof(__wine_unix_call_funcs) / sizeof(__wine_unix_call_funcs[0]) ==
        SCARLETT_XG_XS_UNIX_OP_COUNT,
    "__wine_unix_call_funcs table length must match SCARLETT_XG_XS_UNIX_OP_COUNT");

SCARLETT_XG_API int32_t scarlett_xg_xs_unix_call(uint32_t opcode, void* params) {
    if (opcode >= SCARLETT_XG_XS_UNIX_OP_COUNT) {
        return SCARLETT_ERR_UNIX_CALL_INVALID_OPCODE;
    }
    return __wine_unix_call_funcs[opcode](params);
}

SCARLETT_XG_API void scarlett_xg_xs_set_stub_sink(
    scarlett_stub_sink_fn_t sink,
    void* user_data) {
    std::lock_guard<std::mutex> lock(scarlett::xg_xs::unixlib::g_stub_mutex);
    scarlett::xg_xs::unixlib::g_stub_sink = sink;
    scarlett::xg_xs::unixlib::g_stub_sink_user_data = user_data;
}

SCARLETT_XG_API void scarlett_xg_xs_set_strict_mode(uint32_t strict_mode) {
    scarlett::xg_xs::unixlib::g_strict_mode.store(
        strict_mode != 0u ? 1u : 0u, std::memory_order_release);
}

} /* extern "C" */
