/*
 * Proton-Scarlett — Host Linux Win32 Shim (Tests Only)
 *
 * Implements the Win32 APIs used by winrt_sys against a temporary sandbox
 * directory on Linux for automated host unit tests.
 */

#define _POSIX_C_SOURCE 200809L

#include "../winrt_sys_internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* ============================================================================
 * Test Sandbox Controls
 * ============================================================================ */

static char g_sandbox_root[1024] = "/tmp/scarlett_winrt_sys_test";
static DWORD g_last_error = 0;

static uwp_package_identity_t g_test_pkg_identity;
static int g_test_pkg_identity_valid = 0;

typedef struct TestEnvVar {
    wchar_t name[128];
    wchar_t value[512];
} TestEnvVar;

static TestEnvVar g_env_vars[32];
static uint32_t g_env_count = 0;

static char g_last_log[1024];
static uint32_t g_log_count = 0;

void win32_host_shim_set_root(const char *unix_root) {
    snprintf(g_sandbox_root, sizeof(g_sandbox_root), "%s", unix_root ? unix_root : "/tmp/scarlett_winrt_sys_test");
    mkdir(g_sandbox_root, 0755);
}

void win32_host_shim_clear_env(void) {
    g_env_count = 0;
}

void win32_host_shim_set_env(const wchar_t *name, const wchar_t *value) {
    for (uint32_t i = 0; i < g_env_count; ++i) {
        if (wcscmp(g_env_vars[i].name, name) == 0) {
            if (!value) {
                g_env_vars[i] = g_env_vars[--g_env_count];
            } else {
                winrt_sys_copy_wstr(g_env_vars[i].value, 512, value);
            }
            return;
        }
    }
    if (value && g_env_count < 32u) {
        winrt_sys_copy_wstr(g_env_vars[g_env_count].name, 128, name);
        winrt_sys_copy_wstr(g_env_vars[g_env_count].value, 512, value);
        g_env_count++;
    }
}

void win32_host_shim_set_pkg_identity(const uwp_package_identity_t *id) {
    if (id) {
        g_test_pkg_identity = *id;
        g_test_pkg_identity_valid = 1;
    } else {
        memset(&g_test_pkg_identity, 0, sizeof(g_test_pkg_identity));
        g_test_pkg_identity_valid = 0;
    }
}

void win32_host_shim_reset_logs(void) {
    g_last_log[0] = '\0';
    g_log_count = 0;
}

uint32_t win32_host_shim_get_log_count(void) {
    return g_log_count;
}

const char *win32_host_shim_get_last_log(void) {
    return g_last_log;
}

/* ============================================================================
 * uwphost Hooks Required by winrt_sys
 * ============================================================================ */

const uwp_package_identity_t *uwp_core_get_package_identity(void) {
    return g_test_pkg_identity_valid ? &g_test_pkg_identity : NULL;
}

void uwp_bridge_log(const char *channel, const char *fmt, ...) {
    char msg[768];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt ? fmt : "", ap);
    va_end(ap);
    snprintf(g_last_log, sizeof(g_last_log), "[%s] %s", channel ? channel : "sys", msg);
    g_log_count++;
}

/* ============================================================================
 * Path Translation: Windows (C:\..., GAME:\..., Z:\...) -> Unix Sandbox
 * ============================================================================ */

static void win_to_unix_path(LPCWSTR wpath, char *out, size_t cap) {
    if (!wpath || !out || cap == 0) return;
    char utf8[1024];
    int n = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, utf8, (int)sizeof(utf8), NULL, NULL);
    if (n <= 0) utf8[0] = '\0';
    for (char *p = utf8; *p; ++p) {
        if (*p == '\\') *p = '/';
    }

    if (strncmp(utf8, "Z:/", 3) == 0 || strncmp(utf8, "z:/", 3) == 0) {
        /* If Z:/ already points to /tmp/... allow direct mapping or sandbox */
        if (strncmp(utf8 + 2, g_sandbox_root, strlen(g_sandbox_root)) == 0) {
            snprintf(out, cap, "%s", utf8 + 2);
            return;
        }
        snprintf(out, cap, "%s/drive_z/%s", g_sandbox_root, utf8 + 3);
        return;
    }
    if (strncmp(utf8, "GAME:/", 6) == 0 || strncmp(utf8, "game:/", 6) == 0) {
        snprintf(out, cap, "%s/drive_game/%s", g_sandbox_root, utf8 + 6);
        return;
    }
    if (strlen(utf8) >= 3 && utf8[1] == ':' && utf8[2] == '/') {
        snprintf(out, cap, "%s/drive_%c/%s", g_sandbox_root,
                 (utf8[0] >= 'A' && utf8[0] <= 'Z') ? (utf8[0] + 32) : utf8[0],
                 utf8 + 3);
        return;
    }
    if (utf8[0] == '/') {
        snprintf(out, cap, "%s", utf8);
        return;
    }
    snprintf(out, cap, "%s/%s", g_sandbox_root, utf8);
}

