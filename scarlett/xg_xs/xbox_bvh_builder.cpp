#include "xbox_bvh_builder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace scarlett::xg_xs {

namespace {

struct Aabb {
    float min[3];
    float max[3];

    static Aabb empty() noexcept {
        constexpr float inf = std::numeric_limits<float>::infinity();
        return {{inf, inf, inf}, {-inf, -inf, -inf}};
    }

    void expand(const float p[3]) noexcept {
        for (int i = 0; i < 3; ++i) {
            min[i] = std::min(min[i], p[i]);
            max[i] = std::max(max[i], p[i]);
        }
    }

    void expand(const Aabb& o) noexcept {
        for (int i = 0; i < 3; ++i) {
            min[i] = std::min(min[i], o.min[i]);
            max[i] = std::max(max[i], o.max[i]);
        }
    }

    float surface_area() const noexcept {
        const float dx = std::max(0.0f, max[0] - min[0]);
        const float dy = std::max(0.0f, max[1] - min[1]);
        const float dz = std::max(0.0f, max[2] - min[2]);
        return 2.0f * (dx * dy + dy * dz + dz * dx);
    }

    void centroid(float out[3]) const noexcept {
        out[0] = 0.5f * (min[0] + max[0]);
        out[1] = 0.5f * (min[1] + max[1]);
        out[2] = 0.5f * (min[2] + max[2]);
    }
};

struct PrimItem {
    Aabb bounds;
    float centroid[3];
    uint32_t prim_index;
    uint32_t geom_index;
    uint32_t geom_flags;
    float v0[3];
    float v1[3];
    float v2[3];
};

struct BinNode {
    Aabb bounds{};
    int left{-1};
    int right{-1};
    uint32_t prim_start{0};
    uint32_t prim_count{0};
    bool is_leaf() const noexcept { return left < 0 && right < 0; }
};

uint64_t align_up_64(uint64_t v, uint64_t a) noexcept {
    return (v + a - 1ULL) & ~(a - 1ULL);
}

bool read_triangle(
    const XBVH_TRIANGLES_DESC& desc,
    uint32_t tri_idx,
    float v0[3],
    float v1[3],
    float v2[3]) noexcept {
    if (desc.vertex_buffer_addr == 0ULL || desc.vertex_stride_bytes < 12u) {
        return false;
    }
    const auto* vb = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(desc.vertex_buffer_addr));
    uint32_t i0 = 0u, i1 = 0u, i2 = 0u;
    if (desc.index_buffer_addr != 0ULL && desc.index_count > 0u) {
        if (tri_idx * 3u + 2u >= desc.index_count) {
            return false;
        }
        const auto* ib = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(desc.index_buffer_addr));
        if (desc.index_format_bytes == 2u) {
            const auto* ib16 = reinterpret_cast<const uint16_t*>(ib);
            i0 = ib16[tri_idx * 3u + 0u];
            i1 = ib16[tri_idx * 3u + 1u];
            i2 = ib16[tri_idx * 3u + 2u];
        } else if (desc.index_format_bytes == 4u) {
            const auto* ib32 = reinterpret_cast<const uint32_t*>(ib);
            i0 = ib32[tri_idx * 3u + 0u];
            i1 = ib32[tri_idx * 3u + 1u];
            i2 = ib32[tri_idx * 3u + 2u];
        } else {
            return false;
        }
    } else {
        i0 = tri_idx * 3u + 0u;
        i1 = tri_idx * 3u + 1u;
        i2 = tri_idx * 3u + 2u;
    }

    if (i0 >= desc.vertex_count || i1 >= desc.vertex_count || i2 >= desc.vertex_count) {
        return false;
    }
    std::memcpy(v0, vb + static_cast<size_t>(i0) * desc.vertex_stride_bytes, 12);
    std::memcpy(v1, vb + static_cast<size_t>(i1) * desc.vertex_stride_bytes, 12);
    std::memcpy(v2, vb + static_cast<size_t>(i2) * desc.vertex_stride_bytes, 12);
    return true;
}

void transform_point(const float m[12], const float p[3], float out[3]) noexcept {
    out[0] = m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3];
    out[1] = m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7];
    out[2] = m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11];
}

void transform_vector(const float m[12], const float v[3], float out[3]) noexcept {
    out[0] = m[0] * v[0] + m[1] * v[1] + m[2] * v[2];
    out[1] = m[4] * v[0] + m[5] * v[1] + m[6] * v[2];
    out[2] = m[8] * v[0] + m[9] * v[1] + m[10] * v[2];
}

bool invert_affine_3x4(const float m[12], float inv[12]) noexcept {
    const float a00 = m[0], a01 = m[1], a02 = m[2];
    const float a10 = m[4], a11 = m[5], a12 = m[6];
    const float a20 = m[8], a21 = m[9], a22 = m[10];

    const float c00 = a11 * a22 - a12 * a21;
    const float c01 = -(a10 * a22 - a12 * a20);
    const float c02 = a10 * a21 - a11 * a20;
    const float det = a00 * c00 + a01 * c01 + a02 * c02;
    if (std::fabs(det) < 1e-12f) {
        return false;
    }
    const float inv_det = 1.0f / det;
    inv[0] = c00 * inv_det;
    inv[1] = (a02 * a21 - a01 * a22) * inv_det;
    inv[2] = (a01 * a12 - a02 * a11) * inv_det;
    inv[4] = c01 * inv_det;
    inv[5] = (a00 * a22 - a02 * a20) * inv_det;
    inv[6] = (a02 * a10 - a00 * a12) * inv_det;
    inv[8] = c02 * inv_det;
    inv[9] = (a01 * a20 - a00 * a21) * inv_det;
    inv[10] = (a00 * a11 - a01 * a10) * inv_det;

    const float tx = m[3], ty = m[7], tz = m[11];
    inv[3] = -(inv[0] * tx + inv[1] * ty + inv[2] * tz);
    inv[7] = -(inv[4] * tx + inv[5] * ty + inv[6] * tz);
    inv[11] = -(inv[8] * tx + inv[9] * ty + inv[10] * tz);
    return true;
}

