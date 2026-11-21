/*
 * Proton-Scarlett — Audio, Input, Media & Upscaling Unified Unixlib ABI Contract
 *
 * Strictly freestanding C11 / C++20 header shared identically across:
 *   - PE64 DLL shims (xaudio2_9.dll, AcpHal.dll, GameInput.dll, Bink2Scarlett.dll,
 *                     amd_fidelityfx_loader_xbox.dll, amd_fidelityfx_upscaler_xbox.dll)
 *   - Native Linux unixlib ELF64 module (audio_scarlett_unix.so)
 *   - Host C++20 unit tests
 *
 * Zero dependencies on <windows.h>, libc, POSIX, libopus, libavcodec, or libvulkan.
 * All cross-boundary structs use explicit fixed-width types, explicit padding,
 * and compile-time SCARLETT_STATIC_ASSERT layout checks.
 */

#ifndef SCARLETT_AUDIO_MEDIA_UNIX_ABI_H
#define SCARLETT_AUDIO_MEDIA_UNIX_ABI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "scarlett_abi_common.h"
#include "scarlett_errors.h"
#include "scarlett_stub_thunk.h"
#include "scarlett_unix_call.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Portable Calling Convention & Host Bridge Registration
 * ============================================================================ */

#if defined(__x86_64__) || defined(_M_X64)
#if defined(__GNUC__) || defined(__clang__)
#define SCARLETT_WIN64_API __attribute__((ms_abi))
#elif defined(_MSC_VER)
#define SCARLETT_WIN64_API __stdcall
#else
#define SCARLETT_WIN64_API
#endif
#else
#define SCARLETT_WIN64_API
#endif

#if !defined(WINE_UNIX_CALL) && !defined(_WIN32) && !defined(__MINGW32__)
typedef int32_t (*scarlett_audio_unix_bridge_fn_t)(uint32_t opcode, void *params);
void scarlett_audio_pe_set_unix_bridge(scarlett_audio_unix_bridge_fn_t bridge_fn);
scarlett_audio_unix_bridge_fn_t scarlett_audio_pe_get_unix_bridge(void);
#endif

/* ============================================================================
 * Extended Unixlib Opcodes (0..7 defined in scarlett_unix_call.h; 8..20 here)
 * ============================================================================ */

typedef enum scarlett_audio_unix_ex_op_e {
    SCARLETT_AUDIO_UNIX_EX_CREATE_ENGINE       = 8,
    SCARLETT_AUDIO_UNIX_EX_DESTROY_ENGINE      = 9,
    SCARLETT_AUDIO_UNIX_EX_CREATE_VOICE        = 10,
    SCARLETT_AUDIO_UNIX_EX_DESTROY_VOICE       = 11,
    SCARLETT_AUDIO_UNIX_EX_VOICE_CONTROL       = 12,
    SCARLETT_AUDIO_UNIX_EX_SUBMIT_VOICE_BUFFER = 13,
    SCARLETT_AUDIO_UNIX_EX_RENDER_QUANTUM      = 14,
    SCARLETT_AUDIO_UNIX_EX_APU_MEM             = 15,
    SCARLETT_AUDIO_UNIX_EX_XDSP_OP             = 16,
    SCARLETT_AUDIO_UNIX_EX_INPUT_RING_OP       = 17,
    SCARLETT_AUDIO_UNIX_EX_BINK2_OP            = 18,
    SCARLETT_AUDIO_UNIX_EX_FSR_OP              = 19,
    SCARLETT_AUDIO_UNIX_EX_REPORT_STUB         = 20,
    SCARLETT_AUDIO_UNIX_EX_OP_COUNT            = 21
} scarlett_audio_unix_ex_op_t;

/* ============================================================================
 * Audio Constants, Formats, XAPO & HRTF
 * ============================================================================ */

#define SCARLETT_AUDIO_FORMAT_PCM_S16      UINT32_C(0x0001)
#define SCARLETT_AUDIO_FORMAT_IEEE_FLOAT   UINT32_C(0x0003)
#define SCARLETT_AUDIO_FORMAT_MS_ADPCM     UINT32_C(0x0002)
#define SCARLETT_AUDIO_FORMAT_IMA_ADPCM    UINT32_C(0x0011)
#define SCARLETT_AUDIO_FORMAT_XWMA         UINT32_C(0x0161)
#define SCARLETT_AUDIO_FORMAT_XMA2         UINT32_C(0x0166)
#define SCARLETT_AUDIO_FORMAT_OPUS         UINT32_C(0x704F)
#define SCARLETT_AUDIO_FORMAT_ADAT_OPUS    UINT32_C(0x54414441) /* "ADAT" LE */

