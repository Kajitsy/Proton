/*
 * Proton-Scarlett — Host Linux Win32 Shim Header (Tests Only)
 *
 * Included ONLY when compiling winrt_sys host unit tests on non-Windows hosts
 * (#ifndef _WIN32). Never compiled into PE64 binaries.
 */

#ifndef WINRT_SYS_WIN32_HOST_SHIM_H
#define WINRT_SYS_WIN32_HOST_SHIM_H

#ifndef _WIN32

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WINAPI
#define WINAPI
#endif
#ifndef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE
#endif

typedef uint8_t  BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint32_t UINT;
typedef int32_t  LONG;
typedef int64_t  LONGLONG;
typedef uint64_t ULONGLONG;
typedef uint16_t WCHAR;
typedef wchar_t  WCHAR_NATIVE;
typedef const wchar_t *LPCWSTR;
typedef wchar_t *LPWSTR;
typedef const char *LPCSTR;
typedef char *LPSTR;
typedef void *HANDLE;
typedef void *HMODULE;
typedef void *LPVOID;
typedef const void *LPCVOID;
typedef uint64_t UINT_PTR;
typedef int64_t INT_PTR;
typedef uint32_t UINT32;
typedef int32_t INT32;
typedef uint64_t UINT64;
typedef int64_t INT64;
typedef uint16_t UINT16;
typedef int16_t INT16;
typedef uint8_t UINT8;
typedef uint8_t boolean;

typedef GUID IID;
typedef IID *LPIID;

typedef struct HSTRING__ {
    int unused;
} *HSTRING;

typedef struct HSTRING_HEADER {
    char reserved[24];
} HSTRING_HEADER;

typedef struct EventRegistrationToken {
    int64_t value;
} EventRegistrationToken;

typedef enum TrustLevel {
    BaseTrust = 0,
    PartialTrust = 1,
    FullTrust = 2
} TrustLevel;

typedef enum AsyncStatus {
    Started = 0,
    Completed = 1,
    Canceled = 2,
    Error = 3
} AsyncStatus;

typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;

typedef struct _WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA;

typedef struct _WIN32_FIND_DATAW {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    wchar_t cFileName[260];
    wchar_t cAlternateFileName[14];
} WIN32_FIND_DATAW;

typedef struct _CRITICAL_SECTION {
    void *impl;
} CRITICAL_SECTION;

typedef union _LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG  HighPart;
    };
    LONGLONG QuadPart;
} LARGE_INTEGER;

typedef struct _OVERLAPPED OVERLAPPED;
typedef struct _SECURITY_ATTRIBUTES SECURITY_ATTRIBUTES;

typedef enum GET_FILEEX_INFO_LEVELS {
    GetFileExInfoStandard = 0
} GET_FILEEX_INFO_LEVELS;

#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#endif
#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)0xFFFFFFFFu)
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define S_OK                    ((HRESULT)0x00000000L)
#define S_FALSE                 ((HRESULT)0x00000001L)
#define E_NOTIMPL               ((HRESULT)0x80004001L)
#define E_NOINTERFACE           ((HRESULT)0x80004002L)
#define E_POINTER               ((HRESULT)0x80004003L)
#define E_ABORT                 ((HRESULT)0x80004004L)
#define E_FAIL                  ((HRESULT)0x80004005L)
#define E_UNEXPECTED            ((HRESULT)0x8000FFFFL)
#define E_INVALIDARG            ((HRESULT)0x80070057L)
#define E_ACCESSDENIED          ((HRESULT)0x80070005L)
#define E_OUTOFMEMORY           ((HRESULT)0x8007000EL)
#define E_BOUNDS                ((HRESULT)0x8000000BL)
#define E_ILLEGAL_METHOD_CALL   ((HRESULT)0x8000000EL)
#define CLASS_E_CLASSNOTAVAILABLE ((HRESULT)0x80040111L)

#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr)    (((HRESULT)(hr)) < 0)
#define HRESULT_FROM_WIN32(x) ((HRESULT)(x) <= 0 ? ((HRESULT)(x)) : ((HRESULT) (((x) & 0x0000FFFF) | (7 << 16) | 0x80000000)))

#define ERROR_SUCCESS           0u
#define ERROR_FILE_NOT_FOUND    2u
#define ERROR_PATH_NOT_FOUND    3u
#define ERROR_ACCESS_DENIED     5u
#define ERROR_ALREADY_EXISTS    183u
#define ERROR_NO_MORE_FILES     18u
#define ERROR_DEVICE_NOT_CONNECTED 1167u

#define FILE_ATTRIBUTE_READONLY   0x00000001u
#define FILE_ATTRIBUTE_DIRECTORY  0x00000010u
#define FILE_ATTRIBUTE_ARCHIVE    0x00000020u

#define GENERIC_READ              0x80000000u
#define GENERIC_WRITE             0x40000000u
#define FILE_SHARE_READ           0x00000001u
#define FILE_SHARE_WRITE          0x00000002u
#define CREATE_NEW                1u
#define CREATE_ALWAYS             2u
#define OPEN_EXISTING             3u
#define OPEN_ALWAYS               4u
#define FILE_ATTRIBUTE_NORMAL     0x00000080u

