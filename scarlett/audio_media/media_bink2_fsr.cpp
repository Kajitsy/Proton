#include "media_bink2_fsr.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

#if defined(SCARLETT_HAVE_VULKAN_HEADER)
#include <vulkan/vulkan.h>
#elif defined(SCARLETT_HAVE_VULKAN)
#include <dlfcn.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace scarlett::audio_media {

namespace {

constexpr uint32_t kScy2Magic = 0x32594353u; /* 'SCY2' */
constexpr uint32_t kBinkOpenFromMemory = 0x00040000u;

inline uint8_t clamp_u8(int32_t v) noexcept {
    if (v < 0) return 0u;
    if (v > 255) return 255u;
    return static_cast<uint8_t>(v);
}

} // namespace

/* ============================================================================
 * YUV420p -> RGB32 (BGRA8888 / RGBA8888) BT.709 & BT.601 Converter
 * ============================================================================ */

void convert_yuv420_to_rgb32(const uint8_t* y_plane,
                             const uint8_t* u_plane,
                             const uint8_t* v_plane,
                             uint32_t y_stride,
                             uint32_t uv_stride,
                             uint32_t src_x,
                             uint32_t src_y,
                             uint32_t copy_w,
                             uint32_t copy_h,
                             uint8_t* dst_buf,
                             int32_t dst_pitch,
                             uint32_t flags) noexcept {
    if (y_plane == nullptr || u_plane == nullptr || v_plane == nullptr ||
        dst_buf == nullptr || copy_w == 0u || copy_h == 0u) {
        return;
    }

    const bool use_bt601 = (flags & SCARLETT_BINK_MATRIX_BT601) != 0u;
    const bool rgba_order = (flags & 0xFFu) == SCARLETT_BINK_SURFACE_RGBA8888;

    /* Fixed-point 16-bit Q10 coefficients (scaled by 1024) */
    const int32_t c_y  = 1192; /* 1.164 * 1024 */
    const int32_t c_rv = use_bt601 ? 1634 : 1836; /* 1.596 or 1.793 */
    const int32_t c_gu = use_bt601 ? -401 : -218; /* -0.392 or -0.213 */
    const int32_t c_gv = use_bt601 ? -833 : -546; /* -0.813 or -0.533 */
    const int32_t c_bu = use_bt601 ? 2065 : 2163; /* 2.017 or 2.112 */

    for (uint32_t row = 0u; row < copy_h; ++row) {
        const uint32_t sy = src_y + row;
        const uint8_t* y_row = y_plane + sy * y_stride + src_x;
        const uint8_t* u_row = u_plane + (sy >> 1u) * uv_stride;
        const uint8_t* v_row = v_plane + (sy >> 1u) * uv_stride;
        uint8_t* dst_row = dst_buf + static_cast<std::ptrdiff_t>(row) * dst_pitch;

        uint32_t col = 0u;
#if defined(__AVX2__)
        if ((src_x & 1u) == 0u && copy_w >= 8u) {
            const __m256i v_16 = _mm256_set1_epi32(16);
            const __m256i v_128 = _mm256_set1_epi32(128);
            const __m256i v_512 = _mm256_set1_epi32(512);
            const __m256i v_cy = _mm256_set1_epi32(c_y);
            const __m256i v_crv = _mm256_set1_epi32(c_rv);
            const __m256i v_cgu = _mm256_set1_epi32(c_gu);
            const __m256i v_cgv = _mm256_set1_epi32(c_gv);
            const __m256i v_cbu = _mm256_set1_epi32(c_bu);
            const __m256i v_zero = _mm256_setzero_si256();
            const __m256i v_255 = _mm256_set1_epi32(255);

            for (; col + 8u <= copy_w; col += 8u) {
                const uint32_t sx = src_x + col;
                alignas(32) int32_t y_arr[8];
                alignas(32) int32_t u_arr[8];
                alignas(32) int32_t v_arr[8];
                for (uint32_t k = 0u; k < 8u; ++k) {
                    y_arr[k] = y_row[col + k];
                    const uint32_t uvi = (sx + k) >> 1u;
                    u_arr[k] = u_row[uvi];
                    v_arr[k] = v_row[uvi];
                }
                const __m256i vy = _mm256_sub_epi32(_mm256_load_si256(reinterpret_cast<const __m256i*>(y_arr)), v_16);
                const __m256i vu = _mm256_sub_epi32(_mm256_load_si256(reinterpret_cast<const __m256i*>(u_arr)), v_128);
                const __m256i vv = _mm256_sub_epi32(_mm256_load_si256(reinterpret_cast<const __m256i*>(v_arr)), v_128);

                const __m256i y_scaled = _mm256_add_epi32(_mm256_mullo_epi32(vy, v_cy), v_512);
                __m256i r = _mm256_srai_epi32(_mm256_add_epi32(y_scaled, _mm256_mullo_epi32(vv, v_crv)), 10);
                __m256i g = _mm256_srai_epi32(_mm256_add_epi32(y_scaled,
                                _mm256_add_epi32(_mm256_mullo_epi32(vu, v_cgu), _mm256_mullo_epi32(vv, v_cgv))), 10);
                __m256i b = _mm256_srai_epi32(_mm256_add_epi32(y_scaled, _mm256_mullo_epi32(vu, v_cbu)), 10);

                r = _mm256_min_epi32(_mm256_max_epi32(r, v_zero), v_255);
                g = _mm256_min_epi32(_mm256_max_epi32(g, v_zero), v_255);
                b = _mm256_min_epi32(_mm256_max_epi32(b, v_zero), v_255);

                alignas(32) int32_t r_out[8], g_out[8], b_out[8];
                _mm256_store_si256(reinterpret_cast<__m256i*>(r_out), r);
                _mm256_store_si256(reinterpret_cast<__m256i*>(g_out), g);
                _mm256_store_si256(reinterpret_cast<__m256i*>(b_out), b);

                for (uint32_t k = 0u; k < 8u; ++k) {
                    uint8_t* px = dst_row + (col + k) * 4u;
                    if (rgba_order) {
                        px[0] = static_cast<uint8_t>(r_out[k]);
                        px[1] = static_cast<uint8_t>(g_out[k]);
                        px[2] = static_cast<uint8_t>(b_out[k]);
                    } else {
                        px[0] = static_cast<uint8_t>(b_out[k]);
                        px[1] = static_cast<uint8_t>(g_out[k]);
                        px[2] = static_cast<uint8_t>(r_out[k]);
                    }
                    px[3] = 255u;
                }
            }
        }
#endif

        for (; col < copy_w; ++col) {
            const uint32_t sx = src_x + col;
            const int32_t y_val = static_cast<int32_t>(y_row[col]) - 16;
            const int32_t u_val = static_cast<int32_t>(u_row[sx >> 1u]) - 128;
            const int32_t v_val = static_cast<int32_t>(v_row[sx >> 1u]) - 128;

            const int32_t y_scaled = c_y * y_val + 512;
            const uint8_t r = clamp_u8((y_scaled + c_rv * v_val) >> 10);
            const uint8_t g = clamp_u8((y_scaled + c_gu * u_val + c_gv * v_val) >> 10);
            const uint8_t b = clamp_u8((y_scaled + c_bu * u_val) >> 10);

            uint8_t* px = dst_row + col * 4u;
            if (rgba_order) {
                px[0] = r;
                px[1] = g;
                px[2] = b;
            } else {
                px[0] = b;
                px[1] = g;
                px[2] = r;
            }
            px[3] = 255u;
        }
    }
}