/* ============================================================================
 * Mock GameInput.dll & xinput1_4.dll
 * ============================================================================ */

typedef struct MockGiRumbleParams {
    float lowFrequency;
    float highFrequency;
    float leftTrigger;
    float rightTrigger;
} MockGiRumbleParams;

typedef struct MockGiGamepadState {
    uint32_t buttons;
    float    leftTrigger;
    float    rightTrigger;
    float    leftThumbstickX;
    float    leftThumbstickY;
    float    rightThumbstickX;
    float    rightThumbstickY;
    uint32_t extendedButtons;
} MockGiGamepadState;

typedef struct MockXiGamepad {
    WORD  wButtons;
    BYTE  bLeftTrigger;
    BYTE  bRightTrigger;
    int16_t sThumbLX;
    int16_t sThumbLY;
    int16_t sThumbRX;
    int16_t sThumbRY;
} MockXiGamepad;

typedef struct MockXiState {
    DWORD dwPacketNumber;
    MockXiGamepad Gamepad;
} MockXiState;

typedef struct MockXiVibration {
    WORD wLeftMotorSpeed;
    WORD wRightMotorSpeed;
} MockXiVibration;

static int g_mock_gi_present = 0;
static int g_mock_gi_connected = 0;
static MockGiGamepadState g_mock_gi_state;
static uint64_t g_mock_gi_ts = 12345678ULL;
static MockGiRumbleParams g_mock_gi_last_rumble;
static int g_mock_gi_rumble_called = 0;

static int g_mock_xi_present = 0;
static int g_mock_xi_connected[4];
static MockXiState g_mock_xi_state[4];
static MockXiVibration g_mock_xi_last_vib[4];
static int g_mock_xi_vib_called[4];

void win32_host_shim_reset_controllers(void) {
    g_mock_gi_present = 0;
    g_mock_gi_connected = 0;
    memset(&g_mock_gi_state, 0, sizeof(g_mock_gi_state));
    memset(&g_mock_gi_last_rumble, 0, sizeof(g_mock_gi_last_rumble));
    g_mock_gi_rumble_called = 0;
    g_mock_xi_present = 0;
    memset(g_mock_xi_connected, 0, sizeof(g_mock_xi_connected));
    memset(g_mock_xi_state, 0, sizeof(g_mock_xi_state));
    memset(g_mock_xi_last_vib, 0, sizeof(g_mock_xi_last_vib));
    memset(g_mock_xi_vib_called, 0, sizeof(g_mock_xi_vib_called));
}

void win32_host_shim_set_gameinput(int present, int connected, const MockGiGamepadState *st, uint64_t ts) {
    g_mock_gi_present = present;
    g_mock_gi_connected = connected;
    if (st) g_mock_gi_state = *st;
    g_mock_gi_ts = ts;
}

void win32_host_shim_get_gameinput_rumble(int *called, float *lf, float *hf, float *lt, float *rt) {
    if (called) *called = g_mock_gi_rumble_called;
    if (lf) *lf = g_mock_gi_last_rumble.lowFrequency;
    if (hf) *hf = g_mock_gi_last_rumble.highFrequency;
    if (lt) *lt = g_mock_gi_last_rumble.leftTrigger;
    if (rt) *rt = g_mock_gi_last_rumble.rightTrigger;
}

void win32_host_shim_set_xinput(int present, uint32_t idx, int connected,
                                WORD buttons, BYTE lt, BYTE rt,
                                int16_t lx, int16_t ly, int16_t rx, int16_t ry) {
    g_mock_xi_present = present;
    if (idx < 4u) {
        g_mock_xi_connected[idx] = connected;
        g_mock_xi_state[idx].dwPacketNumber++;
        g_mock_xi_state[idx].Gamepad.wButtons = buttons;
        g_mock_xi_state[idx].Gamepad.bLeftTrigger = lt;
        g_mock_xi_state[idx].Gamepad.bRightTrigger = rt;
        g_mock_xi_state[idx].Gamepad.sThumbLX = lx;
        g_mock_xi_state[idx].Gamepad.sThumbLY = ly;
        g_mock_xi_state[idx].Gamepad.sThumbRX = rx;
        g_mock_xi_state[idx].Gamepad.sThumbRY = ry;
    }
}