Aabb compute_transformed_aabb(
    const float m[12],
    const float bmin[3],
    const float bmax[3]) noexcept {
    Aabb out = Aabb::empty();
    for (int corner = 0; corner < 8; ++corner) {
        const float p[3] = {
            (corner & 1) ? bmax[0] : bmin[0],
            (corner & 2) ? bmax[1] : bmin[1],
            (corner & 4) ? bmax[2] : bmin[2],
        };
        float tp[3];
        transform_point(m, p, tp);
        out.expand(tp);
    }
    return out;
}

int build_binary_sah(
    std::vector<PrimItem>& prims,
    std::vector<BinNode>& nodes,
    uint32_t start,
    uint32_t count) {
    const int node_idx = static_cast<int>(nodes.size());
    nodes.emplace_back();

    Aabb node_bounds = Aabb::empty();
    Aabb cent_bounds = Aabb::empty();
    for (uint32_t i = 0; i < count; ++i) {
        node_bounds.expand(prims[start + i].bounds);
        cent_bounds.expand(prims[start + i].centroid);
    }
    nodes[node_idx].bounds = node_bounds;
    nodes[node_idx].prim_start = start;
    nodes[node_idx].prim_count = count;

    if (count <= 1u) {
        return node_idx;
    }

    constexpr uint32_t kBins = 16u;
    float best_cost = std::numeric_limits<float>::infinity();
    int best_axis = -1;
    uint32_t best_split = 0u;
    const float parent_sa = std::max(node_bounds.surface_area(), 1e-8f);

    for (int axis = 0; axis < 3; ++axis) {
        const float cmin = cent_bounds.min[axis];
        const float cmax = cent_bounds.max[axis];
        const float extent = cmax - cmin;
        if (extent <= 1e-7f) {
            continue;
        }

        struct Bin {
            Aabb bounds = Aabb::empty();
            uint32_t count = 0u;
        };
        std::array<Bin, kBins> bins;
        const float scale = static_cast<float>(kBins) / extent;

        for (uint32_t i = 0; i < count; ++i) {
            uint32_t b = static_cast<uint32_t>((prims[start + i].centroid[axis] - cmin) * scale);
            if (b >= kBins) {
                b = kBins - 1u;
            }
            bins[b].count++;
            bins[b].bounds.expand(prims[start + i].bounds);
        }

        std::array<Aabb, kBins> left_aabb;
        std::array<uint32_t, kBins> left_cnt{};
        Aabb cur_aabb = Aabb::empty();
        uint32_t cur_cnt = 0u;
        for (uint32_t i = 0; i < kBins - 1u; ++i) {
            cur_cnt += bins[i].count;
            if (bins[i].count > 0u) {
                cur_aabb.expand(bins[i].bounds);
            }
            left_aabb[i] = cur_aabb;
            left_cnt[i] = cur_cnt;
        }

        cur_aabb = Aabb::empty();
        cur_cnt = 0u;
        for (int i = static_cast<int>(kBins) - 1; i >= 1; --i) {
            cur_cnt += bins[i].count;
            if (bins[i].count > 0u) {
                cur_aabb.expand(bins[i].bounds);
            }
            const uint32_t lcnt = left_cnt[i - 1];
            if (lcnt == 0u || cur_cnt == 0u) {
                continue;
            }
            const float cost = 1.0f +
                (left_aabb[i - 1].surface_area() * static_cast<float>(lcnt) +
                 cur_aabb.surface_area() * static_cast<float>(cur_cnt)) / parent_sa;
            if (cost < best_cost) {
                best_cost = cost;
                best_axis = axis;
                best_split = static_cast<uint32_t>(i);
            }
        }
    }

    uint32_t mid = start + count / 2u;
    if (best_axis >= 0) {
        const float cmin = cent_bounds.min[best_axis];
        const float extent = cent_bounds.max[best_axis] - cmin;
        const float scale = static_cast<float>(kBins) / extent;
        auto* begin_p = prims.data() + start;
        auto* end_p = begin_p + count;
        auto* split_p = std::partition(begin_p, end_p, [&](const PrimItem& p) {
            uint32_t b = static_cast<uint32_t>((p.centroid[best_axis] - cmin) * scale);
            if (b >= kBins) {
                b = kBins - 1u;
            }
            return b < best_split;
        });
        mid = static_cast<uint32_t>(split_p - prims.data());
        if (mid == start || mid == start + count) {
            mid = start + count / 2u;
        }
    } else {
        // Fallback: split longest axis by median
        const float dx = cent_bounds.max[0] - cent_bounds.min[0];
        const float dy = cent_bounds.max[1] - cent_bounds.min[1];
        const float dz = cent_bounds.max[2] - cent_bounds.min[2];
        const int axis = (dx >= dy && dx >= dz) ? 0 : ((dy >= dz) ? 1 : 2);
        std::nth_element(
            prims.begin() + start,
            prims.begin() + mid,
            prims.begin() + start + count,
            [axis](const PrimItem& a, const PrimItem& b) {
                return a.centroid[axis] < b.centroid[axis];
            });
    }

    const int left_idx = build_binary_sah(prims, nodes, start, mid - start);
    const int right_idx = build_binary_sah(prims, nodes, mid, start + count - mid);
    nodes[node_idx].left = left_idx;
    nodes[node_idx].right = right_idx;
    return node_idx;
}

