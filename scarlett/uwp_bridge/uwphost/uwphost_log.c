/*
 * Proton-Scarlett — UWPHOST.dll Diagnostic Logger & Strict-Mode Handler
 *
 * Implements:
 *   - uwp_bridge_log()
 *   - uwp_bridge_log_unknown_class()
 *   - UwpHostLogUnknownClass()
 *   - Portable UTF-8 / UTF-16 helpers & environment queries
 *
 * Strict rule: Under _WIN32 (PE64), uses Win32 APIs ONLY (zero POSIX calls).
 */

#include "uwphost_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile int32_t g_strict_mode_initialized = 0;
static volatile int32_t g_strict_mode_enabled = 0;
static uwp_strict_abort_hook_fn g_strict_abort_hook = NULL;

void uwp_wstr_to_utf8(const wchar_t *src, char *dst, size_t dst_size) {
    if (!dst || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }
#ifdef _WIN32
    int written = WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, (int)dst_size, NULL, NULL);
    if (written <= 0) {
        size_t i = 0;
        while (src[i] != L'\0' && i + 1 < dst_size) {
            uint32_t ch = (uint32_t)src[i];
            dst[i] = (ch < 0x80u) ? (char)ch : '?';
            ++i;
        }
        dst[i] = '\0';
    }
#else
    size_t out = 0;
    for (size_t i = 0; src[i] != L'\0'; ++i) {
        uint32_t cp = (uint32_t)src[i];
        if (cp < 0x80u) {
            if (out + 1 >= dst_size) break;
            dst[out++] = (char)cp;
        } else if (cp < 0x800u) {
            if (out + 2 >= dst_size) break;
            dst[out++] = (char)(0xC0u | (cp >> 6));
            dst[out++] = (char)(0x80u | (cp & 0x3Fu));
        } else if (cp < 0x10000u) {
            if (out + 3 >= dst_size) break;
            dst[out++] = (char)(0xE0u | (cp >> 12));
            dst[out++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
            dst[out++] = (char)(0x80u | (cp & 0x3Fu));
        } else if (cp <= 0x10FFFFu) {
            if (out + 4 >= dst_size) break;
            dst[out++] = (char)(0xF0u | (cp >> 18));
            dst[out++] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
            dst[out++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
            dst[out++] = (char)(0x80u | (cp & 0x3Fu));
        }
    }
    dst[out] = '\0';
#endif
}

void uwp_utf8_to_wstr(const char *src, wchar_t *dst, size_t dst_chars) {
    if (!dst || dst_chars == 0) {
        return;
    }
    dst[0] = L'\0';
    if (!src) {
        return;
    }
#ifdef _WIN32
    int written = MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, (int)dst_chars);
    if (written <= 0) {
        size_t i = 0;
        while (src[i] != '\0' && i + 1 < dst_chars) {
            dst[i] = (wchar_t)(uint8_t)src[i];
            ++i;
        }
        dst[i] = L'\0';
    }
#else
    size_t out = 0;
    const uint8_t *p = (const uint8_t *)src;
    while (*p != 0 && out + 1 < dst_chars) {
        uint32_t cp = 0;
        if (*p < 0x80u) {
            cp = *p++;
        } else if ((*p & 0xE0u) == 0xC0u && (p[1] & 0xC0u) == 0x80u) {
            cp = ((uint32_t)(p[0] & 0x1Fu) << 6) | (uint32_t)(p[1] & 0x3Fu);
            p += 2;
        } else if ((*p & 0xF0u) == 0xE0u && (p[1] & 0xC0u) == 0x80u && (p[2] & 0xC0u) == 0x80u) {
            cp = ((uint32_t)(p[0] & 0x0Fu) << 12) |
                 ((uint32_t)(p[1] & 0x3Fu) << 6) |
                 (uint32_t)(p[2] & 0x3Fu);
            p += 3;
        } else if ((*p & 0xF8u) == 0xF0u && (p[1] & 0xC0u) == 0x80u &&
                   (p[2] & 0xC0u) == 0x80u && (p[3] & 0xC0u) == 0x80u) {
            cp = ((uint32_t)(p[0] & 0x07u) << 18) |
                 ((uint32_t)(p[1] & 0x3Fu) << 12) |
                 ((uint32_t)(p[2] & 0x3Fu) << 6) |
                 (uint32_t)(p[3] & 0x3Fu);
            p += 4;
        } else {
            cp = '?';
            ++p;
        }
        dst[out++] = (wchar_t)cp;
    }
    dst[out] = L'\0';
#endif
}

