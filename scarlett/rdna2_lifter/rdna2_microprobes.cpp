/*
 * Proton-Scarlett — Stage 1 Real Vulkan 1.4 Compute Microprobes,
 * Clean-Room SHA-256 Pipeline Cache Key, & In-Process SPIRV-Tools Validator.
 *
 * Self-contained clean-room Vulkan 1.4 C ABI loaded dynamically via
 * dlopen("libvulkan.so.1") — zero compile-time dependency on <vulkan/vulkan.h>.
 *
 * Every GPU microprobe reads its input operands strictly from a VkBuffer
 * (StorageBuffer, set=0 binding=0) and writes results to VkBuffer (binding=1)
 * — NEVER compile-time constants — preventing driver constant-folding.
 */

#include "rdna2_lifter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace scarlett::rdna2 {

/* ============================================================================
 * Clean-Room SHA-256 (FIPS 180-4)
 * ============================================================================ */

namespace {

inline uint32_t rotr32(uint32_t x, uint32_t n) noexcept {
    return (x >> n) | (x << (32u - n));
}

constexpr uint32_t kSha256K[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

} // namespace

Sha256Digest compute_sha256(const uint8_t* data, size_t size) noexcept {
    uint32_t h[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };

    auto compress = [&](const uint8_t blk[64]) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(blk[i * 4 + 0]) << 24u) |
                   (static_cast<uint32_t>(blk[i * 4 + 1]) << 16u) |
                   (static_cast<uint32_t>(blk[i * 4 + 2]) << 8u) |
                   static_cast<uint32_t>(blk[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr32(w[i - 15], 7u) ^ rotr32(w[i - 15], 18u) ^ (w[i - 15] >> 3u);
            uint32_t s1 = rotr32(w[i - 2], 17u) ^ rotr32(w[i - 2], 19u) ^ (w[i - 2] >> 10u);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr32(e, 6u) ^ rotr32(e, 11u) ^ rotr32(e, 25u);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t t1 = hh + S1 + ch + kSha256K[i] + w[i];
            uint32_t S0 = rotr32(a, 2u) ^ rotr32(a, 13u) ^ rotr32(a, 22u);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    };

    const size_t full = size / 64u;
    for (size_t i = 0; i < full; ++i) {
        compress(data + i * 64u);
    }
    uint8_t tail[64];
    std::memset(tail, 0, sizeof(tail));
    const size_t rem = size % 64u;
    if (rem > 0u && data != nullptr) {
        std::memcpy(tail, data + full * 64u, rem);
    }
    tail[rem] = 0x80u;
    if (rem >= 56u) {
        compress(tail);
        std::memset(tail, 0, sizeof(tail));
    }
    const uint64_t bit_len = static_cast<uint64_t>(size) * 8ULL;
    for (int i = 0; i < 8; ++i) {
        tail[56 + i] = static_cast<uint8_t>((bit_len >> (56 - i * 8)) & 0xFFu);
    }
    compress(tail);

    Sha256Digest out{};
    for (int i = 0; i < 8; ++i) {
        out.bytes[i * 4 + 0] = static_cast<uint8_t>((h[i] >> 24u) & 0xFFu);
        out.bytes[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16u) & 0xFFu);
        out.bytes[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8u) & 0xFFu);
        out.bytes[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xFFu);
    }
    return out;
}

std::string Sha256Digest::to_hex() const {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string s;
    s.reserve(64);
    for (uint8_t b : bytes) {
        s.push_back(kHex[b >> 4u]);
        s.push_back(kHex[b & 0xFu]);
    }
    return s;
}

Sha256Digest compute_pipeline_cache_key(const GpuMicroprobeProfile& profile,
                                        scarlett_shader_mode_t mode,
                                        WaveStrategy wave_strategy,
                                        RtStrategy rt_strategy,
                                        const uint8_t* shader_bytes,
                                        size_t shader_size) noexcept {
    std::vector<uint8_t> buf;
    buf.reserve(48u + shader_size);
    auto push_u32 = [&](uint32_t v) {
        buf.push_back(static_cast<uint8_t>(v & 0xFFu));
        buf.push_back(static_cast<uint8_t>((v >> 8u) & 0xFFu));
        buf.push_back(static_cast<uint8_t>((v >> 16u) & 0xFFu));
        buf.push_back(static_cast<uint8_t>((v >> 24u) & 0xFFu));
    };
    auto push_u64 = [&](uint64_t v) {
        push_u32(static_cast<uint32_t>(v & 0xFFFFFFFFu));
        push_u32(static_cast<uint32_t>(v >> 32u));
    };
    push_u32(0x52444E32u); /* "RDN2" domain separator */
    push_u32(profile.vendor_id);
    push_u32(profile.device_id);
    push_u32(profile.driver_version);
    push_u64(profile.probe_bitmask);
    push_u32(profile.subgroup_size);
    push_u32(static_cast<uint32_t>(mode));
    push_u32(static_cast<uint32_t>(wave_strategy));
    push_u32(static_cast<uint32_t>(rt_strategy));
    if (shader_bytes != nullptr && shader_size > 0u) {
        buf.insert(buf.end(), shader_bytes, shader_bytes + shader_size);
    }
    return compute_sha256(buf.data(), buf.size());
}

/* ============================================================================
 * SPIR-V 1.6 Compute Microprobe Generators (Strict VkBuffer Input Operands!)
 * ============================================================================ */

namespace {

void spv_op(std::vector<uint32_t>& v, uint16_t op, std::initializer_list<uint32_t> args) {
    v.push_back((static_cast<uint32_t>(1u + args.size()) << 16u) | op);
    for (uint32_t a : args) v.push_back(a);
}

void spv_str(std::vector<uint32_t>& v, const char* s) {
    const size_t len = std::strlen(s);
    const size_t words = (len + 1u + 3u) / 4u;
    for (size_t i = 0; i < words; ++i) {
        uint32_t w = 0u;
        for (size_t b = 0; b < 4u; ++b) {
            size_t p = i * 4u + b;
            if (p < len) w |= static_cast<uint32_t>(static_cast<uint8_t>(s[p])) << (b * 8u);
        }
        v.push_back(w);
    }
}

void spv_op_str(std::vector<uint32_t>& v, uint16_t op, std::initializer_list<uint32_t> prefix, const char* s) {
    const size_t sw = (std::strlen(s) + 1u + 3u) / 4u;
    v.push_back((static_cast<uint32_t>(1u + prefix.size() + sw) << 16u) | op);
    for (uint32_t a : prefix) v.push_back(a);
    spv_str(v, s);
}

struct ProbeSpvBuilder {
    uint32_t next_id{1};
    std::vector<uint32_t> caps, exts, modes, decos, types, consts, globals, body, ifaces;
    uint32_t glsl_id{0}, main_id{0};
    uint32_t t_void{0}, t_u32{0}, t_f32{0}, t_bool{0}, t_fn{0};
    uint32_t t_rt_u32{0}, t_ssbo{0}, p_ssbo{0}, p_ssbo_u32{0};
    uint32_t v_in{0}, v_out{0};
    uint32_t c_u0{0}, c_u1{0}, c_u2{0}, c_u3{0};

    uint32_t id() { return next_id++; }

    void init_common() {
        spv_op(caps, 17, {1}); /* OpCapability Shader */
        glsl_id = id();
        main_id = id();

        spv_op(modes, 16, {main_id, 17, 1, 1, 1}); /* LocalSize 1 1 1 */

        t_void = id(); spv_op(types, 19, {t_void});
        t_bool = id(); spv_op(types, 20, {t_bool});
        t_u32  = id(); spv_op(types, 21, {t_u32, 32, 0});
        t_f32  = id(); spv_op(types, 22, {t_f32, 32});
        t_fn   = id(); spv_op(types, 33, {t_fn, t_void});

        t_rt_u32 = id(); spv_op(types, 29, {t_rt_u32, t_u32});
        spv_op(decos, 71, {t_rt_u32, 6, 4}); /* ArrayStride 4 */

        t_ssbo = id(); spv_op(types, 30, {t_ssbo, t_rt_u32});
        spv_op(decos, 71, {t_ssbo, 2}); /* Block */
        spv_op(decos, 72, {t_ssbo, 0, 35, 0}); /* Offset 0 */

        p_ssbo = id(); spv_op(types, 32, {p_ssbo, 12, t_ssbo});
        p_ssbo_u32 = id(); spv_op(types, 32, {p_ssbo_u32, 12, t_u32});

        c_u0 = id(); spv_op(consts, 43, {t_u32, c_u0, 0});
        c_u1 = id(); spv_op(consts, 43, {t_u32, c_u1, 1});
        c_u2 = id(); spv_op(consts, 43, {t_u32, c_u2, 2});
        c_u3 = id(); spv_op(consts, 43, {t_u32, c_u3, 3});

        v_in = id();
        spv_op(globals, 59, {p_ssbo, v_in, 12});
        spv_op(decos, 71, {v_in, 34, 0}); /* DescriptorSet 0 */
        spv_op(decos, 71, {v_in, 33, 0}); /* Binding 0 */
        spv_op(decos, 71, {v_in, 24});    /* NonWritable */
        ifaces.push_back(v_in);

        v_out = id();
        spv_op(globals, 59, {p_ssbo, v_out, 12});
        spv_op(decos, 71, {v_out, 34, 0}); /* DescriptorSet 0 */
        spv_op(decos, 71, {v_out, 33, 1}); /* Binding 1 */
        ifaces.push_back(v_out);

        spv_op(body, 54, {t_void, main_id, 0, t_fn});
        spv_op(body, 248, {id()}); /* OpLabel */
    }