uint32_t emit_bvh4_recursive(
    const std::vector<BinNode>& bin_nodes,
    const std::vector<PrimItem>& prims,
    int bin_idx,
    uint32_t parent_box_idx,
    std::vector<ScarlettRdn2Bvh4Box32Node>& out_boxes,
    std::vector<ScarlettRdn2Bvh4TriLeafNode>& out_leaves) {
    const BinNode& bn = bin_nodes[bin_idx];
    if (bn.is_leaf()) {
        const uint32_t leaf_idx = static_cast<uint32_t>(out_leaves.size());
        ScarlettRdn2Bvh4TriLeafNode leaf{};
        const PrimItem& p = prims[bn.prim_start];
        std::memcpy(leaf.v0, p.v0, sizeof(leaf.v0));
        std::memcpy(leaf.v1, p.v1, sizeof(leaf.v1));
        std::memcpy(leaf.v2, p.v2, sizeof(leaf.v2));
        leaf.primitive_index = p.prim_index;
        leaf.geometry_index_and_flags = (p.geom_index & 0x00FFFFFFu) | ((p.geom_flags & 0xFFu) << 24u);
        leaf.instance_index = p.prim_index;
        leaf.parent_node_index = parent_box_idx;
        out_leaves.push_back(leaf);
        return (leaf_idx << 3u) | SCARLETT_BVH4_NODE_TRIANGLE;
    }

    // Collapse up to 4 children by expanding highest-SA internal child
    std::array<int, 4> children{};
    uint32_t child_count = 0u;
    children[child_count++] = bn.left;
    children[child_count++] = bn.right;

    while (child_count < 4u) {
        int expand_slot = -1;
        float best_sa = -1.0f;
        for (uint32_t c = 0u; c < child_count; ++c) {
            const BinNode& cn = bin_nodes[children[c]];
            if (!cn.is_leaf()) {
                const float sa = cn.bounds.surface_area();
                if (sa > best_sa) {
                    best_sa = sa;
                    expand_slot = static_cast<int>(c);
                }
            }
        }
        if (expand_slot < 0) {
            break;
        }
        const BinNode& exp_node = bin_nodes[children[expand_slot]];
        children[expand_slot] = exp_node.left;
        children[child_count++] = exp_node.right;
    }

    // Allocate this box node in pre-order (guarantees parent_idx < child_idx!)
    const uint32_t box_idx = static_cast<uint32_t>(out_boxes.size());
    out_boxes.emplace_back();

    ScarlettRdn2Bvh4Box32Node box{};
    box.child_count = child_count;
    box.parent_node_index = parent_box_idx;
    box.first_prim_or_leaf = bn.prim_start;
    for (uint32_t i = 0u; i < 4u; ++i) {
        box.child_ptrs[i] = SCARLETT_BVH4_INVALID_PTR;
        box.min_x[i] = box.min_y[i] = box.min_z[i] = 0.0f;
        box.max_x[i] = box.max_y[i] = box.max_z[i] = 0.0f;
    }

    for (uint32_t c = 0u; c < child_count; ++c) {
        const BinNode& cn = bin_nodes[children[c]];
        box.min_x[c] = cn.bounds.min[0];
        box.min_y[c] = cn.bounds.min[1];
        box.min_z[c] = cn.bounds.min[2];
        box.max_x[c] = cn.bounds.max[0];
        box.max_y[c] = cn.bounds.max[1];
        box.max_z[c] = cn.bounds.max[2];
        box.child_ptrs[c] = emit_bvh4_recursive(
            bin_nodes, prims, children[c], box_idx, out_boxes, out_leaves);
    }

    out_boxes[box_idx] = box;
    return (box_idx << 3u) | SCARLETT_BVH4_NODE_BOX32;
}

uint32_t count_total_primitives(const XBVH_BUILD_INPUTS& inputs) noexcept {
    if (inputs.num_descs == 0u || inputs.descs_addr == 0ULL) {
        return 0u;
    }
    if (inputs.type == XBVH_AS_TYPE_TOP_LEVEL) {
        return inputs.num_descs;
    }
    const auto* geoms = reinterpret_cast<const XBVH_TRIANGLES_DESC*>(
        static_cast<uintptr_t>(inputs.descs_addr));
    uint32_t total = 0u;
    for (uint32_t i = 0u; i < inputs.num_descs; ++i) {
        const uint32_t count = (geoms[i].index_count > 0u)
                                   ? (geoms[i].index_count / 3u)
                                   : (geoms[i].vertex_count / 3u);
        total += count;
    }
    return total;
}

bool intersect_aabb(
    const float orig[3],
    const float inv_dir[3],
    float t_min,
    float t_max,
    float bmin_x, float bmin_y, float bmin_z,
    float bmax_x, float bmax_y, float bmax_z,
    float& out_t_near) noexcept {
    const float tx1 = (bmin_x - orig[0]) * inv_dir[0];
    const float tx2 = (bmax_x - orig[0]) * inv_dir[0];
    float tmin = std::min(tx1, tx2);
    float tmax = std::max(tx1, tx2);

    const float ty1 = (bmin_y - orig[1]) * inv_dir[1];
    const float ty2 = (bmax_y - orig[1]) * inv_dir[1];
    tmin = std::max(tmin, std::min(ty1, ty2));
    tmax = std::min(tmax, std::max(ty1, ty2));

    const float tz1 = (bmin_z - orig[2]) * inv_dir[2];
    const float tz2 = (bmax_z - orig[2]) * inv_dir[2];
    tmin = std::max(tmin, std::min(tz1, tz2));
    tmax = std::min(tmax, std::max(tz1, tz2));

    tmin = std::max(tmin, t_min);
    tmax = std::min(tmax, t_max);
    out_t_near = tmin;
    return tmax >= tmin;
}

