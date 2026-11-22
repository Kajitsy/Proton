/*
 * Proton-Scarlett — Clean-room CRT UWP Forwarder Runtime Support
 *
 * Implements:
 *   - Freestanding Win32 diagnostic logging & Zero-Silent-Stubs trampoline
 *   - Clean-room IEEE-754 std::cyl_bessel_i / cyl_bessel_if
 *   - Clean-room WRL ModuleBase::GetActivationFactory & TerminateModule
 *   - Clean-room Platform::IntPtr, Windows::Foundation::Rect::Contains,
 *     Platform::Details::Heap::AlignedAllocate / AlignedFree
 */

#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "crt_app_runtime.h"

#include <stdarg.h>

#if !defined(_WIN32)
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

/* ============================================================================
 * Freestanding Memory & String Utilities
 * ============================================================================ */

void crt_app_zero(void *dst, size_t len) {
    uint8_t *p = (uint8_t *)dst;
    if (!p) return;
    for (size_t i = 0; i < len; ++i) {
        p[i] = 0u;
    }
}

void crt_app_copy_cstr(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0u) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0u;
    while (i + 1u < cap && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static int crt_streq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        ++a;
        ++b;
    }
    return *a == *b;
}

static int crt_wcseq(const wchar_t *a, const wchar_t *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        ++a;
        ++b;
    }
    return *a == *b;
}

#ifdef _WIN32
static size_t crt_strlen(const char *s) {
    size_t n = 0u;
    if (!s) return 0u;
    while (s[n] != '\0') ++n;
    return n;
}
#endif

/* ============================================================================
 * Freestanding Diagnostic Formatter & Output
 * ============================================================================ */

typedef struct crt_fmt_buf_s {
    char  *buf;
    size_t cap;
    size_t len;
} crt_fmt_buf_t;

static void crt_fmt_putc(crt_fmt_buf_t *fb, char c) {
    if (fb->len + 1u < fb->cap) {
        fb->buf[fb->len++] = c;
    }
}

static void crt_fmt_puts(crt_fmt_buf_t *fb, const char *s) {
    if (!s) s = "(null)";
    while (*s) {
        crt_fmt_putc(fb, *s++);
    }
}

static void crt_fmt_u64(crt_fmt_buf_t *fb, uint64_t val, unsigned base, int upper, int min_width, char pad) {
    char tmp[32];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int idx = 0;
    if (val == 0u) {
        tmp[idx++] = '0';
    } else {
        while (val != 0u && idx < 32) {
            tmp[idx++] = digits[val % base];
            val /= base;
        }
    }
    while (idx < min_width && idx < 32) {
        tmp[idx++] = pad;
    }
    while (idx > 0) {
        crt_fmt_putc(fb, tmp[--idx]);
    }
}

static void crt_vsnprintf(char *out, size_t cap, const char *fmt, va_list ap) {
    crt_fmt_buf_t fb;
    fb.buf = out;
    fb.cap = cap;
    fb.len = 0u;
    if (!out || cap == 0u) return;

    for (const char *p = fmt; p && *p; ++p) {
        if (*p != '%') {
            crt_fmt_putc(&fb, *p);
            continue;
        }
        ++p;
        char pad = ' ';
        if (*p == '0') {
            pad = '0';
            ++p;
        }
        int width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            ++p;
        }
        int is_long = 0;
        if (*p == 'l') {
            is_long = 1;
            ++p;
            if (*p == 'l') {
                is_long = 2;
                ++p;
            }
        }
        switch (*p) {
        case '%':
            crt_fmt_putc(&fb, '%');
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            crt_fmt_puts(&fb, s);
            break;
        }
        case 'd':
        case 'i': {
            int64_t v = (is_long >= 2) ? va_arg(ap, int64_t)
                      : (is_long == 1) ? (int64_t)va_arg(ap, long)
                                       : (int64_t)va_arg(ap, int);
            if (v < 0) {
                crt_fmt_putc(&fb, '-');
                crt_fmt_u64(&fb, (uint64_t)(-v), 10u, 0, width, pad);
            } else {
                crt_fmt_u64(&fb, (uint64_t)v, 10u, 0, width, pad);
            }
            break;
        }
        case 'u': {
            uint64_t v = (is_long >= 2) ? va_arg(ap, uint64_t)
                       : (is_long == 1) ? (uint64_t)va_arg(ap, unsigned long)
                                        : (uint64_t)va_arg(ap, unsigned int);
            crt_fmt_u64(&fb, v, 10u, 0, width, pad);
            break;
        }
        case 'x':
        case 'X': {
            uint64_t v = (is_long >= 2) ? va_arg(ap, uint64_t)
                       : (is_long == 1) ? (uint64_t)va_arg(ap, unsigned long)
                                        : (uint64_t)va_arg(ap, unsigned int);
            crt_fmt_u64(&fb, v, 16u, (*p == 'X'), width, pad);
            break;
        }
        case 'p': {
            uintptr_t v = (uintptr_t)va_arg(ap, void *);
            crt_fmt_puts(&fb, "0x");
            crt_fmt_u64(&fb, (uint64_t)v, 16u, 0, 16, '0');
            break;
        }
        case '\0':
            --p;
            break;
        default:
            crt_fmt_putc(&fb, '%');
            crt_fmt_putc(&fb, *p);
            break;
        }
    }
    fb.buf[fb.len] = '\0';
}