#define SCARLETT_ADAT_RING_CAPACITY_BYTES  UINT32_C(524288)     /* 512 KiB */
#define SCARLETT_ADAT_MAGIC                UINT32_C(0x54414441) /* "ADAT" LE */
#define SCARLETT_AUDIO_QUANTUM_FRAMES      UINT32_C(480)        /* 10ms @ 48kHz */
#define SCARLETT_AUDIO_MAX_CHANNELS        UINT32_C(8)

typedef enum scarlett_audio_sink_mode_e {
    SCARLETT_AUDIO_SINK_AUTO     = 0,
    SCARLETT_AUDIO_SINK_HEADLESS = 1,
    SCARLETT_AUDIO_SINK_PIPEWIRE = 2,
    SCARLETT_AUDIO_SINK_OPENAL   = 3
} scarlett_audio_sink_mode_t;

typedef enum scarlett_audio_voice_kind_e {
    SCARLETT_VOICE_SOURCE    = 0,
    SCARLETT_VOICE_SUBMIX    = 1,
    SCARLETT_VOICE_MASTERING = 2
} scarlett_audio_voice_kind_t;

typedef enum scarlett_audio_voice_ctrl_op_e {
    SCARLETT_VOICE_CTRL_START        = 0,
    SCARLETT_VOICE_CTRL_STOP         = 1,
    SCARLETT_VOICE_CTRL_SET_VOLUME   = 2,
    SCARLETT_VOICE_CTRL_GET_VOLUME   = 3,
    SCARLETT_VOICE_CTRL_FLUSH        = 4,
    SCARLETT_VOICE_CTRL_SET_HRTF_POS = 5,
    SCARLETT_VOICE_CTRL_SET_XAPO     = 6,
    SCARLETT_VOICE_CTRL_GET_DETAILS  = 7
} scarlett_audio_voice_ctrl_op_t;

typedef enum scarlett_xapo_type_e {
    SCARLETT_XAPO_NONE         = 0,
    SCARLETT_XAPO_LOWPASS      = 1,
    SCARLETT_XAPO_REVERB       = 2,
    SCARLETT_XAPO_VOLUME_METER = 3
} scarlett_xapo_type_t;

typedef struct scarlett_audio_engine_create_params_s {
    uint32_t sink_mode;          /* scarlett_audio_sink_mode_t */
    uint32_t sample_rate_hz;     /* 0 -> 48000 */
    uint32_t channels;           /* 0 -> 2 */
    uint32_t enable_worker_thread;
    uint32_t enable_hrtf;
    uint32_t out_active_sink;
    uint64_t out_engine_handle;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_audio_engine_create_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_audio_engine_create_params_t) == 40,
                       "scarlett_audio_engine_create_params_t must be 40 bytes");

typedef struct scarlett_audio_voice_create_params_s {
    uint64_t engine_handle;
    uint64_t submix_voice_handle; /* 0 = route to mastering */
    uint64_t extradata_addr;      /* Optional codec extradata (e.g. XMA2 34B/52B) */
    uint32_t extradata_bytes;
    uint32_t voice_kind;          /* scarlett_audio_voice_kind_t */
    uint32_t format_tag;          /* SCARLETT_AUDIO_FORMAT_* */
    uint32_t channels;
    uint32_t sample_rate_hz;
    uint32_t block_align;
    uint32_t bits_per_sample;
    uint32_t enable_hrtf;
    uint32_t xapo_type;           /* scarlett_xapo_type_t */
    uint32_t flags;
    uint64_t out_voice_handle;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_audio_voice_create_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_audio_voice_create_params_t) == 80,
                       "scarlett_audio_voice_create_params_t must be 80 bytes");

typedef struct scarlett_audio_voice_ctrl_params_s {
    uint64_t voice_handle;
    uint32_t ctrl_op;             /* scarlett_audio_voice_ctrl_op_t */
    uint32_t param_u32;
    float    param_f0;            /* volume or HRTF azimuth_rad or cutoff_hz */
    float    param_f1;            /* HRTF elevation_rad or wet_mix */
    float    param_f2;            /* HRTF distance */
    uint32_t out_channels;
    uint32_t out_sample_rate;
    scarlett_hresult_t out_status;
} scarlett_audio_voice_ctrl_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_audio_voice_ctrl_params_t) == 40,
                       "scarlett_audio_voice_ctrl_params_t must be 40 bytes");