    uint32_t load_in_u32(uint32_t idx_const) {
        uint32_t p = id();
        spv_op(body, 65, {p_ssbo_u32, p, v_in, c_u0, idx_const});
        uint32_t v = id();
        spv_op(body, 61, {t_u32, v, p});
        return v;
    }

    uint32_t load_in_f32(uint32_t idx_const) {
        uint32_t u = load_in_u32(idx_const);
        uint32_t f = id();
        spv_op(body, 124, {t_f32, f, u});
        return f;
    }

    void store_out_u32(uint32_t idx_const, uint32_t val_u32) {
        uint32_t p = id();
        spv_op(body, 65, {p_ssbo_u32, p, v_out, c_u0, idx_const});
        spv_op(body, 62, {p, val_u32});
    }

    void store_out_f32(uint32_t idx_const, uint32_t val_f32) {
        uint32_t u = id();
        spv_op(body, 124, {t_u32, u, val_f32});
        store_out_u32(idx_const, u);
    }

    std::vector<uint32_t> finish() {
        spv_op(body, 253, {}); /* OpReturn */
        spv_op(body, 56, {});  /* OpFunctionEnd */

        std::vector<uint32_t> out;
        out.push_back(0x07230203u);
        out.push_back(0x00010600u); /* SPIR-V 1.6 */
        out.push_back(0x005C0001u);
        out.push_back(next_id);
        out.push_back(0u);
        out.insert(out.end(), caps.begin(), caps.end());
        out.insert(out.end(), exts.begin(), exts.end());
        spv_op_str(out, 11, {glsl_id}, "GLSL.std.450");
        spv_op(out, 14, {0, 1}); /* Logical GLSL450 */
        {
            const uint32_t wc = static_cast<uint32_t>(3u + 2u + ifaces.size());
            out.push_back((wc << 16u) | 15u);
            out.push_back(5u); /* GLCompute */
            out.push_back(main_id);
            spv_str(out, "main");
            for (uint32_t iv : ifaces) out.push_back(iv);
        }
        out.insert(out.end(), modes.begin(), modes.end());
        out.insert(out.end(), decos.begin(), decos.end());
        out.insert(out.end(), types.begin(), types.end());
        out.insert(out.end(), consts.begin(), consts.end());
        out.insert(out.end(), globals.begin(), globals.end());
        out.insert(out.end(), body.begin(), body.end());
        return out;
    }
};

} // namespace

/* Probe 1: OpFmaKHR / GLSL.std.450 Fma single-rounding test reading (a,b,c) from VkBuffer */
std::vector<uint32_t> build_microprobe_fma_spirv(bool use_op_fma_khr) {
    ProbeSpvBuilder pb;
    pb.init_common();
    uint32_t a = pb.load_in_f32(pb.c_u0);
    uint32_t b = pb.load_in_f32(pb.c_u1);
    uint32_t c = pb.load_in_f32(pb.c_u2);
    uint32_t r = pb.id();
    if (use_op_fma_khr) {
        spv_op(pb.caps, 17, {6030}); /* Capability FMAKHR */
        spv_op_str(pb.exts, 10, {}, "SPV_KHR_fma");
        spv_op(pb.body, 4427, {pb.t_f32, r, a, b, c}); /* OpFmaKHR */
    } else {
        spv_op(pb.body, 12, {pb.t_f32, r, pb.glsl_id, 50, a, b, c}); /* GLSL.std.450 Fma */
        spv_op(pb.decos, 71, {r, 42}); /* NoContraction */
    }
    pb.store_out_f32(pb.c_u0, r);
    return pb.finish();
}

/* Probe 2: f32 denormal FTZ vs preserve reading subnormal operand from VkBuffer */
std::vector<uint32_t> build_microprobe_denorm_spirv(bool ftz_mode) {
    ProbeSpvBuilder pb;
    pb.init_common();
    if (ftz_mode) {
        spv_op(pb.caps, 17, {4465}); /* Capability DenormFlushToZero */
        spv_op(pb.modes, 16, {pb.main_id, 4460, 32}); /* DenormFlushToZero 32 */
    } else {
        spv_op(pb.caps, 17, {4464}); /* Capability DenormPreserve */
        spv_op(pb.modes, 16, {pb.main_id, 4459, 32}); /* DenormPreserve 32 */
    }
    uint32_t a = pb.load_in_f32(pb.c_u0);
    uint32_t b = pb.load_in_f32(pb.c_u1);
    uint32_t r = pb.id();
    spv_op(pb.body, 133, {pb.t_f32, r, a, b}); /* OpFMul */
    pb.store_out_f32(pb.c_u0, r);
    return pb.finish();
}

/* Probe 3: NaN propagation on NMin / NMax reading (qNaN, 3.5f) from VkBuffer */
std::vector<uint32_t> build_microprobe_nan_minmax_spirv() {
    ProbeSpvBuilder pb;
    pb.init_common();
    uint32_t nan_in = pb.load_in_f32(pb.c_u0);
    uint32_t val_in = pb.load_in_f32(pb.c_u1);
    uint32_t r_min = pb.id();
    spv_op(pb.body, 12, {pb.t_f32, r_min, pb.glsl_id, 79, nan_in, val_in}); /* NMin */
    uint32_t r_max = pb.id();
    spv_op(pb.body, 12, {pb.t_f32, r_max, pb.glsl_id, 80, val_in, nan_in}); /* NMax */
    pb.store_out_f32(pb.c_u0, r_min);
    pb.store_out_f32(pb.c_u1, r_max);
    return pb.finish();
}

/* Probe 4: SubgroupSize hardware measurement into VkBuffer */
std::vector<uint32_t> build_microprobe_subgroup_size_spirv() {
    ProbeSpvBuilder pb;
    pb.init_common();
    spv_op(pb.caps, 17, {61}); /* GroupNonUniform */
    uint32_t p_in_u32 = pb.id();
    spv_op(pb.types, 32, {p_in_u32, 1, pb.t_u32}); /* Input pointer */
    uint32_t v_sg = pb.id();
    spv_op(pb.globals, 59, {p_in_u32, v_sg, 1});
    spv_op(pb.decos, 71, {v_sg, 11, 36}); /* BuiltIn SubgroupSize (36) */
    pb.ifaces.push_back(v_sg);

    uint32_t sg = pb.id();
    spv_op(pb.body, 61, {pb.t_u32, sg, v_sg});
    uint32_t bias = pb.load_in_u32(pb.c_u0);
    uint32_t out_v = pb.id();
    spv_op(pb.body, 128, {pb.t_u32, out_v, sg, bias}); /* OpIAdd */
    pb.store_out_u32(pb.c_u0, out_v);
    return pb.finish();
}

/* Probe 5: Native Float16 NaN/denorm/FMA probe reading packed u32 from VkBuffer */
std::vector<uint32_t> build_microprobe_f16_spirv() {
    ProbeSpvBuilder pb;
    pb.init_common();
    spv_op(pb.caps, 17, {9}); /* Capability Float16 */
    uint32_t t_f16 = pb.id();
    spv_op(pb.types, 22, {t_f16, 16});
    uint32_t t_v2f16 = pb.id();
    spv_op(pb.types, 23, {t_v2f16, t_f16, 2});

    uint32_t u0 = pb.load_in_u32(pb.c_u0);
    uint32_t u1 = pb.load_in_u32(pb.c_u1);
    uint32_t h0 = pb.id(), h1 = pb.id(), hres = pb.id(), ures = pb.id();
    spv_op(pb.body, 124, {t_v2f16, h0, u0});
    spv_op(pb.body, 124, {t_v2f16, h1, u1});
    spv_op(pb.body, 12, {t_v2f16, hres, pb.glsl_id, 79, h0, h1}); /* NMin v2f16 */
    spv_op(pb.body, 124, {pb.t_u32, ures, hres});
    pb.store_out_u32(pb.c_u0, ures);
    return pb.finish();
}