static void crt_emit_raw_line(const char *line) {
#ifdef _WIN32
    OutputDebugStringA(line);
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr != NULL && hErr != INVALID_HANDLE_VALUE) {
        DWORD written = 0u;
        WriteFile(hErr, line, (DWORD)crt_strlen(line), &written, NULL);
    }
#else
    fputs(line, stderr);
    fflush(stderr);
#endif
}

void crt_app_log(const char *channel, const char *fmt, ...) {
    char msg[768];
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    crt_vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    crt_fmt_buf_t fb;
    fb.buf = line;
    fb.cap = sizeof(line);
    fb.len = 0u;
    crt_fmt_puts(&fb, "[SCARLETT-CRT:");
    crt_fmt_puts(&fb, channel ? channel : "app");
    crt_fmt_puts(&fb, "] ");
    crt_fmt_puts(&fb, msg);
    if (fb.len == 0u || fb.buf[fb.len - 1u] != '\n') {
        crt_fmt_putc(&fb, '\n');
    }
    fb.buf[fb.len] = '\0';
    crt_emit_raw_line(line);
}

/* ============================================================================
 * Diagnostic Auto-Stub Dispatcher (Zero Silent Stubs)
 * ============================================================================ */

static scarlett_stub_sink_fn_t g_stub_sink = NULL;
static void *g_stub_sink_ud = NULL;
static scarlett_stub_context_t g_last_stub_ctx;
static int g_strict_override = -1;
static volatile uint32_t g_stub_hits = 0u;

#ifdef _WIN32
static volatile LONG g_stub_lock = 0;
static void crt_spin_lock(volatile LONG *lk) {
    while (InterlockedCompareExchange(lk, 1, 0) != 0) {
        SwitchToThread();
    }
}
static void crt_spin_unlock(volatile LONG *lk) {
    InterlockedExchange(lk, 0);
}
#else
static pthread_mutex_t g_stub_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static int crt_env_strict_enabled(void) {
    if (g_strict_override == 0) return 0;
    if (g_strict_override > 0) return 1;
#ifdef _WIN32
    char val[32];
    crt_app_zero(val, sizeof(val));
    DWORD n = GetEnvironmentVariableA(SCARLETT_ENV_STRICT, val, (DWORD)sizeof(val));
    if (n == 0u || n >= sizeof(val)) return 0;
#else
    const char *val = getenv(SCARLETT_ENV_STRICT);
    if (!val || !val[0]) return 0;
#endif
    return (crt_streq(val, "1") || crt_streq(val, "true") ||
            crt_streq(val, "TRUE") || crt_streq(val, "on"));
}

static uint32_t crt_get_tid(void) {
#ifdef _WIN32
    return (uint32_t)GetCurrentThreadId();
#else
    return (uint32_t)syscall(SYS_gettid);
#endif
}

static void crt_resolve_caller(uint64_t ret_addr, uint64_t *out_base, uint64_t *out_rva,
                               char *out_name, size_t out_name_cap) {
    *out_base = 0u;
    *out_rva = ret_addr;
    crt_app_copy_cstr(out_name, out_name_cap, "<unknown>");
#ifdef _WIN32
    HMODULE mod = NULL;
    if (ret_addr != 0u &&
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(uintptr_t)ret_addr,
                           &mod)) {
        *out_base = (uint64_t)(uintptr_t)mod;
        if (ret_addr >= *out_base) {
            *out_rva = ret_addr - *out_base;
        }
        char mod_path[260];
        crt_app_zero(mod_path, sizeof(mod_path));
        if (GetModuleFileNameA(mod, mod_path, (DWORD)sizeof(mod_path)) > 0u) {
            const char *last_sep = mod_path;
            for (const char *p = mod_path; *p; ++p) {
                if (*p == '\\' || *p == '/') {
                    last_sep = p + 1;
                }
            }
            crt_app_copy_cstr(out_name, out_name_cap, last_sep);
        }
    }
