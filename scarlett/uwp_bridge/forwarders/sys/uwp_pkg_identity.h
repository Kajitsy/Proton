/*
 * Proton-Scarlett — Clean-Room UWP Package Identity & AppPolicy Resolver
 *
 * Freestanding C11 / C++20 header (zero POSIX/Linux dependencies).
 * Uses uint16_t UTF-16 code units so PE64 (_WIN32) and Linux host unit tests
 * share the exact 16-bit Windows PACKAGE_ID / AppModel ABI.
 *
 * Resolution order when PROTON_UWP_BRIDGE=1:
 *   1. PROTON_UWP_PACKAGE_* / PROTON_UWP_AUMID environment variables
 *   2. Fallback to PROTON_UWP_MANIFEST or AppxManifest.xml / MicrosoftGame.Config
 *      alongside the current executable
 *   3. Derives deterministic 13-char Windows PublisherId via SHA-256(UTF-16LE
 *      Publisher) + Crockford base32 when not supplied explicitly
 */

#ifndef SCARLETT_UWP_PKG_IDENTITY_H
#define SCARLETT_UWP_PKG_IDENTITY_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define UWP_PKG_MAX_STR 260u
#define UWP_PKG_MAX_PATH 520u
#define UWP_PKG_PUBLISHER_ID_LEN 13u

#define UWP_ERROR_SUCCESS 0
#define UWP_ERROR_INVALID_PARAMETER 87
#define UWP_ERROR_INSUFFICIENT_BUFFER 122
#define UWP_APPMODEL_ERROR_NO_PACKAGE 15700
#define UWP_APPMODEL_ERROR_NO_APPLICATION 15703

#define UWP_PROC_ARCH_X86 0u
#define UWP_PROC_ARCH_ARM 5u
#define UWP_PROC_ARCH_AMD64 9u
#define UWP_PROC_ARCH_NEUTRAL 11u
#define UWP_PROC_ARCH_ARM64 12u

#define UWP_APPPOLICY_TERM_EXIT_PROCESS 0
#define UWP_APPPOLICY_TERM_TERMINATE_PROCESS 1

#define UWP_APPPOLICY_WIN_NONE 0
#define UWP_APPPOLICY_WIN_UNIVERSAL 1
#define UWP_APPPOLICY_WIN_CLASSIC_DESKTOP 2

typedef struct uwp_pkg_version_s {
    union {
        uint64_t Version;
        struct {
            uint16_t Revision;
            uint16_t Build;
            uint16_t Minor;
            uint16_t Major;
        };
    };
} uwp_pkg_version_t;

typedef struct uwp_pkg_id_s {
    uint32_t reserved;
    uint32_t processorArchitecture;
    uwp_pkg_version_t version;
    uint16_t *name;
    uint16_t *publisher;
    uint16_t *resourceId;
    uint16_t *publisherId;
} uwp_pkg_id_t;

typedef struct uwp_pkg_resolved_s {
    uint32_t valid;
    uint32_t bridge_enabled;
    uint32_t architecture;
    uwp_pkg_version_t version;
    uint16_t name[UWP_PKG_MAX_STR];
    uint16_t publisher[UWP_PKG_MAX_STR];
    uint16_t resource_id[64];
    uint16_t publisher_id[32];
    uint16_t family_name[UWP_PKG_MAX_STR];
    uint16_t full_name[UWP_PKG_MAX_STR];
    uint16_t app_id[128];
    uint16_t aumid[UWP_PKG_MAX_STR];
    uint16_t package_dir[UWP_PKG_MAX_PATH];
} uwp_pkg_resolved_t;

/* Host-testable environment and file reader callbacks */
typedef uint32_t (*uwp_pkg_get_env_fn_t)(
    const uint16_t *name, uint16_t *buf, uint32_t cap, void *user_data);
typedef uint32_t (*uwp_pkg_get_exe_path_fn_t)(
    uint16_t *buf, uint32_t cap, void *user_data);
typedef int (*uwp_pkg_read_file_fn_t)(
    const uint16_t *path, uint8_t *buf, size_t cap, size_t *out_len, void *user_data);

typedef struct uwp_pkg_provider_s {
    uwp_pkg_get_env_fn_t get_env;
    uwp_pkg_get_exe_path_fn_t get_exe_path;
    uwp_pkg_read_file_fn_t read_file;
    void *user_data;
} uwp_pkg_provider_t;

static inline void uwp_pkg_zero(void *dst, size_t n) {
    volatile uint8_t *p = (volatile uint8_t *)dst;
    for (size_t i = 0; i < n; ++i) {
        p[i] = 0u;
    }
}

static inline size_t uwp_u16_len(const uint16_t *s) {
    size_t n = 0u;
    if (s == NULL) {
        return 0u;
    }
    while (s[n] != 0u) {
        ++n;
    }
    return n;
}