/* ============================================================================
 * Self-Contained Vulkan 1.4 C ABI Runner (dlopen("libvulkan.so.1"))
 * ============================================================================ */

namespace vk_abi {

using VkBool32 = uint32_t;
using VkFlags = uint32_t;
using VkDeviceSize = uint64_t;
using VkResult = int32_t;

using VkInstance = void*;
using VkPhysicalDevice = void*;
using VkDevice = void*;
using VkQueue = void*;
using VkCommandBuffer = void*;

using VkDeviceMemory = uint64_t;
using VkBuffer = uint64_t;
using VkShaderModule = uint64_t;
using VkDescriptorSetLayout = uint64_t;
using VkPipelineLayout = uint64_t;
using VkPipeline = uint64_t;
using VkPipelineCache = uint64_t;
using VkDescriptorPool = uint64_t;
using VkDescriptorSet = uint64_t;
using VkCommandPool = uint64_t;
using VkFence = uint64_t;
using VkSemaphore = uint64_t;
using PFN_vkVoidFunction = void(*)();

constexpr uint32_t VK_API_VERSION_1_3 = (1u << 22u) | (3u << 12u);
constexpr uint32_t VK_API_VERSION_1_2 = (1u << 22u) | (2u << 12u);
constexpr VkDeviceSize VK_WHOLE_SIZE = UINT64_MAX;

struct VkApplicationInfo {
    uint32_t sType;
    const void* pNext;
    const char* pApplicationName;
    uint32_t applicationVersion;
    const char* pEngineName;
    uint32_t engineVersion;
    uint32_t apiVersion;
};

struct VkInstanceCreateInfo {
    uint32_t sType;
    const void* pNext;
    VkFlags flags;
    const VkApplicationInfo* pApplicationInfo;
    uint32_t enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
};

struct VkExtensionProperties {
    char extensionName[256];
    uint32_t specVersion;
};

struct VkPhysicalDeviceProperties {
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    uint32_t deviceType;
    char deviceName[256];
    uint8_t pipelineCacheUUID[16];
    alignas(8) uint8_t limits[504];
    uint32_t sparseProperties[5];
};
static_assert(sizeof(VkPhysicalDeviceProperties) == 824);

struct VkPhysicalDeviceProperties2 {
    uint32_t sType;
    void* pNext;
    VkPhysicalDeviceProperties properties;
};
static_assert(sizeof(VkPhysicalDeviceProperties2) == 840);

struct VkPhysicalDeviceSubgroupProperties {
    uint32_t sType; /* 1000094000 */
    void* pNext;
    uint32_t subgroupSize;
    VkFlags supportedStages;
    VkFlags supportedOperations;
    VkBool32 quadOperationsInAllStages;
};

struct VkPhysicalDeviceSubgroupSizeControlProperties {
    uint32_t sType; /* 1000225000 */
    void* pNext;
    uint32_t minSubgroupSize;
    uint32_t maxSubgroupSize;
    uint32_t maxComputeWorkgroupSubgroups;
    VkFlags requiredSubgroupSizeStages;
};

struct VkPhysicalDeviceFeatures2 {
    uint32_t sType; /* 1000059000 */
    void* pNext;
    uint32_t features[55];
};

struct VkPhysicalDeviceShaderFloat16Int8Features {
    uint32_t sType; /* 1000082000 */
    void* pNext;
    VkBool32 shaderFloat16;
    VkBool32 shaderInt8;
};

struct VkPhysicalDeviceBufferDeviceAddressFeatures {
    uint32_t sType; /* 1000257000 */
    void* pNext;
    VkBool32 bufferDeviceAddress;
    VkBool32 bufferDeviceAddressCaptureReplay;
    VkBool32 bufferDeviceAddressMultiDevice;
};

struct VkQueueFamilyProperties {
    VkFlags queueFlags;
    uint32_t queueCount;
    uint32_t timestampValidBits;
    uint32_t minImageTransferGranularity[3];
};

struct VkMemoryType {
    VkFlags propertyFlags;
    uint32_t heapIndex;
};

struct VkMemoryHeap {
    VkDeviceSize size;
    VkFlags flags;
};

struct VkPhysicalDeviceMemoryProperties {
    uint32_t memoryTypeCount;
    VkMemoryType memoryTypes[32];
    uint32_t memoryHeapCount;
    VkMemoryHeap memoryHeaps[16];
};

struct VkDeviceQueueCreateInfo {
    uint32_t sType; /* 2 */
    const void* pNext;
    VkFlags flags;
    uint32_t queueFamilyIndex;
    uint32_t queueCount;
    const float* pQueuePriorities;
};

struct VkDeviceCreateInfo {
    uint32_t sType; /* 3 */
    const void* pNext;
    VkFlags flags;
    uint32_t queueCreateInfoCount;
    const VkDeviceQueueCreateInfo* pQueueCreateInfos;
    uint32_t enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
    const void* pEnabledFeatures;
};

struct VkBufferCreateInfo {
    uint32_t sType; /* 12 */
    const void* pNext;
    VkFlags flags;
    VkDeviceSize size;
    VkFlags usage;
    uint32_t sharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t* pQueueFamilyIndices;
};

struct VkMemoryRequirements {
    VkDeviceSize size;
    VkDeviceSize alignment;
    uint32_t memoryTypeBits;
};

struct VkMemoryAllocateFlagsInfo {
    uint32_t sType; /* 1000060000 */
    const void* pNext;
    VkFlags flags;
    uint32_t deviceMask;
};

struct VkMemoryAllocateInfo {
    uint32_t sType; /* 5 */
    const void* pNext;
    VkDeviceSize allocationSize;
    uint32_t memoryTypeIndex;
};

struct VkBufferDeviceAddressInfo {
    uint32_t sType; /* 1000244001 */
    const void* pNext;
    VkBuffer buffer;
};

struct VkShaderModuleCreateInfo {
    uint32_t sType; /* 16 */
    const void* pNext;
    VkFlags flags;
    size_t codeSize;
    const uint32_t* pCode;
};

struct VkDescriptorSetLayoutBinding {
    uint32_t binding;
    uint32_t descriptorType;
    uint32_t descriptorCount;
    VkFlags stageFlags;
    const void* pImmutableSamplers;
};

struct VkDescriptorSetLayoutCreateInfo {
    uint32_t sType; /* 32 */
    const void* pNext;
    VkFlags flags;
    uint32_t bindingCount;
    const VkDescriptorSetLayoutBinding* pBindings;
};

struct VkPipelineLayoutCreateInfo {
    uint32_t sType; /* 30 */
    const void* pNext;
    VkFlags flags;
    uint32_t setLayoutCount;
    const VkDescriptorSetLayout* pSetLayouts;
    uint32_t pushConstantRangeCount;
    const void* pPushConstantRanges;
};

struct VkPipelineShaderStageCreateInfo {
    uint32_t sType; /* 18 */
    const void* pNext;
    VkFlags flags;
    VkFlags stage;
    VkShaderModule module;
    const char* pName;
    const void* pSpecializationInfo;
};

struct VkComputePipelineCreateInfo {
    uint32_t sType; /* 29 */
    const void* pNext;
    VkFlags flags;
    VkPipelineShaderStageCreateInfo stage;
    VkPipelineLayout layout;
    VkPipeline basePipelineHandle;
    int32_t basePipelineIndex;
};

struct VkDescriptorPoolSize {
    uint32_t type;
    uint32_t descriptorCount;
};

struct VkDescriptorPoolCreateInfo {
    uint32_t sType; /* 33 */
    const void* pNext;
    VkFlags flags;
    uint32_t maxSets;
    uint32_t poolSizeCount;
    const VkDescriptorPoolSize* pPoolSizes;
};

struct VkDescriptorSetAllocateInfo {
    uint32_t sType; /* 34 */
    const void* pNext;
    VkDescriptorPool descriptorPool;
    uint32_t descriptorSetCount;
    const VkDescriptorSetLayout* pSetLayouts;
};

struct VkDescriptorBufferInfo {
    VkBuffer buffer;
    VkDeviceSize offset;
    VkDeviceSize range;
};

struct VkWriteDescriptorSet {
    uint32_t sType; /* 35 */
    const void* pNext;
    VkDescriptorSet dstSet;
    uint32_t dstBinding;
    uint32_t dstArrayElement;
    uint32_t descriptorCount;
    uint32_t descriptorType;
    const void* pImageInfo;
    const VkDescriptorBufferInfo* pBufferInfo;
    const void* pTexelBufferView;
};

struct VkCommandPoolCreateInfo {
    uint32_t sType; /* 39 */
    const void* pNext;
    VkFlags flags;
    uint32_t queueFamilyIndex;
};

struct VkCommandBufferAllocateInfo {
    uint32_t sType; /* 40 */
    const void* pNext;
    VkCommandPool commandPool;
    uint32_t level;
    uint32_t commandBufferCount;
};

struct VkCommandBufferBeginInfo {
    uint32_t sType; /* 42 */
    const void* pNext;
    VkFlags flags;
    const void* pInheritanceInfo;
};

struct VkSubmitInfo {
    uint32_t sType; /* 4 */
    const void* pNext;
    uint32_t waitSemaphoreCount;
    const VkSemaphore* pWaitSemaphores;
    const VkFlags* pWaitDstStageMask;
    uint32_t commandBufferCount;
    const VkCommandBuffer* pCommandBuffers;
    uint32_t signalSemaphoreCount;
    const VkSemaphore* pSignalSemaphores;
};

struct VkFenceCreateInfo {
    uint32_t sType; /* 8 */
    const void* pNext;
    VkFlags flags;
};

/* Minimal scoped Vulkan 1.3/1.4 compute session (zero global static destruction order hazards!) */
class ScopedVulkanCompute {
public:
    bool init(bool enable_bda = false, bool enable_fp16 = false) {
        lib_ = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!lib_) return false;