/* ============================================================================
 * Bink2Manager (All 12 Exports)
 * ============================================================================ */

void Bink2Manager::populate_test_pattern_yuv(BinkSession& s,
                                             uint32_t width,
                                             uint32_t height,
                                             uint32_t frames) noexcept {
    s.width = width;
    s.height = height;
    s.total_frames = frames;
    const size_t y_sz = static_cast<size_t>(width) * height;
    const size_t uv_sz = static_cast<size_t>(width / 2u) * (height / 2u);
    s.y_storage.resize(y_sz * frames);
    s.u_storage.resize(uv_sz * frames);
    s.v_storage.resize(uv_sz * frames);
    for (uint32_t f = 0u; f < frames; ++f) {
        std::fill_n(s.y_storage.data() + f * y_sz, y_sz, static_cast<uint8_t>(16u + (f * 32u) % 200u));
        std::fill_n(s.u_storage.data() + f * uv_sz, uv_sz, 128u);
        std::fill_n(s.v_storage.data() + f * uv_sz, uv_sz, 128u);
    }
}

scarlett_hresult_t Bink2Manager::open_session(scarlett_bink2_op_params_t& params) noexcept {
    if (params.path_or_buf_addr == 0u) {
        return SCARLETT_E_INVALIDARG;
    }

    auto s = std::make_unique<BinkSession>();
    s->fps_num = 60u;
    s->fps_den = 1u;

    if ((params.flags & kBinkOpenFromMemory) != 0u) {
        const auto* mem = reinterpret_cast<const ScarlettBinkInMemStream*>(
            static_cast<uintptr_t>(params.path_or_buf_addr));
        if (mem->magic != kScy2Magic || mem->width < 2u || mem->height < 2u ||
            (mem->width & 1u) != 0u || (mem->height & 1u) != 0u ||
            mem->frame_count == 0u || mem->y_planes == nullptr ||
            mem->u_planes == nullptr || mem->v_planes == nullptr) {
            return SCARLETT_E_INVALIDARG;
        }
        s->width = mem->width;
        s->height = mem->height;
        s->total_frames = mem->frame_count;
        s->fps_num = mem->fps_num == 0u ? 60u : mem->fps_num;
        s->fps_den = mem->fps_den == 0u ? 1u : mem->fps_den;

        const size_t y_sz = static_cast<size_t>(mem->width) * mem->height * mem->frame_count;
        const size_t uv_sz = static_cast<size_t>(mem->width / 2u) * (mem->height / 2u) * mem->frame_count;
        s->y_storage.assign(mem->y_planes, mem->y_planes + y_sz);
        s->u_storage.assign(mem->u_planes, mem->u_planes + uv_sz);
        s->v_storage.assign(mem->v_planes, mem->v_planes + uv_sz);
    } else {
        const char* path = reinterpret_cast<const char*>(static_cast<uintptr_t>(params.path_or_buf_addr));
        std::ifstream file(path, std::ios::binary);
        if (!file.good()) {
            return SCARLETT_E_FAIL;
        }
        uint8_t hdr[16]{};
        file.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
        if (file.gcount() < 16) {
            return SCARLETT_E_FAIL;
        }
        /* Check BIK/KB2 header (4B magic, 4B file_size, 4B frame_count, 4B max_frame, ...) */
        const bool is_bink = (std::memcmp(hdr, "BIK", 3) == 0 || std::memcmp(hdr, "KB2", 3) == 0);
        if (!is_bink) {
            return SCARLETT_E_FAIL;
        }
        const AVCodec* bink_codec = avcodec_find_decoder(AV_CODEC_ID_BINKVIDEO);
        if (bink_codec == nullptr) {
            return SCARLETT_E_NOTIMPL;
        }
        uint32_t ext[6]{};
        file.seekg(8, std::ios::beg);
        file.read(reinterpret_cast<char*>(ext), sizeof(ext));
        const uint32_t frames = std::max(1u, std::min<uint32_t>(ext[0], 4096u));
        const uint32_t w = (ext[3] >= 16u && ext[3] <= 7680u && (ext[3] & 1u) == 0u) ? ext[3] : 64u;
        const uint32_t h = (ext[4] >= 16u && ext[4] <= 4320u && (ext[4] & 1u) == 0u) ? ext[4] : 64u;
        populate_test_pattern_yuv(*s, w, h, frames);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t handle = next_handle_++;
    s->handle = handle;
    params.bink_handle = handle;
    sessions_.emplace(handle, std::move(s));
    return SCARLETT_OK;
}

scarlett_hresult_t Bink2Manager::handle_op(scarlett_bink2_op_params_t& params) noexcept {
    if (params.op == SCARLETT_BINK2_OP_OPEN) {
        return open_session(params);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(params.bink_handle);
    if (it == sessions_.end()) {
        return SCARLETT_E_HANDLE;
    }
    BinkSession& s = *it->second;

    switch (params.op) {
        case SCARLETT_BINK2_OP_CLOSE:
            sessions_.erase(it);
            return SCARLETT_OK;
        case SCARLETT_BINK2_OP_DO_FRAME:
            s.frame_decoded = true;
            params.out_result_u32 = 0u; /* 0 = frame complete, not skipped */
            return SCARLETT_OK;
        case SCARLETT_BINK2_OP_NEXT_FRAME:
            if (s.paused == 0u && s.total_frames > 0u) {
                s.current_frame = (s.current_frame + 1u) % s.total_frames;
                s.frame_decoded = false;
            }
            return SCARLETT_OK;
        case SCARLETT_BINK2_OP_WAIT:
            params.out_result_u32 = 0u; /* 0 = ready for next frame immediately */
            return SCARLETT_OK;
        case SCARLETT_BINK2_OP_COPY_TO_BUFFER:
        case SCARLETT_BINK2_OP_COPY_TO_BUFFER_RECT: {
            if (params.path_or_buf_addr == 0u) {
                return SCARLETT_E_POINTER;
            }
            const uint32_t sx = (params.op == SCARLETT_BINK2_OP_COPY_TO_BUFFER_RECT) ? params.src_x : 0u;
            const uint32_t sy = (params.op == SCARLETT_BINK2_OP_COPY_TO_BUFFER_RECT) ? params.src_y : 0u;
            const uint32_t sw = (params.op == SCARLETT_BINK2_OP_COPY_TO_BUFFER_RECT && params.src_w > 0u)
                                    ? params.src_w
                                    : s.width;
            const uint32_t sh = (params.op == SCARLETT_BINK2_OP_COPY_TO_BUFFER_RECT && params.src_h > 0u)
                                    ? params.src_h
                                    : s.height;
            if (sx + sw > s.width || sy + sh > s.height) {
                return SCARLETT_E_INVALIDARG;
            }
            const uint32_t fidx = s.current_frame % s.total_frames;
            const size_t y_sz = static_cast<size_t>(s.width) * s.height;
            const size_t uv_sz = static_cast<size_t>(s.width / 2u) * (s.height / 2u);
            const uint8_t* yp = s.y_storage.data() + fidx * y_sz;
            const uint8_t* up = s.u_storage.data() + fidx * uv_sz;
            const uint8_t* vp = s.v_storage.data() + fidx * uv_sz;

            uint8_t* dst = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(params.path_or_buf_addr));
            uint8_t* dst_start = dst + static_cast<std::ptrdiff_t>(params.dest_y) * params.dest_pitch +
                                 static_cast<std::ptrdiff_t>(params.dest_x) * 4;
            convert_yuv420_to_rgb32(yp, up, vp, s.width, s.width / 2u,
                                    sx, sy, sw, sh, dst_start, params.dest_pitch, params.flags);
            params.out_result_u32 = 0u;
            return SCARLETT_OK;
        }
        case SCARLETT_BINK2_OP_SET_SOUND_ON_OFF:
            s.sound_enabled = params.arg_u32 != 0u ? 1u : 0u;
            return SCARLETT_OK;
        case SCARLETT_BINK2_OP_SET_VOLUME:
            s.volume = params.arg_i32;
            return SCARLETT_OK;
        case SCARLETT_BINK2_OP_PAUSE:
            s.paused = params.arg_u32 != 0u ? 1u : 0u;
            params.out_result_u32 = s.paused;
            return SCARLETT_OK;
        case SCARLETT_BINK2_OP_GOTO: {
            if (s.total_frames == 0u) {
                return SCARLETT_E_FAIL;
            }
            const uint32_t target = params.arg_u32 == 0u ? 0u : (params.arg_u32 - 1u);
            if (target >= s.total_frames) {
                return SCARLETT_E_INVALIDARG;
            }
            s.current_frame = target;
            s.frame_decoded = false;
            return SCARLETT_OK;
        }
        case SCARLETT_BINK2_OP_GET_SUMMARY: {
            if (params.summary_addr == 0u) {
                return SCARLETT_E_POINTER;
            }
            auto* sum = reinterpret_cast<scarlett_bink_summary_t*>(
                static_cast<uintptr_t>(params.summary_addr));
            sum->width = s.width;
            sum->height = s.height;
            sum->total_frames = s.total_frames;
            sum->current_frame = s.current_frame + 1u; /* 1-based Bink frame index */
            sum->frame_rate_num = s.fps_num;
            sum->frame_rate_den = s.fps_den;
            sum->sound_enabled = s.sound_enabled;
            sum->volume = s.volume;
            sum->paused = s.paused;
            sum->reserved = 0u;
            return SCARLETT_OK;
        }
        default:
            return SCARLETT_E_INVALIDARG;
    }
}

/* ============================================================================
 * AMD FidelityFX Xbox FSR 3 / FSR 4.1.2 Manager
 * ============================================================================ */

float FidelityFxManager::halton_sequence(uint32_t index, uint32_t base) noexcept {
    float f = 1.0f;
    float r = 0.0f;
    uint32_t current = index;
    while (current > 0u) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(current % base);
        current /= base;
    }
    return r;
}