int uwp_get_env_utf8(const char *name, char *buf, size_t buf_size) {
    if (!name || !buf || buf_size == 0) {
        return 0;
    }
    buf[0] = '\0';
#ifdef _WIN32
    DWORD len = GetEnvironmentVariableA(name, buf, (DWORD)buf_size);
    if (len == 0 || len >= (DWORD)buf_size) {
        buf[0] = '\0';
        return 0;
    }
    return 1;
#else
    const char *val = getenv(name);
    if (!val || val[0] == '\0') {
        return 0;
    }
    size_t len = strlen(val);
    if (len >= buf_size) {
        len = buf_size - 1;
    }
    memcpy(buf, val, len);
    buf[len] = '\0';
    return 1;
#endif
}

int uwp_get_env_wstr(const wchar_t *name, wchar_t *buf, size_t buf_chars) {
    if (!name || !buf || buf_chars == 0) {
        return 0;
    }
    buf[0] = L'\0';
#ifdef _WIN32
    DWORD len = GetEnvironmentVariableW(name, buf, (DWORD)buf_chars);
    if (len == 0 || len >= (DWORD)buf_chars) {
        buf[0] = L'\0';
        return 0;
    }
    return 1;
#else
    char name_utf8[128];
    char val_utf8[UWP_MAX_PATH_CHARS * 3];
    uwp_wstr_to_utf8(name, name_utf8, sizeof(name_utf8));
    if (!uwp_get_env_utf8(name_utf8, val_utf8, sizeof(val_utf8))) {
        return 0;
    }
    uwp_utf8_to_wstr(val_utf8, buf, buf_chars);
    return buf[0] != L'\0';
#endif
}

void uwp_log_set_strict_mode(uint32_t enabled) {
    g_strict_mode_enabled = enabled ? 1 : 0;
    g_strict_mode_initialized = 1;
}

uint32_t uwp_log_is_strict_mode(void) {
    if (!g_strict_mode_initialized) {
        char env_val[32];
        if (uwp_get_env_utf8("PROTON_XBOX_STRICT", env_val, sizeof(env_val))) {
            g_strict_mode_enabled = (env_val[0] == '1' ||
                                     env_val[0] == 'y' ||
                                     env_val[0] == 'Y' ||
                                     env_val[0] == 't' ||
                                     env_val[0] == 'T') ? 1 : 0;
        }
        g_strict_mode_initialized = 1;
    }
    return (uint32_t)g_strict_mode_enabled;
}

void uwp_log_set_strict_abort_hook(uwp_strict_abort_hook_fn hook) {
    g_strict_abort_hook = hook;
}

void uwp_format_guid(REFIID iid, char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) {
        return;
    }
    if (!iid) {
        snprintf(buf, buf_size, "{NULL}");
        return;
    }
    snprintf(buf, buf_size,
             "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
             (unsigned)iid->Data1,
             (unsigned)iid->Data2,
             (unsigned)iid->Data3,
             (unsigned)iid->Data4[0], (unsigned)iid->Data4[1],
             (unsigned)iid->Data4[2], (unsigned)iid->Data4[3],
             (unsigned)iid->Data4[4], (unsigned)iid->Data4[5],
             (unsigned)iid->Data4[6], (unsigned)iid->Data4[7]);
}

static void uwp_write_log_line(const char *line) {
    if (!line) {
        return;
    }
#ifdef _WIN32
    OutputDebugStringA(line);
    HANDLE h_err = GetStdHandle(STD_ERROR_HANDLE);
    if (h_err != NULL && h_err != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(h_err, line, (DWORD)strlen(line), &written, NULL);
    }
#else
    fputs(line, stderr);
    fflush(stderr);
#endif
}

void uwp_bridge_log(const char *channel, const char *fmt, ...) {
    char msg[1536];
    char line[1792];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt ? fmt : "", ap);
    va_end(ap);

    size_t msg_len = strlen(msg);
    while (msg_len > 0 && (msg[msg_len - 1] == '\n' || msg[msg_len - 1] == '\r')) {
        msg[--msg_len] = '\0';
    }

    snprintf(line, sizeof(line), "[scarlett:uwp:%s][tid=%u] %s\n",
             channel ? channel : "core",
             (unsigned)uwp_current_thread_id(),
             msg);
    uwp_write_log_line(line);
}