void win32_host_shim_get_xinput_vib(uint32_t idx, int *called, WORD *left, WORD *right) {
    if (idx < 4u) {
        if (called) *called = g_mock_xi_vib_called[idx];
        if (left) *left = g_mock_xi_last_vib[idx].wLeftMotorSpeed;
        if (right) *right = g_mock_xi_last_vib[idx].wRightMotorSpeed;
    }
}

/* Mock GameInput COM objects */
typedef struct MockGiDeviceVtbl {
    HRESULT  (WINAPI *QueryInterface)(void *s, REFIID r, void **p);
    uint32_t (WINAPI *AddRef)(void *s);
    uint32_t (WINAPI *Release)(void *s);
    HRESULT  (WINAPI *GetDeviceInfo)(void *s, const void **info);
    uint32_t (WINAPI *GetDeviceStatus)(void *s);
    void     (WINAPI *SetRumbleState)(void *s, const MockGiRumbleParams *p);
    HRESULT  (WINAPI *SetHapticMotorState)(void *s, uint32_t m, const void *p);
} MockGiDeviceVtbl;

typedef struct MockGiReadingVtbl {
    HRESULT  (WINAPI *QueryInterface)(void *s, REFIID r, void **p);
    uint32_t (WINAPI *AddRef)(void *s);
    uint32_t (WINAPI *Release)(void *s);
    uint32_t (WINAPI *GetInputKind)(void *s);
    uint64_t (WINAPI *GetSequenceNumber)(void *s, uint32_t k);
    uint64_t (WINAPI *GetTimestamp)(void *s);
    void     (WINAPI *GetDevice)(void *s, void **dev);
    uint32_t (WINAPI *GetGamepadState)(void *s, MockGiGamepadState *st);
} MockGiReadingVtbl;

typedef struct MockGiVtbl {
    HRESULT  (WINAPI *QueryInterface)(void *s, REFIID r, void **p);
    uint32_t (WINAPI *AddRef)(void *s);
    uint32_t (WINAPI *Release)(void *s);
    uint64_t (WINAPI *GetCurrentTimestamp)(void *s);
    HRESULT  (WINAPI *GetCurrentReading)(void *s, uint32_t kind, void *dev, void **reading);
    HRESULT  (WINAPI *GetNextReading)(void *s, void *ref, uint32_t kind, void *dev, void **reading);
    HRESULT  (WINAPI *RegisterDeviceCallback)(void *s, void *d, uint32_t k, uint32_t st, uint32_t ek, void *c, void *cb, uint64_t *t);
    HRESULT  (WINAPI *GetPreviousReading)(void *s, void *ref, uint32_t kind, void *dev, void **reading);
} MockGiVtbl;

static HRESULT WINAPI m_qi(void *s, REFIID r, void **p) { (void)r; *p = s; return S_OK; }
static uint32_t WINAPI m_addref(void *s) { (void)s; return 1; }
static uint32_t WINAPI m_release(void *s) { (void)s; return 1; }

static HRESULT WINAPI m_dev_info(void *s, const void **info) { (void)s; *info = NULL; return S_OK; }
static uint32_t WINAPI m_dev_status(void *s) { (void)s; return g_mock_gi_connected ? 1u : 0u; }
static void WINAPI m_dev_rumble(void *s, const MockGiRumbleParams *p) {
    (void)s;
    if (p) {
        g_mock_gi_last_rumble = *p;
        g_mock_gi_rumble_called = 1;
    }
}
static HRESULT WINAPI m_dev_haptic(void *s, uint32_t m, const void *p) { (void)s; (void)m; (void)p; return E_NOTIMPL; }
static const MockGiDeviceVtbl g_m_dev_vtbl = {
    m_qi, m_addref, m_release, m_dev_info, m_dev_status, m_dev_rumble, m_dev_haptic
};
static struct { const MockGiDeviceVtbl *lpVtbl; } g_m_dev = { &g_m_dev_vtbl };