static inline void uwp_u16_copy(uint16_t *dst, size_t cap, const uint16_t *src) {
    size_t i = 0u;
    if (dst == NULL || cap == 0u) {
        return;
    }
    if (src != NULL) {
        while (i + 1u < cap && src[i] != 0u) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = 0u;
}

static inline void uwp_u16_from_ascii(uint16_t *dst, size_t cap, const char *src) {
    size_t i = 0u;
    if (dst == NULL || cap == 0u) {
        return;
    }
    if (src != NULL) {
        while (i + 1u < cap && src[i] != '\0') {
            dst[i] = (uint16_t)(uint8_t)src[i];
            ++i;
        }
    }
    dst[i] = 0u;
}

static inline uint16_t uwp_u16_tolower(uint16_t c) {
    if (c >= (uint16_t)'A' && c <= (uint16_t)'Z') {
        return (uint16_t)(c + ('a' - 'A'));
    }
    return c;
}

static inline int uwp_u16_icmp(const uint16_t *a, const uint16_t *b) {
    if (a == b) {
        return 0;
    }
    if (a == NULL) {
        return -1;
    }
    if (b == NULL) {
        return 1;
    }
    while (*a != 0u && *b != 0u) {
        uint16_t ca = uwp_u16_tolower(*a);
        uint16_t cb = uwp_u16_tolower(*b);
        if (ca != cb) {
            return (ca < cb) ? -1 : 1;
        }
        ++a;
        ++b;
    }
    if (*a == *b) {
        return 0;
    }
    return (*a < *b) ? -1 : 1;
}

static inline int uwp_u16_icmp_ascii(const uint16_t *a, const char *b) {
    if (a == NULL || b == NULL) {
        return (a == NULL && b == NULL) ? 0 : -1;
    }
    while (*a != 0u && *b != '\0') {
        uint16_t ca = uwp_u16_tolower(*a);
        uint16_t cb = uwp_u16_tolower((uint16_t)(uint8_t)*b);
        if (ca != cb) {
            return (ca < cb) ? -1 : 1;
        }
        ++a;
        ++b;
    }
    if (*a == 0u && *b == '\0') {
        return 0;
    }
    return (*a == 0u) ? -1 : 1;
}

static inline size_t uwp_u16_append(uint16_t *dst, size_t cap, size_t pos, const uint16_t *src) {
    if (dst == NULL || cap == 0u || src == NULL) {
        return pos;
    }
    while (*src != 0u && pos + 1u < cap) {
        dst[pos++] = *src++;
    }
    dst[pos] = 0u;
    return pos;
}

static inline size_t uwp_u16_append_ascii(uint16_t *dst, size_t cap, size_t pos, const char *src) {
    if (dst == NULL || cap == 0u || src == NULL) {
        return pos;
    }
    while (*src != '\0' && pos + 1u < cap) {
        dst[pos++] = (uint16_t)(uint8_t)*src++;
    }
    dst[pos] = 0u;
    return pos;
}

static inline size_t uwp_u16_append_u16_dec(uint16_t *dst, size_t cap, size_t pos, uint16_t val) {
    char tmp[8];
    int n = 0;
    if (val == 0u) {
        tmp[n++] = '0';
    } else {
        while (val != 0u && n < 8) {
            tmp[n++] = (char)('0' + (val % 10u));
            val /= 10u;
        }
    }
    while (n > 0 && pos + 1u < cap) {
        dst[pos++] = (uint16_t)(uint8_t)tmp[--n];
    }
    if (pos < cap) {
        dst[pos] = 0u;
    }
    return pos;
}

/* ============================================================================
 * Clean-Room SHA-256 + Windows PublisherId Base32
 * ============================================================================ */

static inline uint32_t uwp_rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32u - n));
}