        using PFN_GetInstProc = PFN_vkVoidFunction(*)(VkInstance, const char*);
        gipa_ = reinterpret_cast<PFN_GetInstProc>(dlsym(lib_, "vkGetInstanceProcAddr"));
        if (!gipa_) return false;

        auto create_inst = reinterpret_cast<VkResult(*)(const VkInstanceCreateInfo*, const void*, VkInstance*)>(
            gipa_(nullptr, "vkCreateInstance"));
        if (!create_inst) return false;

        VkApplicationInfo app{};
        app.sType = 0u;
        app.pApplicationName = "scarlett-rdna2-lifter";
        app.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo ici{};
        ici.sType = 1u;
        ici.pApplicationInfo = &app;
        if (create_inst(&ici, nullptr, &instance_) != 0 || !instance_) {
            app.apiVersion = VK_API_VERSION_1_2;
            if (create_inst(&ici, nullptr, &instance_) != 0 || !instance_) {
                return false;
            }
        }

        auto enum_phys = reinterpret_cast<VkResult(*)(VkInstance, uint32_t*, VkPhysicalDevice*)>(
            gipa_(instance_, "vkEnumeratePhysicalDevices"));
        uint32_t count = 0u;
        if (!enum_phys || enum_phys(instance_, &count, nullptr) != 0 || count == 0u) return false;
        std::vector<VkPhysicalDevice> devs(count);
        if (enum_phys(instance_, &count, devs.data()) != 0 || count == 0u) return false;
        phys_ = devs[0];

        /* Query properties + subgroup properties */
        auto get_props2 = reinterpret_cast<void(*)(VkPhysicalDevice, VkPhysicalDeviceProperties2*)>(
            gipa_(instance_, "vkGetPhysicalDeviceProperties2"));
        auto get_props = reinterpret_cast<void(*)(VkPhysicalDevice, VkPhysicalDeviceProperties*)>(
            gipa_(instance_, "vkGetPhysicalDeviceProperties"));
        if (get_props2) {
            sg_props_.sType = 1000094000u;
            sg_ctrl_props_.sType = 1000225000u;
            sg_props_.pNext = &sg_ctrl_props_;
            props2_.sType = 1000059001u;
            props2_.pNext = &sg_props_;
            get_props2(phys_, &props2_);
        } else if (get_props) {
            get_props(phys_, &props2_.properties);
            sg_props_.subgroupSize = 32u;
        }

        /* Query extensions */
        auto enum_ext = reinterpret_cast<VkResult(*)(VkPhysicalDevice, const char*, uint32_t*, VkExtensionProperties*)>(
            gipa_(instance_, "vkEnumerateDeviceExtensionProperties"));
        if (enum_ext) {
            uint32_t ec = 0u;
            if (enum_ext(phys_, nullptr, &ec, nullptr) == 0 && ec > 0u) {
                exts_.resize(ec);
                enum_ext(phys_, nullptr, &ec, exts_.data());
            }
        }

        /* Query features2 (Float16 + BufferDeviceAddress) */
        auto get_feats2 = reinterpret_cast<void(*)(VkPhysicalDevice, VkPhysicalDeviceFeatures2*)>(
            gipa_(instance_, "vkGetPhysicalDeviceFeatures2"));
        if (get_feats2) {
            f16_feats_.sType = 1000082000u;
            bda_feats_.sType = 1000257000u;
            f16_feats_.pNext = &bda_feats_;
            feats2_.sType = 1000059000u;
            feats2_.pNext = &f16_feats_;
            get_feats2(phys_, &feats2_);
        }

        /* Find compute queue family */
        auto get_qf = reinterpret_cast<void(*)(VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties*)>(
            gipa_(instance_, "vkGetPhysicalDeviceQueueFamilyProperties"));
        uint32_t qfc = 0u;
        get_qf(phys_, &qfc, nullptr);
        std::vector<VkQueueFamilyProperties> qfs(qfc);
        get_qf(phys_, &qfc, qfs.data());
        for (uint32_t i = 0; i < qfc; ++i) {
            if ((qfs[i].queueFlags & 0x2u) != 0u) {
                compute_qf_ = i;
                break;
            }
        }
        if (compute_qf_ == UINT32_MAX) return false;

        /* Memory properties */
        auto get_mem = reinterpret_cast<void(*)(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties*)>(
            gipa_(instance_, "vkGetPhysicalDeviceMemoryProperties"));
        get_mem(phys_, &mem_props_);

        /* Create logical device */
        float prio = 1.0f;
        VkDeviceQueueCreateInfo qci{};
        qci.sType = 2u;
        qci.queueFamilyIndex = compute_qf_;
        qci.queueCount = 1u;
        qci.pQueuePriorities = &prio;

        VkPhysicalDeviceBufferDeviceAddressFeatures req_bda{};
        req_bda.sType = 1000257000u;
        req_bda.bufferDeviceAddress = 1u;

        VkPhysicalDeviceShaderFloat16Int8Features req_f16{};
        req_f16.sType = 1000082000u;
        req_f16.shaderFloat16 = 1u;

        const void* pnext = nullptr;
        if (enable_bda && bda_feats_.bufferDeviceAddress) {
            req_bda.pNext = const_cast<void*>(pnext);
            pnext = &req_bda;
            bda_enabled_ = true;
        }
        if (enable_fp16 && f16_feats_.shaderFloat16) {
            req_f16.pNext = const_cast<void*>(pnext);
            pnext = &req_f16;
        }

        VkDeviceCreateInfo dci{};
        dci.sType = 3u;
        dci.pNext = pnext;
        dci.queueCreateInfoCount = 1u;
        dci.pQueueCreateInfos = &qci;

        auto create_dev = reinterpret_cast<VkResult(*)(VkPhysicalDevice, const VkDeviceCreateInfo*, const void*, VkDevice*)>(
            gipa_(instance_, "vkCreateDevice"));
        if (!create_dev || create_dev(phys_, &dci, nullptr, &device_) != 0 || !device_) return false;

        gdpa_ = reinterpret_cast<PFN_vkVoidFunction(*)(VkDevice, const char*)>(
            gipa_(instance_, "vkGetDeviceProcAddr"));
        if (!gdpa_) return false;

        auto get_queue = reinterpret_cast<void(*)(VkDevice, uint32_t, uint32_t, VkQueue*)>(
            gdpa_(device_, "vkGetDeviceQueue"));
        get_queue(device_, compute_qf_, 0u, &queue_);
        return queue_ != nullptr;
    }

    ~ScopedVulkanCompute() {
        if (device_ && gdpa_) {
            auto wait_idle = reinterpret_cast<VkResult(*)(VkDevice)>(gdpa_(device_, "vkDeviceWaitIdle"));
            if (wait_idle) wait_idle(device_);
            auto destroy_dev = reinterpret_cast<void(*)(VkDevice, const void*)>(gdpa_(device_, "vkDestroyDevice"));
            if (destroy_dev) destroy_dev(device_, nullptr);
        }
        if (instance_ && gipa_) {
            auto destroy_inst = reinterpret_cast<void(*)(VkInstance, const void*)>(gipa_(instance_, "vkDestroyInstance"));
            if (destroy_inst) destroy_inst(instance_, nullptr);
        }
        if (lib_) dlclose(lib_);
    }

    bool has_ext(const char* name) const {
        for (const auto& e : exts_) {
            if (std::strcmp(e.extensionName, name) == 0) return true;
        }
        return false;
    }

