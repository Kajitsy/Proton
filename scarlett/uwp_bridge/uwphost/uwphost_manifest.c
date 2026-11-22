/*
 * Proton-Scarlett — Clean-Room AppxManifest.xml Parser & Package Identity
 *
 * Parses Windows UWP AppxManifest.xml into uwp_package_identity_t:
 *   - <Identity Name Publisher Version ProcessorArchitecture>
 *   - <Properties><DisplayName>
 *   - <Applications><Application Id Executable EntryPoint>
 *   - <Extensions><Extension Category="windows.activatableClass.inProcessServer">
 *       <InProcessServer><Path> + <ActivatableClass ActivatableClassId ThreadingModel>
 *
 * Also computes Windows Appx PublisherId (SHA-256 of UTF-16LE Publisher,
 * first 8 bytes encoded in 13-char Crockford-like Base32) and applies
 * PROTON_UWP_* environment overrides cleanly.
 *
 * Under _WIN32 (PE64): uses Win32 file APIs ONLY (zero POSIX calls).
 */

#include "uwphost_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uwp_package_identity_t g_pkg_identity;
static volatile int32_t g_pkg_initialized = 0;

/* ============================================================================
 * Clean-Room FIPS 180-4 SHA-256 & Appx PublisherId Base32 Encoder
 * ============================================================================ */

static const uint32_t k_sha256_k[64] = {
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

static inline uint32_t uwp_rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32u - n));
}

typedef struct uwp_sha256_ctx_s {
    uint32_t state[8];
    uint64_t bit_len;
    uint8_t  block[64];
    size_t   block_len;
} uwp_sha256_ctx_t;

static void uwp_sha256_transform(uwp_sha256_ctx_t *ctx, const uint8_t blk[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)blk[i * 4 + 0] << 24) |
               ((uint32_t)blk[i * 4 + 1] << 16) |
               ((uint32_t)blk[i * 4 + 2] << 8)  |
               ((uint32_t)blk[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = uwp_rotr32(w[i - 15], 7) ^ uwp_rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = uwp_rotr32(w[i - 2], 17) ^ uwp_rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = uwp_rotr32(e, 6) ^ uwp_rotr32(e, 11) ^ uwp_rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + S1 + ch + k_sha256_k[i] + w[i];
        uint32_t S0 = uwp_rotr32(a, 2) ^ uwp_rotr32(a, 13) ^ uwp_rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void uwp_sha256_init(uwp_sha256_ctx_t *ctx) {
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
    ctx->bit_len = 0;
    ctx->block_len = 0;
}

static void uwp_sha256_update(uwp_sha256_ctx_t *ctx, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        ctx->block[ctx->block_len++] = data[i];
        if (ctx->block_len == 64) {
            uwp_sha256_transform(ctx, ctx->block);
            ctx->bit_len += 512;
            ctx->block_len = 0;
        }
    }
}

static void uwp_sha256_final(uwp_sha256_ctx_t *ctx, uint8_t out[32]) {
    uint64_t total_bits = ctx->bit_len + (uint64_t)ctx->block_len * 8u;
    ctx->block[ctx->block_len++] = 0x80u;
    if (ctx->block_len > 56) {
        while (ctx->block_len < 64) ctx->block[ctx->block_len++] = 0u;
        uwp_sha256_transform(ctx, ctx->block);
        ctx->block_len = 0;
    }
    while (ctx->block_len < 56) ctx->block[ctx->block_len++] = 0u;
    for (int i = 7; i >= 0; --i) {
        ctx->block[ctx->block_len++] = (uint8_t)((total_bits >> (i * 8)) & 0xFFu);
    }
    uwp_sha256_transform(ctx, ctx->block);
    for (int i = 0; i < 8; ++i) {
        out[i * 4 + 0] = (uint8_t)((ctx->state[i] >> 24) & 0xFFu);
        out[i * 4 + 1] = (uint8_t)((ctx->state[i] >> 16) & 0xFFu);
        out[i * 4 + 2] = (uint8_t)((ctx->state[i] >> 8)  & 0xFFu);
        out[i * 4 + 3] = (uint8_t)(ctx->state[i] & 0xFFu);
    }
}

void uwp_manifest_compute_publisher_id(const wchar_t *publisher, wchar_t out_pub_id[16]) {
    static const wchar_t k_base32[33] = L"0123456789abcdefghjkmnpqrstvwxyz";
    if (!out_pub_id) {
        return;
    }
    if (!publisher || publisher[0] == L'\0') {
        wcscpy(out_pub_id, L"0000000000000");
        return;
    }

    uwp_sha256_ctx_t ctx;
    uwp_sha256_init(&ctx);
    for (size_t i = 0; publisher[i] != L'\0'; ++i) {
        uint32_t cp = (uint32_t)publisher[i];
        if (cp <= 0xFFFFu) {
            uint8_t le[2] = { (uint8_t)(cp & 0xFFu), (uint8_t)((cp >> 8) & 0xFFu) };
            uwp_sha256_update(&ctx, le, 2);
        } else {
            uint32_t v = cp - 0x10000u;
            uint16_t hi = (uint16_t)(0xD800u + (v >> 10));
            uint16_t lo = (uint16_t)(0xDC00u + (v & 0x3FFu));
            uint8_t le[4] = {
                (uint8_t)(hi & 0xFFu), (uint8_t)(hi >> 8),
                (uint8_t)(lo & 0xFFu), (uint8_t)(lo >> 8)
            };
            uwp_sha256_update(&ctx, le, 4);
        }
    }

    uint8_t digest[32];
    uwp_sha256_final(&ctx, digest);

    /* Extract 64 bits from digest[0..7] + 1 zero bit = 65 bits -> 13 base32 chars */
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        val = (val << 8) | (uint64_t)digest[i];
    }
    /* First 12 groups come from bits [63..4], 13th group is (bits [3..0] << 1) */
    for (int i = 0; i < 12; ++i) {
        uint32_t shift = (uint32_t)(59 - i * 5);
        uint32_t idx = (uint32_t)((val >> shift) & 0x1Fu);
        out_pub_id[i] = k_base32[idx];
    }
    out_pub_id[12] = k_base32[(uint32_t)((val & 0x0Fu) << 1)];
    out_pub_id[13] = L'\0';
}