static uint32_t WINAPI m_rd_kind(void *s) { (void)s; return 0x8u; }
static uint64_t WINAPI m_rd_seq(void *s, uint32_t k) { (void)s; (void)k; return 1u; }
static uint64_t WINAPI m_rd_ts(void *s) { (void)s; return g_mock_gi_ts; }
static void WINAPI m_rd_dev(void *s, void **dev) { (void)s; *dev = &g_m_dev; }
static uint32_t WINAPI m_rd_gp(void *s, MockGiGamepadState *st) {
    (void)s;
    if (!g_mock_gi_connected || !st) return 0u;
    *st = g_mock_gi_state;
    return 1u;
}
static const MockGiReadingVtbl g_m_rd_vtbl = {
    m_qi, m_addref, m_release, m_rd_kind, m_rd_seq, m_rd_ts, m_rd_dev, m_rd_gp
};
static struct { const MockGiReadingVtbl *lpVtbl; } g_m_rd = { &g_m_rd_vtbl };

static uint64_t WINAPI m_gi_ts(void *s) { (void)s; return g_mock_gi_ts; }
static HRESULT WINAPI m_gi_cur(void *s, uint32_t kind, void *dev, void **reading) {
    (void)s; (void)dev;
    if (!reading) return E_POINTER;
    *reading = NULL;
    if ((kind & 0x8u) == 0u || !g_mock_gi_connected) return E_FAIL;
    *reading = &g_m_rd;
    return S_OK;
}
static HRESULT WINAPI m_gi_unimpl(void *s, void *a, uint32_t b, void *c, void **d) {
    (void)s; (void)a; (void)b; (void)c; (void)d; return E_NOTIMPL;
}
static HRESULT WINAPI m_gi_reg(void *s, void *d, uint32_t k, uint32_t st, uint32_t ek, void *c, void *cb, uint64_t *t) {
    (void)s; (void)d; (void)k; (void)st; (void)ek; (void)c; (void)cb; (void)t; return E_NOTIMPL;
}
static const MockGiVtbl g_m_gi_vtbl = {
    m_qi, m_addref, m_release, m_gi_ts, m_gi_cur, m_gi_unimpl, m_gi_reg, m_gi_unimpl
};
static struct { const MockGiVtbl *lpVtbl; } g_m_gi = { &g_m_gi_vtbl };

static HRESULT WINAPI mock_GameInputCreate(void **gameInput) {
    if (!g_mock_gi_present || !gameInput) return E_FAIL;
    *gameInput = &g_m_gi;
    return S_OK;
}

static DWORD WINAPI mock_XInputGetState(DWORD idx, MockXiState *st) {
    if (!g_mock_xi_present || idx >= 4u || !g_mock_xi_connected[idx] || !st) {
        return ERROR_DEVICE_NOT_CONNECTED;
    }
    *st = g_mock_xi_state[idx];
    return ERROR_SUCCESS;
}

static DWORD WINAPI mock_XInputSetState(DWORD idx, MockXiVibration *vib) {
    if (!g_mock_xi_present || idx >= 4u || !g_mock_xi_connected[idx] || !vib) {
        return ERROR_DEVICE_NOT_CONNECTED;
    }
    g_mock_xi_last_vib[idx] = *vib;
    g_mock_xi_vib_called[idx] = 1;
    return ERROR_SUCCESS;
}

/* ============================================================================
 * Win32 API Implementations
 * ============================================================================ */

HANDLE WINAPI GetProcessHeap(void) { return (HANDLE)(uintptr_t)0x1; }
LPVOID WINAPI HeapAlloc(HANDLE h, DWORD flags, size_t bytes) {
    (void)h;
    if (flags & HEAP_ZERO_MEMORY) return calloc(1, bytes ? bytes : 1);
    return malloc(bytes ? bytes : 1);
}
BOOL WINAPI HeapFree(HANDLE h, DWORD flags, LPVOID mem) {
    (void)h; (void)flags;
    free(mem);
    return TRUE;
}
LPVOID WINAPI CoTaskMemAlloc(size_t cb) { return calloc(1, cb ? cb : 1); }
void WINAPI CoTaskMemFree(LPVOID pv) { free(pv); }

void WINAPI InitializeCriticalSection(CRITICAL_SECTION *lpcs) {
    pthread_mutex_t *m = (pthread_mutex_t *)malloc(sizeof(*m));
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &attr);
    pthread_mutexattr_destroy(&attr);
    lpcs->impl = m;
}
void WINAPI EnterCriticalSection(CRITICAL_SECTION *lpcs) {
    if (lpcs && lpcs->impl) pthread_mutex_lock((pthread_mutex_t *)lpcs->impl);
}
void WINAPI LeaveCriticalSection(CRITICAL_SECTION *lpcs) {
    if (lpcs && lpcs->impl) pthread_mutex_unlock((pthread_mutex_t *)lpcs->impl);
}
void WINAPI DeleteCriticalSection(CRITICAL_SECTION *lpcs) {
    if (lpcs && lpcs->impl) {
        pthread_mutex_destroy((pthread_mutex_t *)lpcs->impl);
        free(lpcs->impl);
        lpcs->impl = NULL;
    }
}