#else
    Dl_info info;
    crt_app_zero(&info, sizeof(info));
    if (ret_addr != 0u && dladdr((const void *)(uintptr_t)ret_addr, &info) != 0) {
        if (info.dli_fbase != NULL) {
            *out_base = (uint64_t)(uintptr_t)info.dli_fbase;
            if (ret_addr >= *out_base) {
                *out_rva = ret_addr - *out_base;
            }
        }
        if (info.dli_fname != NULL && info.dli_fname[0] != '\0') {
            const char *slash = strrchr(info.dli_fname, '/');
            crt_app_copy_cstr(out_name, out_name_cap, slash ? (slash + 1) : info.dli_fname);
        }
    }
#endif
}

void crt_app_stub_set_sink(scarlett_stub_sink_fn_t sink, void *user_data) {
#ifdef _WIN32
    crt_spin_lock(&g_stub_lock);
#else
    pthread_mutex_lock(&g_stub_mutex);
#endif
    g_stub_sink = sink;
    g_stub_sink_ud = user_data;
#ifdef _WIN32
    crt_spin_unlock(&g_stub_lock);
#else
    pthread_mutex_unlock(&g_stub_mutex);
#endif
}

void crt_app_stub_get_last_context(scarlett_stub_context_t *out_ctx) {
    if (!out_ctx) return;
#ifdef _WIN32
    crt_spin_lock(&g_stub_lock);
#else
    pthread_mutex_lock(&g_stub_mutex);
#endif
    *out_ctx = g_last_stub_ctx;
#ifdef _WIN32
    crt_spin_unlock(&g_stub_lock);
#else
    pthread_mutex_unlock(&g_stub_mutex);
#endif
}

void crt_app_stub_set_strict_override(int strict_override) {
    g_strict_override = strict_override;
}

scarlett_hresult_t crt_app_stub_dispatch(
    const scarlett_stub_descriptor_t *desc,
    uint64_t return_address,
    uint64_t rcx,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    int strict = crt_env_strict_enabled();
    scarlett_stub_context_t ctx;
    crt_app_zero(&ctx, sizeof(ctx));

#ifdef _WIN32
    uint32_t hit = (uint32_t)InterlockedIncrement((volatile LONG *)&g_stub_hits);
#else
    uint32_t hit = __atomic_add_fetch(&g_stub_hits, 1u, __ATOMIC_ACQ_REL);
#endif

    ctx.abi_version = SCARLETT_STUB_THUNK_ABI_VERSION;
    ctx.flags = desc ? desc->flags : (uint32_t)SCARLETT_STUB_FLAG_NONE;
    if (strict) {
        ctx.flags |= (uint32_t)SCARLETT_STUB_FLAG_STRICT_MODE;
    }
    ctx.ordinal = desc ? desc->ordinal : SCARLETT_STUB_ORDINAL_NONE;
    ctx.vtable_slot = desc ? desc->vtable_slot : SCARLETT_STUB_SLOT_NONE;
    ctx.thread_id = crt_get_tid();
    ctx.strict_mode = strict ? 1u : 0u;
    ctx.return_address = return_address;
    crt_resolve_caller(return_address,
                       &ctx.caller_module_base,
                       &ctx.caller_rva,
                       ctx.caller_module_name,
                       sizeof(ctx.caller_module_name));
    ctx.rcx = rcx;
    ctx.rdx = rdx;
    ctx.r8 = r8;
    ctx.r9 = r9;
    scarlett_hresult_t raw_hr = desc ? desc->default_error : SCARLETT_ERR_UNIMPLEMENTED_STUB;
    ctx.return_hr = strict ? SCARLETT_ERR_STRICT_STUB_ABORT
                           : scarlett_stub_enforce_error(raw_hr);
    ctx.hit_count = hit;

    crt_app_copy_cstr(ctx.dll_name, sizeof(ctx.dll_name),
                      (desc && desc->dll_name) ? desc->dll_name : "<unknown.dll>");
    crt_app_copy_cstr(ctx.symbol_name, sizeof(ctx.symbol_name),
                      (desc && desc->symbol_name) ? desc->symbol_name : "<unknown>");

    scarlett_stub_sink_fn_t sink_fn = NULL;
    void *sink_ud = NULL;
#ifdef _WIN32
    crt_spin_lock(&g_stub_lock);
#else
    pthread_mutex_lock(&g_stub_mutex);
#endif
    g_last_stub_ctx = ctx;
    sink_fn = g_stub_sink;
    sink_ud = g_stub_sink_ud;
#ifdef _WIN32
    crt_spin_unlock(&g_stub_lock);
#else
    pthread_mutex_unlock(&g_stub_mutex);
#endif

    crt_app_log("STUB",
                "[%s] dll=%s symbol=%s ord=%u caller=%s rva=0x%llX (ret=0x%llX base=0x%llX) "
                "RCX=0x%016llX RDX=0x%016llX R8=0x%016llX R9=0x%016llX tid=%u hit=%u hr=0x%08X",
                ctx.strict_mode ? "STRICT" : "FAIL",
                ctx.dll_name,
                ctx.symbol_name,
                (unsigned)ctx.ordinal,
                ctx.caller_module_name,
                (unsigned long long)ctx.caller_rva,
                (unsigned long long)ctx.return_address,
                (unsigned long long)ctx.caller_module_base,
                (unsigned long long)ctx.rcx,
                (unsigned long long)ctx.rdx,
                (unsigned long long)ctx.r8,
                (unsigned long long)ctx.r9,
                (unsigned)ctx.thread_id,
                (unsigned)ctx.hit_count,
                (unsigned)(uint32_t)ctx.return_hr);

    scarlett_stub_action_t action = scarlett_stub_resolve_action(&ctx);
    if (sink_fn != NULL) {
        action = sink_fn(&ctx, sink_ud);
    }

    if (action == SCARLETT_STUB_ACTION_LOG_AND_ABORT) {
        crt_app_log("STRICT-ABORT",
                    "terminating process with %s (0x%08X)",
                    scarlett_error_name(SCARLETT_ERR_STRICT_STUB_ABORT),
                    (unsigned)(uint32_t)SCARLETT_ERR_STRICT_STUB_ABORT);
#ifdef _WIN32
        ExitProcess((UINT)SCARLETT_ERR_STRICT_STUB_ABORT);
#else
        _exit((int)((uint32_t)SCARLETT_ERR_STRICT_STUB_ABORT & 0xFFu));
#endif
    }

    return scarlett_stub_enforce_error(ctx.return_hr);
}