static void uwp_resolve_caller_module(void *caller_ret_addr,
                                      char *mod_name, size_t mod_name_size,
                                      uint64_t *out_rva) {
    if (mod_name && mod_name_size > 0) {
        snprintf(mod_name, mod_name_size, "<unknown>");
    }
    if (out_rva) {
        *out_rva = 0;
    }
    if (!caller_ret_addr) {
        return;
    }
#ifdef _WIN32
    HMODULE hmod = NULL;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)caller_ret_addr,
            &hmod) && hmod != NULL) {
        uintptr_t base = (uintptr_t)hmod;
        uintptr_t addr = (uintptr_t)caller_ret_addr;
        if (out_rva && addr >= base) {
            *out_rva = (uint64_t)(addr - base);
        }
        wchar_t path[UWP_MAX_PATH_CHARS];
        DWORD len = GetModuleFileNameW(hmod, path, UWP_MAX_PATH_CHARS);
        if (len > 0 && len < UWP_MAX_PATH_CHARS) {
            const wchar_t *base_name = path;
            for (DWORD i = 0; i < len; ++i) {
                if (path[i] == L'\\' || path[i] == L'/') {
                    base_name = &path[i + 1];
                }
            }
            uwp_wstr_to_utf8(base_name, mod_name, mod_name_size);
        }
    }
#else
    if (out_rva) {
        *out_rva = (uint64_t)(uintptr_t)caller_ret_addr;
    }
    if (mod_name && mod_name_size > 0) {
        snprintf(mod_name, mod_name_size, "<host>");
    }
#endif
}

void uwp_bridge_log_unknown_class(
    const wchar_t *class_id,
    REFIID iid,
    void *caller_ret_addr,
    uint64_t rcx,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    char class_utf8[UWP_MAX_IDENTITY_CHARS * 2];
    char iid_buf[64];
    char caller_mod[128];
    uint64_t caller_rva = 0;
    uint32_t strict = uwp_log_is_strict_mode();

    uwp_wstr_to_utf8(class_id ? class_id : L"<null>", class_utf8, sizeof(class_utf8));
    uwp_format_guid(iid, iid_buf, sizeof(iid_buf));
    uwp_resolve_caller_module(caller_ret_addr, caller_mod, sizeof(caller_mod), &caller_rva);

    uwp_bridge_log(
        "unknown_class",
        "UNIMPLEMENTED WinRT class='%s' iid=%s caller=%s+0x%llx (ret=%p) "
        "regs=[RCX=0x%016llx RDX=0x%016llx R8=0x%016llx R9=0x%016llx] strict=%u",
        class_utf8,
        iid_buf,
        caller_mod,
        (unsigned long long)caller_rva,
        caller_ret_addr,
        (unsigned long long)rcx,
        (unsigned long long)rdx,
        (unsigned long long)r8,
        (unsigned long long)r9,
        (unsigned)strict);

    if (strict != 0u) {
        uwp_bridge_log(
            "strict",
            "PROTON_XBOX_STRICT=1 aborting on unknown WinRT class '%s' with %s (0x%08X)",
            class_utf8,
            scarlett_error_name(SCARLETT_ERR_STRICT_STUB_ABORT),
            (unsigned)(uint32_t)SCARLETT_ERR_STRICT_STUB_ABORT);
        if (g_strict_abort_hook != NULL) {
            g_strict_abort_hook(class_utf8, SCARLETT_ERR_STRICT_STUB_ABORT);
            return;
        }
#ifdef _WIN32
        TerminateProcess(GetCurrentProcess(), (UINT)(uint32_t)SCARLETT_ERR_STRICT_STUB_ABORT);
#else
        abort();
#endif
    }
}

void UWP_API UwpHostLogUnknownClass(
    UWP_HSTRING classId,
    REFIID iid,
    void *caller_ret_addr,
    uint64_t rcx,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    const wchar_t *raw = WindowsGetStringRawBuffer(classId, NULL);
    void *ret = caller_ret_addr;
#if defined(__GNUC__) || defined(__clang__)
    if (!ret) {
        ret = __builtin_return_address(0);
    }
#endif
    uwp_bridge_log_unknown_class(raw, iid, ret, rcx, rdx, r8, r9);
}