bool intersect_triangle_mt(
    const float orig[3],
    const float dir[3],
    float t_min,
    float t_max,
    const float v0[3],
    const float v1[3],
    const float v2[3],
    float& out_t,
    float& out_u,
    float& out_v) noexcept {
    const float e1[3] = {v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2]};
    const float e2[3] = {v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2]};
    const float p[3] = {
        dir[1] * e2[2] - dir[2] * e2[1],
        dir[2] * e2[0] - dir[0] * e2[2],
        dir[0] * e2[1] - dir[1] * e2[0],
    };
    const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (std::fabs(det) < 1e-9f) {
        return false;
    }
    const float inv_det = 1.0f / det;
    const float s[3] = {orig[0] - v0[0], orig[1] - v0[1], orig[2] - v0[2]};
    const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv_det;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }
    const float q[3] = {
        s[1] * e1[2] - s[2] * e1[1],
        s[2] * e1[0] - s[0] * e1[2],
        s[0] * e1[1] - s[1] * e1[0],
    };
    const float v = (dir[0] * q[0] + dir[1] * q[1] + dir[2] * q[2]) * inv_det;
    if (v < 0.0f || (u + v) > 1.0f) {
        return false;
    }
    const float t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv_det;
    if (t < t_min || t > t_max) {
        return false;
    }
    out_t = t;
    out_u = u;
    out_v = v;
    return true;
}

void compute_node_aabb(
    uint32_t node_ptr,
    const ScarlettRdn2Bvh4Box32Node* boxes,
    const ScarlettRdn2Bvh4TriLeafNode* leaves,
    uint32_t as_type,
    Aabb& out_aabb) noexcept {
    const uint32_t node_type = node_ptr & 0x7u;
    const uint32_t node_idx = node_ptr >> 3u;
    if (node_type == SCARLETT_BVH4_NODE_TRIANGLE) {
        const auto& leaf = leaves[node_idx];
        if (as_type == XBVH_AS_TYPE_TOP_LEVEL) {
            out_aabb.min[0] = leaf.v0[0];
            out_aabb.min[1] = leaf.v0[1];
            out_aabb.min[2] = leaf.v0[2];
            out_aabb.max[0] = leaf.v1[0];
            out_aabb.max[1] = leaf.v1[1];
            out_aabb.max[2] = leaf.v1[2];
        } else {
            out_aabb = Aabb::empty();
            out_aabb.expand(leaf.v0);
            out_aabb.expand(leaf.v1);
            out_aabb.expand(leaf.v2);
        }
    } else {
        const auto& box = boxes[node_idx];
        out_aabb = Aabb::empty();
        for (uint32_t c = 0u; c < box.child_count; ++c) {
            const float bmin[3] = {box.min_x[c], box.min_y[c], box.min_z[c]};
            const float bmax[3] = {box.max_x[c], box.max_y[c], box.max_z[c]};
            out_aabb.expand(bmin);
            out_aabb.expand(bmax);
        }
    }
}