static inline void uwp_sha256(const uint8_t *data, size_t len, uint8_t out[32]) {
    static const uint32_t k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };
    uint32_t state[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };

    uint8_t block[64];
    size_t pos = 0u;
    uint64_t bit_len = (uint64_t)len * 8u;

    while (pos + 64u <= len) {
        const uint8_t *b = data + pos;
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = ((uint32_t)b[i * 4] << 24) | ((uint32_t)b[i * 4 + 1] << 16) |
                   ((uint32_t)b[i * 4 + 2] << 8) | (uint32_t)b[i * 4 + 3];
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = uwp_rotr32(w[i - 15], 7u) ^ uwp_rotr32(w[i - 15], 18u) ^ (w[i - 15] >> 3);
            uint32_t s1 = uwp_rotr32(w[i - 2], 17u) ^ uwp_rotr32(w[i - 2], 19u) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = state[0], b0 = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = uwp_rotr32(e, 6u) ^ uwp_rotr32(e, 11u) ^ uwp_rotr32(e, 25u);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = h + S1 + ch + k[i] + w[i];
            uint32_t S0 = uwp_rotr32(a, 2u) ^ uwp_rotr32(a, 13u) ^ uwp_rotr32(a, 22u);
            uint32_t maj = (a & b0) ^ (a & c) ^ (b0 & c);
            uint32_t temp2 = S0 + maj;
            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b0; b0 = a; a = temp1 + temp2;
        }
        state[0] += a; state[1] += b0; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
        pos += 64u;
    }

    size_t rem = len - pos;
    for (size_t i = 0; i < 64u; ++i) {
        block[i] = (i < rem) ? data[pos + i] : 0u;
    }
    block[rem] = 0x80u;
    if (rem >= 56u) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
                   ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = uwp_rotr32(w[i - 15], 7u) ^ uwp_rotr32(w[i - 15], 18u) ^ (w[i - 15] >> 3);
            uint32_t s1 = uwp_rotr32(w[i - 2], 17u) ^ uwp_rotr32(w[i - 2], 19u) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = state[0], b0 = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = uwp_rotr32(e, 6u) ^ uwp_rotr32(e, 11u) ^ uwp_rotr32(e, 25u);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = h + S1 + ch + k[i] + w[i];
            uint32_t S0 = uwp_rotr32(a, 2u) ^ uwp_rotr32(a, 13u) ^ uwp_rotr32(a, 22u);
            uint32_t maj = (a & b0) ^ (a & c) ^ (b0 & c);
            uint32_t temp2 = S0 + maj;
            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b0; b0 = a; a = temp1 + temp2;
        }
        state[0] += a; state[1] += b0; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
        for (size_t i = 0; i < 64u; ++i) {
            block[i] = 0u;
        }
    }
    for (int i = 0; i < 8; ++i) {
        block[56 + i] = (uint8_t)((bit_len >> (56 - i * 8)) & 0xFFu);
    }
    {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
                   ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = uwp_rotr32(w[i - 15], 7u) ^ uwp_rotr32(w[i - 15], 18u) ^ (w[i - 15] >> 3);
            uint32_t s1 = uwp_rotr32(w[i - 2], 17u) ^ uwp_rotr32(w[i - 2], 19u) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = state[0], b0 = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = uwp_rotr32(e, 6u) ^ uwp_rotr32(e, 11u) ^ uwp_rotr32(e, 25u);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = h + S1 + ch + k[i] + w[i];
            uint32_t S0 = uwp_rotr32(a, 2u) ^ uwp_rotr32(a, 13u) ^ uwp_rotr32(a, 22u);
            uint32_t maj = (a & b0) ^ (a & c) ^ (b0 & c);
            uint32_t temp2 = S0 + maj;
            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b0; b0 = a; a = temp1 + temp2;
        }
        state[0] += a; state[1] += b0; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }
    for (int i = 0; i < 8; ++i) {
        out[i * 4] = (uint8_t)((state[i] >> 24) & 0xFFu);
        out[i * 4 + 1] = (uint8_t)((state[i] >> 16) & 0xFFu);
        out[i * 4 + 2] = (uint8_t)((state[i] >> 8) & 0xFFu);
        out[i * 4 + 3] = (uint8_t)(state[i] & 0xFFu);
    }
}

static inline void uwp_pkg_compute_publisher_id(const uint16_t *publisher, uint16_t out_id[32]) {
    static const char alphabet[] = "0123456789abcdefghjkmnpqrstvwxyz";
    uint8_t utf16le[UWP_PKG_MAX_STR * 2u];
    size_t ulen = uwp_u16_len(publisher);
    if (ulen > UWP_PKG_MAX_STR) {
        ulen = UWP_PKG_MAX_STR;
    }
    for (size_t i = 0u; i < ulen; ++i) {
        utf16le[i * 2u] = (uint8_t)(publisher[i] & 0xFFu);
        utf16le[i * 2u + 1u] = (uint8_t)((publisher[i] >> 8) & 0xFFu);
    }
    uint8_t digest[32];
    uwp_sha256(utf16le, ulen * 2u, digest);

    uint64_t val = 0u;
    for (int i = 0; i < 8; ++i) {
        val = (val << 8) | (uint64_t)digest[i];
    }
    for (int i = 0; i < 13; ++i) {
        int shift = 59 - i * 5;
        uint32_t idx;
        if (shift >= 0) {
            idx = (uint32_t)((val >> shift) & 0x1Fu);
        } else {
            idx = (uint32_t)((val << (-shift)) & 0x1Fu);
        }
        out_id[i] = (uint16_t)(uint8_t)alphabet[idx];
    }
    out_id[13] = 0u;
}

/* ============================================================================
 * Version, Architecture & XML Manifest Parsing Helpers
 * ============================================================================ */

static inline int uwp_pkg_parse_version_u16(const uint16_t *ver_str, uwp_pkg_version_t *out) {
    uint16_t parts[4] = {0u, 0u, 0u, 0u};
    int part_idx = 0;
    uint32_t acc = 0u;
    int saw_digit = 0;
    if (ver_str == NULL || out == NULL || ver_str[0] == 0u) {
        return 0;
    }
    for (size_t i = 0u; ver_str[i] != 0u; ++i) {
        uint16_t c = ver_str[i];
        if (c >= (uint16_t)'0' && c <= (uint16_t)'9') {
            acc = acc * 10u + (uint32_t)(c - (uint16_t)'0');
            if (acc > 0xFFFFu) {
                acc = 0xFFFFu;
            }
            saw_digit = 1;
        } else if (c == (uint16_t)'.' && part_idx < 3) {
            parts[part_idx++] = (uint16_t)acc;
            acc = 0u;
            saw_digit = 0;
        } else {
            break;
        }
    }
    if (saw_digit && part_idx < 4) {
        parts[part_idx++] = (uint16_t)acc;
    }
    if (part_idx == 0) {
        return 0;
    }
    out->Major = parts[0];
    out->Minor = parts[1];
    out->Build = parts[2];
    out->Revision = parts[3];
    return 1;
}