typedef struct scarlett_audio_voice_buffer_params_s {
    uint64_t voice_handle;
    uint64_t buffer_addr;
    uint32_t buffer_bytes;
    uint32_t flags;
    uint32_t play_begin;
    uint32_t play_length;
    uint32_t loop_count;
    uint32_t packet_count;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_audio_voice_buffer_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_audio_voice_buffer_params_t) == 48,
                       "scarlett_audio_voice_buffer_params_t must be 48 bytes");

typedef struct scarlett_audio_render_quantum_params_s {
    uint64_t engine_handle;
    uint64_t out_pcm_f32_addr;    /* Optional stereo/multichannel float output buffer */
    uint32_t frame_count;         /* 0 -> SCARLETT_AUDIO_QUANTUM_FRAMES */
    uint32_t out_frames_rendered;
    uint32_t out_active_voices;
    uint32_t out_xruns;
    uint32_t out_resamplers;
    uint32_t out_xma_voices;
    uint64_t out_total_cycles;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_audio_render_quantum_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_audio_render_quantum_params_t) == 56,
                       "scarlett_audio_render_quantum_params_t must be 56 bytes");

/* ============================================================================
 * APU Memory & AcpHal SHAPE Context Operations
 * ============================================================================ */

typedef enum scarlett_apu_mem_op_e {
    SCARLETT_APU_OP_CREATE_HEAP      = 0,
    SCARLETT_APU_OP_ALLOC            = 1,
    SCARLETT_APU_OP_FREE             = 2,
    SCARLETT_APU_OP_ACPHAL_CREATE    = 3,
    SCARLETT_APU_OP_ACPHAL_RELEASE   = 4,
    SCARLETT_APU_OP_ALLOC_SHAPE_CTX  = 5,
    SCARLETT_APU_OP_FREE_SHAPE_CTX   = 6
} scarlett_apu_mem_op_t;

typedef struct scarlett_audio_apu_mem_params_s {
    uint32_t op;                  /* scarlett_apu_mem_op_t */
    uint32_t alignment;
    uint64_t size_or_count;
    uint64_t handle_or_ptr;
    uint64_t out_ptr_or_handle;
    uint64_t out_aux;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_audio_apu_mem_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_audio_apu_mem_params_t) == 48,
                       "scarlett_audio_apu_mem_params_t must be 48 bytes");

/* ============================================================================
 * XDSP FFT / IFFT / Convolution Reverb / HRTF Direct Test Operations
 * ============================================================================ */

typedef enum scarlett_xdsp_op_e {
    SCARLETT_XDSP_OP_FFT          = 0,
    SCARLETT_XDSP_OP_IFFT         = 1,
    SCARLETT_XDSP_OP_CONV_REVERB  = 2,
    SCARLETT_XDSP_OP_HRTF_RENDER  = 3
} scarlett_xdsp_op_t;

typedef struct scarlett_audio_xdsp_params_s {
    uint32_t op;                  /* scarlett_xdsp_op_t */
    uint32_t count;               /* FFT size (power of 2) or sample count */
    uint64_t in_real_addr;
    uint64_t in_imag_or_ir_addr;
    uint64_t out_real_addr;
    uint64_t out_imag_addr;
    uint32_t ir_len;
    float    param_f0;            /* azimuth_rad or wet_mix */
    float    param_f1;            /* elevation_rad */
    scarlett_hresult_t out_status;
} scarlett_audio_xdsp_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_audio_xdsp_params_t) == 56,
                       "scarlett_audio_xdsp_params_t must be 56 bytes");

/* ============================================================================
 * GameInput v4.0 Ring Buffer & 4-Motor Vibration Operations
 * ============================================================================ */

#define SCARLETT_GAMEINPUT_KIND_UNKNOWN   UINT32_C(0x00000000)
#define SCARLETT_GAMEINPUT_KIND_KEYBOARD  UINT32_C(0x00000001)
#define SCARLETT_GAMEINPUT_KIND_MOUSE     UINT32_C(0x00000002)
#define SCARLETT_GAMEINPUT_KIND_GAMEPAD   UINT32_C(0x00000008)

typedef enum scarlett_input_ring_op_e {
    SCARLETT_INPUT_OP_CREATE_INSTANCE   = 0,
    SCARLETT_INPUT_OP_DESTROY_INSTANCE  = 1,
    SCARLETT_INPUT_OP_PUSH_READING      = 2,
    SCARLETT_INPUT_OP_GET_CURRENT       = 3,
    SCARLETT_INPUT_OP_GET_NEXT          = 4,
    SCARLETT_INPUT_OP_GET_PREVIOUS      = 5,
    SCARLETT_INPUT_OP_GET_TIMESTAMP     = 6,
    SCARLETT_INPUT_OP_QUERY_RUMBLE_PKT  = 7,
    SCARLETT_INPUT_OP_SET_TRANSPORT     = 8
} scarlett_input_ring_op_t;