    uint32_t find_host_mem_type(uint32_t type_bits) const {
        constexpr uint32_t kHostVisCoh = 0x2u | 0x4u; /* HOST_VISIBLE | HOST_COHERENT */
        for (uint32_t i = 0; i < mem_props_.memoryTypeCount; ++i) {
            if (((type_bits >> i) & 1u) != 0u &&
                (mem_props_.memoryTypes[i].propertyFlags & kHostVisCoh) == kHostVisCoh) {
                return i;
            }
        }
        return UINT32_MAX;
    }

    VulkanComputeRunResult dispatch(const std::vector<uint32_t>& spv,
                                    const std::vector<uint32_t>& in_words,
                                    uint32_t out_word_count,
                                    uint32_t dispatch_x,
                                    const void* bda_data,
                                    size_t bda_size,
                                    uint64_t* out_bda_va) {
        VulkanComputeRunResult res{};
        if (!device_ || spv.empty()) {
            res.error = "Vulkan device not initialized or empty SPIR-V";
            return res;
        }

        auto vkCreateBuffer = reinterpret_cast<VkResult(*)(VkDevice, const VkBufferCreateInfo*, const void*, VkBuffer*)>(gdpa_(device_, "vkCreateBuffer"));
        auto vkDestroyBuffer = reinterpret_cast<void(*)(VkDevice, VkBuffer, const void*)>(gdpa_(device_, "vkDestroyBuffer"));
        auto vkGetBufferMemoryRequirements = reinterpret_cast<void(*)(VkDevice, VkBuffer, VkMemoryRequirements*)>(gdpa_(device_, "vkGetBufferMemoryRequirements"));
        auto vkAllocateMemory = reinterpret_cast<VkResult(*)(VkDevice, const VkMemoryAllocateInfo*, const void*, VkDeviceMemory*)>(gdpa_(device_, "vkAllocateMemory"));
        auto vkFreeMemory = reinterpret_cast<void(*)(VkDevice, VkDeviceMemory, const void*)>(gdpa_(device_, "vkFreeMemory"));
        auto vkBindBufferMemory = reinterpret_cast<VkResult(*)(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize)>(gdpa_(device_, "vkBindBufferMemory"));
        auto vkMapMemory = reinterpret_cast<VkResult(*)(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkFlags, void**)>(gdpa_(device_, "vkMapMemory"));
        auto vkUnmapMemory = reinterpret_cast<void(*)(VkDevice, VkDeviceMemory)>(gdpa_(device_, "vkUnmapMemory"));
        auto vkCreateShaderModule = reinterpret_cast<VkResult(*)(VkDevice, const VkShaderModuleCreateInfo*, const void*, VkShaderModule*)>(gdpa_(device_, "vkCreateShaderModule"));
        auto vkDestroyShaderModule = reinterpret_cast<void(*)(VkDevice, VkShaderModule, const void*)>(gdpa_(device_, "vkDestroyShaderModule"));
        auto vkCreateDescriptorSetLayout = reinterpret_cast<VkResult(*)(VkDevice, const VkDescriptorSetLayoutCreateInfo*, const void*, VkDescriptorSetLayout*)>(gdpa_(device_, "vkCreateDescriptorSetLayout"));
        auto vkDestroyDescriptorSetLayout = reinterpret_cast<void(*)(VkDevice, VkDescriptorSetLayout, const void*)>(gdpa_(device_, "vkDestroyDescriptorSetLayout"));
        auto vkCreatePipelineLayout = reinterpret_cast<VkResult(*)(VkDevice, const VkPipelineLayoutCreateInfo*, const void*, VkPipelineLayout*)>(gdpa_(device_, "vkCreatePipelineLayout"));
        auto vkDestroyPipelineLayout = reinterpret_cast<void(*)(VkDevice, VkPipelineLayout, const void*)>(gdpa_(device_, "vkDestroyPipelineLayout"));
        auto vkCreateComputePipelines = reinterpret_cast<VkResult(*)(VkDevice, VkPipelineCache, uint32_t, const VkComputePipelineCreateInfo*, const void*, VkPipeline*)>(gdpa_(device_, "vkCreateComputePipelines"));
        auto vkDestroyPipeline = reinterpret_cast<void(*)(VkDevice, VkPipeline, const void*)>(gdpa_(device_, "vkDestroyPipeline"));
        auto vkCreateDescriptorPool = reinterpret_cast<VkResult(*)(VkDevice, const VkDescriptorPoolCreateInfo*, const void*, VkDescriptorPool*)>(gdpa_(device_, "vkCreateDescriptorPool"));
        auto vkDestroyDescriptorPool = reinterpret_cast<void(*)(VkDevice, VkDescriptorPool, const void*)>(gdpa_(device_, "vkDestroyDescriptorPool"));
        auto vkAllocateDescriptorSets = reinterpret_cast<VkResult(*)(VkDevice, const VkDescriptorSetAllocateInfo*, VkDescriptorSet*)>(gdpa_(device_, "vkAllocateDescriptorSets"));
        auto vkUpdateDescriptorSets = reinterpret_cast<void(*)(VkDevice, uint32_t, const VkWriteDescriptorSet*, uint32_t, const void*)>(gdpa_(device_, "vkUpdateDescriptorSets"));
        auto vkCreateCommandPool = reinterpret_cast<VkResult(*)(VkDevice, const VkCommandPoolCreateInfo*, const void*, VkCommandPool*)>(gdpa_(device_, "vkCreateCommandPool"));
        auto vkDestroyCommandPool = reinterpret_cast<void(*)(VkDevice, VkCommandPool, const void*)>(gdpa_(device_, "vkDestroyCommandPool"));
        auto vkAllocateCommandBuffers = reinterpret_cast<VkResult(*)(VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer*)>(gdpa_(device_, "vkAllocateCommandBuffers"));
        auto vkBeginCommandBuffer = reinterpret_cast<VkResult(*)(VkCommandBuffer, const VkCommandBufferBeginInfo*)>(gdpa_(device_, "vkBeginCommandBuffer"));
        auto vkEndCommandBuffer = reinterpret_cast<VkResult(*)(VkCommandBuffer)>(gdpa_(device_, "vkEndCommandBuffer"));
        auto vkCmdBindPipeline = reinterpret_cast<void(*)(VkCommandBuffer, uint32_t, VkPipeline)>(gdpa_(device_, "vkCmdBindPipeline"));
        auto vkCmdBindDescriptorSets = reinterpret_cast<void(*)(VkCommandBuffer, uint32_t, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet*, uint32_t, const uint32_t*)>(gdpa_(device_, "vkCmdBindDescriptorSets"));
        auto vkCmdDispatch = reinterpret_cast<void(*)(VkCommandBuffer, uint32_t, uint32_t, uint32_t)>(gdpa_(device_, "vkCmdDispatch"));
        auto vkQueueSubmit = reinterpret_cast<VkResult(*)(VkQueue, uint32_t, const VkSubmitInfo*, VkFence)>(gdpa_(device_, "vkQueueSubmit"));
        auto vkQueueWaitIdle = reinterpret_cast<VkResult(*)(VkQueue)>(gdpa_(device_, "vkQueueWaitIdle"));
        auto vkGetBufferDeviceAddress = reinterpret_cast<uint64_t(*)(VkDevice, const VkBufferDeviceAddressInfo*)>(gdpa_(device_, "vkGetBufferDeviceAddress"));

        auto make_buf = [&](VkDeviceSize bytes, VkFlags usage, bool need_bda_addr,
                            VkBuffer& out_b, VkDeviceMemory& out_m) -> bool {
            VkBufferCreateInfo bci{};
            bci.sType = 12u;
            bci.size = std::max<VkDeviceSize>(bytes, 256u);
            bci.usage = usage;
            if (vkCreateBuffer(device_, &bci, nullptr, &out_b) != 0) return false;
            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device_, out_b, &req);
            uint32_t mti = find_host_mem_type(req.memoryTypeBits);
            if (mti == UINT32_MAX) return false;
            VkMemoryAllocateFlagsInfo afi{};
            afi.sType = 1000060000u;
            afi.flags = 0x2u; /* VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT */
            VkMemoryAllocateInfo mai{};
            mai.sType = 5u;
            mai.pNext = need_bda_addr ? &afi : nullptr;
            mai.allocationSize = req.size;
            mai.memoryTypeIndex = mti;
            if (vkAllocateMemory(device_, &mai, nullptr, &out_m) != 0) return false;
            return vkBindBufferMemory(device_, out_b, out_m, 0u) == 0;
        };

        VkBuffer bda_buf = 0, in_buf = 0, out_buf = 0;
        VkDeviceMemory bda_mem = 0, in_mem = 0, out_mem = 0;
        VkShaderModule mod = 0;
        VkDescriptorSetLayout dsl = 0;
        VkPipelineLayout pl = 0;
        VkPipeline pipe = 0;
        VkDescriptorPool dpool = 0;
        VkCommandPool cpool = 0;