LONG InterlockedIncrement(volatile LONG *Addend) {
    return __atomic_add_fetch(Addend, 1, __ATOMIC_SEQ_CST);
}
LONG InterlockedDecrement(volatile LONG *Addend) {
    return __atomic_sub_fetch(Addend, 1, __ATOMIC_SEQ_CST);
}
LONG InterlockedCompareExchange(volatile LONG *Destination, LONG Exchange, LONG Comperand) {
    LONG expected = Comperand;
    __atomic_compare_exchange_n(Destination, &expected, Exchange, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}

DWORD WINAPI GetEnvironmentVariableW(LPCWSTR lpName, LPWSTR lpBuffer, DWORD nSize) {
    if (!lpName) return 0;
    for (uint32_t i = 0; i < g_env_count; ++i) {
        if (wcscmp(g_env_vars[i].name, lpName) == 0) {
            DWORD len = (DWORD)wcslen(g_env_vars[i].value);
            if (!lpBuffer || nSize <= len) return len + 1u;
            memcpy(lpBuffer, g_env_vars[i].value, (size_t)(len + 1u) * sizeof(wchar_t));
            return len;
        }
    }
    return 0;
}

DWORD WINAPI GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize) {
    (void)hModule;
    const wchar_t *exe = L"C:\\App\\TestApp.exe";
    DWORD len = (DWORD)wcslen(exe);
    if (lpFilename && nSize > len) {
        memcpy(lpFilename, exe, (size_t)(len + 1u) * sizeof(wchar_t));
    }
    return len;
}

DWORD WINAPI GetLastError(void) { return g_last_error; }

static void ensure_parent_dirs(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
}

BOOL WINAPI CreateDirectoryW(LPCWSTR lpPathName, SECURITY_ATTRIBUTES *sa) {
    (void)sa;
    char up[1024];
    win_to_unix_path(lpPathName, up, sizeof(up));
    ensure_parent_dirs(up);
    if (mkdir(up, 0755) == 0) {
        g_last_error = 0;
        return TRUE;
    }
    g_last_error = (errno == EEXIST) ? ERROR_ALREADY_EXISTS : ERROR_PATH_NOT_FOUND;
    return FALSE;
}

BOOL WINAPI RemoveDirectoryW(LPCWSTR lpPathName) {
    char up[1024];
    win_to_unix_path(lpPathName, up, sizeof(up));
    if (rmdir(up) == 0) {
        g_last_error = 0;
        return TRUE;
    }
    g_last_error = ERROR_PATH_NOT_FOUND;
    return FALSE;
}

BOOL WINAPI DeleteFileW(LPCWSTR lpFileName) {
    char up[1024];
    win_to_unix_path(lpFileName, up, sizeof(up));
    if (unlink(up) == 0) {
        g_last_error = 0;
        return TRUE;
    }
    g_last_error = ERROR_FILE_NOT_FOUND;
    return FALSE;
}

DWORD WINAPI GetFileAttributesW(LPCWSTR lpFileName) {
    char up[1024];
    win_to_unix_path(lpFileName, up, sizeof(up));
    struct stat st;
    if (stat(up, &st) != 0) {
        g_last_error = ERROR_FILE_NOT_FOUND;
        return INVALID_FILE_ATTRIBUTES;
    }
    g_last_error = 0;
    return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
}

BOOL WINAPI GetFileAttributesExW(LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS lvl, LPVOID lpInfo) {
    (void)lvl;
    char up[1024];
    win_to_unix_path(lpFileName, up, sizeof(up));
    struct stat st;
    if (stat(up, &st) != 0 || !lpInfo) {
        g_last_error = ERROR_FILE_NOT_FOUND;
        return FALSE;
    }
    WIN32_FILE_ATTRIBUTE_DATA *fad = (WIN32_FILE_ATTRIBUTE_DATA *)lpInfo;
    memset(fad, 0, sizeof(*fad));
    fad->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    uint64_t ft = ((uint64_t)st.st_mtime + 11644473600ULL) * 10000000ULL;
    fad->ftCreationTime.dwLowDateTime = (DWORD)(ft & 0xFFFFFFFFu);
    fad->ftCreationTime.dwHighDateTime = (DWORD)(ft >> 32);
    return TRUE;
}

typedef struct ShimFileHandle {
    int magic;
    int fd;
} ShimFileHandle;