static inline uint32_t uwp_pkg_arch_from_u16(const uint16_t *arch) {
    if (arch == NULL || arch[0] == 0u) {
        return UWP_PROC_ARCH_AMD64;
    }
    if (uwp_u16_icmp_ascii(arch, "x64") == 0 || uwp_u16_icmp_ascii(arch, "amd64") == 0) {
        return UWP_PROC_ARCH_AMD64;
    }
    if (uwp_u16_icmp_ascii(arch, "x86") == 0) {
        return UWP_PROC_ARCH_X86;
    }
    if (uwp_u16_icmp_ascii(arch, "arm64") == 0) {
        return UWP_PROC_ARCH_ARM64;
    }
    if (uwp_u16_icmp_ascii(arch, "arm") == 0) {
        return UWP_PROC_ARCH_ARM;
    }
    if (uwp_u16_icmp_ascii(arch, "neutral") == 0) {
        return UWP_PROC_ARCH_NEUTRAL;
    }
    return UWP_PROC_ARCH_AMD64;
}

static inline const char *uwp_pkg_arch_to_ascii(uint32_t arch) {
    switch (arch) {
        case UWP_PROC_ARCH_X86: return "x86";
        case UWP_PROC_ARCH_ARM: return "arm";
        case UWP_PROC_ARCH_AMD64: return "x64";
        case UWP_PROC_ARCH_NEUTRAL: return "neutral";
        case UWP_PROC_ARCH_ARM64: return "arm64";
        default: return "x64";
    }
}

static inline void uwp_utf8_to_u16_unescape(
    const char *src, size_t src_len, uint16_t *dst, size_t dst_cap) {
    size_t i = 0u;
    size_t out = 0u;
    if (dst == NULL || dst_cap == 0u) {
        return;
    }
    while (i < src_len && out + 1u < dst_cap) {
        uint8_t b = (uint8_t)src[i];
        if (b == '&') {
            if (i + 5u <= src_len && src[i + 1] == 'a' && src[i + 2] == 'm' &&
                src[i + 3] == 'p' && src[i + 4] == ';') {
                dst[out++] = '&'; i += 5u; continue;
            }
            if (i + 4u <= src_len && src[i + 1] == 'l' && src[i + 2] == 't' && src[i + 3] == ';') {
                dst[out++] = '<'; i += 4u; continue;
            }
            if (i + 4u <= src_len && src[i + 1] == 'g' && src[i + 2] == 't' && src[i + 3] == ';') {
                dst[out++] = '>'; i += 4u; continue;
            }
            if (i + 6u <= src_len && src[i + 1] == 'q' && src[i + 2] == 'u' &&
                src[i + 3] == 'o' && src[i + 4] == 't' && src[i + 5] == ';') {
                dst[out++] = '"'; i += 6u; continue;
            }
            if (i + 6u <= src_len && src[i + 1] == 'a' && src[i + 2] == 'p' &&
                src[i + 3] == 'o' && src[i + 4] == 's' && src[i + 5] == ';') {
                dst[out++] = '\''; i += 6u; continue;
            }
        }
        if (b < 0x80u) {
            dst[out++] = (uint16_t)b;
            ++i;
        } else if ((b & 0xE0u) == 0xC0u && i + 1u < src_len) {
            uint32_t cp = ((uint32_t)(b & 0x1Fu) << 6) | ((uint32_t)src[i + 1] & 0x3Fu);
            dst[out++] = (uint16_t)cp;
            i += 2u;
        } else if ((b & 0xF0u) == 0xE0u && i + 2u < src_len) {
            uint32_t cp = ((uint32_t)(b & 0x0Fu) << 12) |
                          (((uint32_t)src[i + 1] & 0x3Fu) << 6) |
                          ((uint32_t)src[i + 2] & 0x3Fu);
            dst[out++] = (uint16_t)cp;
            i += 3u;
        } else {
            dst[out++] = (uint16_t)'?';
            ++i;
        }
    }
    dst[out] = 0u;
}

static inline int uwp_xml_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static inline int uwp_xml_tag_match(const char *p, const char *end, const char *tag) {
    size_t tlen = 0u;
    while (tag[tlen] != '\0') {
        if (p + tlen >= end || p[tlen] != tag[tlen]) {
            return 0;
        }
        ++tlen;
    }
    if (p + tlen >= end) {
        return 0;
    }
    char next = p[tlen];
    return uwp_xml_is_space(next) || next == '>' || next == '/';
}

static inline int uwp_xml_extract_attr(
    const char *tag_start,
    const char *tag_end,
    const char *attr_name,
    uint16_t *out_val,
    size_t out_cap) {
    size_t alen = 0u;
    while (attr_name[alen] != '\0') {
        ++alen;
    }
    const char *p = tag_start;
    while (p < tag_end) {
        /* Skip whitespace */
        while (p < tag_end && uwp_xml_is_space(*p)) {
            ++p;
        }
        if (p >= tag_end) {
            break;
        }
        /* Skip quoted values if we landed on one */
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            while (p < tag_end && *p != q) {
                ++p;
            }
            if (p < tag_end) {
                ++p;
            }
            continue;
        }
        const char *name_start = p;
        while (p < tag_end && !uwp_xml_is_space(*p) && *p != '=' && *p != '>' && *p != '/') {
            ++p;
        }
        size_t nlen = (size_t)(p - name_start);
        while (p < tag_end && uwp_xml_is_space(*p)) {
            ++p;
        }
        if (p < tag_end && *p == '=') {
            ++p;
            while (p < tag_end && uwp_xml_is_space(*p)) {
                ++p;
            }
            if (p < tag_end && (*p == '"' || *p == '\'')) {
                char q = *p++;
                const char *val_start = p;
                while (p < tag_end && *p != q) {
                    ++p;
                }
                const char *val_end = p;
                if (p < tag_end) {
                    ++p;
                }
                if (nlen == alen) {
                    int eq = 1;
                    for (size_t k = 0u; k < alen; ++k) {
                        if (name_start[k] != attr_name[k]) {
                            eq = 0;
                            break;
                        }
                    }
                    if (eq) {
                        uwp_utf8_to_u16_unescape(
                            val_start, (size_t)(val_end - val_start), out_val, out_cap);
                        return 1;
                    }
                }
            }
        } else {
            if (p == name_start) {
                ++p;
            }
        }
    }
    return 0;
}