/* ============================================================================
 * Dynamic Resolution Helper
 * ============================================================================ */

void *crt_app_resolve_proc(const char *dll_name, const char *proc_name) {
#ifdef _WIN32
    if (!dll_name || !proc_name) return NULL;
    HMODULE mod = GetModuleHandleA(dll_name);
    if (!mod) {
        mod = LoadLibraryA(dll_name);
    }
    if (!mod) return NULL;
    return (void *)GetProcAddress(mod, proc_name);
#else
    (void)dll_name;
    (void)proc_name;
    return NULL;
#endif
}

/* ============================================================================
 * Clean-Room IEEE-754 Math: std::cyl_bessel_i / cyl_bessel_if
 * ============================================================================ */

static double crt_fabs(double x) {
    return (x < 0.0) ? -x : x;
}

static double crt_sqrt_d(double x) {
    if (x < 0.0) return 0.0 / 0.0;
    if (x == 0.0) return 0.0;
    double g = (x >= 1.0) ? x : 1.0;
    for (int i = 0; i < 64; ++i) {
        double next = 0.5 * (g + x / g);
        if (crt_fabs(next - g) <= 1e-16 * crt_fabs(next)) {
            g = next;
            break;
        }
        g = next;
    }
    return g;
}

static double crt_exp_d(double x) {
    const double LN2 = 0.693147180559945309417232121458176568;
    if (x != x) return x;
    if (x > 709.78) return 1.0 / 0.0;
    if (x < -745.13) return 0.0;

    int k = (int)(x >= 0.0 ? (x / LN2 + 0.5) : (x / LN2 - 0.5));
    double r = x - (double)k * LN2;

    double term = 1.0;
    double sum = 1.0;
    for (int n = 1; n <= 20; ++n) {
        term *= r / (double)n;
        sum += term;
        if (crt_fabs(term) < 1e-17 * crt_fabs(sum)) break;
    }

    if (k > 0) {
        while (k >= 30) {
            sum *= 1073741824.0;
            k -= 30;
        }
        while (k > 0) {
            sum *= 2.0;
            --k;
        }
    } else if (k < 0) {
        while (k <= -30) {
            sum /= 1073741824.0;
            k += 30;
        }
        while (k < 0) {
            sum *= 0.5;
            ++k;
        }
    }
    return sum;
}