HANDLE WINAPI CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                          SECURITY_ATTRIBUTES *sa, DWORD dwCreationDisposition,
                          DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    (void)dwShareMode; (void)sa; (void)dwFlagsAndAttributes; (void)hTemplateFile;
    char up[1024];
    win_to_unix_path(lpFileName, up, sizeof(up));
    ensure_parent_dirs(up);

    int flags = 0;
    if ((dwDesiredAccess & GENERIC_READ) && (dwDesiredAccess & GENERIC_WRITE)) flags = O_RDWR;
    else if (dwDesiredAccess & GENERIC_WRITE) flags = O_WRONLY;
    else flags = O_RDONLY;

    if (dwCreationDisposition == CREATE_NEW) flags |= O_CREAT | O_EXCL;
    else if (dwCreationDisposition == CREATE_ALWAYS) flags |= O_CREAT | O_TRUNC;
    else if (dwCreationDisposition == OPEN_ALWAYS) flags |= O_CREAT;

    int fd = open(up, flags, 0644);
    if (fd < 0) {
        g_last_error = (errno == EEXIST) ? ERROR_ALREADY_EXISTS : ERROR_FILE_NOT_FOUND;
        return INVALID_HANDLE_VALUE;
    }
    ShimFileHandle *h = (ShimFileHandle *)malloc(sizeof(*h));
    h->magic = 0x53464831;
    h->fd = fd;
    g_last_error = 0;
    return (HANDLE)h;
}

BOOL WINAPI ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nToRead, DWORD *lpRead, OVERLAPPED *ov) {
    (void)ov;
    ShimFileHandle *h = (ShimFileHandle *)hFile;
    if (!h || h->magic != 0x53464831) return FALSE;
    ssize_t r = read(h->fd, lpBuffer, nToRead);
    if (r < 0) { if (lpRead) *lpRead = 0; return FALSE; }
    if (lpRead) *lpRead = (DWORD)r;
    return TRUE;
}

BOOL WINAPI WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nToWrite, DWORD *lpWritten, OVERLAPPED *ov) {
    (void)ov;
    ShimFileHandle *h = (ShimFileHandle *)hFile;
    if (!h || h->magic != 0x53464831) return FALSE;
    ssize_t w = write(h->fd, lpBuffer, nToWrite);
    if (w < 0) { if (lpWritten) *lpWritten = 0; return FALSE; }
    if (lpWritten) *lpWritten = (DWORD)w;
    return TRUE;
}

BOOL WINAPI CloseHandle(HANDLE hObject) {
    ShimFileHandle *h = (ShimFileHandle *)hObject;
    if (!h || h->magic != 0x53464831) return FALSE;
    close(h->fd);
    free(h);
    return TRUE;
}

typedef struct ShimFindHandle {
    DIR *dir;
    char dir_path[1024];
} ShimFindHandle;

static void fill_find_data(const char *dir_path, const char *name, WIN32_FIND_DATAW *fd) {
    memset(fd, 0, sizeof(*fd));
    char full[1024];
    snprintf(full, sizeof(full), "%s/%s", dir_path, name);
    struct stat st;
    if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
        fd->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    } else {
        fd->dwFileAttributes = FILE_ATTRIBUTE_ARCHIVE;
    }
    MultiByteToWideChar(CP_UTF8, 0, name, -1, fd->cFileName, 260);
}

HANDLE WINAPI FindFirstFileW(LPCWSTR lpFileName, WIN32_FIND_DATAW *lpFindFileData) {
    char up[1024];
    win_to_unix_path(lpFileName, up, sizeof(up));
    /* Strip trailing wildcard */
    size_t len = strlen(up);
    if (len >= 2 && up[len - 1] == '*' && up[len - 2] == '/') {
        up[len - 2] = '\0';
    }
    DIR *d = opendir(up);
    if (!d) return INVALID_HANDLE_VALUE;
    struct dirent *ent = readdir(d);
    if (!ent) { closedir(d); return INVALID_HANDLE_VALUE; }
    ShimFindHandle *fh = (ShimFindHandle *)malloc(sizeof(*fh));
    fh->dir = d;
    snprintf(fh->dir_path, sizeof(fh->dir_path), "%s", up);
    fill_find_data(fh->dir_path, ent->d_name, lpFindFileData);
    return (HANDLE)fh;
}

BOOL WINAPI FindNextFileW(HANDLE hFindFile, WIN32_FIND_DATAW *lpFindFileData) {
    ShimFindHandle *fh = (ShimFindHandle *)hFindFile;
    if (!fh || !fh->dir) return FALSE;
    struct dirent *ent = readdir(fh->dir);
    if (!ent) return FALSE;
    fill_find_data(fh->dir_path, ent->d_name, lpFindFileData);
    return TRUE;
}