static inline int uwp_pkg_parse_manifest_utf8(
    const char *xml, size_t len, uwp_pkg_resolved_t *out) {
    if (xml == NULL || len == 0u || out == NULL) {
        return 0;
    }
    const char *p = xml;
    const char *end = xml + len;
    if (len >= 3u && (uint8_t)p[0] == 0xEFu && (uint8_t)p[1] == 0xBBu && (uint8_t)p[2] == 0xBFu) {
        p += 3u;
    }

    int got_identity = 0;
    int got_app = 0;

    while (p < end) {
        if (*p != '<') {
            ++p;
            continue;
        }
        if (p + 4u <= end && p[1] == '!' && p[2] == '-' && p[3] == '-') {
            p += 4u;
            while (p + 3u <= end && !(p[0] == '-' && p[1] == '-' && p[2] == '>')) {
                ++p;
            }
            p = (p + 3u <= end) ? (p + 3u) : end;
            continue;
        }
        ++p;
        if (p >= end || *p == '/' || *p == '?' || *p == '!') {
            continue;
        }
        const char *tag_start = p;
        /* Scan to unquoted '>' */
        char quote = 0;
        while (p < end) {
            if (quote != 0) {
                if (*p == quote) {
                    quote = 0;
                }
            } else {
                if (*p == '"' || *p == '\'') {
                    quote = *p;
                } else if (*p == '>') {
                    break;
                }
            }
            ++p;
        }
        const char *tag_end = p;
        if (p < end) {
            ++p;
        }

        if (!got_identity && uwp_xml_tag_match(tag_start, tag_end, "Identity")) {
            uint16_t tmp[UWP_PKG_MAX_STR];
            if (out->name[0] == 0u) {
                uwp_xml_extract_attr(tag_start, tag_end, "Name", out->name, UWP_PKG_MAX_STR);
            }
            if (out->publisher[0] == 0u) {
                uwp_xml_extract_attr(
                    tag_start, tag_end, "Publisher", out->publisher, UWP_PKG_MAX_STR);
            }
            if (out->version.Version == 0u &&
                uwp_xml_extract_attr(tag_start, tag_end, "Version", tmp, UWP_PKG_MAX_STR)) {
                uwp_pkg_parse_version_u16(tmp, &out->version);
            }
            if (uwp_xml_extract_attr(
                    tag_start, tag_end, "ProcessorArchitecture", tmp, UWP_PKG_MAX_STR)) {
                out->architecture = uwp_pkg_arch_from_u16(tmp);
            }
            got_identity = (out->name[0] != 0u);
        } else if (!got_app &&
                   (uwp_xml_tag_match(tag_start, tag_end, "Application") ||
                    uwp_xml_tag_match(tag_start, tag_end, "Executable"))) {
            if (out->app_id[0] == 0u &&
                uwp_xml_extract_attr(tag_start, tag_end, "Id", out->app_id, 128u)) {
                got_app = 1;
            }
        }
    }
    return got_identity;
}