static double crt_ln_d(double x) {
    const double LN2 = 0.693147180559945309417232121458176568;
    if (x < 0.0) return 0.0 / 0.0;
    if (x == 0.0) return -1.0 / 0.0;
    int k = 0;
    double m = x;
    while (m >= 1.4142135623730950488) {
        m *= 0.5;
        ++k;
    }
    while (m < 0.7071067811865475244) {
        m *= 2.0;
        --k;
    }
    double y = (m - 1.0) / (m + 1.0);
    double y2 = y * y;
    double term = y;
    double sum = y;
    for (int n = 3; n <= 35; n += 2) {
        term *= y2;
        double add = term / (double)n;
        sum += add;
        if (crt_fabs(add) < 1e-17 * crt_fabs(sum)) break;
    }
    return 2.0 * sum + (double)k * LN2;
}

static double crt_pow_d(double base, double ex) {
    if (ex == 0.0) return 1.0;
    if (base == 0.0) return (ex > 0.0) ? 0.0 : (1.0 / 0.0);
    return crt_exp_d(ex * crt_ln_d(base));
}

static double crt_tgamma_pos(double z) {
    static const double p[9] = {
        0.99999999999980993227684700473478,
        676.520368121885098567009190444019,
       -1259.13921672240287047156078755283,
        771.3234287776530788486528258894,
       -176.61502916214059906584551354,
        12.507343278686904814458936853,
       -0.13857109526572011689554707,
        9.984369578019570859563e-6,
        1.50563273514931155834e-7
    };
    const double SQRT_2PI = 2.506628274631000502415765284811;
    double zm1 = z - 1.0;
    double x = p[0];
    for (int i = 1; i < 9; ++i) {
        x += p[i] / (zm1 + (double)i);
    }
    double t = zm1 + 7.5;
    return SQRT_2PI * crt_pow_d(t, zm1 + 0.5) * crt_exp_d(-t) * x;
}

double crt_app_cyl_bessel_i(double nu, double x) {
    const double TWO_PI = 6.283185307179586476925286766559;
    if (nu != nu || x != x) return 0.0 / 0.0;
    double abs_nu = crt_fabs(nu);
    int is_int_nu = ((double)((long long)abs_nu) == abs_nu);

    if (x == 0.0) {
        return (abs_nu == 0.0) ? 1.0 : 0.0;
    }

    double ax = crt_fabs(x);
    double sign = 1.0;
    if (x < 0.0) {
        if (is_int_nu) {
            if (((long long)abs_nu) & 1LL) sign = -1.0;
        } else {
            return 0.0 / 0.0;
        }
    }

    if (ax > 700.0) {
        return sign * (1.0 / 0.0);
    }

    double res;
    if (ax <= 25.0) {
        double half_x = 0.5 * ax;
        double x2_4 = half_x * half_x;
        double prefix = (nu == 0.0) ? 1.0 : (crt_pow_d(half_x, nu) / crt_tgamma_pos(nu + 1.0));
        if (nu < 0.0 && is_int_nu) {
            /* I_{-n}(x) = I_n(x) for integer n */
            double n_pos = abs_nu;
            prefix = (n_pos == 0.0) ? 1.0 : (crt_pow_d(half_x, n_pos) / crt_tgamma_pos(n_pos + 1.0));
            nu = n_pos;
        }
        double term = 1.0;
        double sum = 1.0;
        for (int k = 1; k <= 120; ++k) {
            term *= x2_4 / ((double)k * (nu + (double)k));
            sum += term;
            if (crt_fabs(term) <= 1e-16 * crt_fabs(sum)) break;
        }
        res = prefix * sum;
    } else {
        /* Hankel asymptotic expansion for large x */
        double mu = 4.0 * nu * nu;
        double term = 1.0;
        double sum = 1.0;
        double denom = 8.0 * ax;
        for (int k = 1; k <= 16; ++k) {
            double odd = (double)(2 * k - 1);
            double next_term = term * (-(mu - odd * odd)) / ((double)k * denom);
            if (crt_fabs(next_term) > crt_fabs(term) && k > 2) break;
            term = next_term;
            sum += term;
            if (crt_fabs(term) <= 1e-15 * crt_fabs(sum)) break;
        }
        res = (crt_exp_d(ax) / crt_sqrt_d(TWO_PI * ax)) * sum;
    }

    return sign * res;
}

float crt_app_cyl_bessel_if(float nu, float x) {
    return (float)crt_app_cyl_bessel_i((double)nu, (double)x);
}