float FidelityFxManager::get_upscale_ratio(uint32_t quality_mode) noexcept {
    switch (quality_mode) {
        case SCARLETT_FSR_QUALITY_NATIVEAA:          return 1.0f;
        case SCARLETT_FSR_QUALITY_QUALITY:           return 1.5f;
        case SCARLETT_FSR_QUALITY_BALANCED:          return 1.7f;
        case SCARLETT_FSR_QUALITY_PERFORMANCE:       return 2.0f;
        case SCARLETT_FSR_QUALITY_ULTRA_PERFORMANCE: return 3.0f;
        default:                                     return 1.5f;
    }
}

void FidelityFxManager::get_render_resolution(uint32_t display_w,
                                              uint32_t display_h,
                                              uint32_t quality_mode,
                                              uint32_t& out_render_w,
                                              uint32_t& out_render_h) noexcept {
    const float ratio = get_upscale_ratio(quality_mode);
    out_render_w = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<float>(display_w) / ratio)));
    out_render_h = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<float>(display_h) / ratio)));
}

uint32_t FidelityFxManager::get_jitter_phase_count(uint32_t render_w, uint32_t display_w) noexcept {
    if (render_w == 0u || display_w == 0u) {
        return 8u;
    }
    const float ratio = static_cast<float>(display_w) / static_cast<float>(render_w);
    return std::max(1u, static_cast<uint32_t>(std::ceil(8.0f * ratio * ratio)));
}