static inline void uwp_pkg_finalize_derived_fields(uwp_pkg_resolved_t *out) {
    /* Extract publisher_id from family_name or full_name if provided */
    if (out->publisher_id[0] == 0u) {
        const uint16_t *src = (out->family_name[0] != 0u) ? out->family_name
                            : ((out->full_name[0] != 0u) ? out->full_name : NULL);
        if (src != NULL) {
            size_t len = uwp_u16_len(src);
            size_t last_us = len;
            for (size_t i = 0u; i < len; ++i) {
                if (src[i] == (uint16_t)'_') {
                    last_us = i;
                }
            }
            if (last_us < len && (len - last_us - 1u) == UWP_PKG_PUBLISHER_ID_LEN) {
                uwp_u16_copy(out->publisher_id, 32u, &src[last_us + 1u]);
            }
        }
    }

    /* Extract name/version/arch/resourceId from full_name if missing */
    if (out->full_name[0] != 0u && out->name[0] == 0u) {
        const uint16_t *s = out->full_name;
        const uint16_t *u1 = NULL, *u2 = NULL, *u3 = NULL, *u4 = NULL;
        for (const uint16_t *c = s; *c != 0u; ++c) {
            if (*c == (uint16_t)'_') {
                if (!u1) u1 = c;
                else if (!u2) u2 = c;
                else if (!u3) u3 = c;
                else if (!u4) { u4 = c; break; }
            }
        }
        if (u1 != NULL && u2 != NULL && u3 != NULL && u4 != NULL) {
            size_t nlen = (size_t)(u1 - s);
            if (nlen >= UWP_PKG_MAX_STR) nlen = UWP_PKG_MAX_STR - 1u;
            for (size_t i = 0u; i < nlen; ++i) out->name[i] = s[i];
            out->name[nlen] = 0u;

            uint16_t tmp[64];
            size_t vlen = (size_t)(u2 - (u1 + 1));
            if (vlen >= 64u) vlen = 63u;
            for (size_t i = 0u; i < vlen; ++i) tmp[i] = u1[1 + i];
            tmp[vlen] = 0u;
            if (out->version.Version == 0u) {
                uwp_pkg_parse_version_u16(tmp, &out->version);
            }

            size_t alen = (size_t)(u3 - (u2 + 1));
            if (alen >= 64u) alen = 63u;
            for (size_t i = 0u; i < alen; ++i) tmp[i] = u2[1 + i];
            tmp[alen] = 0u;
            out->architecture = uwp_pkg_arch_from_u16(tmp);

            size_t rlen = (size_t)(u4 - (u3 + 1));
            if (rlen >= 64u) rlen = 63u;
            for (size_t i = 0u; i < rlen; ++i) out->resource_id[i] = u3[1 + i];
            out->resource_id[rlen] = 0u;
        }
    }

    if (out->publisher_id[0] == 0u && out->publisher[0] != 0u) {
        uwp_pkg_compute_publisher_id(out->publisher, out->publisher_id);
    }
    if (out->publisher_id[0] == 0u && out->name[0] != 0u) {
        uwp_pkg_compute_publisher_id(out->name, out->publisher_id);
    }

    if (out->family_name[0] == 0u && out->name[0] != 0u && out->publisher_id[0] != 0u) {
        size_t pos = 0u;
        pos = uwp_u16_append(out->family_name, UWP_PKG_MAX_STR, pos, out->name);
        pos = uwp_u16_append_ascii(out->family_name, UWP_PKG_MAX_STR, pos, "_");
        uwp_u16_append(out->family_name, UWP_PKG_MAX_STR, pos, out->publisher_id);
    }

    if (out->full_name[0] == 0u && out->name[0] != 0u && out->publisher_id[0] != 0u) {
        size_t pos = 0u;
        pos = uwp_u16_append(out->full_name, UWP_PKG_MAX_STR, pos, out->name);
        pos = uwp_u16_append_ascii(out->full_name, UWP_PKG_MAX_STR, pos, "_");
        pos = uwp_u16_append_u16_dec(
            out->full_name, UWP_PKG_MAX_STR, pos, out->version.Major);
        pos = uwp_u16_append_ascii(out->full_name, UWP_PKG_MAX_STR, pos, ".");
        pos = uwp_u16_append_u16_dec(
            out->full_name, UWP_PKG_MAX_STR, pos, out->version.Minor);
        pos = uwp_u16_append_ascii(out->full_name, UWP_PKG_MAX_STR, pos, ".");
        pos = uwp_u16_append_u16_dec(
            out->full_name, UWP_PKG_MAX_STR, pos, out->version.Build);
        pos = uwp_u16_append_ascii(out->full_name, UWP_PKG_MAX_STR, pos, ".");
        pos = uwp_u16_append_u16_dec(
            out->full_name, UWP_PKG_MAX_STR, pos, out->version.Revision);
        pos = uwp_u16_append_ascii(out->full_name, UWP_PKG_MAX_STR, pos, "_");
        pos = uwp_u16_append_ascii(
            out->full_name, UWP_PKG_MAX_STR, pos, uwp_pkg_arch_to_ascii(out->architecture));
        pos = uwp_u16_append_ascii(out->full_name, UWP_PKG_MAX_STR, pos, "_");
        pos = uwp_u16_append(out->full_name, UWP_PKG_MAX_STR, pos, out->resource_id);
        pos = uwp_u16_append_ascii(out->full_name, UWP_PKG_MAX_STR, pos, "_");
        uwp_u16_append(out->full_name, UWP_PKG_MAX_STR, pos, out->publisher_id);
    }

    if (out->app_id[0] == 0u) {
        uwp_u16_from_ascii(out->app_id, 128u, "App");
    }

    if (out->aumid[0] == 0u && out->family_name[0] != 0u) {
        size_t pos = 0u;
        pos = uwp_u16_append(out->aumid, UWP_PKG_MAX_STR, pos, out->family_name);
        pos = uwp_u16_append_ascii(out->aumid, UWP_PKG_MAX_STR, pos, "!");
        uwp_u16_append(out->aumid, UWP_PKG_MAX_STR, pos, out->app_id);
    }

    out->valid = (out->full_name[0] != 0u && out->family_name[0] != 0u && out->name[0] != 0u)
                     ? 1u : 0u;
}