/* ============================================================================
 * Clean-Room Concurrency Helpers
 * ============================================================================ */

static volatile uint32_t g_next_async_id = 0u;

uint32_t crt_app_next_async_id(void) {
#ifdef _WIN32
    return (uint32_t)InterlockedIncrement((volatile LONG *)&g_next_async_id);
#else
    return __atomic_add_fetch(&g_next_async_id, 1u, __ATOMIC_ACQ_REL);
#endif
}

void crt_app_yield_thread(void) {
#ifdef _WIN32
    SwitchToThread();
#else
    sched_yield();
#endif
}

/* ============================================================================
 * Clean-Room Platform::IntPtr / Rect / Object / Aligned Heap
 * ============================================================================ */

void *crt_app_intptr_from_i32(void *self, int32_t value) {
    if (self != NULL) {
        *(int64_t *)self = (int64_t)value;
    }
    return self;
}

int32_t crt_app_intptr_to_i32(const void *self) {
    if (self == NULL) return 0;
    return (int32_t)(*(const int64_t *)self);
}

uint8_t crt_app_rect_contains(const float *rect_xywh, uint64_t point_packed) {
    if (rect_xywh == NULL) return 0u;
    float px = 0.0f;
    float py = 0.0f;
    uint32_t ux = (uint32_t)(point_packed & 0xFFFFFFFFu);
    uint32_t uy = (uint32_t)((point_packed >> 32) & 0xFFFFFFFFu);
    union { uint32_t u; float f; } cx, cy;
    cx.u = ux;
    cy.u = uy;
    px = cx.f;
    py = cy.f;

    float rx = rect_xywh[0];
    float ry = rect_xywh[1];
    float rw = rect_xywh[2];
    float rh = rect_xywh[3];
    if (rw < 0.0f || rh < 0.0f) return 0u;
    return (px >= rx && px <= rx + rw && py >= ry && py <= ry + rh) ? 1u : 0u;
}

typedef struct crt_inspectable_vtbl_s {
    int32_t (
#ifdef _WIN32
        __stdcall
#endif
        *QueryInterface)(void *This, const crt_app_guid_t *riid, void **ppv);
    uint32_t (
#ifdef _WIN32
        __stdcall
#endif
        *AddRef)(void *This);
    uint32_t (
#ifdef _WIN32
        __stdcall
#endif
        *Release)(void *This);
    int32_t (
#ifdef _WIN32
        __stdcall
#endif
        *GetIids)(void *This, uint32_t *iidCount, crt_app_guid_t **iids);
    int32_t (
#ifdef _WIN32
        __stdcall
#endif
        *GetRuntimeClassName)(void *This, void **className);
    int32_t (
#ifdef _WIN32
        __stdcall
#endif
        *GetTrustLevel)(void *This, int32_t *trustLevel);
} crt_inspectable_vtbl_t;

static int32_t
#ifdef _WIN32
__stdcall
#endif
crt_base_obj_qi(void *This, const crt_app_guid_t *riid, void **ppv) {
    if (!ppv) return (int32_t)0x80004003u; /* E_POINTER */
    *ppv = NULL;
    if (!riid) return (int32_t)0x80070057u; /* E_INVALIDARG */
    /* IUnknown or IInspectable */
    if ((riid->Data1 == 0x00000000u && riid->Data2 == 0x0000u && riid->Data3 == 0x0000u &&
         riid->Data4[0] == 0xC0u && riid->Data4[7] == 0x46u) ||
        (riid->Data1 == 0xAF86E2E0u && riid->Data2 == 0xB12Du && riid->Data3 == 0x4C6Au)) {
        *ppv = This;
        return 0; /* S_OK */
    }
    return (int32_t)0x80004002u; /* E_NOINTERFACE */
}

static uint32_t
#ifdef _WIN32
__stdcall
#endif
crt_base_obj_addref(void *This) {
    (void)This;
    return 1u;
}

static uint32_t
#ifdef _WIN32
__stdcall
#endif
crt_base_obj_release(void *This) {
    (void)This;
    return 1u;
}

static int32_t
#ifdef _WIN32
__stdcall
#endif
crt_base_obj_notimpl(void *This, ...) {
    (void)This;
    return (int32_t)0x80004001u; /* E_NOTIMPL */
}

static int32_t
#ifdef _WIN32
__stdcall
#endif
crt_base_obj_trust(void *This, int32_t *trust) {
    (void)This;
    if (trust) *trust = 0; /* BaseTrust */
    return 0;
}