scarlett_hresult_t perform_refit_update(
    const XBVH_BUILD_INPUTS& inputs,
    void* dst_bvh,
    uint64_t dst_capacity,
    const void* src_bvh,
    uint64_t& out_written_bytes) noexcept {
    const void* source = (src_bvh != nullptr) ? src_bvh : dst_bvh;
    if (source == nullptr || dst_bvh == nullptr) {
        return SCARLETT_E_POINTER;
    }
    const auto* src_hdr = static_cast<const ScarlettBvh4Header*>(source);
    if (src_hdr->magic != SCARLETT_BVH4_MAGIC || src_hdr->as_type != inputs.type) {
        return SCARLETT_E_INVALIDARG;
    }
    if ((src_hdr->build_flags & XBVH_BUILD_FLAG_ALLOW_UPDATE) == 0u) {
        return SCARLETT_E_INVALIDARG;
    }
    if (dst_capacity < src_hdr->total_bytes) {
        return SCARLETT_E_OUTOFMEMORY;
    }
    if (dst_bvh != source) {
        std::memcpy(dst_bvh, source, static_cast<size_t>(src_hdr->total_bytes));
    }

    auto* hdr = static_cast<ScarlettBvh4Header*>(dst_bvh);
    auto* base_ptr = static_cast<uint8_t*>(dst_bvh);
    auto* boxes = reinterpret_cast<ScarlettRdn2Bvh4Box32Node*>(base_ptr + hdr->box_nodes_offset);
    auto* leaves = reinterpret_cast<ScarlettRdn2Bvh4TriLeafNode*>(base_ptr + hdr->leaf_nodes_offset);

    if (inputs.type == XBVH_AS_TYPE_BOTTOM_LEVEL) {
        const auto* geoms = reinterpret_cast<const XBVH_TRIANGLES_DESC*>(
            static_cast<uintptr_t>(inputs.descs_addr));
        std::vector<uint32_t> geom_offsets(inputs.num_descs, 0u);
        uint32_t accum = 0u;
        for (uint32_t g = 0u; g < inputs.num_descs; ++g) {
            geom_offsets[g] = accum;
            accum += (geoms[g].index_count > 0u)
                         ? (geoms[g].index_count / 3u)
                         : (geoms[g].vertex_count / 3u);
        }
        if (accum != hdr->leaf_node_count) {
            return SCARLETT_E_INVALIDARG;
        }
        for (uint32_t l = 0u; l < hdr->leaf_node_count; ++l) {
            const uint32_t geom_idx = leaves[l].geometry_index_and_flags & 0x00FFFFFFu;
            if (geom_idx >= inputs.num_descs) {
                return SCARLETT_E_INVALIDARG;
            }
            const uint32_t local_tri = leaves[l].primitive_index - geom_offsets[geom_idx];
            if (!read_triangle(geoms[geom_idx], local_tri, leaves[l].v0, leaves[l].v1, leaves[l].v2)) {
                return SCARLETT_E_INVALIDARG;
            }
        }
    } else {
        if (inputs.num_descs != hdr->tlas_instance_count) {
            return SCARLETT_E_INVALIDARG;
        }
        const auto* inst_descs = reinterpret_cast<const XBVH_INSTANCE_DESC*>(
            static_cast<uintptr_t>(inputs.descs_addr));
        auto* inst_recs = reinterpret_cast<ScarlettBvh4TlasInstanceRecord*>(
            base_ptr + hdr->tlas_instances_offset);
        for (uint32_t l = 0u; l < hdr->leaf_node_count; ++l) {
            const uint32_t idx = leaves[l].instance_index;
            const auto& idesc = inst_descs[idx];
            const auto* blas_hdr = reinterpret_cast<const ScarlettBvh4Header*>(
                static_cast<uintptr_t>(idesc.blas_header_addr));
            if (blas_hdr == nullptr || blas_hdr->magic != SCARLETT_BVH4_MAGIC) {
                return SCARLETT_E_INVALIDARG;
            }
            const Aabb world_aabb = compute_transformed_aabb(
                idesc.transform, blas_hdr->root_aabb_min, blas_hdr->root_aabb_max);
            std::memcpy(leaves[l].v0, world_aabb.min, 12);
            std::memcpy(leaves[l].v1, world_aabb.max, 12);
            if (!invert_affine_3x4(idesc.transform, inst_recs[idx].inv_transform)) {
                return SCARLETT_E_INVALIDARG;
            }
            inst_recs[idx].instance_mask = idesc.instance_mask;
            inst_recs[idx].blas_header_addr = idesc.blas_header_addr;
        }
    }

    // Bottom-up refit in reverse pre-order
    for (int i = static_cast<int>(hdr->box_node_count) - 1; i >= 0; --i) {
        auto& box = boxes[i];
        for (uint32_t c = 0u; c < box.child_count; ++c) {
            Aabb ca{};
            compute_node_aabb(box.child_ptrs[c], boxes, leaves, hdr->as_type, ca);
            box.min_x[c] = ca.min[0];
            box.min_y[c] = ca.min[1];
            box.min_z[c] = ca.min[2];
            box.max_x[c] = ca.max[0];
            box.max_y[c] = ca.max[1];
            box.max_z[c] = ca.max[2];
        }
    }

    if (hdr->root_node_ptr != SCARLETT_BVH4_INVALID_PTR) {
        Aabb ra{};
        compute_node_aabb(hdr->root_node_ptr, boxes, leaves, hdr->as_type, ra);
        std::memcpy(hdr->root_aabb_min, ra.min, 12);
        std::memcpy(hdr->root_aabb_max, ra.max, 12);
    }

    out_written_bytes = hdr->total_bytes;
    return SCARLETT_OK;
}

} // namespace

scarlett_hresult_t bvh_get_prebuild_info(
    const XBVH_BUILD_INPUTS& inputs,
    XBVH_PREBUILD_INFO& out_info) noexcept {
    if (inputs.type != XBVH_AS_TYPE_BOTTOM_LEVEL && inputs.type != XBVH_AS_TYPE_TOP_LEVEL) {
        return SCARLETT_E_INVALIDARG;
    }
    const uint32_t prims = count_total_primitives(inputs);
    const uint64_t max_boxes = std::max<uint64_t>(1ULL, prims);
    const uint64_t inst_bytes = (inputs.type == XBVH_AS_TYPE_TOP_LEVEL)
                                    ? static_cast<uint64_t>(prims) * sizeof(ScarlettBvh4TlasInstanceRecord)
                                    : 0ULL;
    const uint64_t result_bytes = align_up_64(
        sizeof(ScarlettBvh4Header) +
            max_boxes * sizeof(ScarlettRdn2Bvh4Box32Node) +
            static_cast<uint64_t>(prims) * sizeof(ScarlettRdn2Bvh4TriLeafNode) +
            inst_bytes + 64ULL,
        256ULL);

    out_info.ResultDataMaxSizeInBytes = result_bytes;
    out_info.ScratchDataSizeInBytes = align_up_64(
        std::max<uint64_t>(4096ULL, static_cast<uint64_t>(prims) * 128ULL), 256ULL);
    out_info.UpdateScratchDataSizeInBytes = align_up_64(
        std::max<uint64_t>(1024ULL, static_cast<uint64_t>(prims) * 32ULL), 256ULL);
    return SCARLETT_OK;
}