/* ============================================================================
 * Path & String Utilities
 * ============================================================================ */

static void uwp_wstr_copy(wchar_t *dst, size_t dst_chars, const wchar_t *src) {
    if (!dst || dst_chars == 0) return;
    dst[0] = L'\0';
    if (!src) return;
    size_t i = 0;
    while (src[i] != L'\0' && i + 1 < dst_chars) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = L'\0';
}

static int uwp_ascii_casecmp(const char *a, const char *b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        ++a;
        ++b;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

void uwp_manifest_normalize_path(const wchar_t *src, wchar_t *dst, size_t dst_chars) {
    if (!dst || dst_chars == 0) return;
    dst[0] = L'\0';
    if (!src || src[0] == L'\0') return;
#ifdef _WIN32
    /* Defensively translate leading Unix '/' path into Wine Z:\... path */
    if (src[0] == L'/' && src[1] != L'/') {
        size_t pos = 0;
        if (dst_chars > 2) {
            dst[pos++] = L'Z';
            dst[pos++] = L':';
        }
        for (size_t i = 0; src[i] != L'\0' && pos + 1 < dst_chars; ++i) {
            dst[pos++] = (src[i] == L'/') ? L'\\' : src[i];
        }
        dst[pos] = L'\0';
        return;
    }
#endif
    uwp_wstr_copy(dst, dst_chars, src);
}

static void uwp_extract_dir(const wchar_t *file_path, wchar_t *out_dir, size_t out_chars) {
    if (!out_dir || out_chars == 0) return;
    out_dir[0] = L'\0';
    if (!file_path) return;
    const wchar_t *last_sep = NULL;
    for (const wchar_t *p = file_path; *p != L'\0'; ++p) {
        if (*p == L'\\' || *p == L'/') {
            last_sep = p;
        }
    }
    if (!last_sep) return;
    size_t len = (size_t)(last_sep - file_path);
    if (len >= out_chars) len = out_chars - 1;
    memcpy(out_dir, file_path, len * sizeof(wchar_t));
    out_dir[len] = L'\0';
}

static void uwp_parse_version_str(const char *ver, uwp_package_identity_t *out) {
    unsigned maj = 0, min = 0, bld = 0, rev = 0;
    if (ver) {
        sscanf(ver, "%u.%u.%u.%u", &maj, &min, &bld, &rev);
    }
    out->ver_major = (uint16_t)maj;
    out->ver_minor = (uint16_t)min;
    out->ver_build = (uint16_t)bld;
    out->ver_revision = (uint16_t)rev;
}

static uint32_t uwp_parse_arch_str(const char *arch) {
    if (!arch || arch[0] == '\0') return 9u; /* default x64 */
    if (uwp_ascii_casecmp(arch, "x64") == 0) return 9u;
    if (uwp_ascii_casecmp(arch, "x86") == 0) return 0u;
    if (uwp_ascii_casecmp(arch, "arm") == 0) return 5u;
    if (uwp_ascii_casecmp(arch, "neutral") == 0) return 11u;
    if (uwp_ascii_casecmp(arch, "arm64") == 0) return 12u;
    return 9u;
}

static const wchar_t *uwp_arch_to_wstr(uint32_t arch) {
    switch (arch) {
        case 0u:  return L"x86";
        case 5u:  return L"arm";
        case 9u:  return L"x64";
        case 11u: return L"neutral";
        case 12u: return L"arm64";
        default:  return L"x64";
    }
}

static uint32_t uwp_parse_threading_model(const char *tm) {
    if (!tm) return 0u;
    if (uwp_ascii_casecmp(tm, "sta") == 0) return 1u;
    if (uwp_ascii_casecmp(tm, "mta") == 0) return 2u;
    return 0u; /* both */
}

/* ============================================================================
 * Clean-Room Streaming XML Parser for AppxManifest.xml
 * ============================================================================ */

static size_t uwp_xml_unescape(const char *src, size_t src_len, char *dst, size_t dst_size) {
    size_t out = 0;
    size_t i = 0;
    while (i < src_len && out + 1 < dst_size) {
        if (src[i] == '&') {
            if (i + 4 < src_len && memcmp(&src[i], "&amp;", 5) == 0) {
                dst[out++] = '&'; i += 5; continue;
            }
            if (i + 3 < src_len && memcmp(&src[i], "&lt;", 4) == 0) {
                dst[out++] = '<'; i += 4; continue;
            }
            if (i + 3 < src_len && memcmp(&src[i], "&gt;", 4) == 0) {
                dst[out++] = '>'; i += 4; continue;
            }
            if (i + 5 < src_len && memcmp(&src[i], "&quot;", 6) == 0) {
                dst[out++] = '"'; i += 6; continue;
            }
            if (i + 5 < src_len && memcmp(&src[i], "&apos;", 6) == 0) {
                dst[out++] = '\''; i += 6; continue;
            }
            if (i + 2 < src_len && src[i + 1] == '#') {
                size_t j = i + 2;
                unsigned long cp = 0;
                if (j < src_len && (src[j] == 'x' || src[j] == 'X')) {
                    ++j;
                    while (j < src_len && src[j] != ';' && isxdigit((unsigned char)src[j])) {
                        char c = src[j++];
                        cp = (cp << 4) + (unsigned long)(isdigit((unsigned char)c)
                                                             ? (c - '0')
                                                             : (tolower((unsigned char)c) - 'a' + 10));
                    }
                } else {
                    while (j < src_len && src[j] != ';' && isdigit((unsigned char)src[j])) {
                        cp = cp * 10u + (unsigned long)(src[j++] - '0');
                    }
                }
                if (j < src_len && src[j] == ';' && cp > 0 && cp < 0x80u) {
                    dst[out++] = (char)cp;
                    i = j + 1;
                    continue;
                }
            }
        }
        dst[out++] = src[i++];
    }
    dst[out] = '\0';
    return out;
}

static const char *uwp_local_name(const char *qname) {
    const char *colon = strchr(qname, ':');
    return colon ? (colon + 1) : qname;
}

static void uwp_trim_inplace(char *s) {
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
    size_t start = 0;
    while (s[start] && isspace((unsigned char)s[start])) {
        ++start;
    }
    if (start > 0) {
        memmove(s, s + start, len - start + 1);
    }
}

typedef struct uwp_xml_attr_s {
    char name[64];
    char value[512];
} uwp_xml_attr_t;

#define UWP_XML_MAX_ATTRS 16
#define UWP_XML_MAX_DEPTH 32

typedef struct uwp_manifest_parser_state_s {
    uwp_package_identity_t *out;
    char stack[UWP_XML_MAX_DEPTH][64];
    int  depth;
    int  has_application;
    int  in_inproc_extension;
    int  in_inproc_server;
    int  in_properties;
    uint32_t inproc_block_start;
    wchar_t  inproc_dll_path[UWP_MAX_PATH_CHARS];
    char text_buf[512];
    size_t text_len;
} uwp_manifest_parser_state_t;

static const char *uwp_find_attr(const uwp_xml_attr_t *attrs, int count, const char *local_name) {
    for (int i = 0; i < count; ++i) {
        if (uwp_ascii_casecmp(uwp_local_name(attrs[i].name), local_name) == 0) {
            return attrs[i].value;
        }
    }
    return NULL;
}

static void uwp_manifest_on_start_tag(uwp_manifest_parser_state_t *st,
                                      const char *tag_name,
                                      const uwp_xml_attr_t *attrs,
                                      int attr_count,
                                      int self_closing) {
    const char *local = uwp_local_name(tag_name);

    if (uwp_ascii_casecmp(local, "Identity") == 0 && st->depth <= 2) {
        const char *name = uwp_find_attr(attrs, attr_count, "Name");
        const char *pub = uwp_find_attr(attrs, attr_count, "Publisher");
        const char *ver = uwp_find_attr(attrs, attr_count, "Version");
        const char *arch = uwp_find_attr(attrs, attr_count, "ProcessorArchitecture");
        if (name) uwp_utf8_to_wstr(name, st->out->name, UWP_MAX_IDENTITY_CHARS);
        if (pub) uwp_utf8_to_wstr(pub, st->out->publisher, UWP_MAX_IDENTITY_CHARS);
        if (ver) uwp_parse_version_str(ver, st->out);
        st->out->architecture = uwp_parse_arch_str(arch);
        st->out->valid = 1u;
    } else if (uwp_ascii_casecmp(local, "Properties") == 0) {
        st->in_properties = 1;
    } else if (uwp_ascii_casecmp(local, "Application") == 0 && !st->has_application) {
        const char *id = uwp_find_attr(attrs, attr_count, "Id");
        const char *exe = uwp_find_attr(attrs, attr_count, "Executable");
        const char *ep = uwp_find_attr(attrs, attr_count, "EntryPoint");
        if (id) uwp_utf8_to_wstr(id, st->out->app_id, 128);
        if (exe) uwp_utf8_to_wstr(exe, st->out->executable, UWP_MAX_PATH_CHARS);
        if (ep) uwp_utf8_to_wstr(ep, st->out->entry_point, UWP_MAX_IDENTITY_CHARS);
        st->has_application = 1;
    } else if (uwp_ascii_casecmp(local, "VisualElements") == 0) {
        const char *dn = uwp_find_attr(attrs, attr_count, "DisplayName");
        if (dn && dn[0] != '\0' && st->out->display_name[0] == L'\0') {
            uwp_utf8_to_wstr(dn, st->out->display_name, UWP_MAX_IDENTITY_CHARS);
        }
    } else if (uwp_ascii_casecmp(local, "Extension") == 0) {
        const char *cat = uwp_find_attr(attrs, attr_count, "Category");
        if (cat && uwp_ascii_casecmp(cat, "windows.activatableClass.inProcessServer") == 0) {
            st->in_inproc_extension = 1;
        }
    } else if (uwp_ascii_casecmp(local, "InProcessServer") == 0 && st->in_inproc_extension) {
        st->in_inproc_server = 1;
        st->inproc_dll_path[0] = L'\0';
        st->inproc_block_start = st->out->inproc_class_count;
    } else if (uwp_ascii_casecmp(local, "ActivatableClass") == 0 && st->in_inproc_server) {
        const char *cid = uwp_find_attr(attrs, attr_count, "ActivatableClassId");
        const char *tm = uwp_find_attr(attrs, attr_count, "ThreadingModel");
        if (cid && cid[0] != '\0' && st->out->inproc_class_count < UWP_MAX_INPROC_CLASSES) {
            uwp_inproc_class_entry_t *e = &st->out->inproc_classes[st->out->inproc_class_count++];
            memset(e, 0, sizeof(*e));
            uwp_utf8_to_wstr(cid, e->class_id, UWP_MAX_IDENTITY_CHARS);
            uwp_wstr_copy(e->dll_path, UWP_MAX_PATH_CHARS, st->inproc_dll_path);
            e->threading_model = uwp_parse_threading_model(tm);
        }
    }

    st->text_len = 0;
    st->text_buf[0] = '\0';

    if (!self_closing && st->depth < UWP_XML_MAX_DEPTH) {
        snprintf(st->stack[st->depth], sizeof(st->stack[st->depth]), "%s", local);
        st->depth++;
    }
}

static void uwp_manifest_on_end_tag(uwp_manifest_parser_state_t *st, const char *tag_name) {
    const char *local = uwp_local_name(tag_name);
    st->text_buf[st->text_len] = '\0';
    uwp_trim_inplace(st->text_buf);

    if (st->in_properties && uwp_ascii_casecmp(local, "DisplayName") == 0) {
        if (st->text_buf[0] != '\0') {
            uwp_utf8_to_wstr(st->text_buf, st->out->display_name, UWP_MAX_IDENTITY_CHARS);
        }
    } else if (uwp_ascii_casecmp(local, "Properties") == 0) {
        st->in_properties = 0;
    } else if (st->in_inproc_server && uwp_ascii_casecmp(local, "Path") == 0) {
        if (st->text_buf[0] != '\0') {
            uwp_utf8_to_wstr(st->text_buf, st->inproc_dll_path, UWP_MAX_PATH_CHARS);
            /* Backfill any ActivatableClass entries declared prior to <Path> */
            for (uint32_t i = st->inproc_block_start; i < st->out->inproc_class_count; ++i) {
                if (st->out->inproc_classes[i].dll_path[0] == L'\0') {
                    uwp_wstr_copy(st->out->inproc_classes[i].dll_path,
                                  UWP_MAX_PATH_CHARS,
                                  st->inproc_dll_path);
                }
            }
        }
    } else if (uwp_ascii_casecmp(local, "InProcessServer") == 0) {
        st->in_inproc_server = 0;
        st->inproc_dll_path[0] = L'\0';
    } else if (uwp_ascii_casecmp(local, "Extension") == 0) {
        st->in_inproc_extension = 0;
        st->in_inproc_server = 0;
    }

    if (st->depth > 0) {
        st->depth--;
    }
    st->text_len = 0;
    st->text_buf[0] = '\0';
}

/* Converts UTF-16LE / UTF-16BE XML buffer to UTF-8 if a UTF-16 BOM is present */
static char *uwp_normalize_xml_to_utf8(const uint8_t *data, size_t len, size_t *out_len) {
    if (!data || len == 0) {
        *out_len = 0;
        return NULL;
    }
    /* UTF-8 BOM */
    if (len >= 3 && data[0] == 0xEFu && data[1] == 0xBBu && data[2] == 0xBFu) {
        data += 3;
        len -= 3;
        char *buf = (char *)malloc(len + 1);
        if (!buf) return NULL;
        memcpy(buf, data, len);
        buf[len] = '\0';
        *out_len = len;
        return buf;
    }
    /* UTF-16LE or UTF-16BE BOM */
    if (len >= 2 && ((data[0] == 0xFFu && data[1] == 0xFEu) ||
                     (data[0] == 0xFEu && data[1] == 0xFFu))) {
        int big_endian = (data[0] == 0xFEu);
        size_t units = (len - 2) / 2;
        size_t cap = units * 3 + 4;
        char *buf = (char *)malloc(cap);
        if (!buf) return NULL;
        size_t out = 0;
        for (size_t i = 0; i < units; ++i) {
            uint16_t u = big_endian
                ? (uint16_t)(((uint16_t)data[2 + i * 2] << 8) | data[2 + i * 2 + 1])
                : (uint16_t)((uint16_t)data[2 + i * 2] | ((uint16_t)data[2 + i * 2 + 1] << 8));
            if (u < 0x80u) {
                if (out + 1 < cap) buf[out++] = (char)u;
            } else if (u < 0x800u) {
                if (out + 2 < cap) {
                    buf[out++] = (char)(0xC0u | (u >> 6));
                    buf[out++] = (char)(0x80u | (u & 0x3Fu));
                }
            } else {
                if (out + 3 < cap) {
                    buf[out++] = (char)(0xE0u | (u >> 12));
                    buf[out++] = (char)(0x80u | ((u >> 6) & 0x3Fu));
                    buf[out++] = (char)(0x80u | (u & 0x3Fu));
                }
            }
        }
        buf[out] = '\0';
        *out_len = out;
        return buf;
    }

    char *buf = (char *)malloc(len + 1);
    if (!buf) return NULL;
    memcpy(buf, data, len);
    buf[len] = '\0';
    *out_len = len;
    return buf;
}

static void uwp_finalize_identity_fields(uwp_package_identity_t *out) {
    if (!out) return;

    if (out->name[0] == L'\0') {
        uwp_wstr_copy(out->name, UWP_MAX_IDENTITY_CHARS, L"ProtonScarlettUwpApp");
    }
    if (out->publisher[0] == L'\0') {
        uwp_wstr_copy(out->publisher, UWP_MAX_IDENTITY_CHARS, L"CN=ProtonScarlett");
    }
    if (out->app_id[0] == L'\0') {
        uwp_wstr_copy(out->app_id, 128, L"App");
    }
    if (out->display_name[0] == L'\0') {
        uwp_wstr_copy(out->display_name, UWP_MAX_IDENTITY_CHARS, out->name);
    }

    if (out->publisher_id[0] == L'\0') {
        wchar_t pub_id[16];
        uwp_manifest_compute_publisher_id(out->publisher, pub_id);
        uwp_wstr_copy(out->publisher_id, 64, pub_id);
    }

    if (out->family_name[0] == L'\0') {
        char name_u8[UWP_MAX_IDENTITY_CHARS];
        char pubid_u8[64];
        char fam_u8[UWP_MAX_IDENTITY_CHARS + 128];
        uwp_wstr_to_utf8(out->name, name_u8, sizeof(name_u8));
        uwp_wstr_to_utf8(out->publisher_id, pubid_u8, sizeof(pubid_u8));
        snprintf(fam_u8, sizeof(fam_u8), "%s_%s", name_u8, pubid_u8);
        uwp_utf8_to_wstr(fam_u8, out->family_name, UWP_MAX_IDENTITY_CHARS);
    }

    if (out->full_name[0] == L'\0') {
        char name_u8[UWP_MAX_IDENTITY_CHARS];
        char arch_u8[32];
        char pubid_u8[64];
        char full_u8[UWP_MAX_IDENTITY_CHARS + 160];
        uwp_wstr_to_utf8(out->name, name_u8, sizeof(name_u8));
        uwp_wstr_to_utf8(uwp_arch_to_wstr(out->architecture), arch_u8, sizeof(arch_u8));
        uwp_wstr_to_utf8(out->publisher_id, pubid_u8, sizeof(pubid_u8));
        snprintf(full_u8, sizeof(full_u8), "%s_%u.%u.%u.%u_%s__%s",
                 name_u8,
                 (unsigned)out->ver_major,
                 (unsigned)out->ver_minor,
                 (unsigned)out->ver_build,
                 (unsigned)out->ver_revision,
                 arch_u8,
                 pubid_u8);
        uwp_utf8_to_wstr(full_u8, out->full_name, UWP_MAX_IDENTITY_CHARS);
    }

    if (out->aumid[0] == L'\0') {
        char fam_u8[UWP_MAX_IDENTITY_CHARS];
        char app_u8[128];
        char aumid_u8[UWP_MAX_IDENTITY_CHARS + 160];
        uwp_wstr_to_utf8(out->family_name, fam_u8, sizeof(fam_u8));
        uwp_wstr_to_utf8(out->app_id, app_u8, sizeof(app_u8));
        snprintf(aumid_u8, sizeof(aumid_u8), "%s!%s", fam_u8, app_u8);
        uwp_utf8_to_wstr(aumid_u8, out->aumid, UWP_MAX_IDENTITY_CHARS);
    }

    /* Default storage folders if not supplied via env */
    if (out->local_folder[0] == L'\0' || out->roaming_folder[0] == L'\0' || out->temp_folder[0] == L'\0') {
        char base_u8[UWP_MAX_PATH_CHARS];
        char fam_u8[UWP_MAX_IDENTITY_CHARS];
        char dir_u8[UWP_MAX_PATH_CHARS + UWP_MAX_IDENTITY_CHARS + 64];
        uwp_wstr_to_utf8(out->family_name, fam_u8, sizeof(fam_u8));
        if (!uwp_get_env_utf8("LOCALAPPDATA", base_u8, sizeof(base_u8))) {
            snprintf(base_u8, sizeof(base_u8), "C:\\users\\steamuser\\AppData\\Local");
        }
        if (out->local_folder[0] == L'\0') {
            snprintf(dir_u8, sizeof(dir_u8), "%s\\Packages\\%s\\LocalState", base_u8, fam_u8);
            uwp_utf8_to_wstr(dir_u8, out->local_folder, UWP_MAX_PATH_CHARS);
        }
        if (out->roaming_folder[0] == L'\0') {
            snprintf(dir_u8, sizeof(dir_u8), "%s\\Packages\\%s\\RoamingState", base_u8, fam_u8);
            uwp_utf8_to_wstr(dir_u8, out->roaming_folder, UWP_MAX_PATH_CHARS);
        }
        if (out->temp_folder[0] == L'\0') {
            snprintf(dir_u8, sizeof(dir_u8), "%s\\Packages\\%s\\TempState", base_u8, fam_u8);
            uwp_utf8_to_wstr(dir_u8, out->temp_folder, UWP_MAX_PATH_CHARS);
        }
    }
}

int uwp_manifest_parse_buffer(const uint8_t *data, size_t len,
                              const wchar_t *install_dir,
                              uwp_package_identity_t *out) {
    if (!data || len == 0 || !out) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->architecture = 9u; /* default x64 */
    if (install_dir && install_dir[0] != L'\0') {
        uwp_manifest_normalize_path(install_dir, out->install_path, UWP_MAX_PATH_CHARS);
    }

    size_t xml_len = 0;
    char *xml = uwp_normalize_xml_to_utf8(data, len, &xml_len);
    if (!xml) {
        return 0;
    }

    uwp_manifest_parser_state_t st;
    memset(&st, 0, sizeof(st));
    st.out = out;

    size_t i = 0;
    while (i < xml_len) {
        if (xml[i] != '<') {
            size_t start = i;
            while (i < xml_len && xml[i] != '<') ++i;
            if (st.depth > 0 && st.text_len + 1 < sizeof(st.text_buf)) {
                char chunk[512];
                size_t n = uwp_xml_unescape(&xml[start], i - start, chunk, sizeof(chunk));
                size_t avail = sizeof(st.text_buf) - 1 - st.text_len;
                if (n > avail) n = avail;
                memcpy(&st.text_buf[st.text_len], chunk, n);
                st.text_len += n;
                st.text_buf[st.text_len] = '\0';
            }
            continue;
        }

        /* Skip XML comments <!-- ... --> */
        if (i + 3 < xml_len && memcmp(&xml[i], "<!--", 4) == 0) {
            i += 4;
            while (i + 2 < xml_len && memcmp(&xml[i], "-->", 3) != 0) ++i;
            i = (i + 2 < xml_len) ? (i + 3) : xml_len;
            continue;
        }
        /* Skip processing instructions <? ... ?> */
        if (i + 1 < xml_len && xml[i + 1] == '?') {
            i += 2;
            while (i + 1 < xml_len && !(xml[i] == '?' && xml[i + 1] == '>')) ++i;
            i = (i + 1 < xml_len) ? (i + 2) : xml_len;
            continue;
        }
        /* Handle CDATA <![CDATA[ ... ]]> */
        if (i + 8 < xml_len && memcmp(&xml[i], "<![CDATA[", 9) == 0) {
            i += 9;
            size_t start = i;
            while (i + 2 < xml_len && memcmp(&xml[i], "]]>", 3) != 0) ++i;
            size_t n = i - start;
            if (st.depth > 0 && st.text_len + 1 < sizeof(st.text_buf)) {
                size_t avail = sizeof(st.text_buf) - 1 - st.text_len;
                if (n > avail) n = avail;
                memcpy(&st.text_buf[st.text_len], &xml[start], n);
                st.text_len += n;
                st.text_buf[st.text_len] = '\0';
            }
            i = (i + 2 < xml_len) ? (i + 3) : xml_len;
            continue;
        }
        /* Skip <!DOCTYPE ...> or other declarations */
        if (i + 1 < xml_len && xml[i + 1] == '!') {
            i += 2;
            while (i < xml_len && xml[i] != '>') ++i;
            if (i < xml_len) ++i;
            continue;
        }

        /* Closing tag </Tag> */
        if (i + 1 < xml_len && xml[i + 1] == '/') {
            i += 2;
            while (i < xml_len && isspace((unsigned char)xml[i])) ++i;
            size_t start = i;
            while (i < xml_len && xml[i] != '>' && !isspace((unsigned char)xml[i])) ++i;
            char tag_name[64];
            size_t tlen = i - start;
            if (tlen >= sizeof(tag_name)) tlen = sizeof(tag_name) - 1;
            memcpy(tag_name, &xml[start], tlen);
            tag_name[tlen] = '\0';
            while (i < xml_len && xml[i] != '>') ++i;
            if (i < xml_len) ++i;
            uwp_manifest_on_end_tag(&st, tag_name);
            continue;
        }

        /* Opening tag <Tag attr="val" ... /> */
        ++i; /* skip '<' */
        while (i < xml_len && isspace((unsigned char)xml[i])) ++i;
        size_t tstart = i;
        while (i < xml_len && xml[i] != '>' && xml[i] != '/' && !isspace((unsigned char)xml[i])) ++i;
        char tag_name[64];
        size_t tlen = i - tstart;
        if (tlen >= sizeof(tag_name)) tlen = sizeof(tag_name) - 1;
        memcpy(tag_name, &xml[tstart], tlen);
        tag_name[tlen] = '\0';

        uwp_xml_attr_t attrs[UWP_XML_MAX_ATTRS];
        int attr_count = 0;
        int self_closing = 0;

        while (i < xml_len) {
            while (i < xml_len && isspace((unsigned char)xml[i])) ++i;
            if (i >= xml_len) break;
            if (xml[i] == '>') {
                ++i;
                break;
            }
            if (xml[i] == '/' && i + 1 < xml_len && xml[i + 1] == '>') {
                self_closing = 1;
                i += 2;
                break;
            }
            size_t astart = i;
            while (i < xml_len && xml[i] != '=' && xml[i] != '>' && xml[i] != '/' &&
                   !isspace((unsigned char)xml[i])) {
                ++i;
            }
            size_t alen = i - astart;
            while (i < xml_len && isspace((unsigned char)xml[i])) ++i;
            if (i >= xml_len || xml[i] != '=') {
                continue;
            }
            ++i; /* skip '=' */
            while (i < xml_len && isspace((unsigned char)xml[i])) ++i;
            if (i >= xml_len) break;
            char quote = xml[i];
            if (quote != '"' && quote != '\'') {
                while (i < xml_len && !isspace((unsigned char)xml[i]) && xml[i] != '>') ++i;
                continue;
            }
            ++i;
            size_t vstart = i;
            while (i < xml_len && xml[i] != quote) ++i;
            size_t vlen = i - vstart;
            if (i < xml_len && xml[i] == quote) ++i;

            if (attr_count < UWP_XML_MAX_ATTRS && alen > 0) {
                if (alen >= sizeof(attrs[attr_count].name)) {
                    alen = sizeof(attrs[attr_count].name) - 1;
                }
                memcpy(attrs[attr_count].name, &xml[astart], alen);
                attrs[attr_count].name[alen] = '\0';
                uwp_xml_unescape(&xml[vstart], vlen,
                                 attrs[attr_count].value,
                                 sizeof(attrs[attr_count].value));
                attr_count++;
            }
        }

        if (tag_name[0] != '\0') {
            uwp_manifest_on_start_tag(&st, tag_name, attrs, attr_count, self_closing);
        }
    }

    free(xml);
    if (!out->valid) {
        return 0;
    }
    uwp_finalize_identity_fields(out);
    return 1;
}

void uwp_manifest_apply_env_overrides(uwp_package_identity_t *id) {
    if (!id) return;
    wchar_t buf[UWP_MAX_PATH_CHARS];
    char ver_u8[64];

    if (uwp_get_env_wstr(L"PROTON_UWP_PACKAGE_NAME", buf, UWP_MAX_IDENTITY_CHARS)) {
        uwp_wstr_copy(id->name, UWP_MAX_IDENTITY_CHARS, buf);
        id->valid = 1u;
    }
    if (uwp_get_env_wstr(L"PROTON_UWP_PACKAGE_PUBLISHER", buf, UWP_MAX_IDENTITY_CHARS)) {
        uwp_wstr_copy(id->publisher, UWP_MAX_IDENTITY_CHARS, buf);
        wchar_t pub_id[16];
        uwp_manifest_compute_publisher_id(id->publisher, pub_id);
        uwp_wstr_copy(id->publisher_id, 64, pub_id);
        id->valid = 1u;
    }
    if (uwp_get_env_utf8("PROTON_UWP_PACKAGE_VERSION", ver_u8, sizeof(ver_u8))) {
        uwp_parse_version_str(ver_u8, id);
    }
    if (uwp_get_env_wstr(L"PROTON_UWP_PACKAGE_FULL_NAME", buf, UWP_MAX_IDENTITY_CHARS)) {
        uwp_wstr_copy(id->full_name, UWP_MAX_IDENTITY_CHARS, buf);
    }
    if (uwp_get_env_wstr(L"PROTON_UWP_PACKAGE_FAMILY_NAME", buf, UWP_MAX_IDENTITY_CHARS)) {
        uwp_wstr_copy(id->family_name, UWP_MAX_IDENTITY_CHARS, buf);
        const wchar_t *us = wcsrchr(buf, L'_');
        if (us && us[1] != L'\0') {
            uwp_wstr_copy(id->publisher_id, 64, us + 1);
        }
    }
    if (uwp_get_env_wstr(L"PROTON_UWP_AUMID", buf, UWP_MAX_IDENTITY_CHARS)) {
        uwp_wstr_copy(id->aumid, UWP_MAX_IDENTITY_CHARS, buf);
        const wchar_t *bang = wcschr(buf, L'!');
        if (bang && bang[1] != L'\0') {
            uwp_wstr_copy(id->app_id, 128, bang + 1);
        }
    }
    if (uwp_get_env_wstr(L"PROTON_UWP_PACKAGE_DIR", buf, UWP_MAX_PATH_CHARS)) {
        uwp_manifest_normalize_path(buf, id->install_path, UWP_MAX_PATH_CHARS);
    }
    if (uwp_get_env_wstr(L"PROTON_UWP_LOCAL_STATE", buf, UWP_MAX_PATH_CHARS)) {
        uwp_manifest_normalize_path(buf, id->local_folder, UWP_MAX_PATH_CHARS);
    }
    if (uwp_get_env_wstr(L"PROTON_UWP_ROAMING_STATE", buf, UWP_MAX_PATH_CHARS)) {
        uwp_manifest_normalize_path(buf, id->roaming_folder, UWP_MAX_PATH_CHARS);
    }
    if (uwp_get_env_wstr(L"PROTON_UWP_TEMP_STATE", buf, UWP_MAX_PATH_CHARS)) {
        uwp_manifest_normalize_path(buf, id->temp_folder, UWP_MAX_PATH_CHARS);
    }

    uwp_finalize_identity_fields(id);
}

int uwp_manifest_load_file(const wchar_t *path, uwp_package_identity_t *out) {
    if (!path || path[0] == L'\0' || !out) {
        return 0;
    }
    wchar_t norm_path[UWP_MAX_PATH_CHARS];
    uwp_manifest_normalize_path(path, norm_path, UWP_MAX_PATH_CHARS);

    uint8_t *buf = NULL;
    size_t file_size = 0;

#ifdef _WIN32
    HANDLE h = CreateFileW(norm_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    LARGE_INTEGER li;
    if (!GetFileSizeEx(h, &li) || li.QuadPart <= 0 || li.QuadPart > (4 * 1024 * 1024)) {
        CloseHandle(h);
        return 0;
    }
    file_size = (size_t)li.QuadPart;
    buf = (uint8_t *)malloc(file_size);
    if (!buf) {
        CloseHandle(h);
        return 0;
    }
    DWORD total_read = 0;
    while (total_read < (DWORD)file_size) {
        DWORD chunk = 0;
        if (!ReadFile(h, buf + total_read, (DWORD)file_size - total_read, &chunk, NULL) || chunk == 0) {
            break;
        }
        total_read += chunk;
    }
    CloseHandle(h);
    if (total_read != (DWORD)file_size) {
        free(buf);
        return 0;
    }
#else
    char path_u8[UWP_MAX_PATH_CHARS * 3];
    uwp_wstr_to_utf8(norm_path, path_u8, sizeof(path_u8));
    FILE *fp = fopen(path_u8, "rb");
    if (!fp) {
        return 0;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    long sz = ftell(fp);
    if (sz <= 0 || sz > (4 * 1024 * 1024)) {
        fclose(fp);
        return 0;
    }
    rewind(fp);
    file_size = (size_t)sz;
    buf = (uint8_t *)malloc(file_size);
    if (!buf) {
        fclose(fp);
        return 0;
    }
    if (fread(buf, 1, file_size, fp) != file_size) {
        free(buf);
        fclose(fp);
        return 0;
    }
    fclose(fp);
#endif

    wchar_t install_dir[UWP_MAX_PATH_CHARS];
    uwp_extract_dir(norm_path, install_dir, UWP_MAX_PATH_CHARS);
    int ok = uwp_manifest_parse_buffer(buf, file_size, install_dir, out);
    free(buf);
    return ok;
}

int uwp_manifest_initialize(const wchar_t *explicit_manifest_path) {
    uwp_package_identity_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    int loaded = 0;
    wchar_t candidate[UWP_MAX_PATH_CHARS];

    if (explicit_manifest_path && explicit_manifest_path[0] != L'\0') {
        loaded = uwp_manifest_load_file(explicit_manifest_path, &parsed);
    }

    if (!loaded && uwp_get_env_wstr(L"PROTON_UWP_MANIFEST", candidate, UWP_MAX_PATH_CHARS)) {
        loaded = uwp_manifest_load_file(candidate, &parsed);
    }

#ifdef _WIN32
    if (!loaded) {
        wchar_t exe_path[UWP_MAX_PATH_CHARS];
        DWORD len = GetModuleFileNameW(NULL, exe_path, UWP_MAX_PATH_CHARS);
        if (len > 0 && len < UWP_MAX_PATH_CHARS) {
            wchar_t exe_dir[UWP_MAX_PATH_CHARS];
            uwp_extract_dir(exe_path, exe_dir, UWP_MAX_PATH_CHARS);
            if (exe_dir[0] != L'\0') {
                size_t dlen = wcslen(exe_dir);
                if (dlen + 18 < UWP_MAX_PATH_CHARS) {
                    uwp_wstr_copy(candidate, UWP_MAX_PATH_CHARS, exe_dir);
                    wcscat(candidate, L"\\AppxManifest.xml");
                    loaded = uwp_manifest_load_file(candidate, &parsed);
                }
                /* Also check parent dir (e.g., entrypoint\App.exe -> ..\AppxManifest.xml) */
                if (!loaded) {
                    wchar_t parent_dir[UWP_MAX_PATH_CHARS];
                    uwp_extract_dir(exe_dir, parent_dir, UWP_MAX_PATH_CHARS);
                    if (parent_dir[0] != L'\0' && wcslen(parent_dir) + 18 < UWP_MAX_PATH_CHARS) {
                        uwp_wstr_copy(candidate, UWP_MAX_PATH_CHARS, parent_dir);
                        wcscat(candidate, L"\\AppxManifest.xml");
                        loaded = uwp_manifest_load_file(candidate, &parsed);
                    }
                }
            }
        }
    }
#endif

    if (!loaded && uwp_get_env_wstr(L"PROTON_UWP_PACKAGE_DIR", candidate, UWP_MAX_PATH_CHARS)) {
        wchar_t norm_dir[UWP_MAX_PATH_CHARS];
        uwp_manifest_normalize_path(candidate, norm_dir, UWP_MAX_PATH_CHARS);
        size_t dlen = wcslen(norm_dir);
        if (dlen > 0 && dlen + 18 < UWP_MAX_PATH_CHARS) {
            if (norm_dir[dlen - 1] != L'\\' && norm_dir[dlen - 1] != L'/') {
                wcscat(norm_dir, L"\\");
            }
            wcscat(norm_dir, L"AppxManifest.xml");
            loaded = uwp_manifest_load_file(norm_dir, &parsed);
        }
    }

    if (!loaded) {
        loaded = uwp_manifest_load_file(L"AppxManifest.xml", &parsed);
    }

    if (!loaded) {
        parsed.architecture = 9u;
    }

    uwp_manifest_apply_env_overrides(&parsed);
    g_pkg_identity = parsed;
    g_pkg_initialized = 1;

    char name_u8[UWP_MAX_IDENTITY_CHARS];
    char fam_u8[UWP_MAX_IDENTITY_CHARS];
    char ep_u8[UWP_MAX_IDENTITY_CHARS];
    uwp_wstr_to_utf8(g_pkg_identity.name, name_u8, sizeof(name_u8));
    uwp_wstr_to_utf8(g_pkg_identity.family_name, fam_u8, sizeof(fam_u8));
    uwp_wstr_to_utf8(g_pkg_identity.entry_point, ep_u8, sizeof(ep_u8));
    uwp_bridge_log("manifest",
                   "loaded=%d name='%s' family='%s' ver=%u.%u.%u.%u entry='%s' inproc_classes=%u",
                   loaded,
                   name_u8,
                   fam_u8,
                   (unsigned)g_pkg_identity.ver_major,
                   (unsigned)g_pkg_identity.ver_minor,
                   (unsigned)g_pkg_identity.ver_build,
                   (unsigned)g_pkg_identity.ver_revision,
                   ep_u8,
                   (unsigned)g_pkg_identity.inproc_class_count);
    return loaded;
}

void uwp_manifest_set_identity_for_test(const uwp_package_identity_t *id) {
    if (!id) {
        memset(&g_pkg_identity, 0, sizeof(g_pkg_identity));
        g_pkg_initialized = 0;
        return;
    }
    g_pkg_identity = *id;
    g_pkg_initialized = 1;
}

const uwp_package_identity_t *uwp_core_get_package_identity(void) {
    if (!g_pkg_initialized) {
        uwp_manifest_initialize(NULL);
    }
    return &g_pkg_identity;
}

int uwp_manifest_find_inproc_class(const wchar_t *class_id, uwp_inproc_class_entry_t *out) {
    if (!class_id || class_id[0] == L'\0') {
        return 0;
    }
    const uwp_package_identity_t *id = uwp_core_get_package_identity();
    for (uint32_t i = 0; i < id->inproc_class_count && i < UWP_MAX_INPROC_CLASSES; ++i) {
        if (wcscmp(id->inproc_classes[i].class_id, class_id) == 0) {
            if (out) {
                *out = id->inproc_classes[i];
            }
            return 1;
        }
    }
    return 0;
}