BOOL WINAPI FindClose(HANDLE hFindFile) {
    ShimFindHandle *fh = (ShimFindHandle *)hFindFile;
    if (!fh) return FALSE;
    if (fh->dir) closedir(fh->dir);
    free(fh);
    return TRUE;
}

int WINAPI WideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWSTR lpWideCharStr, int cchWideChar,
                               LPSTR lpMultiByteStr, int cbMultiByte, LPCSTR lpDefaultChar, BOOL *lpUsedDefaultChar) {
    (void)CodePage; (void)dwFlags; (void)lpDefaultChar; (void)lpUsedDefaultChar;
    if (!lpWideCharStr) return 0;
    size_t wlen = (cchWideChar < 0) ? (wcslen(lpWideCharStr) + 1u) : (size_t)cchWideChar;
    /* Clean UTF-8 encoder for BMP + surrogate/wide */
    size_t need = 0;
    for (size_t i = 0; i < wlen; ++i) {
        uint32_t ch = (uint32_t)lpWideCharStr[i];
        if (ch == 0 && cchWideChar < 0) { need += 1u; break; }
        if (ch < 0x80u) need += 1u;
        else if (ch < 0x800u) need += 2u;
        else if (ch < 0x10000u) need += 3u;
        else need += 4u;
    }
    if (!lpMultiByteStr || cbMultiByte == 0) return (int)need;
    size_t out = 0;
    for (size_t i = 0; i < wlen && out < (size_t)cbMultiByte; ++i) {
        uint32_t ch = (uint32_t)lpWideCharStr[i];
        if (ch == 0 && cchWideChar < 0) { lpMultiByteStr[out++] = '\0'; break; }
        if (ch < 0x80u) {
            lpMultiByteStr[out++] = (char)ch;
        } else if (ch < 0x800u && out + 2u <= (size_t)cbMultiByte) {
            lpMultiByteStr[out++] = (char)(0xC0u | (ch >> 6));
            lpMultiByteStr[out++] = (char)(0x80u | (ch & 0x3Fu));
        } else if (ch < 0x10000u && out + 3u <= (size_t)cbMultiByte) {
            lpMultiByteStr[out++] = (char)(0xE0u | (ch >> 12));
            lpMultiByteStr[out++] = (char)(0x80u | ((ch >> 6) & 0x3Fu));
            lpMultiByteStr[out++] = (char)(0x80u | (ch & 0x3Fu));
        } else if (out + 4u <= (size_t)cbMultiByte) {
            lpMultiByteStr[out++] = (char)(0xF0u | (ch >> 18));
            lpMultiByteStr[out++] = (char)(0x80u | ((ch >> 12) & 0x3Fu));
            lpMultiByteStr[out++] = (char)(0x80u | ((ch >> 6) & 0x3Fu));
            lpMultiByteStr[out++] = (char)(0x80u | (ch & 0x3Fu));
        }
    }
    return (int)out;
}

int WINAPI MultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCSTR lpMultiByteStr, int cbMultiByte,
                               LPWSTR lpWideCharStr, int cchWideChar) {
    (void)CodePage; (void)dwFlags;
    if (!lpMultiByteStr) return 0;
    size_t blen = (cbMultiByte < 0) ? (strlen(lpMultiByteStr) + 1u) : (size_t)cbMultiByte;
    size_t need = 0;
    for (size_t i = 0; i < blen;) {
        unsigned char c = (unsigned char)lpMultiByteStr[i];
        if (c == 0 && cbMultiByte < 0) { need++; break; }
        if (c < 0x80u) i += 1;
        else if ((c & 0xE0u) == 0xC0u) i += 2;
        else if ((c & 0xF0u) == 0xE0u) i += 3;
        else i += 4;
        need++;
    }
    if (!lpWideCharStr || cchWideChar == 0) return (int)need;
    size_t out = 0;
    for (size_t i = 0; i < blen && out < (size_t)cchWideChar;) {
        unsigned char c = (unsigned char)lpMultiByteStr[i];
        if (c == 0 && cbMultiByte < 0) { lpWideCharStr[out++] = L'\0'; break; }
        uint32_t cp = 0;
        if (c < 0x80u) { cp = c; i += 1; }
        else if ((c & 0xE0u) == 0xC0u && i + 1 < blen) {
            cp = ((uint32_t)(c & 0x1Fu) << 6) | (uint32_t)(lpMultiByteStr[i + 1] & 0x3Fu);
            i += 2;
        } else if ((c & 0xF0u) == 0xE0u && i + 2 < blen) {
            cp = ((uint32_t)(c & 0x0Fu) << 12) | ((uint32_t)(lpMultiByteStr[i + 1] & 0x3Fu) << 6) |
                 (uint32_t)(lpMultiByteStr[i + 2] & 0x3Fu);
            i += 3;
        } else if (i + 3 < blen) {
            cp = ((uint32_t)(c & 0x07u) << 18) | ((uint32_t)(lpMultiByteStr[i + 1] & 0x3Fu) << 12) |
                 ((uint32_t)(lpMultiByteStr[i + 2] & 0x3Fu) << 6) | (uint32_t)(lpMultiByteStr[i + 3] & 0x3Fu);
            i += 4;
        } else { cp = '?'; i += 1; }
        lpWideCharStr[out++] = (wchar_t)cp;
    }
    return (int)out;
}