static inline void uwp_pkg_resolve_with_provider(
    const uwp_pkg_provider_t *prov,
    int require_bridge_env,
    uwp_pkg_resolved_t *out) {
    uwp_pkg_zero(out, sizeof(*out));
    out->architecture = UWP_PROC_ARCH_AMD64;
    if (prov == NULL || prov->get_env == NULL) {
        return;
    }

    uint16_t env_key[64];
    uint16_t env_val[UWP_PKG_MAX_PATH];

    uwp_u16_from_ascii(env_key, 64u, "PROTON_UWP_BRIDGE");
    uint32_t n = prov->get_env(env_key, env_val, UWP_PKG_MAX_PATH, prov->user_data);
    int bridge_on = (n > 0u && uwp_u16_icmp_ascii(env_val, "1") == 0);
    out->bridge_enabled = bridge_on ? 1u : 0u;

    if (require_bridge_env && !bridge_on) {
        return;
    }

    uwp_u16_from_ascii(env_key, 64u, "PROTON_UWP_PACKAGE_NAME");
    if (prov->get_env(env_key, env_val, UWP_PKG_MAX_STR, prov->user_data) > 0u) {
        uwp_u16_copy(out->name, UWP_PKG_MAX_STR, env_val);
    }

    uwp_u16_from_ascii(env_key, 64u, "PROTON_UWP_PACKAGE_PUBLISHER");
    if (prov->get_env(env_key, env_val, UWP_PKG_MAX_STR, prov->user_data) > 0u) {
        uwp_u16_copy(out->publisher, UWP_PKG_MAX_STR, env_val);
    }

    uwp_u16_from_ascii(env_key, 64u, "PROTON_UWP_PACKAGE_VERSION");
    if (prov->get_env(env_key, env_val, UWP_PKG_MAX_STR, prov->user_data) > 0u) {
        uwp_pkg_parse_version_u16(env_val, &out->version);
    }

    uwp_u16_from_ascii(env_key, 64u, "PROTON_UWP_PACKAGE_FULL_NAME");
    if (prov->get_env(env_key, env_val, UWP_PKG_MAX_STR, prov->user_data) > 0u) {
        uwp_u16_copy(out->full_name, UWP_PKG_MAX_STR, env_val);
    }

    uwp_u16_from_ascii(env_key, 64u, "PROTON_UWP_PACKAGE_FAMILY_NAME");
    if (prov->get_env(env_key, env_val, UWP_PKG_MAX_STR, prov->user_data) > 0u) {
        uwp_u16_copy(out->family_name, UWP_PKG_MAX_STR, env_val);
    }

    uwp_u16_from_ascii(env_key, 64u, "PROTON_UWP_AUMID");
    if (prov->get_env(env_key, env_val, UWP_PKG_MAX_STR, prov->user_data) > 0u) {
        uwp_u16_copy(out->aumid, UWP_PKG_MAX_STR, env_val);
    }

    uwp_u16_from_ascii(env_key, 64u, "PROTON_UWP_PACKAGE_DIR");
    if (prov->get_env(env_key, env_val, UWP_PKG_MAX_PATH, prov->user_data) > 0u) {
        uwp_u16_copy(out->package_dir, UWP_PKG_MAX_PATH, env_val);
    }

    uint16_t exe_dir[UWP_PKG_MAX_PATH];
    exe_dir[0] = 0u;
    if (prov->get_exe_path != NULL &&
        prov->get_exe_path(exe_dir, UWP_PKG_MAX_PATH, prov->user_data) > 0u) {
        size_t len = uwp_u16_len(exe_dir);
        while (len > 0u && exe_dir[len - 1u] != (uint16_t)'\\' && exe_dir[len - 1u] != (uint16_t)'/') {
            --len;
        }
        if (len > 3u || (len > 1u && exe_dir[len - 2u] != (uint16_t)':')) {
            exe_dir[len - 1u] = 0u;
        } else if (len == 1u) {
            exe_dir[0] = 0u;
        }
        if (out->package_dir[0] == 0u && exe_dir[0] != 0u) {
            uwp_u16_copy(out->package_dir, UWP_PKG_MAX_PATH, exe_dir);
        }
    }

    if ((out->name[0] == 0u || out->publisher[0] == 0u || out->version.Version == 0u ||
         out->aumid[0] == 0u) && prov->read_file != NULL) {
        char xml_buf[65536];
        size_t xml_len = 0u;
        int parsed = 0;

        uwp_u16_from_ascii(env_key, 64u, "PROTON_UWP_MANIFEST");
        if (prov->get_env(env_key, env_val, UWP_PKG_MAX_PATH, prov->user_data) > 0u) {
            if (prov->read_file(env_val, (uint8_t *)xml_buf, sizeof(xml_buf), &xml_len, prov->user_data)) {
                parsed = uwp_pkg_parse_manifest_utf8(xml_buf, xml_len, out);
            }
        }

        if (!parsed && exe_dir[0] != 0u) {
            static const char *const candidates[2] = {
                "\\AppxManifest.xml",
                "\\MicrosoftGame.Config"
            };
            for (int c = 0; c < 2 && !parsed; ++c) {
                uint16_t mpath[UWP_PKG_MAX_PATH];
                size_t pos = 0u;
                pos = uwp_u16_append(mpath, UWP_PKG_MAX_PATH, pos, exe_dir);
                uwp_u16_append_ascii(mpath, UWP_PKG_MAX_PATH, pos, candidates[c]);
                if (prov->read_file(mpath, (uint8_t *)xml_buf, sizeof(xml_buf), &xml_len, prov->user_data)) {
                    parsed = uwp_pkg_parse_manifest_utf8(xml_buf, xml_len, out);
                }
            }
        }
    }

    uwp_pkg_finalize_derived_fields(out);
}

/* ============================================================================
 * AppModel Output Formatting Helpers (Exact Windows Buffer Contracts)
 * ============================================================================ */