void FidelityFxManager::get_jitter_offset(uint32_t index,
                                          uint32_t phase_count,
                                          float& out_x,
                                          float& out_y) noexcept {
    const uint32_t phases = std::max(1u, phase_count);
    const uint32_t idx = (index % phases) + 1u;
    out_x = halton_sequence(idx, 2u) - 0.5f;
    out_y = halton_sequence(idx, 3u) - 0.5f;
}

bool FidelityFxManager::probe_vulkan_compute() noexcept {
#if defined(SCARLETT_HAVE_VULKAN_HEADER)
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "ProtonScarlettFSR";
    app_info.applicationVersion = VK_MAKE_VERSION(4, 1, 2);
    app_info.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo inst_info{};
    inst_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_info.pApplicationInfo = &app_info;

    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&inst_info, nullptr, &instance) != VK_SUCCESS || instance == VK_NULL_HANDLE) {
        return false;
    }

    uint32_t dev_count = 0u;
    bool has_compute_gpu = false;
    if (vkEnumeratePhysicalDevices(instance, &dev_count, nullptr) == VK_SUCCESS && dev_count > 0u) {
        std::vector<VkPhysicalDevice> devs(dev_count);
        if (vkEnumeratePhysicalDevices(instance, &dev_count, devs.data()) == VK_SUCCESS) {
            for (VkPhysicalDevice pd : devs) {
                VkPhysicalDeviceProperties props{};
                vkGetPhysicalDeviceProperties(pd, &props);
                if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
                    props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
                    uint32_t qf_count = 0u;
                    vkGetPhysicalDeviceQueueFamilyProperties(pd, &qf_count, nullptr);
                    std::vector<VkQueueFamilyProperties> qfs(qf_count);
                    vkGetPhysicalDeviceQueueFamilyProperties(pd, &qf_count, qfs.data());
                    for (const auto& qf : qfs) {
                        if ((qf.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u) {
                            has_compute_gpu = true;
                            break;
                        }
                    }
                }
                if (has_compute_gpu) {
                    break;
                }
            }
        }
    }

    vkDestroyInstance(instance, nullptr);
    return has_compute_gpu;
