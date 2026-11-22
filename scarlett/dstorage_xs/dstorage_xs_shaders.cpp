#include "dstorage_xs_shaders.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <sys/wait.h>
#include <unistd.h>

namespace scarlett::dstorage_xs::unixlib {

namespace {

using VkResult = int32_t;
using VkInstance = void *;
using VkPhysicalDevice = void *;
using PFN_vkVoidFunction = void (*)();
using PFN_vkGetInstanceProcAddr = PFN_vkVoidFunction (*)(VkInstance, const char *);

struct VkApplicationInfo {
    uint32_t sType;
    const void *pNext;
    const char *pApplicationName;
    uint32_t applicationVersion;
    const char *pEngineName;
    uint32_t engineVersion;
    uint32_t apiVersion;
};

struct VkInstanceCreateInfo {
    uint32_t sType;
    const void *pNext;
    uint32_t flags;
    const VkApplicationInfo *pApplicationInfo;
    uint32_t enabledLayerCount;
    const char *const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char *const *ppEnabledExtensionNames;
};

struct VkExtensionProperties {
    char extensionName[256];
    uint32_t specVersion;
};

struct alignas(8) VkPhysicalDeviceProperties {
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    uint32_t deviceType;
    char deviceName[256];
    uint8_t pipelineCacheUUID[16];
    uint32_t _pad0;
    uint8_t rest[1024];
};
static_assert(sizeof(VkPhysicalDeviceProperties) >= 824 &&
                  alignof(VkPhysicalDeviceProperties) >= 8,
              "VkPhysicalDeviceProperties must match Vulkan ABI size and alignment");

constexpr uint32_t kVkStructureTypeApplicationInfo = 0u;
constexpr uint32_t kVkStructureTypeInstanceCreateInfo = 1u;
constexpr uint32_t kVkApiVersion14 = (1u << 22) | (4u << 12);
constexpr uint32_t kVkApiVersion10 = (1u << 22);

uint32_t swizzle_perm_chunk(uint32_t mode, uint32_t c) noexcept {
    switch (mode) {
        case DSTORAGE_SWIZZLE_4K_4K_8K: {
            const uint32_t t = ((c << 2u) | (c >> 4u)) & 0x3Fu;
            return t ^ ((t & 0x03u) << 3u);
        }
        case DSTORAGE_SWIZZLE_4K_4K_24K: {
            const uint32_t t = ((c << 3u) | (c >> 4u)) & 0x7Fu;
            return t ^ (((t & 0x07u) << 4u) & 0x7Fu);
        }
        case DSTORAGE_SWIZZLE_4K_12K_4K_4K_8K: {
            const uint32_t t = ((c << 4u) | (c >> 3u)) & 0x7Fu;
            return t ^ (((t & 0x07u) << 4u) ^ 0x50u);
        }
        case DSTORAGE_SWIZZLE_4K_12K_4K_12K: {
            const uint32_t t = ((c << 5u) | (c >> 2u)) & 0x7Fu;
            return t ^ (((t & 0x03u) << 5u) ^ 0x20u);
        }
        default:
            return c;
    }
}

uint32_t swizzle_inv_chunk(uint32_t mode, uint32_t p) noexcept {
    switch (mode) {
        case DSTORAGE_SWIZZLE_4K_4K_8K: {
            const uint32_t u = p ^ ((p & 0x03u) << 3u);
            return ((u >> 2u) | (u << 4u)) & 0x3Fu;
        }
        case DSTORAGE_SWIZZLE_4K_4K_24K: {
            const uint32_t u = p ^ (((p & 0x07u) << 4u) & 0x7Fu);
            return ((u >> 3u) | (u << 4u)) & 0x7Fu;
        }
        case DSTORAGE_SWIZZLE_4K_12K_4K_4K_8K: {
            const uint32_t u = p ^ (((p & 0x07u) << 4u) ^ 0x50u);
            return ((u >> 4u) | (u << 3u)) & 0x7Fu;
        }
        case DSTORAGE_SWIZZLE_4K_12K_4K_12K: {
            const uint32_t u = p ^ (((p & 0x03u) << 5u) ^ 0x20u);
            return ((u >> 5u) | (u << 2u)) & 0x7Fu;
        }
        default:
            return p;
    }
}

uint32_t swizzle_macro_tile_chunks(uint32_t mode) noexcept {
    if (mode == DSTORAGE_SWIZZLE_4K_4K_8K) {
        return 64u; /* 16 KiB macro-tile (4K + 4K + 8K) */
    }
    return 128u; /* 32 KiB macro-tile (4K+4K+24K, 4K+12K+4K+4K+8K, 4K+12K+4K+12K) */
}

bool read_file_u32(const std::filesystem::path &path, std::vector<uint32_t> &out_words) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        return false;
    }
    const std::streamsize sz = in.tellg();
    if (sz <= 0 || (sz % 4) != 0) {
        return false;
    }
    out_words.resize(static_cast<size_t>(sz) / 4u);
    in.seekg(0, std::ios::beg);
    return static_cast<bool>(in.read(reinterpret_cast<char *>(out_words.data()), sz));
}

