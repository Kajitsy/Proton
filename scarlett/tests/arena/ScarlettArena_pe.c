/*
 * Proton-Scarlett — Synthetic Game PE64 Binary Entrypoint (ScarlettArena.exe)
 *
 * Compiled as a genuine freestanding x86_64 Windows PE64 binary via:
 *   clang --target=x86_64-w64-mingw32 -std=c11 -ffreestanding -c
 *   lld-link /subsystem:console /entry:ScarlettArenaEntry /machine:x64 /nodefaultlib
 *
 * Linked against Proton-Scarlett .def import libraries so PE64 headers,
 * OptionalHeader Magic (0x020B), and COFF Import Directory entries are authentic
 * for Proton launcher detection (`PROTON_XBOX_SCARLETT=auto`) and Wine PE loading.
 */

#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)
#define ARENA_DLLIMPORT __declspec(dllimport)
#define ARENA_WIN64_CALL __stdcall
#else
#define ARENA_DLLIMPORT
#define ARENA_WIN64_CALL
#endif

ARENA_DLLIMPORT int32_t ARENA_WIN64_CALL XMemInitialize(uint32_t model, uint32_t strict_mode);
ARENA_DLLIMPORT int32_t ARENA_WIN64_CALL XGameRuntimeInitialize(void);
ARENA_DLLIMPORT void ARENA_WIN64_CALL XGameRuntimeUninitialize(void);
ARENA_DLLIMPORT uint32_t ARENA_WIN64_CALL XSystemGetDeviceType(void);
ARENA_DLLIMPORT uint32_t ARENA_WIN64_CALL XGamePlatformGetConsoleModel(void);
ARENA_DLLIMPORT int32_t ARENA_WIN64_CALL D3D12XboxCreateDevice(
    const void *pParams,
    const void *riid,
    void **ppDevice);
ARENA_DLLIMPORT uint32_t ARENA_WIN64_CALL XGComputeOptimalSwizzleMode(
    uint32_t resourceType,
    uint32_t format,
    uint32_t width,
    uint32_t height,
    uint32_t depthOrArray,
    uint32_t sampleCount,
    uint32_t bindFlags);
ARENA_DLLIMPORT int32_t ARENA_WIN64_CALL DStorageGetFactory(const void *riid, void **ppv);
ARENA_DLLIMPORT int32_t ARENA_WIN64_CALL XAudio2Create(void **ppXAudio2, uint32_t Flags, uint32_t Proc);
ARENA_DLLIMPORT int32_t ARENA_WIN64_CALL GameInputCreate(void **gameInput);
ARENA_DLLIMPORT uint32_t ARENA_WIN64_CALL ffxFsr4GetVersion(void);
ARENA_DLLIMPORT void ARENA_WIN64_CALL PIXBeginEvent(uint64_t color, const char *fmt, ...);
ARENA_DLLIMPORT void ARENA_WIN64_CALL PIXEndEvent(void);

uint32_t ScarlettArenaEntry(void) {
    const uint32_t model = XGamePlatformGetConsoleModel();
    if (XMemInitialize(model, 0u) < 0) {
        return 1u;
    }
    if (XGameRuntimeInitialize() < 0) {
        return 2u;
    }
    const uint32_t dev_type = XSystemGetDeviceType();
    if (dev_type == 0u) {
        XGameRuntimeUninitialize();
        return 3u;
    }

    PIXBeginEvent(0xFF00AAFFULL, "ScarlettArenaFrame");
    const uint32_t swizzle = XGComputeOptimalSwizzleMode(2u, 98u, 1024u, 1024u, 1u, 1u, 0x8u);
    (void)swizzle;

    void *d3d_dev = (void *)0;
    void *ds_fac = (void *)0;
    void *xa2 = (void *)0;
    void *gi = (void *)0;
    (void)D3D12XboxCreateDevice((const void *)0, (const void *)0, &d3d_dev);
    (void)DStorageGetFactory((const void *)0, &ds_fac);
    (void)XAudio2Create(&xa2, 0u, 0u);
    (void)GameInputCreate(&gi);
    const uint32_t fsr_ver = ffxFsr4GetVersion();
    (void)fsr_ver;

    PIXEndEvent();
    XGameRuntimeUninitialize();
    return 0u;
}