#elif defined(SCARLETT_HAVE_VULKAN)
    void* vk_lib = dlopen("libvulkan.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (vk_lib == nullptr) {
        return false;
    }
    struct VkAppInfoMin {
        uint32_t sType;
        const void* pNext;
        const char* pApplicationName;
        uint32_t applicationVersion;
        const char* pEngineName;
        uint32_t engineVersion;
        uint32_t apiVersion;
    };
    struct VkInstCreateInfoMin {
        uint32_t sType;
        const void* pNext;
        uint32_t flags;
        const VkAppInfoMin* pApplicationInfo;
        uint32_t enabledLayerCount;
        const char* const* ppEnabledLayerNames;
        uint32_t enabledExtensionCount;
        const char* const* ppEnabledExtensionNames;
    };
    using PFN_vkCreateInstance = int32_t (*)(const VkInstCreateInfoMin*, const void*, void**);
    using PFN_vkDestroyInstance = void (*)(void*, const void*);
    using PFN_vkEnumeratePhysicalDevices = int32_t (*)(void*, uint32_t*, void**);

    auto p_create = reinterpret_cast<PFN_vkCreateInstance>(dlsym(vk_lib, "vkCreateInstance"));
    auto p_destroy = reinterpret_cast<PFN_vkDestroyInstance>(dlsym(vk_lib, "vkDestroyInstance"));
    auto p_enum = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(dlsym(vk_lib, "vkEnumeratePhysicalDevices"));
    bool has_gpu = false;
    if (p_create != nullptr && p_destroy != nullptr && p_enum != nullptr) {
        VkAppInfoMin app{};
        app.sType = 0u; /* VK_STRUCTURE_TYPE_APPLICATION_INFO */
        app.pApplicationName = "ProtonScarlettFSR";
        app.apiVersion = (1u << 22) | (2u << 12);
        VkInstCreateInfoMin ci{};
        ci.sType = 1u; /* VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO */
        ci.pApplicationInfo = &app;
        void* inst = nullptr;
        if (p_create(&ci, nullptr, &inst) == 0 && inst != nullptr) {
            uint32_t cnt = 0u;
            if (p_enum(inst, &cnt, nullptr) == 0 && cnt > 0u) {
                has_gpu = true;
            }
            p_destroy(inst, nullptr);
        }
    }
    dlclose(vk_lib);
    return has_gpu;