#define MOCK_GI_HANDLE ((HMODULE)(uintptr_t)0x1001)
#define MOCK_XI_HANDLE ((HMODULE)(uintptr_t)0x1002)

HMODULE WINAPI LoadLibraryA(LPCSTR lpLibFileName) {
    if (!lpLibFileName) return NULL;
    if (strcmp(lpLibFileName, "GameInput.dll") == 0) {
        return g_mock_gi_present ? MOCK_GI_HANDLE : NULL;
    }
    if (strcmp(lpLibFileName, "xinput1_4.dll") == 0) {
        return g_mock_xi_present ? MOCK_XI_HANDLE : NULL;
    }
    return NULL;
}

HMODULE WINAPI GetModuleHandleA(LPCSTR lpModuleName) {
    return LoadLibraryA(lpModuleName);
}

void *WINAPI GetProcAddress(HMODULE hModule, LPCSTR lpProcName) {
    if (!lpProcName) return NULL;
    if (hModule == MOCK_GI_HANDLE && strcmp(lpProcName, "GameInputCreate") == 0) {
        return (void *)mock_GameInputCreate;
    }
    if (hModule == MOCK_XI_HANDLE && strcmp(lpProcName, "XInputGetState") == 0) {
        return (void *)mock_XInputGetState;
    }
    if (hModule == MOCK_XI_HANDLE && strcmp(lpProcName, "XInputSetState") == 0) {
        return (void *)mock_XInputSetState;
    }
    return NULL;
}

BOOL WINAPI FreeLibrary(HMODULE hLibModule) {
    (void)hLibModule;
    return TRUE;
}

ULONGLONG WINAPI GetTickCount64(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ULONGLONG)ts.tv_sec * 1000ULL + (ULONGLONG)(ts.tv_nsec / 1000000ULL);
}

/* ============================================================================
 * Host HSTRING Implementation
 * ============================================================================ */

typedef struct HostHstring {
    volatile LONG ref;
    UINT32 len;
    wchar_t data[1];
} HostHstring;

HRESULT WINAPI WindowsCreateString(LPCWSTR src, UINT32 len, HSTRING *out) {
    if (!out) return E_POINTER;
    *out = NULL;
    if (len == 0) return S_OK;
    if (!src) return E_INVALIDARG;
    HostHstring *h = (HostHstring *)malloc(sizeof(HostHstring) + (size_t)len * sizeof(wchar_t));
    if (!h) return E_OUTOFMEMORY;
    h->ref = 1;
    h->len = len;
    memcpy(h->data, src, (size_t)len * sizeof(wchar_t));
    h->data[len] = L'\0';
    *out = (HSTRING)h;
    return S_OK;
}

HRESULT WINAPI WindowsDuplicateString(HSTRING str, HSTRING *out) {
    if (!out) return E_POINTER;
    *out = NULL;
    if (!str) return S_OK;
    HostHstring *h = (HostHstring *)str;
    InterlockedIncrement(&h->ref);
    *out = str;
    return S_OK;
}

HRESULT WINAPI WindowsDeleteString(HSTRING str) {
    if (!str) return S_OK;
    HostHstring *h = (HostHstring *)str;
    if (InterlockedDecrement(&h->ref) == 0) {
        free(h);
    }
    return S_OK;
}

LPCWSTR WINAPI WindowsGetStringRawBuffer(HSTRING str, UINT32 *len) {
    if (!str) {
        if (len) *len = 0;
        return L"";
    }
    HostHstring *h = (HostHstring *)str;
    if (len) *len = h->len;
    return h->data;
}
