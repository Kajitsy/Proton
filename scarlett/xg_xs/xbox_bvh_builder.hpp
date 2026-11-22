#ifndef SCARLETT_XBOX_BVH_BUILDER_HPP
#define SCARLETT_XBOX_BVH_BUILDER_HPP

#include <cstdint>

#include "xg_xs_unix_abi.h"

namespace scarlett::xg_xs {

#define SCARLETT_BVH4_MAGIC UINT32_C(0x34485642) /* "BVH4" */

struct ScarlettBvh4Header {
    uint32_t magic;
    uint32_t version;
    uint32_t as_type;
    uint32_t build_flags;
    uint32_t root_node_ptr;
    uint32_t box_node_count;
    uint32_t leaf_node_count;
    uint32_t primitive_count;
    uint64_t box_nodes_offset;
    uint64_t leaf_nodes_offset;
    uint64_t total_bytes;
    float root_aabb_min[3];
    float root_aabb_max[3];
    uint64_t tlas_instances_offset;
    uint32_t tlas_instance_count;
    uint32_t reserved;
};

SCARLETT_STATIC_ASSERT(sizeof(ScarlettBvh4Header) == 96,
                       "ScarlettBvh4Header must be 96 bytes");

struct ScarlettBvh4TlasInstanceRecord {
    float inv_transform[12];
    uint32_t instance_id;
    uint32_t instance_mask;
    uint32_t instance_index;
    uint32_t flags;
    uint64_t blas_header_addr;
};

SCARLETT_STATIC_ASSERT(sizeof(ScarlettBvh4TlasInstanceRecord) == 72,
                       "ScarlettBvh4TlasInstanceRecord must be 72 bytes");

scarlett_hresult_t bvh_get_prebuild_info(
    const XBVH_BUILD_INPUTS& inputs,
    XBVH_PREBUILD_INFO& out_info) noexcept;

scarlett_hresult_t bvh_build_acceleration_structure(
    const XBVH_BUILD_INPUTS& inputs,
    void* dst_bvh,
    uint64_t dst_capacity,
    const void* src_bvh,
    void* scratch,
    uint64_t scratch_capacity,
    uint64_t& out_written_bytes) noexcept;

scarlett_hresult_t bvh_intersect_ray(
    const void* bvh_data,
    const XBVH_RAY_DESC& ray,
    XBVH_RAY_HIT& out_hit) noexcept;

scarlett_hresult_t bvh_map_to_vulkan_build_info(
    const XBVH_BUILD_INPUTS& inputs,
    uint32_t has_vk_khr_accel_struct,
    uint64_t dst_as_handle,
    uint64_t src_as_handle,
    uint64_t scratch_bda,
    ScarlettVkAccelTrianglesDataKHR* out_geometries,
    uint32_t geometries_capacity,
    ScarlettVkAccelBuildRangeInfoKHR* out_ranges,
    ScarlettVkAccelBuildGeometryInfoKHR& out_build_info) noexcept;

} // namespace scarlett::xg_xs

#endif // SCARLETT_XBOX_BVH_BUILDER_HPP