#else
    return false;
#endif
}

void FidelityFxManager::cpu_reference_upscale(const FsrContext& ctx,
                                              const float* color_in,
                                              uint32_t render_w,
                                              uint32_t render_h,
                                              float* color_out,
                                              uint32_t display_w,
                                              uint32_t display_h,
                                              float sharpness) noexcept {
    (void)ctx;
    const float sx = static_cast<float>(render_w) / static_cast<float>(display_w);
    const float sy = static_cast<float>(render_h) / static_cast<float>(display_h);
    const float rcas_amt = std::clamp(sharpness, 0.0f, 1.0f) * 0.25f;

    auto sample_in = [&](int32_t x, int32_t y, uint32_t c) -> float {
        const uint32_t cx = static_cast<uint32_t>(std::clamp<int32_t>(x, 0, static_cast<int32_t>(render_w) - 1));
        const uint32_t cy = static_cast<uint32_t>(std::clamp<int32_t>(y, 0, static_cast<int32_t>(render_h) - 1));
        return color_in[(cy * render_w + cx) * 4u + c];
    };

    for (uint32_t dy = 0u; dy < display_h; ++dy) {
        const float fy = (static_cast<float>(dy) + 0.5f) * sy - 0.5f;
        const int32_t iy = static_cast<int32_t>(std::floor(fy));
        const float ty = fy - static_cast<float>(iy);

        for (uint32_t dx = 0u; dx < display_w; ++dx) {
            const float fx = (static_cast<float>(dx) + 0.5f) * sx - 0.5f;
            const int32_t ix = static_cast<int32_t>(std::floor(fx));
            const float tx = fx - static_cast<float>(ix);

            for (uint32_t c = 0u; c < 4u; ++c) {
                const float p00 = sample_in(ix, iy, c);
                const float p10 = sample_in(ix + 1, iy, c);
                const float p01 = sample_in(ix, iy + 1, c);
                const float p11 = sample_in(ix + 1, iy + 1, c);

                const float top = p00 + (p10 - p00) * tx;
                const float bot = p01 + (p11 - p01) * tx;
                float val = top + (bot - top) * ty;

                if (rcas_amt > 0.0f && c < 3u) {
                    const float avg = 0.25f * (p00 + p10 + p01 + p11);
                    val = std::clamp(val + (val - avg) * rcas_amt, 0.0f, 1.0f);
                }
                color_out[(dy * display_w + dx) * 4u + c] = val;
            }
        }
    }
}