static inline int32_t uwp_pkg_copy_out_u16(
    const uint16_t *src, uint32_t *length, uint16_t *out_buf) {
    if (length == NULL) {
        return UWP_ERROR_INVALID_PARAMETER;
    }
    if (*length > 0u && out_buf == NULL) {
        return UWP_ERROR_INVALID_PARAMETER;
    }
    uint32_t needed = (uint32_t)(uwp_u16_len(src) + 1u);
    if (out_buf == NULL || *length < needed) {
        *length = needed;
        return UWP_ERROR_INSUFFICIENT_BUFFER;
    }
    for (uint32_t i = 0u; i < needed; ++i) {
        out_buf[i] = src[i];
    }
    *length = needed;
    return UWP_ERROR_SUCCESS;
}

static inline int32_t uwp_pkg_copy_out_package_id(
    const uwp_pkg_resolved_t *pkg, uint32_t *buf_len, uint8_t *buffer) {
    if (pkg == NULL || !pkg->valid) {
        return UWP_APPMODEL_ERROR_NO_PACKAGE;
    }
    if (buf_len == NULL) {
        return UWP_ERROR_INVALID_PARAMETER;
    }
    if (*buf_len > 0u && buffer == NULL) {
        return UWP_ERROR_INVALID_PARAMETER;
    }
    size_t name_chars = uwp_u16_len(pkg->name) + 1u;
    size_t pub_chars = uwp_u16_len(pkg->publisher) + 1u;
    size_t res_chars = uwp_u16_len(pkg->resource_id) + 1u;
    size_t pid_chars = uwp_u16_len(pkg->publisher_id) + 1u;
    uint32_t needed = (uint32_t)(
        sizeof(uwp_pkg_id_t) +
        (name_chars + pub_chars + res_chars + pid_chars) * sizeof(uint16_t));

    if (buffer == NULL || *buf_len < needed) {
        *buf_len = needed;
        return UWP_ERROR_INSUFFICIENT_BUFFER;
    }

    *buf_len = needed;
    uwp_pkg_id_t *id = (uwp_pkg_id_t *)buffer;
    uint16_t *cursor = (uint16_t *)(buffer + sizeof(uwp_pkg_id_t));

    id->reserved = 0u;
    id->processorArchitecture = pkg->architecture;
    id->version = pkg->version;

    id->name = cursor;
    for (size_t i = 0u; i < name_chars; ++i) cursor[i] = pkg->name[i];
    cursor += name_chars;

    id->publisher = cursor;
    for (size_t i = 0u; i < pub_chars; ++i) cursor[i] = pkg->publisher[i];
    cursor += pub_chars;

    id->resourceId = cursor;
    for (size_t i = 0u; i < res_chars; ++i) cursor[i] = pkg->resource_id[i];
    cursor += res_chars;

    id->publisherId = cursor;
    for (size_t i = 0u; i < pid_chars; ++i) cursor[i] = pkg->publisher_id[i];

    return UWP_ERROR_SUCCESS;
}

static inline int32_t uwp_pkg_get_path_by_full_name(
    const uwp_pkg_resolved_t *pkg,
    const uint16_t *full_name,
    uint32_t *length,
    uint16_t *path) {
    if (length == NULL || full_name == NULL) {
        return UWP_ERROR_INVALID_PARAMETER;
    }
    if (pkg == NULL || !pkg->valid || pkg->package_dir[0] == 0u) {
        return UWP_APPMODEL_ERROR_NO_PACKAGE;
    }
    if (uwp_u16_icmp(pkg->full_name, full_name) != 0) {
        return UWP_APPMODEL_ERROR_NO_PACKAGE;
    }
    return uwp_pkg_copy_out_u16(pkg->package_dir, length, path);
}

#ifdef _WIN32
static inline uint32_t uwp_win32_get_env(
    const uint16_t *name, uint16_t *buf, uint32_t cap, void *ud) {
    (void)ud;
    return (uint32_t)GetEnvironmentVariableW((LPCWSTR)name, (LPWSTR)buf, (DWORD)cap);
}

static inline uint32_t uwp_win32_get_exe_path(uint16_t *buf, uint32_t cap, void *ud) {
    (void)ud;
    return (uint32_t)GetModuleFileNameW(NULL, (LPWSTR)buf, (DWORD)cap);
}

static inline int uwp_win32_read_file(
    const uint16_t *path, uint8_t *buf, size_t cap, size_t *out_len, void *ud) {
    (void)ud;
    HANDLE h = CreateFileW(
        (LPCWSTR)path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    DWORD total = 0u;
    while (total < (DWORD)cap) {
        DWORD chunk = 0u;
        if (!ReadFile(h, buf + total, (DWORD)cap - total, &chunk, NULL) || chunk == 0u) {
            break;
        }
        total += chunk;
    }
    CloseHandle(h);
    *out_len = (size_t)total;
    return (total > 0u) ? 1 : 0;
}

static inline void uwp_pkg_resolve_win32(int require_bridge_env, uwp_pkg_resolved_t *out) {
    uwp_pkg_provider_t prov;
    prov.get_env = uwp_win32_get_env;
    prov.get_exe_path = uwp_win32_get_exe_path;
    prov.read_file = uwp_win32_read_file;
    prov.user_data = NULL;
    uwp_pkg_resolve_with_provider(&prov, require_bridge_env, out);
}
#endif /* _WIN32 */

#ifdef __cplusplus
}
#endif

#endif /* SCARLETT_UWP_PKG_IDENTITY_H */