        auto cleanup = [&]() {
            if (cpool) vkDestroyCommandPool(device_, cpool, nullptr);
            if (dpool) vkDestroyDescriptorPool(device_, dpool, nullptr);
            if (pipe) vkDestroyPipeline(device_, pipe, nullptr);
            if (pl) vkDestroyPipelineLayout(device_, pl, nullptr);
            if (dsl) vkDestroyDescriptorSetLayout(device_, dsl, nullptr);
            if (mod) vkDestroyShaderModule(device_, mod, nullptr);
            if (out_buf) vkDestroyBuffer(device_, out_buf, nullptr);
            if (out_mem) vkFreeMemory(device_, out_mem, nullptr);
            if (in_buf) vkDestroyBuffer(device_, in_buf, nullptr);
            if (in_mem) vkFreeMemory(device_, in_mem, nullptr);
            if (bda_buf) vkDestroyBuffer(device_, bda_buf, nullptr);
            if (bda_mem) vkFreeMemory(device_, bda_mem, nullptr);
        };

        std::vector<uint32_t> effective_in = in_words;
        if (effective_in.size() < 256u) effective_in.resize(256u, 0u);

        /* Optional BDA extra buffer (e.g. raw RDNA2 BVH4 buffer for live GPU traversal!) */
        if (bda_data != nullptr && bda_size > 0u && bda_enabled_ && vkGetBufferDeviceAddress) {
            if (!make_buf(bda_size, 0x20u | 0x20000u /* STORAGE | SHADER_DEVICE_ADDRESS */, true, bda_buf, bda_mem)) {
                cleanup();
                res.error = "Failed to allocate BDA VkBuffer";
                return res;
            }
            void* mp = nullptr;
            vkMapMemory(device_, bda_mem, 0u, bda_size, 0u, &mp);
            std::memcpy(mp, bda_data, bda_size);
            vkUnmapMemory(device_, bda_mem);
            VkBufferDeviceAddressInfo bdai{1000244001u, nullptr, bda_buf};
            const uint64_t va = vkGetBufferDeviceAddress(device_, &bdai);
            if (out_bda_va) *out_bda_va = va;
            /* Patch input SSBO[0..1] with the live GPU BDA address */
            effective_in[0] = static_cast<uint32_t>(va & 0xFFFFFFFFu);
            effective_in[1] = static_cast<uint32_t>(va >> 32u);
        }

        const VkDeviceSize in_bytes = effective_in.size() * 4u;
        const uint32_t eff_out_words = std::max(out_word_count, 64u);
        const VkDeviceSize out_bytes = static_cast<VkDeviceSize>(eff_out_words) * 4u;

        if (!make_buf(in_bytes, 0x20u, false, in_buf, in_mem) ||
            !make_buf(out_bytes, 0x20u, false, out_buf, out_mem)) {
            cleanup();
            res.error = "Failed to allocate input/output VkBuffers";
            return res;
        }

        /* Upload non-constant input words into VkBuffer */
        {
            void* mp = nullptr;
            vkMapMemory(device_, in_mem, 0u, in_bytes, 0u, &mp);
            std::memcpy(mp, effective_in.data(), static_cast<size_t>(in_bytes));
            vkUnmapMemory(device_, in_mem);

            vkMapMemory(device_, out_mem, 0u, out_bytes, 0u, &mp);
            std::memset(mp, 0, static_cast<size_t>(out_bytes));
            vkUnmapMemory(device_, out_mem);
        }

        /* Create shader module */
        VkShaderModuleCreateInfo smci{16u, nullptr, 0u, spv.size() * 4u, spv.data()};
        if (vkCreateShaderModule(device_, &smci, nullptr, &mod) != 0) {
            cleanup();
            res.error = "vkCreateShaderModule failed";
            return res;
        }

        /* Descriptor set layout: binding 0 (input SSBO), binding 1 (output SSBO) */
        VkDescriptorSetLayoutBinding binds[2]{
            {0u, 7u, 1u, 0x20u, nullptr},
            {1u, 7u, 1u, 0x20u, nullptr}
        };
        VkDescriptorSetLayoutCreateInfo dslci{32u, nullptr, 0u, 2u, binds};
        if (vkCreateDescriptorSetLayout(device_, &dslci, nullptr, &dsl) != 0) {
            cleanup();
            res.error = "vkCreateDescriptorSetLayout failed";
            return res;
        }

        VkPipelineLayoutCreateInfo plci{30u, nullptr, 0u, 1u, &dsl, 0u, nullptr};
        if (vkCreatePipelineLayout(device_, &plci, nullptr, &pl) != 0) {
            cleanup();
            res.error = "vkCreatePipelineLayout failed";
            return res;
        }

        VkComputePipelineCreateInfo cpci{};
        cpci.sType = 29u;
        cpci.stage.sType = 18u;
        cpci.stage.stage = 0x20u; /* COMPUTE_BIT */
        cpci.stage.module = mod;
        cpci.stage.pName = "main";
        cpci.layout = pl;
        cpci.basePipelineIndex = -1;
        if (vkCreateComputePipelines(device_, 0u, 1u, &cpci, nullptr, &pipe) != 0) {
            cleanup();
            res.error = "vkCreateComputePipelines failed";
            return res;
        }
        res.pipeline_created = true;

        VkDescriptorPoolSize psz{7u, 2u};
        VkDescriptorPoolCreateInfo dpci{33u, nullptr, 0u, 1u, 1u, &psz};
        vkCreateDescriptorPool(device_, &dpci, nullptr, &dpool);
        VkDescriptorSet dset = 0u;
        VkDescriptorSetAllocateInfo dsai{34u, nullptr, dpool, 1u, &dsl};
        vkAllocateDescriptorSets(device_, &dsai, &dset);

        VkDescriptorBufferInfo bi0{in_buf, 0u, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo bi1{out_buf, 0u, VK_WHOLE_SIZE};
        VkWriteDescriptorSet writes[2]{
            {35u, nullptr, dset, 0u, 0u, 1u, 7u, nullptr, &bi0, nullptr},
            {35u, nullptr, dset, 1u, 0u, 1u, 7u, nullptr, &bi1, nullptr}
        };
        vkUpdateDescriptorSets(device_, 2u, writes, 0u, nullptr);

        VkCommandPoolCreateInfo cmdpci{39u, nullptr, 0u, compute_qf_};
        vkCreateCommandPool(device_, &cmdpci, nullptr, &cpool);
        VkCommandBuffer cmd = nullptr;
        VkCommandBufferAllocateInfo cbai{40u, nullptr, cpool, 0u, 1u};
        vkAllocateCommandBuffers(device_, &cbai, &cmd);

        VkCommandBufferBeginInfo cbbi{42u, nullptr, 1u, nullptr};
        vkBeginCommandBuffer(cmd, &cbbi);
        vkCmdBindPipeline(cmd, 1u /* COMPUTE */, pipe);
        vkCmdBindDescriptorSets(cmd, 1u, pl, 0u, 1u, &dset, 0u, nullptr);
        vkCmdDispatch(cmd, std::max(1u, dispatch_x), 1u, 1u);
        vkEndCommandBuffer(cmd);

        VkSubmitInfo si{};
        si.sType = 4u;
        si.commandBufferCount = 1u;
        si.pCommandBuffers = &cmd;
        if (vkQueueSubmit(queue_, 1u, &si, 0u) != 0 || vkQueueWaitIdle(queue_) != 0) {
            cleanup();
            res.error = "vkQueueSubmit/WaitIdle failed";
            return res;
        }

        res.output_words.resize(out_word_count, 0u);
        {
            void* mp = nullptr;
            vkMapMemory(device_, out_mem, 0u, out_bytes, 0u, &mp);
            std::memcpy(res.output_words.data(), mp, static_cast<size_t>(out_word_count) * 4u);
            vkUnmapMemory(device_, out_mem);
        }

        cleanup();
        res.executed = true;
        return res;
    }

    const VkPhysicalDeviceProperties& props() const { return props2_.properties; }
    const VkPhysicalDeviceSubgroupProperties& sg_props() const { return sg_props_; }
    const VkPhysicalDeviceSubgroupSizeControlProperties& sg_ctrl_props() const { return sg_ctrl_props_; }
    const VkPhysicalDeviceShaderFloat16Int8Features& f16_feats() const { return f16_feats_; }
    const VkPhysicalDeviceBufferDeviceAddressFeatures& bda_feats() const { return bda_feats_; }

private:
    void* lib_{nullptr};
    PFN_vkVoidFunction(*gipa_)(VkInstance, const char*){nullptr};
    PFN_vkVoidFunction(*gdpa_)(VkDevice, const char*){nullptr};
    VkInstance instance_{nullptr};
    VkPhysicalDevice phys_{nullptr};
    VkDevice device_{nullptr};
    VkQueue queue_{nullptr};
    uint32_t compute_qf_{UINT32_MAX};
    bool bda_enabled_{false};