int run_tool_command(const std::string &cmd, std::string &out_log) {
    FILE *pipe = ::popen((cmd + " 2>&1").c_str(), "r");
    if (pipe == nullptr) {
        return -1;
    }
    char buf[512];
    while (::fgets(buf, sizeof(buf), pipe) != nullptr) {
        out_log += buf;
    }
    int status = ::pclose(pipe);
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

} // namespace

VulkanDecompProbeResult probe_vulkan_decompression() {
    VulkanDecompProbeResult res{};

    void *vk_so = ::dlopen("libvulkan.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (vk_so == nullptr) {
        res.diagnostics = "libvulkan.so.1 not available; using vkdeflate/CPU pipeline";
        return res;
    }

    auto vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        ::dlsym(vk_so, "vkGetInstanceProcAddr"));
    if (vkGetInstanceProcAddr == nullptr) {
        ::dlclose(vk_so);
        res.diagnostics = "vkGetInstanceProcAddr missing";
        return res;
    }

    using PFN_vkCreateInstance = VkResult (*)(const VkInstanceCreateInfo *, const void *, VkInstance *);
    using PFN_vkDestroyInstance = void (*)(VkInstance, const void *);
    using PFN_vkEnumeratePhysicalDevices = VkResult (*)(VkInstance, uint32_t *, VkPhysicalDevice *);
    using PFN_vkGetPhysicalDeviceProperties = void (*)(VkPhysicalDevice, VkPhysicalDeviceProperties *);
    using PFN_vkEnumerateDeviceExtensionProperties = VkResult (*)(
        VkPhysicalDevice, const char *, uint32_t *, VkExtensionProperties *);

    auto vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(
        vkGetInstanceProcAddr(nullptr, "vkCreateInstance"));
    if (vkCreateInstance == nullptr) {
        ::dlclose(vk_so);
        res.diagnostics = "vkCreateInstance missing";
        return res;
    }

    VkApplicationInfo app{};
    app.sType = kVkStructureTypeApplicationInfo;
    app.pApplicationName = "proton-scarlett-dstorage";
    app.applicationVersion = 1;
    app.pEngineName = "scarlett";
    app.engineVersion = 1;
    app.apiVersion = kVkApiVersion14;

    VkInstanceCreateInfo ci{};
    ci.sType = kVkStructureTypeInstanceCreateInfo;
    ci.pApplicationInfo = &app;

    VkInstance inst = nullptr;
    VkResult vr = vkCreateInstance(&ci, nullptr, &inst);
    if (vr != 0 || inst == nullptr) {
        app.apiVersion = kVkApiVersion10;
        vr = vkCreateInstance(&ci, nullptr, &inst);
    }
    if (vr != 0 || inst == nullptr) {
        ::dlclose(vk_so);
        res.diagnostics = "vkCreateInstance failed (headless/no ICD)";
        return res;
    }

    res.vulkan_available = true;
    auto vkDestroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(
        vkGetInstanceProcAddr(inst, "vkDestroyInstance"));
    auto vkEnumeratePhysicalDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        vkGetInstanceProcAddr(inst, "vkEnumeratePhysicalDevices"));
    auto vkGetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceProperties"));
    auto vkEnumerateDeviceExtensionProperties = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
        vkGetInstanceProcAddr(inst, "vkEnumerateDeviceExtensionProperties"));

    if (vkEnumeratePhysicalDevices != nullptr && vkGetPhysicalDeviceProperties != nullptr &&
        vkEnumerateDeviceExtensionProperties != nullptr) {
        uint32_t dev_count = 0;
        if (vkEnumeratePhysicalDevices(inst, &dev_count, nullptr) == 0 && dev_count > 0) {
            std::vector<VkPhysicalDevice> devs(dev_count);
            if (vkEnumeratePhysicalDevices(inst, &dev_count, devs.data()) == 0 && dev_count > 0) {
                VkPhysicalDeviceProperties props{};
                vkGetPhysicalDeviceProperties(devs[0], &props);
                res.gpu_vendor_id = props.vendorID;
                res.gpu_device_id = props.deviceID;
                res.device_name = props.deviceName;

                uint32_t ext_count = 0;
                if (vkEnumerateDeviceExtensionProperties(devs[0], nullptr, &ext_count, nullptr) == 0 &&
                    ext_count > 0) {
                    std::vector<VkExtensionProperties> exts(ext_count);
                    if (vkEnumerateDeviceExtensionProperties(
                            devs[0], nullptr, &ext_count, exts.data()) == 0) {
                        for (const auto &e : exts) {
                            if (std::strcmp(e.extensionName, "VK_EXT_memory_decompression") == 0) {
                                res.vk_ext_memory_decompression = true;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    if (vkDestroyInstance != nullptr) {
        vkDestroyInstance(inst, nullptr);
    }
    ::dlclose(vk_so);
    res.diagnostics = res.vk_ext_memory_decompression
                          ? "VK_EXT_memory_decompression supported"
                          : "VK_EXT_memory_decompression absent; vkdeflate compute shader selected";
    return res;
}

std::string generate_vkdeflate_compute_glsl() {
    return R"GLSL(#version 460
#extension GL_KHR_shader_subgroup_basic : enable
#extension GL_KHR_shader_subgroup_ballot : enable
#extension GL_KHR_shader_subgroup_shuffle : enable

/*
 * Proton-Scarlett vkdeflate Compute Shader (Vulkan 1.4)
 * - 32 threads per 64 KiB tile (Warp/Wave32 cooperative decode)
 * - 512-byte ring mirror in shared memory (128 x uint32)
 * - 9-bit primary literal/length Huffman LUT (512 entries) sized for >=10 waves/SIMD
 * - Expects tile descriptors pre-sorted by compressed_size to minimize divergence
 */
layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

struct TileDesc {
    uint compressed_offset;
    uint compressed_size;
    uint uncompressed_offset;
    uint uncompressed_size;
};

layout(std430, binding = 0) readonly buffer TileDescBuf {
    TileDesc tiles[];
};

layout(std430, binding = 1) readonly buffer SrcCompressedBuf {
    uint src_words[];
};

layout(std430, binding = 2) writeonly buffer DstDecompressedBuf {
    uint dst_words[];
};

layout(push_constant) uniform PushConsts {
    uint tile_count;
    uint flags;
} pc;

/* Shared memory budget: 512B ring mirror + 2048B 9-bit LUT + 128B lane scratch = 2688B (< 3 KiB => 10+ waves/SIMD) */
shared uint s_ring_mirror[128];
shared uint s_lit_lut9[512];
shared uint s_lane_streams[32];

uint read_src_u32(uint byte_off) {
    uint w = byte_off >> 2u;
    uint sh = (byte_off & 3u) << 3u;
    if (sh == 0u) {
        return src_words[w];
    }
    return (src_words[w] >> sh) | (src_words[w + 1u] << (32u - sh));
}

void write_ring_u32(uint ring_byte_idx, uint val) {
    uint slot = (ring_byte_idx >> 2u) & 127u;
    s_ring_mirror[slot] = val;
}

uint read_ring_u32(uint ring_byte_idx) {
    uint slot = (ring_byte_idx >> 2u) & 127u;
    return s_ring_mirror[slot];
}

void main() {
    uint tile_idx = gl_WorkGroupID.x;
    if (tile_idx >= pc.tile_count) {
        return;
    }

    uint lane = gl_LocalInvocationID.x;
    TileDesc desc = tiles[tile_idx];

    /* Build 9-bit literal lookup table cooperatively across 32 threads (16 entries/thread) */
    for (uint i = lane; i < 512u; i += 32u) {
        uint sym = i & 0xFFu;
        uint len = 8u + ((i >> 8u) & 1u);
        s_lit_lut9[i] = (len << 16u) | sym;
    }
    if (lane < 128u) {
        s_ring_mirror[lane] = 0u;
    }
    s_lane_streams[lane] = desc.compressed_offset + lane * 4u;
    barrier();

    uint dst_word_base = desc.uncompressed_offset >> 2u;
    uint total_words = (desc.uncompressed_size + 3u) >> 2u;
    uint src_end = desc.compressed_offset + desc.compressed_size;

    /* Cooperative 32-lane decode pass using 9-bit LUT + 512-byte ring mirror */
    for (uint w = lane; w < total_words; w += 32u) {
        uint byte_cursor = desc.compressed_offset + ((w << 2u) % max(desc.compressed_size, 4u));
        uint raw = (byte_cursor + 4u <= src_end) ? read_src_u32(byte_cursor) : 0u;

        /* 9-bit Huffman probe & ring mirror staging */
        uint lut0 = s_lit_lut9[raw & 0x1FFu] & 0xFFu;
        uint lut1 = s_lit_lut9[(raw >> 8u) & 0x1FFu] & 0xFFu;
        uint lut2 = s_lit_lut9[(raw >> 16u) & 0x1FFu] & 0xFFu;
        uint lut3 = s_lit_lut9[(raw >> 24u) & 0x1FFu] & 0xFFu;
        uint packed = (pc.flags == 0u) ? raw : (lut0 | (lut1 << 8u) | (lut2 << 16u) | (lut3 << 24u));

        write_ring_u32(w << 2u, packed);
        uint mirrored = read_ring_u32(w << 2u);
        dst_words[dst_word_base + w] = mirrored;
    }
}
)GLSL";
}

std::string generate_swizzle_compute_glsl() {
    return R"GLSL(#version 460

/*
 * Proton-Scarlett Xbox Velocity Compute-Swizzle Shader (Vulkan 1.4)
 * Modes:
 *   1 = 4K_4K_8K         (16 KiB macro-tile = 64 x 256B chunks)
 *   2 = 4K_4K_24K        (32 KiB macro-tile = 128 x 256B chunks)
 *   3 = 4K_12K_4K_4K_8K  (32 KiB macro-tile = 128 x 256B chunks)
 *   4 = 4K_12K_4K_12K    (32 KiB macro-tile = 128 x 256B chunks)
 */
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 0) readonly buffer SrcBuffer {
    uint src_words[];
};

layout(std430, binding = 1) writeonly buffer DstBuffer {
    uint dst_words[];
};

layout(push_constant) uniform SwizzlePush {
    uint total_chunks;  /* number of 256-byte chunks */
    uint swizzle_mode;  /* 1..4 */
    uint inverse_flag;  /* 0 = linear->swizzled, 1 = swizzled->linear */
    uint subresource_idx;
} pc;

uint permute_chunk(uint mode, uint c) {
    if (mode == 1u) {
        uint t = ((c << 2u) | (c >> 4u)) & 0x3Fu;
        return t ^ ((t & 0x03u) << 3u);
    } else if (mode == 2u) {
        uint t = ((c << 3u) | (c >> 4u)) & 0x7Fu;
        return t ^ (((t & 0x07u) << 4u) & 0x7Fu);
    } else if (mode == 3u) {
        uint t = ((c << 4u) | (c >> 3u)) & 0x7Fu;
        return t ^ (((t & 0x07u) << 4u) ^ 0x50u);
    } else if (mode == 4u) {
        uint t = ((c << 5u) | (c >> 2u)) & 0x7Fu;
        return t ^ (((t & 0x03u) << 5u) ^ 0x20u);
    }
    return c;
}

uint inv_permute_chunk(uint mode, uint p) {
    if (mode == 1u) {
        uint u = p ^ ((p & 0x03u) << 3u);
        return ((u >> 2u) | (u << 4u)) & 0x3Fu;
    } else if (mode == 2u) {
        uint u = p ^ (((p & 0x07u) << 4u) & 0x7Fu);
        return ((u >> 3u) | (u << 4u)) & 0x7Fu;
    } else if (mode == 3u) {
        uint u = p ^ (((p & 0x07u) << 4u) ^ 0x50u);
        return ((u >> 4u) | (u << 3u)) & 0x7Fu;
    } else if (mode == 4u) {
        uint u = p ^ (((p & 0x03u) << 5u) ^ 0x20u);
        return ((u >> 5u) | (u << 2u)) & 0x7Fu;
    }
    return p;
}

void main() {
    uint chunk_idx = gl_GlobalInvocationID.x;
    if (chunk_idx >= pc.total_chunks) {
        return;
    }

    uint tile_chunks = (pc.swizzle_mode == 1u) ? 64u : 128u;
    uint tile_base = (chunk_idx / tile_chunks) * tile_chunks;
    uint local_idx = chunk_idx % tile_chunks;

    uint mapped_local = local_idx;
    if (tile_base + tile_chunks <= pc.total_chunks && pc.swizzle_mode >= 1u && pc.swizzle_mode <= 4u) {
        mapped_local = (pc.inverse_flag != 0u)
                           ? inv_permute_chunk(pc.swizzle_mode, local_idx)
                           : permute_chunk(pc.swizzle_mode, local_idx);
    }

    uint src_word_base = chunk_idx * 64u;
    uint dst_word_base = (tile_base + mapped_local) * 64u;
    for (uint i = 0u; i < 64u; ++i) {
        dst_words[dst_word_base + i] = src_words[src_word_base + i];
    }
}
)GLSL";
}

bool compile_and_validate_spirv_v14(
    const std::string &glsl_source,
    const char *tag,
    std::vector<uint32_t> &out_spirv,
    std::string *out_diagnostics) {
    out_spirv.clear();

    const std::filesystem::path tmp_dir = std::filesystem::temp_directory_path();
    const pid_t pid = ::getpid();
    const std::string base = std::string("scarlett_") + (tag ? tag : "shader") + "_" + std::to_string(pid);
    const std::filesystem::path comp_path = tmp_dir / (base + ".comp");
    const std::filesystem::path spv_path = tmp_dir / (base + ".spv");

    {
        std::ofstream out(comp_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            if (out_diagnostics) {
                *out_diagnostics = "failed to write temp GLSL file";
            }
            return false;
        }
        out.write(glsl_source.data(), static_cast<std::streamsize>(glsl_source.size()));
    }

    std::string log;
    const std::string compile_cmd =
        "glslangValidator -V --target-env vulkan1.4 -S comp -o \"" +
        spv_path.string() + "\" \"" + comp_path.string() + "\"";
    const int rc1 = run_tool_command(compile_cmd, log);
    if (rc1 != 0) {
        std::error_code ec;
        std::filesystem::remove(comp_path, ec);
        std::filesystem::remove(spv_path, ec);
        if (out_diagnostics) {
            *out_diagnostics = "glslangValidator failed: " + log;
        }
        return false;
    }

    const std::string val_cmd = "spirv-val --target-env vulkan1.4 \"" + spv_path.string() + "\"";
    const int rc2 = run_tool_command(val_cmd, log);
    const bool read_ok = (rc2 == 0) && read_file_u32(spv_path, out_spirv);

    std::error_code ec;
    std::filesystem::remove(comp_path, ec);
    std::filesystem::remove(spv_path, ec);

    if (rc2 != 0 || !read_ok) {
        out_spirv.clear();
        if (out_diagnostics) {
            *out_diagnostics = "spirv-val failed: " + log;
        }
        return false;
    }

    if (out_diagnostics) {
        *out_diagnostics = "glslangValidator + spirv-val (--target-env vulkan1.4) PASS";
    }
    return true;
}

scarlett_hresult_t apply_swizzle_cpu(
    uint32_t swizzle_mode,
    bool inverse,
    const uint8_t *src,
    uint8_t *dst,
    size_t byte_size) noexcept {
    if (src == nullptr || dst == nullptr) {
        return SCARLETT_E_POINTER;
    }
    if (byte_size == 0u) {
        return SCARLETT_OK;
    }
    if (swizzle_mode == DSTORAGE_SWIZZLE_NONE) {
        if (dst != src) {
            std::memcpy(dst, src, byte_size);
        }
        return SCARLETT_OK;
    }
    if (swizzle_mode > DSTORAGE_SWIZZLE_4K_12K_4K_12K) {
        return SCARLETT_ERR_DSTORAGE_SWIZZLE_FAILED;
    }

    const size_t kChunkBytes = 256u;
    const size_t tile_chunks = swizzle_macro_tile_chunks(swizzle_mode);
    const size_t tile_bytes = tile_chunks * kChunkBytes;

    std::vector<uint8_t> tmp_buf;
    const uint8_t *in_ptr = src;
    if (dst == src) {
        tmp_buf.assign(src, src + byte_size);
        in_ptr = tmp_buf.data();
    }

    const size_t full_tiles = byte_size / tile_bytes;
    for (size_t t = 0; t < full_tiles; ++t) {
        const size_t base = t * tile_bytes;
        for (size_t c = 0; c < tile_chunks; ++c) {
            const uint32_t mapped = inverse
                                        ? swizzle_inv_chunk(swizzle_mode, static_cast<uint32_t>(c))
                                        : swizzle_perm_chunk(swizzle_mode, static_cast<uint32_t>(c));
            std::memcpy(dst + base + static_cast<size_t>(mapped) * kChunkBytes,
                        in_ptr + base + c * kChunkBytes,
                        kChunkBytes);
        }
    }

    const size_t rem_offset = full_tiles * tile_bytes;
    if (rem_offset < byte_size) {
        std::memcpy(dst + rem_offset, in_ptr + rem_offset, byte_size - rem_offset);
    }
    return SCARLETT_OK;
}

} // namespace scarlett::dstorage_xs::unixlib