scarlett_hresult_t bvh_build_acceleration_structure(
    const XBVH_BUILD_INPUTS& inputs,
    void* dst_bvh,
    uint64_t dst_capacity,
    const void* src_bvh,
    void* scratch,
    uint64_t scratch_capacity,
    uint64_t& out_written_bytes) noexcept {
    (void)scratch;
    (void)scratch_capacity;
    out_written_bytes = 0ULL;

    if (dst_bvh == nullptr) {
        return SCARLETT_E_POINTER;
    }
    if ((inputs.flags & XBVH_BUILD_FLAG_PERFORM_UPDATE) != 0u) {
        return perform_refit_update(inputs, dst_bvh, dst_capacity, src_bvh, out_written_bytes);
    }

    std::vector<PrimItem> prims;
    std::vector<ScarlettBvh4TlasInstanceRecord> tlas_recs;

    if (inputs.type == XBVH_AS_TYPE_BOTTOM_LEVEL) {
        if (inputs.num_descs > 0u && inputs.descs_addr == 0ULL) {
            return SCARLETT_E_INVALIDARG;
        }
        const auto* geoms = reinterpret_cast<const XBVH_TRIANGLES_DESC*>(
            static_cast<uintptr_t>(inputs.descs_addr));
        uint32_t global_prim = 0u;
        for (uint32_t g = 0u; g < inputs.num_descs; ++g) {
            const uint32_t tri_count = (geoms[g].index_count > 0u)
                                           ? (geoms[g].index_count / 3u)
                                           : (geoms[g].vertex_count / 3u);
            for (uint32_t t = 0u; t < tri_count; ++t) {
                PrimItem item{};
                if (!read_triangle(geoms[g], t, item.v0, item.v1, item.v2)) {
                    return SCARLETT_E_INVALIDARG;
                }
                item.bounds = Aabb::empty();
                item.bounds.expand(item.v0);
                item.bounds.expand(item.v1);
                item.bounds.expand(item.v2);
                item.bounds.centroid(item.centroid);
                item.prim_index = global_prim++;
                item.geom_index = g;
                item.geom_flags = geoms[g].geometry_flags;
                prims.push_back(item);
            }
        }
    } else if (inputs.type == XBVH_AS_TYPE_TOP_LEVEL) {
        if (inputs.num_descs > 0u && inputs.descs_addr == 0ULL) {
            return SCARLETT_E_INVALIDARG;
        }
        const auto* inst_descs = reinterpret_cast<const XBVH_INSTANCE_DESC*>(
            static_cast<uintptr_t>(inputs.descs_addr));
        tlas_recs.resize(inputs.num_descs);
        for (uint32_t i = 0u; i < inputs.num_descs; ++i) {
            const auto& idesc = inst_descs[i];
            const auto* blas_hdr = reinterpret_cast<const ScarlettBvh4Header*>(
                static_cast<uintptr_t>(idesc.blas_header_addr));
            if (blas_hdr == nullptr || blas_hdr->magic != SCARLETT_BVH4_MAGIC) {
                return SCARLETT_E_INVALIDARG;
            }
            if (!invert_affine_3x4(idesc.transform, tlas_recs[i].inv_transform)) {
                return SCARLETT_E_INVALIDARG;
            }
            tlas_recs[i].instance_id = idesc.instance_id;
            tlas_recs[i].instance_mask = idesc.instance_mask;
            tlas_recs[i].instance_index = i;
            tlas_recs[i].flags = idesc.flags;
            tlas_recs[i].blas_header_addr = idesc.blas_header_addr;

            const Aabb world_aabb = compute_transformed_aabb(
                idesc.transform, blas_hdr->root_aabb_min, blas_hdr->root_aabb_max);
            PrimItem item{};
            item.bounds = world_aabb;
            item.bounds.centroid(item.centroid);
            std::memcpy(item.v0, world_aabb.min, 12);
            std::memcpy(item.v1, world_aabb.max, 12);
            item.prim_index = i;
            item.geom_index = 0u;
            item.geom_flags = idesc.flags;
            prims.push_back(item);
        }
    } else {
        return SCARLETT_E_INVALIDARG;
    }

    std::vector<ScarlettRdn2Bvh4Box32Node> boxes;
    std::vector<ScarlettRdn2Bvh4TriLeafNode> leaves;
    uint32_t root_ptr = SCARLETT_BVH4_INVALID_PTR;
    Aabb root_aabb = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};

    if (!prims.empty()) {
        std::vector<BinNode> bin_nodes;
        bin_nodes.reserve(prims.size() * 2u);
        const int bin_root = build_binary_sah(
            prims, bin_nodes, 0u, static_cast<uint32_t>(prims.size()));
        root_aabb = bin_nodes[bin_root].bounds;
        boxes.reserve(prims.size());
        leaves.reserve(prims.size());
        root_ptr = emit_bvh4_recursive(
            bin_nodes, prims, bin_root, UINT32_MAX, boxes, leaves);
    }

    const uint64_t box_offset = align_up_64(sizeof(ScarlettBvh4Header), 64ULL);
    const uint64_t leaf_offset = align_up_64(
        box_offset + boxes.size() * sizeof(ScarlettRdn2Bvh4Box32Node), 64ULL);
    const uint64_t inst_offset = align_up_64(
        leaf_offset + leaves.size() * sizeof(ScarlettRdn2Bvh4TriLeafNode), 64ULL);
    const uint64_t total_bytes = align_up_64(
        inst_offset + tlas_recs.size() * sizeof(ScarlettBvh4TlasInstanceRecord), 64ULL);

    if (dst_capacity < total_bytes) {
        return SCARLETT_E_OUTOFMEMORY;
    }

    auto* base_ptr = static_cast<uint8_t*>(dst_bvh);
    std::memset(base_ptr, 0, static_cast<size_t>(total_bytes));

    auto* hdr = reinterpret_cast<ScarlettBvh4Header*>(base_ptr);
    hdr->magic = SCARLETT_BVH4_MAGIC;
    hdr->version = 1u;
    hdr->as_type = inputs.type;
    hdr->build_flags = inputs.flags;
    hdr->root_node_ptr = root_ptr;
    hdr->box_node_count = static_cast<uint32_t>(boxes.size());
    hdr->leaf_node_count = static_cast<uint32_t>(leaves.size());
    hdr->primitive_count = static_cast<uint32_t>(prims.size());
    hdr->box_nodes_offset = box_offset;
    hdr->leaf_nodes_offset = leaf_offset;
    hdr->total_bytes = total_bytes;
    std::memcpy(hdr->root_aabb_min, root_aabb.min, 12);
    std::memcpy(hdr->root_aabb_max, root_aabb.max, 12);
    hdr->tlas_instances_offset = inst_offset;
    hdr->tlas_instance_count = static_cast<uint32_t>(tlas_recs.size());

    if (!boxes.empty()) {
        std::memcpy(base_ptr + box_offset, boxes.data(),
                    boxes.size() * sizeof(ScarlettRdn2Bvh4Box32Node));
    }
    if (!leaves.empty()) {
        std::memcpy(base_ptr + leaf_offset, leaves.data(),
                    leaves.size() * sizeof(ScarlettRdn2Bvh4TriLeafNode));
    }
    if (!tlas_recs.empty()) {
        std::memcpy(base_ptr + inst_offset, tlas_recs.data(),
                    tlas_recs.size() * sizeof(ScarlettBvh4TlasInstanceRecord));
    }

    out_written_bytes = total_bytes;
    return SCARLETT_OK;
}