    VkPhysicalDeviceProperties2 props2_{};
    VkPhysicalDeviceSubgroupProperties sg_props_{};
    VkPhysicalDeviceSubgroupSizeControlProperties sg_ctrl_props_{};
    VkPhysicalDeviceFeatures2 feats2_{};
    VkPhysicalDeviceShaderFloat16Int8Features f16_feats_{};
    VkPhysicalDeviceBufferDeviceAddressFeatures bda_feats_{};
    VkPhysicalDeviceMemoryProperties mem_props_{};
    std::vector<VkExtensionProperties> exts_;
};

} // namespace vk_abi

/* ============================================================================
 * Public Stage 1 Microprobe Execution & Live Vulkan Compute Dispatch
 * ============================================================================ */

GpuMicroprobeProfile run_vulkan_microprobes() noexcept {
    GpuMicroprobeProfile prof{};
    vk_abi::ScopedVulkanCompute vk;
    if (!vk.init(true, false)) {
        return make_nvidia_warp32_profile(false, false);
    }

    prof.vulkan_probed = true;
    prof.vendor_id = vk.props().vendorID;
    prof.device_id = vk.props().deviceID;
    prof.driver_version = vk.props().driverVersion;
    prof.api_version = vk.props().apiVersion;
    prof.device_name = vk.props().deviceName;
    prof.subgroup_size = std::max(16u, vk.sg_props().subgroupSize);
    prof.min_subgroup_size = vk.sg_ctrl_props().minSubgroupSize ? vk.sg_ctrl_props().minSubgroupSize : prof.subgroup_size;
    prof.max_subgroup_size = vk.sg_ctrl_props().maxSubgroupSize ? vk.sg_ctrl_props().maxSubgroupSize : prof.subgroup_size;
    prof.probe_bitmask |= probe_bits::EXECUTED_ON_LIVE_VULKAN;

    /* Extension capabilities */
    if (vk.has_ext("VK_EXT_subgroup_size_control")) prof.probe_bitmask |= probe_bits::EXT_SUBGROUP_SIZE_CONTROL;
    if (vk.has_ext("VK_KHR_shader_fma"))            prof.probe_bitmask |= probe_bits::EXT_SPV_KHR_FMA;
    if (vk.has_ext("VK_KHR_ray_query"))             prof.probe_bitmask |= probe_bits::EXT_RAY_QUERY;
    if (vk.has_ext("VK_KHR_shader_maximal_reconvergence")) prof.probe_bitmask |= probe_bits::EXT_MAXIMAL_RECONVERGENCE;
    if (vk.has_ext("VK_KHR_shader_quad_control"))   prof.probe_bitmask |= probe_bits::EXT_QUAD_CONTROL;
    if (vk.has_ext("VK_KHR_shader_float_controls2")) prof.probe_bitmask |= probe_bits::EXT_FLOAT_CONTROLS2;
    if (vk.bda_feats().bufferDeviceAddress)         prof.probe_bitmask |= probe_bits::EXT_BDA_64;

    /* 1. FMA exact single-rounding probe via VkBuffer:
     *    a = 1.0f + 2^-23 (0x3F800001), b = 1.0f - 2^-23 (0x3F7FFFFF), c = -1.0f (0xBF800000)
     *    True exact FMA(a, b, c) = -2^-46 = -1.42108547e-14f (0xA8800000), NOT 0.0f! */
    {
        const bool use_khr_fma = prof.has(probe_bits::EXT_SPV_KHR_FMA);
        auto spv = build_microprobe_fma_spirv(use_khr_fma);
        auto r = vk.dispatch(spv, {0x3F800001u, 0x3F7FFFFFu, 0xBF800000u}, 4u, 1u, nullptr, 0u, nullptr);
        if (r.executed && !r.output_words.empty()) {
            prof.fma_probe_bits = r.output_words[0];
            if (prof.fma_probe_bits != 0u && prof.fma_probe_bits != 0x80000000u) {
                prof.probe_bitmask |= probe_bits::FMA_SINGLE_ROUND_EXACT;
            }
        }
    }

    /* 2. f32 Denormal FTZ probe via VkBuffer: 0x00400000 (subnormal) * 1.0f (0x3F800000) */
    {
        auto spv_ftz = build_microprobe_denorm_spirv(true);
        auto r = vk.dispatch(spv_ftz, {0x00400000u, 0x3F800000u}, 4u, 1u, nullptr, 0u, nullptr);
        if (r.executed && !r.output_words.empty()) {
            prof.denorm_ftz_probe_bits = r.output_words[0];
            if ((prof.denorm_ftz_probe_bits & 0x7FFFFFFFu) == 0u) {
                prof.probe_bitmask |= probe_bits::F32_DENORM_FTZ_NATIVE;
            }
        }
        auto spv_pres = build_microprobe_denorm_spirv(false);
        auto rp = vk.dispatch(spv_pres, {0x00400000u, 0x3F800000u}, 4u, 1u, nullptr, 0u, nullptr);
        if (rp.executed && !rp.output_words.empty() && rp.output_words[0] == 0x00400000u) {
            prof.probe_bitmask |= probe_bits::F32_DENORM_PRESERVE;
        }
    }

    /* 3. f32 NaN-suppressing NMin/NMax probe via VkBuffer: (qNaN=0x7FC00000, 3.5f=0x40600000) */
    {
        auto spv_nan = build_microprobe_nan_minmax_spirv();
        auto r = vk.dispatch(spv_nan, {0x7FC00000u, 0x40600000u}, 4u, 1u, nullptr, 0u, nullptr);
        if (r.executed && r.output_words.size() >= 2u) {
            prof.nan_min_probe_bits = r.output_words[0];
            prof.nan_max_probe_bits = r.output_words[1];
            if (prof.nan_min_probe_bits == 0x40600000u && prof.nan_max_probe_bits == 0x40600000u) {
                prof.probe_bitmask |= probe_bits::F32_NAN_MINMAX_SUPPRESS;
            }
        }
    }

    /* 4. SubgroupSize probe via VkBuffer (pass bias=0 in VkBuffer) */
    {
        auto spv_sg = build_microprobe_subgroup_size_spirv();
        auto r = vk.dispatch(spv_sg, {0u}, 4u, 1u, nullptr, 0u, nullptr);
        if (r.executed && !r.output_words.empty()) {
            prof.measured_subgroup_size = r.output_words[0];
            if (prof.measured_subgroup_size == 64u || prof.max_subgroup_size >= 64u) {
                prof.probe_bitmask |= probe_bits::SUBGROUP_SIZE_64_NATIVE;
            }
            if (prof.measured_subgroup_size == 32u || prof.min_subgroup_size == 32u) {
                prof.probe_bitmask |= probe_bits::SUBGROUP_SIZE_32_NATIVE;
            }
        }
    }

    /* 5. Native FP16 probe (only dispatch if device supports shaderFloat16) */
    if (vk.f16_feats().shaderFloat16) {
        vk_abi::ScopedVulkanCompute vk16;
        if (vk16.init(false, true)) {
            auto spv16 = build_microprobe_f16_spirv();
            /* Packed f16: {qNaN(0x7E00), 2.0h(0x4000)} vs {1.5h(0x3E00), qNaN(0x7E00)} */
            auto r = vk16.dispatch(spv16, {0x40007E00u, 0x7E003E00u}, 4u, 1u, nullptr, 0u, nullptr);
            if (r.executed && !r.output_words.empty()) {
                prof.f16_probe_bits = r.output_words[0];
                prof.probe_bitmask |= probe_bits::F16_NATIVE_SUPPORTED;
                if (prof.f16_probe_bits == 0x40003E00u) {
                    prof.probe_bitmask |= probe_bits::F16_NAN_DENORM_EXACT;
                }
            }
        }
    }

    return prof;
}

VulkanComputeRunResult run_spirv_compute_on_vulkan(const std::vector<uint32_t>& spirv_words,
                                                   const std::vector<uint32_t>& input_words,
                                                   uint32_t output_word_count,
                                                   uint32_t dispatch_x,
                                                   const void* bda_extra_buffer,
                                                   size_t bda_extra_size,
                                                   uint64_t* out_bda_device_addr) noexcept {
    vk_abi::ScopedVulkanCompute vk;
    const bool need_bda = (bda_extra_buffer != nullptr && bda_extra_size > 0u);
    if (!vk.init(need_bda, false)) {
        VulkanComputeRunResult err{};
        err.error = "Unable to initialize Vulkan 1.4 compute device";
        return err;
    }
    return vk.dispatch(spirv_words, input_words, output_word_count, dispatch_x,
                       bda_extra_buffer, bda_extra_size, out_bda_device_addr);
}