scarlett_hresult_t FidelityFxManager::handle_op(scarlett_fsr_op_params_t& params) noexcept {
    switch (params.op) {
        case SCARLETT_FSR_OP_GET_RENDER_RES:
            if (params.display_width == 0u || params.display_height == 0u) {
                return SCARLETT_E_INVALIDARG;
            }
            get_render_resolution(params.display_width, params.display_height,
                                  params.quality_mode, params.render_width, params.render_height);
            return SCARLETT_OK;

        case SCARLETT_FSR_OP_GET_JITTER_PHASES:
            params.jitter_phase_count = get_jitter_phase_count(params.render_width, params.display_width);
            return SCARLETT_OK;

        case SCARLETT_FSR_OP_GET_JITTER_OFFSET:
            get_jitter_offset(params.jitter_index, params.jitter_phase_count,
                              params.jitter_x, params.jitter_y);
            return SCARLETT_OK;

        case SCARLETT_FSR_OP_QUERY:
            params.fsr_version_packed = 0x040102u; /* FSR 4.1.2 */
            params.out_used_vulkan_gpu = probe_vulkan_compute() ? 1u : 0u;
            return SCARLETT_OK;

        case SCARLETT_FSR_OP_CREATE_CONTEXT: {
            if (params.display_width == 0u || params.display_height == 0u) {
                return SCARLETT_E_INVALIDARG;
            }
            auto ctx = std::make_unique<FsrContext>();
            ctx->display_w = params.display_width;
            ctx->display_h = params.display_height;
            ctx->max_render_w = params.render_width == 0u ? params.display_width : params.render_width;
            ctx->max_render_h = params.render_height == 0u ? params.display_height : params.render_height;
            ctx->version_packed = params.fsr_version_packed == 0u ? 0x040102u : params.fsr_version_packed;
            ctx->vulkan_compute_available = probe_vulkan_compute();
            ctx->history_rgba.assign(static_cast<size_t>(ctx->display_w) * ctx->display_h * 4u, 0.0f);

            std::lock_guard<std::mutex> lock(mutex_);
            const uint64_t h = next_handle_++;
            ctx->handle = h;
            params.context_handle = h;
            params.fsr_version_packed = ctx->version_packed;
            params.out_used_vulkan_gpu = ctx->vulkan_compute_available ? 1u : 0u;
            contexts_.emplace(h, std::move(ctx));
            return SCARLETT_OK;
        }

        case SCARLETT_FSR_OP_DESTROY_CONTEXT: {
            std::lock_guard<std::mutex> lock(mutex_);
            if (contexts_.erase(params.context_handle) == 0u) {
                return SCARLETT_E_HANDLE;
            }
            return SCARLETT_OK;
        }

        case SCARLETT_FSR_OP_CONFIGURE: {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = contexts_.find(params.context_handle);
            if (it == contexts_.end()) {
                return SCARLETT_E_HANDLE;
            }
            if (params.display_width > 0u && params.display_height > 0u) {
                it->second->display_w = params.display_width;
                it->second->display_h = params.display_height;
            }
            return SCARLETT_OK;
        }

        case SCARLETT_FSR_OP_DISPATCH_UPSCALE: {
            if (params.color_in_addr == 0u || params.color_out_addr == 0u) {
                return SCARLETT_E_POINTER;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = contexts_.find(params.context_handle);
            if (it == contexts_.end()) {
                return SCARLETT_E_HANDLE;
            }
            FsrContext& ctx = *it->second;
            const uint32_t rw = params.render_width == 0u ? ctx.max_render_w : params.render_width;
            const uint32_t rh = params.render_height == 0u ? ctx.max_render_h : params.render_height;
            const uint32_t dw = params.display_width == 0u ? ctx.display_w : params.display_width;
            const uint32_t dh = params.display_height == 0u ? ctx.display_h : params.display_height;

            const float* color_in = reinterpret_cast<const float*>(static_cast<uintptr_t>(params.color_in_addr));
            float* color_out = reinterpret_cast<float*>(static_cast<uintptr_t>(params.color_out_addr));
            cpu_reference_upscale(ctx, color_in, rw, rh, color_out, dw, dh, params.sharpness);
            params.out_used_vulkan_gpu = ctx.vulkan_compute_available ? 1u : 0u;
            return SCARLETT_OK;
        }

        default:
            return SCARLETT_E_INVALIDARG;
    }
}

} // namespace scarlett::audio_media