#define FILE_BEGIN                0u
#define FILE_CURRENT              1u
#define FILE_END                  2u
#define MOVEFILE_REPLACE_EXISTING 0x00000001u

#define HEAP_ZERO_MEMORY          0x00000008u
#define CP_UTF8                   65001u
#define MAX_PATH                  260

typedef struct IUnknown IUnknown;
typedef struct IInspectable IInspectable;

typedef struct IUnknownVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IUnknown *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IUnknown *This);
    ULONG   (STDMETHODCALLTYPE *Release)(IUnknown *This);
} IUnknownVtbl;

struct IUnknown {
    const IUnknownVtbl *lpVtbl;
};

typedef struct IInspectableVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IInspectable *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IInspectable *This);
    ULONG   (STDMETHODCALLTYPE *Release)(IInspectable *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IInspectable *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IInspectable *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IInspectable *This, TrustLevel *trustLevel);
} IInspectableVtbl;

struct IInspectable {
    const IInspectableVtbl *lpVtbl;
};

/* Win32 API surface shimmed in tests/win32_host_shim.c */
HANDLE WINAPI GetProcessHeap(void);
LPVOID WINAPI HeapAlloc(HANDLE hHeap, DWORD dwFlags, size_t dwBytes);
BOOL   WINAPI HeapFree(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem);
LPVOID WINAPI CoTaskMemAlloc(size_t cb);
void   WINAPI CoTaskMemFree(LPVOID pv);

void WINAPI InitializeCriticalSection(CRITICAL_SECTION *lpcs);
void WINAPI EnterCriticalSection(CRITICAL_SECTION *lpcs);
void WINAPI LeaveCriticalSection(CRITICAL_SECTION *lpcs);
void WINAPI DeleteCriticalSection(CRITICAL_SECTION *lpcs);

LONG InterlockedIncrement(volatile LONG *Addend);
LONG InterlockedDecrement(volatile LONG *Addend);
LONG InterlockedCompareExchange(volatile LONG *Destination, LONG Exchange, LONG Comperand);

DWORD WINAPI GetEnvironmentVariableW(LPCWSTR lpName, LPWSTR lpBuffer, DWORD nSize);
DWORD WINAPI GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize);
DWORD WINAPI GetLastError(void);
BOOL  WINAPI CreateDirectoryW(LPCWSTR lpPathName, SECURITY_ATTRIBUTES *lpSecurityAttributes);
BOOL  WINAPI RemoveDirectoryW(LPCWSTR lpPathName);
BOOL  WINAPI DeleteFileW(LPCWSTR lpFileName);
DWORD WINAPI GetFileAttributesW(LPCWSTR lpFileName);
BOOL  WINAPI GetFileAttributesExW(LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation);
HANDLE WINAPI CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                          SECURITY_ATTRIBUTES *lpSecurityAttributes, DWORD dwCreationDisposition,
                          DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
BOOL  WINAPI ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, DWORD *lpNumberOfBytesRead, OVERLAPPED *lpOverlapped);
BOOL  WINAPI WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, DWORD *lpNumberOfBytesWritten, OVERLAPPED *lpOverlapped);
BOOL  WINAPI SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, LARGE_INTEGER *lpNewFilePointer, DWORD dwMoveMethod);
BOOL  WINAPI GetFileSizeEx(HANDLE hFile, LARGE_INTEGER *lpFileSize);
BOOL  WINAPI SetEndOfFile(HANDLE hFile);
BOOL  WINAPI FlushFileBuffers(HANDLE hFile);
BOOL  WINAPI MoveFileExW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, DWORD dwFlags);
BOOL  WINAPI CloseHandle(HANDLE hObject);
HANDLE WINAPI FindFirstFileW(LPCWSTR lpFileName, WIN32_FIND_DATAW *lpFindFileData);
BOOL  WINAPI FindNextFileW(HANDLE hFindFile, WIN32_FIND_DATAW *lpFindFileData);
BOOL  WINAPI FindClose(HANDLE hFindFile);

int WINAPI WideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWSTR lpWideCharStr, int cchWideChar,
                               LPSTR lpMultiByteStr, int cbMultiByte, LPCSTR lpDefaultChar, BOOL *lpUsedDefaultChar);
int WINAPI MultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCSTR lpMultiByteStr, int cbMultiByte,
                               LPWSTR lpWideCharStr, int cchWideChar);

HMODULE WINAPI LoadLibraryA(LPCSTR lpLibFileName);
HMODULE WINAPI GetModuleHandleA(LPCSTR lpModuleName);
void *  WINAPI GetProcAddress(HMODULE hModule, LPCSTR lpProcName);
BOOL    WINAPI FreeLibrary(HMODULE hLibModule);
ULONGLONG WINAPI GetTickCount64(void);

HRESULT WINAPI WindowsCreateString(LPCWSTR sourceString, UINT32 length, HSTRING *string);
HRESULT WINAPI WindowsDuplicateString(HSTRING string, HSTRING *newString);
HRESULT WINAPI WindowsDeleteString(HSTRING string);
LPCWSTR WINAPI WindowsGetStringRawBuffer(HSTRING string, UINT32 *length);

#ifdef __cplusplus
}
#endif

#endif /* !_WIN32 */
#endif /* WINRT_SYS_WIN32_HOST_SHIM_H */