/* ============================================================================
 * Deterministic Multi-Vendor Hardware Profiles (NVIDIA / AMD RADV / Intel ANV)
 * ============================================================================ */

GpuMicroprobeProfile make_nvidia_warp32_profile(bool native_fp16, bool ray_query) noexcept {
    GpuMicroprobeProfile p{};
    p.vendor_id = 0x10DEu;
    p.device_id = 0x2684u;
    p.driver_version = 0x91000000u;
    p.api_version = (1u << 22u) | (4u << 12u);
    p.subgroup_size = 32u;
    p.min_subgroup_size = 32u;
    p.max_subgroup_size = 32u;
    p.measured_subgroup_size = 32u;
    p.device_name = "NVIDIA Warp32 Profile";
    p.probe_bitmask =
        probe_bits::FMA_SINGLE_ROUND_EXACT |
        probe_bits::F32_DENORM_FTZ_NATIVE |
        probe_bits::F32_DENORM_PRESERVE |
        probe_bits::F32_NAN_MINMAX_SUPPRESS |
        probe_bits::SUBGROUP_SIZE_32_NATIVE |
        probe_bits::EXT_SUBGROUP_SIZE_CONTROL |
        probe_bits::EXT_SPV_KHR_FMA |
        probe_bits::EXT_BDA_64 |
        probe_bits::EXT_MAXIMAL_RECONVERGENCE |
        probe_bits::EXT_QUAD_CONTROL |
        probe_bits::EXT_FLOAT_CONTROLS2;
    if (native_fp16) {
        p.probe_bitmask |= probe_bits::F16_NATIVE_SUPPORTED | probe_bits::F16_NAN_DENORM_EXACT;
    }
    if (ray_query) {
        p.probe_bitmask |= probe_bits::EXT_RAY_QUERY;
    }
    return p;
}

GpuMicroprobeProfile make_amd_radv_profile(bool ray_query) noexcept {
    GpuMicroprobeProfile p{};
    p.vendor_id = 0x1002u;
    p.device_id = 0x73BFu; /* Navi 21 RDNA2 */
    p.driver_version = 0x06400000u;
    p.api_version = (1u << 22u) | (4u << 12u);
    p.subgroup_size = 64u;
    p.min_subgroup_size = 32u;
    p.max_subgroup_size = 64u;
    p.measured_subgroup_size = 64u;
    p.device_name = "AMD RADV RDNA2 Native Wave64 Profile";
    p.probe_bitmask =
        probe_bits::FMA_SINGLE_ROUND_EXACT |
        probe_bits::F32_DENORM_FTZ_NATIVE |
        probe_bits::F32_DENORM_PRESERVE |
        probe_bits::F32_NAN_MINMAX_SUPPRESS |
        probe_bits::F16_NATIVE_SUPPORTED |
        probe_bits::F16_NAN_DENORM_EXACT |
        probe_bits::SUBGROUP_SIZE_64_NATIVE |
        probe_bits::SUBGROUP_SIZE_32_NATIVE |
        probe_bits::EXT_SUBGROUP_SIZE_CONTROL |
        probe_bits::EXT_SPV_KHR_FMA |
        probe_bits::EXT_BDA_64 |
        probe_bits::EXT_MAXIMAL_RECONVERGENCE |
        probe_bits::EXT_QUAD_CONTROL |
        probe_bits::EXT_FLOAT_CONTROLS2;
    if (ray_query) {
        p.probe_bitmask |= probe_bits::EXT_RAY_QUERY;
    }
    return p;
}

GpuMicroprobeProfile make_intel_anv_profile(bool wave64, bool ray_query) noexcept {
    GpuMicroprobeProfile p{};
    p.vendor_id = 0x8086u;
    p.device_id = 0x56A0u;
    p.driver_version = 0x06400000u;
    p.api_version = (1u << 22u) | (4u << 12u);
    p.subgroup_size = wave64 ? 64u : 32u;
    p.min_subgroup_size = 16u;
    p.max_subgroup_size = wave64 ? 64u : 32u;
    p.measured_subgroup_size = p.subgroup_size;
    p.device_name = "Intel ANV Profile";
    p.probe_bitmask =
        probe_bits::FMA_SINGLE_ROUND_EXACT |
        probe_bits::F32_DENORM_FTZ_NATIVE |
        probe_bits::F32_NAN_MINMAX_SUPPRESS |
        probe_bits::F16_NATIVE_SUPPORTED |
        probe_bits::F16_NAN_DENORM_EXACT |
        probe_bits::SUBGROUP_SIZE_32_NATIVE |
        probe_bits::EXT_SUBGROUP_SIZE_CONTROL |
        probe_bits::EXT_BDA_64 |
        probe_bits::EXT_MAXIMAL_RECONVERGENCE;
    if (wave64)    p.probe_bitmask |= probe_bits::SUBGROUP_SIZE_64_NATIVE;
    if (ray_query) p.probe_bitmask |= probe_bits::EXT_RAY_QUERY;
    return p;
}

/* ============================================================================
 * In-Process SPIRV-Tools Vulkan 1.4 Validator (dlopen libSPIRV-Tools-shared.so)
 * ============================================================================ */

namespace {

/* Binary-compatible subset of <spirv-tools/libspirv.h> C API */
using spv_context_t = void*;
struct spv_position_t { size_t line; size_t column; size_t index; };
struct spv_diagnostic_t { spv_position_t position; char* error; bool isTextSource; };

/* SPV_ENV_VULKAN_1_4 enum value verified directly from /usr/include/spirv-tools/libspirv.h */
struct SpirvToolsApi {
    void* handle{nullptr};
    spv_context_t (*create)(int env){nullptr};
    void (*destroy)(spv_context_t ctx){nullptr};
    int (*validate_binary)(spv_context_t ctx, const uint32_t* words, size_t count, spv_diagnostic_t** diag){nullptr};
    void (*diag_destroy)(spv_diagnostic_t* diag){nullptr};
    bool (*parse_env)(const char* str, int* out_env){nullptr};
    int env_vulkan_1_4{-1};

    SpirvToolsApi() {
        handle = dlopen("libSPIRV-Tools-shared.so", RTLD_NOW | RTLD_LOCAL);
        if (!handle) handle = dlopen("libSPIRV-Tools.so", RTLD_NOW | RTLD_LOCAL);
        if (!handle) return;
        create = reinterpret_cast<spv_context_t(*)(int)>(dlsym(handle, "spvContextCreate"));
        destroy = reinterpret_cast<void(*)(spv_context_t)>(dlsym(handle, "spvContextDestroy"));
        validate_binary = reinterpret_cast<int(*)(spv_context_t, const uint32_t*, size_t, spv_diagnostic_t**)>(
            dlsym(handle, "spvValidateBinary"));
        diag_destroy = reinterpret_cast<void(*)(spv_diagnostic_t*)>(dlsym(handle, "spvDiagnosticDestroy"));
        parse_env = reinterpret_cast<bool(*)(const char*, int*)>(dlsym(handle, "spvParseTargetEnv"));
        if (parse_env) {
            int parsed = -1;
            if (parse_env("vulkan1.4", &parsed)) {
                env_vulkan_1_4 = parsed;
            } else if (parse_env("vulkan1.3", &parsed)) {
                env_vulkan_1_4 = parsed;
            }
        }
    }

    ~SpirvToolsApi() {
        if (handle) dlclose(handle);
    }

    bool ready() const noexcept {
        return handle && create && destroy && validate_binary && diag_destroy && env_vulkan_1_4 >= 0;
    }
};

} // namespace

bool validate_spirv_vulkan14(const std::vector<uint32_t>& words,
                             std::string* out_error) noexcept {
    if (words.size() < 5u || words[0] != 0x07230203u) {
        if (out_error) *out_error = "Invalid SPIR-V header magic or truncated binary";
        return false;
    }
    static SpirvToolsApi api;
    if (!api.ready()) {
        if (out_error) *out_error = "libSPIRV-Tools-shared.so unavailable";
        return false;
    }
    spv_context_t ctx = api.create(api.env_vulkan_1_4);
    if (!ctx) {
        if (out_error) *out_error = "spvContextCreate failed";
        return false;
    }
    spv_diagnostic_t* diag = nullptr;
    const int rc = api.validate_binary(ctx, words.data(), words.size(), &diag);
    if (rc != 0 && diag != nullptr) {
        if (out_error && diag->error) {
            *out_error = diag->error;
        }
        api.diag_destroy(diag);
    } else if (diag != nullptr) {
        api.diag_destroy(diag);
    }
    api.destroy(ctx);
    return rc == 0;
}

} // namespace scarlett::rdna2