static const crt_inspectable_vtbl_t g_crt_base_object_vtbl = {
    crt_base_obj_qi,
    crt_base_obj_addref,
    crt_base_obj_release,
    (int32_t (
#ifdef _WIN32
        __stdcall
#endif
        *)(void *, uint32_t *, crt_app_guid_t **))crt_base_obj_notimpl,
    (int32_t (
#ifdef _WIN32
        __stdcall
#endif
        *)(void *, void **))crt_base_obj_notimpl,
    crt_base_obj_trust,
};

void *crt_app_platform_object_ctor(void *self) {
    if (self != NULL) {
        *(const crt_inspectable_vtbl_t **)self = &g_crt_base_object_vtbl;
    }
    return self;
}

/* Aligned allocation tracker so AlignedFree safely handles both aligned & ucrt malloc pointers */
#define CRT_ALIGNED_TRACK_CAP 1024u

typedef struct crt_aligned_entry_s {
    void *aligned_ptr;
    void *raw_ptr;
} crt_aligned_entry_t;

static crt_aligned_entry_t g_aligned_table[CRT_ALIGNED_TRACK_CAP];
#ifdef _WIN32
static volatile LONG g_aligned_lock = 0;
#else
static pthread_mutex_t g_aligned_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static void crt_aligned_lock_acquire(void) {
#ifdef _WIN32
    crt_spin_lock(&g_aligned_lock);
#else
    pthread_mutex_lock(&g_aligned_mutex);
#endif
}

static void crt_aligned_lock_release(void) {
#ifdef _WIN32
    crt_spin_unlock(&g_aligned_lock);
#else
    pthread_mutex_unlock(&g_aligned_mutex);
#endif
}

void *crt_app_aligned_alloc(size_t size, size_t alignment) {
    if (alignment < sizeof(void *)) {
        alignment = sizeof(void *);
    }
    /* Power-of-two rounding */
    size_t align_mask = alignment - 1u;
    if ((alignment & align_mask) != 0u) {
        size_t p2 = sizeof(void *);
        while (p2 < alignment) p2 <<= 1u;
        alignment = p2;
        align_mask = alignment - 1u;
    }

    size_t total = size + alignment + sizeof(void *);
#ifdef _WIN32
    void *raw = HeapAlloc(GetProcessHeap(), 0u, total);
#else
    void *raw = malloc(total);
#endif
    if (!raw) return NULL;

    uintptr_t addr = ((uintptr_t)raw + sizeof(void *) + align_mask) & ~((uintptr_t)align_mask);
    void *aligned = (void *)addr;
    ((void **)aligned)[-1] = raw;

    crt_aligned_lock_acquire();
    for (size_t i = 0; i < CRT_ALIGNED_TRACK_CAP; ++i) {
        if (g_aligned_table[i].aligned_ptr == NULL) {
            g_aligned_table[i].aligned_ptr = aligned;
            g_aligned_table[i].raw_ptr = raw;
            break;
        }
    }
    crt_aligned_lock_release();
    return aligned;
}

void crt_app_aligned_free(void *ptr) {
    if (!ptr) return;

    void *raw = NULL;
    crt_aligned_lock_acquire();
    for (size_t i = 0; i < CRT_ALIGNED_TRACK_CAP; ++i) {
        if (g_aligned_table[i].aligned_ptr == ptr) {
            raw = g_aligned_table[i].raw_ptr;
            g_aligned_table[i].aligned_ptr = NULL;
            g_aligned_table[i].raw_ptr = NULL;
            break;
        }
    }
    crt_aligned_lock_release();

    if (raw != NULL) {
#ifdef _WIN32
        HeapFree(GetProcessHeap(), 0u, raw);
#else
        free(raw);
#endif
        return;
    }

    /* Fallback: pointer came from ucrtbase malloc (e.g. Wine vccorlib140 Allocate) */
#ifdef _WIN32
    typedef void (__cdecl *pfn_free_t)(void *);
    pfn_free_t fn_free = (pfn_free_t)crt_app_resolve_proc("ucrtbase.dll", "free");
    if (fn_free) {
        fn_free(ptr);
    }
#else
    free(ptr);
#endif
}

/* ============================================================================
 * Clean-Room WRL ModuleBase::GetActivationFactory & TerminateModule
 * ============================================================================ */

static const crt_app_guid_t CRT_IID_IActivationFactory = {
    0x00000035u, 0x0000u, 0x0000u,
    { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u }
};