typedef enum scarlett_input_transport_mode_e {
    SCARLETT_INPUT_TRANSPORT_AUTO_MOCK  = 0,
    SCARLETT_INPUT_TRANSPORT_HIDRAW_GIP = 1,
    SCARLETT_INPUT_TRANSPORT_EVDEV_FF   = 2
} scarlett_input_transport_mode_t;

typedef struct scarlett_gamepad_state_s {
    uint32_t buttons;
    float    left_trigger;
    float    right_trigger;
    float    left_thumbstick_x;
    float    left_thumbstick_y;
    float    right_thumbstick_x;
    float    right_thumbstick_y;
    uint32_t extended_buttons;
} scarlett_gamepad_state_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_gamepad_state_t) == 32,
                       "scarlett_gamepad_state_t must be 32 bytes");

typedef struct scarlett_mouse_state_s {
    uint32_t buttons;
    uint32_t reserved;
    int64_t  position_x;
    int64_t  position_y;
    int64_t  wheel_x;
    int64_t  wheel_y;
} scarlett_mouse_state_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_mouse_state_t) == 40,
                       "scarlett_mouse_state_t must be 40 bytes");

typedef struct scarlett_keyboard_state_s {
    uint32_t key_count;
    uint8_t  virtual_keys[16];
    uint32_t scan_codes[4];
    uint32_t reserved;
} scarlett_keyboard_state_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_keyboard_state_t) == 40,
                       "scarlett_keyboard_state_t must be 40 bytes");

typedef struct scarlett_input_reading_record_s {
    uint64_t sequence_number;
    uint64_t timestamp_us;
    uint64_t device_id;
    uint32_t input_kind;
    uint32_t reserved;
    scarlett_gamepad_state_t  gamepad;
    scarlett_mouse_state_t    mouse;
    scarlett_keyboard_state_t keyboard;
} scarlett_input_reading_record_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_input_reading_record_t) == 144,
                       "scarlett_input_reading_record_t must be 144 bytes");

typedef struct scarlett_input_ring_params_s {
    uint64_t instance_handle;
    uint64_t ref_sequence;
    uint64_t device_id;
    uint32_t op;                  /* scarlett_input_ring_op_t */
    uint32_t input_kind;
    uint64_t record_addr;         /* scarlett_input_reading_record_t* */
    uint64_t out_timestamp_us;
    uint64_t aux_addr;            /* Optional GIP hidraw packet / ff_effect dump */
    uint32_t aux_bytes;
    uint32_t out_aux_written;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_input_ring_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_input_ring_params_t) == 72,
                       "scarlett_input_ring_params_t must be 72 bytes");

/* ============================================================================
 * Bink2Scarlett Operations (All 12 Core Exports + YUV->RGB BT.709/BT.601)
 * ============================================================================ */

typedef enum scarlett_bink2_op_e {
    SCARLETT_BINK2_OP_OPEN               = 0,
    SCARLETT_BINK2_OP_CLOSE              = 1,
    SCARLETT_BINK2_OP_DO_FRAME           = 2,
    SCARLETT_BINK2_OP_NEXT_FRAME         = 3,
    SCARLETT_BINK2_OP_WAIT               = 4,
    SCARLETT_BINK2_OP_COPY_TO_BUFFER     = 5,
    SCARLETT_BINK2_OP_COPY_TO_BUFFER_RECT = 6,
    SCARLETT_BINK2_OP_SET_SOUND_ON_OFF   = 7,
    SCARLETT_BINK2_OP_SET_VOLUME         = 8,
    SCARLETT_BINK2_OP_PAUSE              = 9,
    SCARLETT_BINK2_OP_GOTO               = 10,
    SCARLETT_BINK2_OP_GET_SUMMARY        = 11
} scarlett_bink2_op_t;

#define SCARLETT_BINK_SURFACE_BGRA8888  UINT32_C(0)
#define SCARLETT_BINK_SURFACE_RGBA8888  UINT32_C(1)
#define SCARLETT_BINK_MATRIX_BT709      UINT32_C(0x10000000)
#define SCARLETT_BINK_MATRIX_BT601      UINT32_C(0x20000000)