scarlett_hresult_t bvh_intersect_ray(
    const void* bvh_data,
    const XBVH_RAY_DESC& ray,
    XBVH_RAY_HIT& out_hit) noexcept {
    if (bvh_data == nullptr) {
        return SCARLETT_E_POINTER;
    }
    std::memset(&out_hit, 0, sizeof(out_hit));
    out_hit.t = ray.t_max;

    const auto* base_ptr = static_cast<const uint8_t*>(bvh_data);
    const auto* hdr = reinterpret_cast<const ScarlettBvh4Header*>(base_ptr);
    if (hdr->magic != SCARLETT_BVH4_MAGIC) {
        return SCARLETT_E_INVALIDARG;
    }
    if (hdr->root_node_ptr == SCARLETT_BVH4_INVALID_PTR) {
        return SCARLETT_OK;
    }

    const auto* boxes = reinterpret_cast<const ScarlettRdn2Bvh4Box32Node*>(
        base_ptr + hdr->box_nodes_offset);
    const auto* leaves = reinterpret_cast<const ScarlettRdn2Bvh4TriLeafNode*>(
        base_ptr + hdr->leaf_nodes_offset);

    const float inv_dir[3] = {
        (ray.direction[0] != 0.0f) ? (1.0f / ray.direction[0]) : std::numeric_limits<float>::infinity(),
        (ray.direction[1] != 0.0f) ? (1.0f / ray.direction[1]) : std::numeric_limits<float>::infinity(),
        (ray.direction[2] != 0.0f) ? (1.0f / ray.direction[2]) : std::numeric_limits<float>::infinity(),
    };

    struct StackEntry {
        uint32_t ptr;
        float t_near;
    };
    std::array<StackEntry, 128> stack{};
    int sp = 0;
    stack[sp++] = {hdr->root_node_ptr, ray.t_min};

    while (sp > 0) {
        const StackEntry cur = stack[--sp];
        if (cur.t_near > out_hit.t) {
            continue;
        }
        out_hit.nodes_visited++;

        const uint32_t node_type = cur.ptr & 0x7u;
        const uint32_t node_idx = cur.ptr >> 3u;

        if (node_type == SCARLETT_BVH4_NODE_TRIANGLE) {
            const auto& leaf = leaves[node_idx];
            if (hdr->as_type == XBVH_AS_TYPE_TOP_LEVEL) {
                const auto* inst_recs = reinterpret_cast<const ScarlettBvh4TlasInstanceRecord*>(
                    base_ptr + hdr->tlas_instances_offset);
                const auto& irec = inst_recs[leaf.instance_index];
                if ((irec.instance_mask & ray.instance_mask) == 0u) {
                    continue;
                }
                XBVH_RAY_DESC local_ray = ray;
                transform_point(irec.inv_transform, ray.origin, local_ray.origin);
                transform_vector(irec.inv_transform, ray.direction, local_ray.direction);
                local_ray.t_max = out_hit.t;

                XBVH_RAY_HIT blas_hit{};
                const auto* blas_ptr = reinterpret_cast<const void*>(
                    static_cast<uintptr_t>(irec.blas_header_addr));
                if (scarlett_succeeded(bvh_intersect_ray(blas_ptr, local_ray, blas_hit))) {
                    out_hit.nodes_visited += blas_hit.nodes_visited;
                    if (blas_hit.hit != 0u && blas_hit.t < out_hit.t) {
                        out_hit.hit = 1u;
                        out_hit.t = blas_hit.t;
                        out_hit.u = blas_hit.u;
                        out_hit.v = blas_hit.v;
                        out_hit.primitive_index = blas_hit.primitive_index;
                        out_hit.instance_index = irec.instance_id;
                    }
                }
            } else {
                float ht = 0.0f, hu = 0.0f, hv = 0.0f;
                if (intersect_triangle_mt(
                        ray.origin, ray.direction, ray.t_min, out_hit.t,
                        leaf.v0, leaf.v1, leaf.v2, ht, hu, hv)) {
                    out_hit.hit = 1u;
                    out_hit.t = ht;
                    out_hit.u = hu;
                    out_hit.v = hv;
                    out_hit.primitive_index = leaf.primitive_index;
                    out_hit.instance_index = 0u;
                }
            }
            continue;
        }

        const auto& box = boxes[node_idx];
        std::array<StackEntry, 4> hits{};
        int hit_cnt = 0;
        for (uint32_t c = 0u; c < box.child_count; ++c) {
            if (box.child_ptrs[c] == SCARLETT_BVH4_INVALID_PTR) {
                continue;
            }
            float t_near = 0.0f;
            if (intersect_aabb(
                    ray.origin, inv_dir, ray.t_min, out_hit.t,
                    box.min_x[c], box.min_y[c], box.min_z[c],
                    box.max_x[c], box.max_y[c], box.max_z[c],
                    t_near)) {
                hits[hit_cnt++] = {box.child_ptrs[c], t_near};
            }
        }
        // Sort descending by t_near using a 4-element sorting network (5 compare-swaps)
        // so the closest child sits at hits[hit_cnt - 1] and is popped first from stack.
        auto cmp_swap = [&](int i, int j) noexcept {
            if (j < hit_cnt && hits[i].t_near < hits[j].t_near) {
                std::swap(hits[i], hits[j]);
            }
        };
        if (hit_cnt > 1) {
            cmp_swap(0, 1);
            cmp_swap(2, 3);
            cmp_swap(0, 2);
            cmp_swap(1, 3);
            cmp_swap(1, 2);
        }
        for (int i = 0; i < hit_cnt && sp < static_cast<int>(stack.size()); ++i) {
            stack[sp++] = hits[i];
        }
    }

    return SCARLETT_OK;
}