typedef struct crt_iunknown_vtbl_s {
    int32_t (
#ifdef _WIN32
        __stdcall
#endif
        *QueryInterface)(void *This, const crt_app_guid_t *riid, void **ppv);
    uint32_t (
#ifdef _WIN32
        __stdcall
#endif
        *AddRef)(void *This);
    uint32_t (
#ifdef _WIN32
        __stdcall
#endif
        *Release)(void *This);
} crt_iunknown_vtbl_t;

typedef struct crt_iunknown_s {
    const crt_iunknown_vtbl_t *lpVtbl;
} crt_iunknown_t;

int32_t crt_app_wrl_get_activation_factory(
    crt_wrl_module_base_t *module,
    void *activatable_class_id,
    void **out_factory,
    crt_get_hstring_buf_fn_t get_raw_buf) {
    if (!out_factory) return (int32_t)0x80004003u; /* E_POINTER */
    *out_factory = NULL;
    if (!module || !module->vtbl || !activatable_class_id || !get_raw_buf) {
        return (int32_t)0x80070057u; /* E_INVALIDARG */
    }

    uint32_t class_len = 0u;
    const wchar_t *class_name = get_raw_buf(activatable_class_id, &class_len);
    if (!class_name || class_len == 0u) {
        return (int32_t)0x80040111u; /* CLASS_E_CLASSNOTAVAILABLE */
    }

    const crt_wrl_creator_map_t **mid = module->vtbl->get_mid_entry_pointer(module);
    const crt_wrl_creator_map_t **last = module->vtbl->get_last_entry_pointer(module);
    if (!mid || !last || mid >= last) {
        return (int32_t)0x80040111u; /* CLASS_E_CLASSNOTAVAILABLE */
    }

    for (const crt_wrl_creator_map_t **entry = mid + 1; entry < last; ++entry) {
        const crt_wrl_creator_map_t *cm = *entry;
        if (!cm || !cm->activation_id.get_runtime_name) continue;
        const wchar_t *runtime_name = cm->activation_id.get_runtime_name();
        if (!runtime_name || !crt_wcseq(runtime_name, class_name)) continue;

        if (cm->factory_cache && cm->factory_cache->factory) {
#ifdef _WIN32
            PSRWLOCK srw = (PSRWLOCK)module->vtbl->get_lock(module);
            if (srw) AcquireSRWLockShared(srw);
#endif
            crt_iunknown_t *cached = (crt_iunknown_t *)cm->factory_cache->factory;
            if (cached && cached->lpVtbl && cached->lpVtbl->QueryInterface) {
                int32_t hr = cached->lpVtbl->QueryInterface(
                    cached, &CRT_IID_IActivationFactory, out_factory);
#ifdef _WIN32
                if (srw) ReleaseSRWLockShared(srw);
#endif
                return hr;
            }
#ifdef _WIN32
            if (srw) ReleaseSRWLockShared(srw);
#endif
        }

        if (!cm->factory_creator) {
            return (int32_t)0x80040111u;
        }
        unsigned int create_flags = 1u; /* WeakReference / normal activation */
        return cm->factory_creator(
            &create_flags, cm, &CRT_IID_IActivationFactory, out_factory);
    }

    return (int32_t)0x80040111u; /* CLASS_E_CLASSNOTAVAILABLE */
}

uint8_t crt_app_wrl_terminate_module(crt_wrl_module_base_t *module) {
    if (!module || !module->vtbl) return 1u;

    const crt_wrl_creator_map_t **first = module->vtbl->get_first_entry_pointer(module);
    const crt_wrl_creator_map_t **last = module->vtbl->get_last_entry_pointer(module);
    if (first && last && first < last) {
#ifdef _WIN32
        PSRWLOCK srw = (PSRWLOCK)module->vtbl->get_lock(module);
#endif
        for (const crt_wrl_creator_map_t **entry = first + 1; entry < last; ++entry) {
            const crt_wrl_creator_map_t *cm = *entry;
            if (!cm || !cm->factory_cache) continue;
#ifdef _WIN32
            if (srw) AcquireSRWLockExclusive(srw);
#endif
            crt_iunknown_t *cached = (crt_iunknown_t *)cm->factory_cache->factory;
            cm->factory_cache->factory = NULL;
#ifdef _WIN32
            if (srw) ReleaseSRWLockExclusive(srw);
#endif
            if (cached && cached->lpVtbl && cached->lpVtbl->Release) {
                cached->lpVtbl->Release(cached);
            }
        }
    }

    return (module->vtbl->get_object_count(module) == 0u) ? 1u : 0u;
}