typedef struct scarlett_bink_summary_s {
    uint32_t width;
    uint32_t height;
    uint32_t total_frames;
    uint32_t current_frame;
    uint32_t frame_rate_num;
    uint32_t frame_rate_den;
    uint32_t sound_enabled;
    int32_t  volume;
    uint32_t paused;
    uint32_t reserved;
} scarlett_bink_summary_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_bink_summary_t) == 40,
                       "scarlett_bink_summary_t must be 40 bytes");

typedef struct scarlett_bink2_op_params_s {
    uint64_t bink_handle;
    uint64_t path_or_buf_addr;
    uint32_t op;                  /* scarlett_bink2_op_t */
    uint32_t flags;
    int32_t  dest_pitch;
    uint32_t dest_height;
    uint32_t dest_x;
    uint32_t dest_y;
    uint32_t src_x;
    uint32_t src_y;
    uint32_t src_w;
    uint32_t src_h;
    uint32_t arg_u32;             /* frame num / track / pause flag */
    int32_t  arg_i32;             /* volume */
    uint64_t summary_addr;        /* scarlett_bink_summary_t* */
    uint32_t out_result_u32;
    scarlett_hresult_t out_status;
} scarlett_bink2_op_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_bink2_op_params_t) == 80,
                       "scarlett_bink2_op_params_t must be 80 bytes");

/* ============================================================================
 * AMD FidelityFX Xbox FSR 3 / FSR 4.1.2 Operations
 * ============================================================================ */

typedef enum scarlett_fsr_op_e {
    SCARLETT_FSR_OP_CREATE_CONTEXT      = 0,
    SCARLETT_FSR_OP_DESTROY_CONTEXT     = 1,
    SCARLETT_FSR_OP_CONFIGURE           = 2,
    SCARLETT_FSR_OP_QUERY               = 3,
    SCARLETT_FSR_OP_GET_RENDER_RES      = 4,
    SCARLETT_FSR_OP_GET_JITTER_OFFSET   = 5,
    SCARLETT_FSR_OP_GET_JITTER_PHASES   = 6,
    SCARLETT_FSR_OP_DISPATCH_UPSCALE    = 7
} scarlett_fsr_op_t;

typedef enum scarlett_fsr_quality_mode_e {
    SCARLETT_FSR_QUALITY_NATIVEAA          = 0,
    SCARLETT_FSR_QUALITY_QUALITY           = 1, /* 1.5x */
    SCARLETT_FSR_QUALITY_BALANCED          = 2, /* 1.7x */
    SCARLETT_FSR_QUALITY_PERFORMANCE       = 3, /* 2.0x */
    SCARLETT_FSR_QUALITY_ULTRA_PERFORMANCE = 4  /* 3.0x */
} scarlett_fsr_quality_mode_t;

typedef struct scarlett_fsr_op_params_s {
    uint64_t context_handle;
    uint32_t op;                  /* scarlett_fsr_op_t */
    uint32_t quality_mode;        /* scarlett_fsr_quality_mode_t */
    uint32_t display_width;
    uint32_t display_height;
    uint32_t render_width;
    uint32_t render_height;
    uint32_t jitter_index;
    uint32_t jitter_phase_count;
    float    jitter_x;
    float    jitter_y;
    float    sharpness;
    float    frame_time_delta_ms;
    uint64_t color_in_addr;       /* RGBA32F render buffer */
    uint64_t depth_in_addr;       /* Optional R32F depth buffer */
    uint64_t motion_in_addr;      /* Optional RG32F motion vectors */
    uint64_t color_out_addr;      /* RGBA32F upscaled output buffer */
    uint32_t fsr_version_packed;  /* e.g. 0x040102 for FSR 4.1.2 */
    uint32_t out_used_vulkan_gpu; /* 1 if Vulkan compute path ran, 0 if CPU fallback */
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_fsr_op_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_fsr_op_params_t) == 104,
                       "scarlett_fsr_op_params_t must be 104 bytes");

/* ============================================================================
 * Diagnostic Stub Reporting Payload for Audio/Media PE64 Shims
 * ============================================================================ */

typedef struct scarlett_audio_report_stub_params_s {
    scarlett_stub_context_t ctx;
    uint32_t out_action;          /* scarlett_stub_action_t */
    scarlett_hresult_t out_status;
} scarlett_audio_report_stub_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_audio_report_stub_params_t) == 480,
                       "scarlett_audio_report_stub_params_t must be 480 bytes");

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SCARLETT_AUDIO_MEDIA_UNIX_ABI_H */