scarlett_hresult_t bvh_map_to_vulkan_build_info(
    const XBVH_BUILD_INPUTS& inputs,
    uint32_t has_vk_khr_accel_struct,
    uint64_t dst_as_handle,
    uint64_t src_as_handle,
    uint64_t scratch_bda,
    ScarlettVkAccelTrianglesDataKHR* out_geometries,
    uint32_t geometries_capacity,
    ScarlettVkAccelBuildRangeInfoKHR* out_ranges,
    ScarlettVkAccelBuildGeometryInfoKHR& out_build_info) noexcept {
    if (inputs.num_descs > 0u &&
        (out_geometries == nullptr || out_ranges == nullptr ||
         geometries_capacity < inputs.num_descs)) {
        return SCARLETT_E_INVALIDARG;
    }

    std::memset(&out_build_info, 0, sizeof(out_build_info));
    out_build_info.sType = SCARLETT_VK_STYPE_ACCEL_BUILD_GEOMETRY_INFO_KHR;
    out_build_info.type = inputs.type;
    out_build_info.flags = inputs.flags;
    out_build_info.mode = ((inputs.flags & XBVH_BUILD_FLAG_PERFORM_UPDATE) != 0u) ? 1u : 0u;
    out_build_info.srcAccelerationStructure = src_as_handle;
    out_build_info.dstAccelerationStructure = dst_as_handle;
    out_build_info.geometryCount = inputs.num_descs;
    out_build_info.backend_selected = (has_vk_khr_accel_struct != 0u)
                                          ? XBVH_RT_BACKEND_VK_KHR_ACCEL_STRUCT
                                          : XBVH_RT_BACKEND_BDA_BVH4_SOFTWARE;
    out_build_info.pGeometriesAddr = reinterpret_cast<uint64_t>(out_geometries);
    out_build_info.scratchDataDeviceAddress = scratch_bda;

    if (inputs.type == XBVH_AS_TYPE_BOTTOM_LEVEL && inputs.num_descs > 0u) {
        const auto* geoms = reinterpret_cast<const XBVH_TRIANGLES_DESC*>(
            static_cast<uintptr_t>(inputs.descs_addr));
        for (uint32_t i = 0u; i < inputs.num_descs; ++i) {
            auto& vg = out_geometries[i];
            std::memset(&vg, 0, sizeof(vg));
            vg.sType = SCARLETT_VK_STYPE_ACCEL_GEOMETRY_TRIANGLES_KHR;
            vg.vertexFormat = 106u; /* VK_FORMAT_R32G32B32_SFLOAT */
            vg.vertexDataDeviceAddress = geoms[i].vertex_buffer_addr;
            vg.vertexStride = geoms[i].vertex_stride_bytes;
            vg.maxVertex = (geoms[i].vertex_count > 0u) ? (geoms[i].vertex_count - 1u) : 0u;
            if (geoms[i].index_buffer_addr != 0ULL && geoms[i].index_count > 0u) {
                vg.indexType = (geoms[i].index_format_bytes == 2u) ? 0u : 1u;
                vg.indexDataDeviceAddress = geoms[i].index_buffer_addr;
            } else {
                vg.indexType = 1000165000u; /* VK_INDEX_TYPE_NONE_KHR */
                vg.indexDataDeviceAddress = 0ULL;
            }

            out_ranges[i].primitiveCount = (geoms[i].index_count > 0u)
                                               ? (geoms[i].index_count / 3u)
                                               : (geoms[i].vertex_count / 3u);
            out_ranges[i].primitiveOffset = 0u;
            out_ranges[i].firstVertex = 0u;
            out_ranges[i].transformOffset = 0u;
        }
    } else if (inputs.type == XBVH_AS_TYPE_TOP_LEVEL && inputs.num_descs > 0u) {
        out_geometries[0].sType = SCARLETT_VK_STYPE_ACCEL_GEOMETRY_INSTANCES_KHR;
        out_geometries[0].vertexDataDeviceAddress = inputs.descs_addr;
        out_ranges[0].primitiveCount = inputs.num_descs;
        out_ranges[0].primitiveOffset = 0u;
        out_ranges[0].firstVertex = 0u;
        out_ranges[0].transformOffset = 0u;
    }

    return SCARLETT_OK;
}

} // namespace scarlett::xg_xs
