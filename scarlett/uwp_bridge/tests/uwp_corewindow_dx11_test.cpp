/*
 * Proton-Scarlett — Clean-Room Synthetic UWP PE64 D3D11 CoreWindow SwapChain Test
 *
 * Verifies:
 *   1. IDXGIFactory2::CreateSwapChainForCoreWindow via IProtonScarlettCoreWindowBridge
 *      (with Width=0, Height=0, DXGI_SCALING_ASPECT_RATIO_STRETCH,
 *       DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_ALPHA_MODE_PREMULTIPLIED)
 *   2. IProtonScarlettCoreWindowBridge::NotifySwapChainCreated callback
 *   3. IDXGISwapChain1::GetCoreWindow (IUnknown, IProtonScarlettCoreWindowBridge, ICoreWindowInterop)
 *   4. IDXGISwapChain1::GetDesc1 normalization verification
 *   5. IDXGISwapChain::ResizeBuffers (explicit size + 0x0 auto-size)
 *   6. Rendering (ClearRenderTargetView) and Present for 10+ frames
 *   7. Borderless fullscreen transition (SetFullscreenState TRUE -> FALSE)
 *   8. Fallback path via standard ICoreWindowInterop only + invalid-arg rejection
 *
 * Build (MinGW PE64 inside steamrt4):
 *   x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -static-libgcc -static-libstdc++ \
 *     -Iscarlett/uwp_bridge/include \
 *     scarlett/uwp_bridge/tests/uwp_corewindow_dx11_test.cpp \
 *     -o uwp_corewindow_dx11_test.exe \
 *     -ld3d11 -ldxgi -luser32 -lgdi32 -lole32 -luuid
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "uwp_bridge_abi.h"

namespace {

constexpr UINT kInitialWidth  = 800;
constexpr UINT kInitialHeight = 600;
constexpr UINT kResizedWidth  = 1024;
constexpr UINT kResizedHeight = 768;

static bool GuidEqual(REFIID a, const GUID& b) {
    return std::memcmp(&a, &b, sizeof(GUID)) == 0;
}

static LRESULT CALLBACK TestWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

static void PumpWindowMessages(HWND hwnd) {
    MSG msg = {};
    while (PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static HWND CreateTestWindow(UINT clientWidth, UINT clientHeight) {
    static bool s_classRegistered = false;
    HINSTANCE hInstance = GetModuleHandleW(nullptr);
    const wchar_t* className = L"ScarlettSyntheticCoreWindowClass";

    if (!s_classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc   = &TestWndProc;
        wc.hInstance     = hInstance;
        wc.lpszClassName = className;
        if (!RegisterClassExW(&wc)) {
            std::fprintf(stderr, "[FAIL] RegisterClassExW failed (err=%lu)\n", GetLastError());
            return nullptr;
        }
        s_classRegistered = true;
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rc = { 0, 0, static_cast<LONG>(clientWidth), static_cast<LONG>(clientHeight) };
    AdjustWindowRectEx(&rc, style, FALSE, 0);

    HWND hwnd = CreateWindowExW(
        0,
        className,
        L"Proton-Scarlett UWP CoreWindow DX11 Test",
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rc.right - rc.left,
        rc.bottom - rc.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (!hwnd) {
        std::fprintf(stderr, "[FAIL] CreateWindowExW failed (err=%lu)\n", GetLastError());
        return nullptr;
    }

    ShowWindow(hwnd, SW_SHOWNA);
    UpdateWindow(hwnd);
    PumpWindowMessages(hwnd);
    return hwnd;
}

/*
 * Clean-room synthetic CoreWindow implementing exact C-ABI COM vtables from
 * scarlett/uwp_bridge/include/uwp_bridge_abi.h:
 *   - IProtonScarlettCoreWindowBridge (optional via exposeBridge)
 *   - ICoreWindowInterop              (optional via exposeInterop)
 */
struct SyntheticCoreWindow {
    struct InteropSubObject {
        const ICoreWindowInteropBridgeVtbl* lpVtbl;
        SyntheticCoreWindow* parent;
    };

    const IProtonScarlettCoreWindowBridgeVtbl* lpVtbl;
    InteropSubObject interop;

    std::atomic<ULONG> refCount;
    HWND hwnd;
    uint32_t width;
    uint32_t height;
    int32_t fullscreen;
    uint8_t messageHandled;
    bool exposeBridge;
    bool exposeInterop;
    bool swapChainNotified;
    IUnknown* lastNotifiedSwapChain;

    static SyntheticCoreWindow* Create(HWND hwnd, uint32_t w, uint32_t h, bool exposeBridge, bool exposeInterop);

    static HRESULT STDMETHODCALLTYPE Bridge_QueryInterface(void* This, REFIID riid, void** ppvObject) {
        auto* self = reinterpret_cast<SyntheticCoreWindow*>(This);
        if (!ppvObject)
            return E_POINTER;
        *ppvObject = nullptr;

        if (GuidEqual(riid, __uuidof(IUnknown))) {
            self->refCount.fetch_add(1, std::memory_order_relaxed);
            *ppvObject = static_cast<void*>(self);
            return S_OK;
        }
        if (self->exposeBridge && GuidEqual(riid, SCARLETT_IID_IProtonScarlettCoreWindowBridge)) {
            self->refCount.fetch_add(1, std::memory_order_relaxed);
            *ppvObject = static_cast<void*>(self);
            return S_OK;
        }
        if (self->exposeInterop && GuidEqual(riid, SCARLETT_IID_ICoreWindowInterop)) {
            self->refCount.fetch_add(1, std::memory_order_relaxed);
            *ppvObject = static_cast<void*>(&self->interop);
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    static ULONG STDMETHODCALLTYPE Bridge_AddRef(void* This) {
        auto* self = reinterpret_cast<SyntheticCoreWindow*>(This);
        return self->refCount.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    static ULONG STDMETHODCALLTYPE Bridge_Release(void* This) {
        auto* self = reinterpret_cast<SyntheticCoreWindow*>(This);
        ULONG remaining = self->refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0)
            delete self;
        return remaining;
    }

    static HRESULT STDMETHODCALLTYPE Bridge_GetHwnd(void* This, HWND* out_hwnd) {
        auto* self = reinterpret_cast<SyntheticCoreWindow*>(This);
        if (!out_hwnd)
            return E_POINTER;
        *out_hwnd = self->hwnd;
        return self->hwnd ? S_OK : E_FAIL;
    }

    static HRESULT STDMETHODCALLTYPE Bridge_GetClientSize(void* This, uint32_t* out_width, uint32_t* out_height) {
        auto* self = reinterpret_cast<SyntheticCoreWindow*>(This);
        RECT rc = {};
        if (self->hwnd && GetClientRect(self->hwnd, &rc)) {
            uint32_t rw = static_cast<uint32_t>(rc.right - rc.left);
            uint32_t rh = static_cast<uint32_t>(rc.bottom - rc.top);
            if (rw > 0 && rh > 0) {
                self->width = rw;
                self->height = rh;
            }
        }
        if (out_width)
            *out_width = self->width;
        if (out_height)
            *out_height = self->height;
        return S_OK;
    }

    static HRESULT STDMETHODCALLTYPE Bridge_IsFullscreen(void* This, int32_t* out_is_fullscreen) {
        auto* self = reinterpret_cast<SyntheticCoreWindow*>(This);
        if (!out_is_fullscreen)
            return E_POINTER;
        *out_is_fullscreen = self->fullscreen;
        return S_OK;
    }

    static HRESULT STDMETHODCALLTYPE Bridge_NotifySwapChainCreated(void* This, IUnknown* swapchain) {
        auto* self = reinterpret_cast<SyntheticCoreWindow*>(This);
        self->swapChainNotified = true;
        self->lastNotifiedSwapChain = swapchain; // non-owning diagnostic pointer
        return S_OK;
    }

    static HRESULT STDMETHODCALLTYPE Interop_QueryInterface(void* This, REFIID riid, void** ppvObject) {
        auto* sub = reinterpret_cast<InteropSubObject*>(This);
        return Bridge_QueryInterface(sub->parent, riid, ppvObject);
    }

    static ULONG STDMETHODCALLTYPE Interop_AddRef(void* This) {
        auto* sub = reinterpret_cast<InteropSubObject*>(This);
        return Bridge_AddRef(sub->parent);
    }

    static ULONG STDMETHODCALLTYPE Interop_Release(void* This) {
        auto* sub = reinterpret_cast<InteropSubObject*>(This);
        return Bridge_Release(sub->parent);
    }

    static HRESULT STDMETHODCALLTYPE Interop_get_WindowHandle(void* This, HWND* hwnd) {
        auto* sub = reinterpret_cast<InteropSubObject*>(This);
        if (!hwnd)
            return E_POINTER;
        *hwnd = sub->parent->hwnd;
        return sub->parent->hwnd ? S_OK : E_FAIL;
    }

    static HRESULT STDMETHODCALLTYPE Interop_put_MessageHandled(void* This, uint8_t value) {
        auto* sub = reinterpret_cast<InteropSubObject*>(This);
        sub->parent->messageHandled = value;
        return S_OK;
    }
};

static const IProtonScarlettCoreWindowBridgeVtbl g_bridgeVtbl = {
    &SyntheticCoreWindow::Bridge_QueryInterface,
    &SyntheticCoreWindow::Bridge_AddRef,
    &SyntheticCoreWindow::Bridge_Release,
    &SyntheticCoreWindow::Bridge_GetHwnd,
    &SyntheticCoreWindow::Bridge_GetClientSize,
    &SyntheticCoreWindow::Bridge_IsFullscreen,
    &SyntheticCoreWindow::Bridge_NotifySwapChainCreated,
};

static const ICoreWindowInteropBridgeVtbl g_interopVtbl = {
    &SyntheticCoreWindow::Interop_QueryInterface,
    &SyntheticCoreWindow::Interop_AddRef,
    &SyntheticCoreWindow::Interop_Release,
    &SyntheticCoreWindow::Interop_get_WindowHandle,
    &SyntheticCoreWindow::Interop_put_MessageHandled,
};

SyntheticCoreWindow* SyntheticCoreWindow::Create(
    HWND hwnd,
    uint32_t w,
    uint32_t h,
    bool exposeBridge,
    bool exposeInterop) {
    auto* obj = new SyntheticCoreWindow();
    obj->lpVtbl = &g_bridgeVtbl;
    obj->interop.lpVtbl = &g_interopVtbl;
    obj->interop.parent = obj;
    obj->refCount.store(1, std::memory_order_relaxed);
    obj->hwnd = hwnd;
    obj->width = w;
    obj->height = h;
    obj->fullscreen = 0;
    obj->messageHandled = 0;
    obj->exposeBridge = exposeBridge;
    obj->exposeInterop = exposeInterop;
    obj->swapChainNotified = false;
    obj->lastNotifiedSwapChain = nullptr;
    return obj;
}

static bool ClearAndPresentFrames(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    IDXGISwapChain1* swapChain,
    HWND hwnd,
    int frameCount,
    int frameOffset) {
    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr) || !backBuffer) {
        std::fprintf(stderr, "[FAIL] IDXGISwapChain1::GetBuffer(0) failed: hr=0x%08lx\n",
            static_cast<unsigned long>(hr));
        return false;
    }

    ID3D11RenderTargetView* rtv = nullptr;
    hr = device->CreateRenderTargetView(backBuffer, nullptr, &rtv);
    backBuffer->Release();
    if (FAILED(hr) || !rtv) {
        std::fprintf(stderr, "[FAIL] ID3D11Device::CreateRenderTargetView failed: hr=0x%08lx\n",
            static_cast<unsigned long>(hr));
        return false;
    }

    for (int i = 0; i < frameCount; ++i) {
        PumpWindowMessages(hwnd);

        const float t = static_cast<float>(i + frameOffset) / static_cast<float>(frameCount + frameOffset + 1);
        const float clearColor[4] = { 0.1f + 0.7f * t, 0.25f, 0.85f - 0.5f * t, 1.0f };
        context->OMSetRenderTargets(1, &rtv, nullptr);
        context->ClearRenderTargetView(rtv, clearColor);

        hr = swapChain->Present(0, 0);
        if (FAILED(hr)) {
            std::fprintf(stderr, "[FAIL] IDXGISwapChain1::Present failed on frame %d: hr=0x%08lx\n",
                i + frameOffset, static_cast<unsigned long>(hr));
            context->OMSetRenderTargets(0, nullptr, nullptr);
            rtv->Release();
            return false;
        }
    }

    context->OMSetRenderTargets(0, nullptr, nullptr);
    context->Flush();
    rtv->Release();
    return true;
}

} // namespace

int main() {
    std::printf("=== Proton-Scarlett UWP CoreWindow DX11 SwapChain Test ===\n");

    HWND hwnd = CreateTestWindow(kInitialWidth, kInitialHeight);
    if (!hwnd)
        return 1;

    // 1. Create D3D11 device and retrieve IDXGIFactory2
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL actualFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        featureLevels,
        static_cast<UINT>(sizeof(featureLevels) / sizeof(featureLevels[0])),
        D3D11_SDK_VERSION,
        &device,
        &actualFeatureLevel,
        &context);
    if (FAILED(hr) || !device || !context) {
        std::fprintf(stderr, "[FAIL] D3D11CreateDevice failed: hr=0x%08lx\n",
            static_cast<unsigned long>(hr));
        DestroyWindow(hwnd);
        return 2;
    }
    std::printf("[PASS] Created D3D11 device (FL=0x%x)\n", static_cast<unsigned>(actualFeatureLevel));

    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* dxgiAdapter = nullptr;
    IDXGIFactory2* factory2 = nullptr;

    hr = device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (SUCCEEDED(hr))
        hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    if (SUCCEEDED(hr))
        hr = dxgiAdapter->GetParent(IID_PPV_ARGS(&factory2));

    if (FAILED(hr) || !factory2) {
        std::fprintf(stderr, "[FAIL] Failed to obtain IDXGIFactory2: hr=0x%08lx\n",
            static_cast<unsigned long>(hr));
        if (dxgiAdapter) dxgiAdapter->Release();
        if (dxgiDevice) dxgiDevice->Release();
        context->Release();
        device->Release();
        DestroyWindow(hwnd);
        return 3;
    }
    std::printf("[PASS] Acquired IDXGIFactory2\n");

    int exitCode = 0;

    // 2. Negative validation tests
    {
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width              = 0;
        desc.Height             = 0;
        desc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count   = 1;
        desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount        = 2;
        desc.Scaling            = DXGI_SCALING_ASPECT_RATIO_STRETCH;
        desc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode          = DXGI_ALPHA_MODE_IGNORE;

        IDXGISwapChain1* badSwapChain = nullptr;
        hr = factory2->CreateSwapChainForCoreWindow(device, nullptr, &desc, nullptr, &badSwapChain);
        if (hr != DXGI_ERROR_INVALID_CALL || badSwapChain != nullptr) {
            std::fprintf(stderr, "[FAIL] Expected DXGI_ERROR_INVALID_CALL for null pWindow, got hr=0x%08lx\n",
                static_cast<unsigned long>(hr));
            if (badSwapChain) badSwapChain->Release();
            exitCode = 4;
        }

        auto* bogusCoreWin = SyntheticCoreWindow::Create(hwnd, kInitialWidth, kInitialHeight, false, false);
        hr = factory2->CreateSwapChainForCoreWindow(
            device, reinterpret_cast<IUnknown*>(bogusCoreWin), &desc, nullptr, &badSwapChain);
        SyntheticCoreWindow::Bridge_Release(bogusCoreWin);
        if (hr != DXGI_ERROR_INVALID_CALL || badSwapChain != nullptr) {
            std::fprintf(stderr, "[FAIL] Expected DXGI_ERROR_INVALID_CALL for unbridged pWindow, got hr=0x%08lx\n",
                static_cast<unsigned long>(hr));
            if (badSwapChain) badSwapChain->Release();
            exitCode = 5;
        } else if (exitCode == 0) {
            std::printf("[PASS] Negative validation (null & unbridged CoreWindow) returned DXGI_ERROR_INVALID_CALL\n");
        }
    }

    // 3. Primary test: CoreWindow implementing both IProtonScarlettCoreWindowBridge and ICoreWindowInterop
    if (exitCode == 0) {
        auto* coreWindow = SyntheticCoreWindow::Create(hwnd, kInitialWidth, kInitialHeight, true, true);
        IUnknown* coreWindowUnk = reinterpret_cast<IUnknown*>(coreWindow);

        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width              = 0;
        desc.Height             = 0;
        desc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.Stereo             = FALSE;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount        = 2;
        desc.Scaling            = DXGI_SCALING_ASPECT_RATIO_STRETCH;
        desc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode          = DXGI_ALPHA_MODE_PREMULTIPLIED;
        desc.Flags              = 0;

        IDXGISwapChain1* swapChain = nullptr;
        hr = factory2->CreateSwapChainForCoreWindow(device, coreWindowUnk, &desc, nullptr, &swapChain);
        if (FAILED(hr) || !swapChain) {
            std::fprintf(stderr, "[FAIL] CreateSwapChainForCoreWindow failed: hr=0x%08lx\n",
                static_cast<unsigned long>(hr));
            exitCode = 10;
        } else {
            std::printf("[PASS] CreateSwapChainForCoreWindow succeeded (bridge + interop)\n");

            if (!coreWindow->swapChainNotified) {
                std::fprintf(stderr, "[FAIL] NotifySwapChainCreated was not called on bridge\n");
                exitCode = 11;
            }

            DXGI_SWAP_CHAIN_DESC1 actualDesc = {};
            hr = swapChain->GetDesc1(&actualDesc);
            if (FAILED(hr)) {
                std::fprintf(stderr, "[FAIL] GetDesc1 failed: hr=0x%08lx\n", static_cast<unsigned long>(hr));
                exitCode = 12;
            } else if (actualDesc.Width == 0 || actualDesc.Height == 0
                    || actualDesc.Scaling != DXGI_SCALING_STRETCH
                    || actualDesc.AlphaMode != DXGI_ALPHA_MODE_IGNORE
                    || actualDesc.BufferCount < 2) {
                std::fprintf(stderr,
                    "[FAIL] Unexpected normalized desc: %ux%u, scaling=%u, alpha=%u, buffers=%u\n",
                    actualDesc.Width, actualDesc.Height,
                    static_cast<unsigned>(actualDesc.Scaling),
                    static_cast<unsigned>(actualDesc.AlphaMode),
                    actualDesc.BufferCount);
                exitCode = 13;
            } else {
                std::printf("[PASS] Normalized desc verified: %ux%u, scaling=%u, alpha=%u, buffers=%u\n",
                    actualDesc.Width, actualDesc.Height,
                    static_cast<unsigned>(actualDesc.Scaling),
                    static_cast<unsigned>(actualDesc.AlphaMode),
                    actualDesc.BufferCount);
            }

            // Verify GetCoreWindow for IUnknown, IProtonScarlettCoreWindowBridge, and ICoreWindowInterop
            if (exitCode == 0) {
                IUnknown* queriedUnk = nullptr;
                hr = swapChain->GetCoreWindow(__uuidof(IUnknown), reinterpret_cast<void**>(&queriedUnk));
                if (FAILED(hr) || queriedUnk != coreWindowUnk) {
                    std::fprintf(stderr, "[FAIL] GetCoreWindow(IID_IUnknown) failed: hr=0x%08lx, ptr=%p (expected %p)\n",
                        static_cast<unsigned long>(hr), static_cast<void*>(queriedUnk), static_cast<void*>(coreWindowUnk));
                    exitCode = 14;
                }
                if (queriedUnk)
                    queriedUnk->Release();

                IProtonScarlettCoreWindowBridge* queriedBridge = nullptr;
                hr = swapChain->GetCoreWindow(
                    SCARLETT_IID_IProtonScarlettCoreWindowBridge,
                    reinterpret_cast<void**>(&queriedBridge));
                if (FAILED(hr) || !queriedBridge) {
                    std::fprintf(stderr, "[FAIL] GetCoreWindow(IProtonScarlettCoreWindowBridge) failed: hr=0x%08lx\n",
                        static_cast<unsigned long>(hr));
                    exitCode = 15;
                } else {
                    HWND queriedHwnd = nullptr;
                    hr = queriedBridge->lpVtbl->GetHwnd(queriedBridge, &queriedHwnd);
                    if (FAILED(hr) || queriedHwnd != hwnd) {
                        std::fprintf(stderr, "[FAIL] GetCoreWindow->GetHwnd mismatch: hr=0x%08lx, hwnd=%p\n",
                            static_cast<unsigned long>(hr), static_cast<void*>(queriedHwnd));
                        exitCode = 16;
                    }
                    queriedBridge->lpVtbl->Release(queriedBridge);
                }

                struct ICoreWindowInteropObj {
                    const ICoreWindowInteropBridgeVtbl* lpVtbl;
                };
                ICoreWindowInteropObj* queriedInterop = nullptr;
                hr = swapChain->GetCoreWindow(
                    SCARLETT_IID_ICoreWindowInterop,
                    reinterpret_cast<void**>(&queriedInterop));
                if (FAILED(hr) || !queriedInterop) {
                    std::fprintf(stderr, "[FAIL] GetCoreWindow(ICoreWindowInterop) failed: hr=0x%08lx\n",
                        static_cast<unsigned long>(hr));
                    exitCode = 17;
                } else {
                    HWND interopHwnd = nullptr;
                    hr = queriedInterop->lpVtbl->get_WindowHandle(queriedInterop, &interopHwnd);
                    if (FAILED(hr) || interopHwnd != hwnd) {
                        std::fprintf(stderr, "[FAIL] GetCoreWindow->get_WindowHandle mismatch: hr=0x%08lx, hwnd=%p\n",
                            static_cast<unsigned long>(hr), static_cast<void*>(interopHwnd));
                        exitCode = 18;
                    }
                    queriedInterop->lpVtbl->Release(queriedInterop);
                }

                if (exitCode == 0)
                    std::printf("[PASS] GetCoreWindow verified for IUnknown, IProtonScarlettCoreWindowBridge, and ICoreWindowInterop\n");
            }

            // Verify UWP background color / rotation helpers
            if (exitCode == 0) {
                const DXGI_RGBA bgColor = { 0.05f, 0.1f, 0.2f, 1.0f };
                DXGI_RGBA readBackColor = {};
                DXGI_MODE_ROTATION rot = DXGI_MODE_ROTATION_UNSPECIFIED;
                if (FAILED(swapChain->SetBackgroundColor(&bgColor))
                 || FAILED(swapChain->GetBackgroundColor(&readBackColor))
                 || FAILED(swapChain->SetRotation(DXGI_MODE_ROTATION_IDENTITY))
                 || FAILED(swapChain->GetRotation(&rot))
                 || rot != DXGI_MODE_ROTATION_IDENTITY) {
                    std::fprintf(stderr, "[FAIL] SetBackgroundColor/GetBackgroundColor/GetRotation failed\n");
                    exitCode = 19;
                }
            }

            // Clear & Present first 5 frames before ResizeBuffers
            if (exitCode == 0) {
                if (!ClearAndPresentFrames(device, context, swapChain, hwnd, 5, 0))
                    exitCode = 20;
                else
                    std::printf("[PASS] Cleared and presented 5 initial frames\n");
            }

            // ResizeBuffers (explicit dimensions)
            if (exitCode == 0) {
                hr = swapChain->ResizeBuffers(2, kResizedWidth, kResizedHeight, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
                if (FAILED(hr)) {
                    std::fprintf(stderr, "[FAIL] ResizeBuffers(%ux%u) failed: hr=0x%08lx\n",
                        kResizedWidth, kResizedHeight, static_cast<unsigned long>(hr));
                    exitCode = 21;
                } else {
                    DXGI_SWAP_CHAIN_DESC1 resizedDesc = {};
                    swapChain->GetDesc1(&resizedDesc);
                    if (resizedDesc.Width != kResizedWidth || resizedDesc.Height != kResizedHeight) {
                        std::fprintf(stderr, "[FAIL] ResizeBuffers desc mismatch: got %ux%u\n",
                            resizedDesc.Width, resizedDesc.Height);
                        exitCode = 22;
                    } else {
                        std::printf("[PASS] ResizeBuffers(%ux%u) succeeded\n", resizedDesc.Width, resizedDesc.Height);
                    }
                }
            }

            // Clear & Present 5 more frames after explicit resize (total 10 frames)
            if (exitCode == 0) {
                if (!ClearAndPresentFrames(device, context, swapChain, hwnd, 5, 5))
                    exitCode = 23;
                else
                    std::printf("[PASS] Cleared and presented 5 post-resize frames (10 total)\n");
            }

            // ResizeBuffers(0, 0, 0, UNKNOWN, 0) auto-size test + borderless fullscreen test
            if (exitCode == 0) {
                hr = swapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
                if (FAILED(hr)) {
                    std::fprintf(stderr, "[FAIL] ResizeBuffers(0, 0, 0, UNKNOWN, 0) failed: hr=0x%08lx\n",
                        static_cast<unsigned long>(hr));
                    exitCode = 24;
                } else {
                    DXGI_SWAP_CHAIN_DESC1 autoDesc = {};
                    swapChain->GetDesc1(&autoDesc);
                    if (autoDesc.Width == 0 || autoDesc.Height == 0) {
                        std::fprintf(stderr, "[FAIL] Auto-resized dimensions are zero (%ux%u)\n",
                            autoDesc.Width, autoDesc.Height);
                        exitCode = 25;
                    } else {
                        std::printf("[PASS] ResizeBuffers(0,0,0) auto-sized to %ux%u\n",
                            autoDesc.Width, autoDesc.Height);
                    }
                }
            }

            if (exitCode == 0) {
                hr = swapChain->SetFullscreenState(TRUE, nullptr);
                if (FAILED(hr)) {
                    std::fprintf(stderr, "[FAIL] SetFullscreenState(TRUE) failed: hr=0x%08lx\n",
                        static_cast<unsigned long>(hr));
                    exitCode = 26;
                } else {
                    BOOL isFullscreen = FALSE;
                    swapChain->GetFullscreenState(&isFullscreen, nullptr);
                    if (!isFullscreen) {
                        std::fprintf(stderr, "[FAIL] Expected GetFullscreenState == TRUE\n");
                        exitCode = 27;
                    } else if (!ClearAndPresentFrames(device, context, swapChain, hwnd, 2, 10)) {
                        exitCode = 28;
                    } else if (FAILED(swapChain->SetFullscreenState(FALSE, nullptr))) {
                        std::fprintf(stderr, "[FAIL] SetFullscreenState(FALSE) failed\n");
                        exitCode = 29;
                    } else {
                        std::printf("[PASS] Borderless fullscreen toggle (TRUE -> Present x2 -> FALSE) succeeded\n");
                    }
                }
            }

            swapChain->Release();
        }

        SyntheticCoreWindow::Bridge_Release(coreWindow);
    }

    // 4. Fallback test: standard Windows ICoreWindowInterop ONLY (no Proton-Scarlett bridge)
    if (exitCode == 0) {
        auto* interopOnlyWindow = SyntheticCoreWindow::Create(hwnd, kInitialWidth, kInitialHeight, false, true);
        IUnknown* interopOnlyUnk = reinterpret_cast<IUnknown*>(interopOnlyWindow);

        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width              = 0;
        desc.Height             = 0;
        desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count   = 1;
        desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount        = 2;
        desc.Scaling            = DXGI_SCALING_ASPECT_RATIO_STRETCH;
        desc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode          = DXGI_ALPHA_MODE_STRAIGHT;

        IDXGISwapChain1* fallbackSwapChain = nullptr;
        hr = factory2->CreateSwapChainForCoreWindow(device, interopOnlyUnk, &desc, nullptr, &fallbackSwapChain);
        if (FAILED(hr) || !fallbackSwapChain) {
            std::fprintf(stderr, "[FAIL] ICoreWindowInterop fallback CreateSwapChainForCoreWindow failed: hr=0x%08lx\n",
                static_cast<unsigned long>(hr));
            exitCode = 30;
        } else {
            if (!ClearAndPresentFrames(device, context, fallbackSwapChain, hwnd, 2, 0))
                exitCode = 31;
            else
                std::printf("[PASS] ICoreWindowInterop-only fallback path verified\n");

            // 5. Verify CreateDirect3D11DeviceFromDXGIDevice & CreateDirect3D11SurfaceFromDXGISurface exports
            if (exitCode == 0) {
                HMODULE d3d11Mod = GetModuleHandleW(L"d3d11.dll");
                using PfnCreateDev = HRESULT (WINAPI *)(IDXGIDevice*, IUnknown**);
                using PfnCreateSurf = HRESULT (WINAPI *)(IDXGISurface*, IUnknown**);
                auto pfnCreateDev = d3d11Mod
                    ? reinterpret_cast<PfnCreateDev>(reinterpret_cast<void*>(GetProcAddress(d3d11Mod, "CreateDirect3D11DeviceFromDXGIDevice")))
                    : nullptr;
                auto pfnCreateSurf = d3d11Mod
                    ? reinterpret_cast<PfnCreateSurf>(reinterpret_cast<void*>(GetProcAddress(d3d11Mod, "CreateDirect3D11SurfaceFromDXGISurface")))
                    : nullptr;

                struct IAccessVtbl {
                    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void*, REFIID, void**);
                    ULONG   (STDMETHODCALLTYPE *AddRef)(void*);
                    ULONG   (STDMETHODCALLTYPE *Release)(void*);
                    HRESULT (STDMETHODCALLTYPE *GetInterface)(void*, REFIID, void**);
                };
                struct IAccessObj { const IAccessVtbl* lpVtbl; };

                static constexpr GUID kIidDxgiAccess =
                    { 0xa9b3d012u, 0x3df2u, 0x4ee3u, { 0xb8u, 0xd1u, 0x86u, 0x95u, 0xf4u, 0x57u, 0xd3u, 0xc1u } };

                if (!pfnCreateDev || !pfnCreateSurf) {
                    std::fprintf(stderr, "[FAIL] Missing CreateDirect3D11* exports in d3d11.dll\n");
                    exitCode = 32;
                } else {
                    IUnknown* winrtDev = nullptr;
                    hr = pfnCreateDev(dxgiDevice, &winrtDev);
                    if (FAILED(hr) || !winrtDev) {
                        std::fprintf(stderr, "[FAIL] CreateDirect3D11DeviceFromDXGIDevice failed: hr=0x%08lx\n",
                            static_cast<unsigned long>(hr));
                        exitCode = 33;
                    } else {
                        IAccessObj* access = nullptr;
                        hr = winrtDev->QueryInterface(kIidDxgiAccess, reinterpret_cast<void**>(&access));
                        if (FAILED(hr) || !access) {
                            std::fprintf(stderr, "[FAIL] QI(IDirect3DDxgiInterfaceAccess) on device failed: hr=0x%08lx\n",
                                static_cast<unsigned long>(hr));
                            exitCode = 34;
                        } else {
                            IDXGIDevice* unwrappedDev = nullptr;
                            hr = access->lpVtbl->GetInterface(access, __uuidof(IDXGIDevice),
                                reinterpret_cast<void**>(&unwrappedDev));
                            if (FAILED(hr) || !unwrappedDev) {
                                std::fprintf(stderr, "[FAIL] GetInterface(IDXGIDevice) failed: hr=0x%08lx\n",
                                    static_cast<unsigned long>(hr));
                                exitCode = 35;
                            } else {
                                unwrappedDev->Release();
                            }
                            access->lpVtbl->Release(access);
                        }
                        winrtDev->Release();
                    }

                    if (exitCode == 0) {
                        IDXGISurface* dxgiSurf = nullptr;
                        hr = fallbackSwapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiSurf));
                        if (SUCCEEDED(hr) && dxgiSurf) {
                            IUnknown* winrtSurf = nullptr;
                            hr = pfnCreateSurf(dxgiSurf, &winrtSurf);
                            dxgiSurf->Release();
                            if (FAILED(hr) || !winrtSurf) {
                                std::fprintf(stderr, "[FAIL] CreateDirect3D11SurfaceFromDXGISurface failed: hr=0x%08lx\n",
                                    static_cast<unsigned long>(hr));
                                exitCode = 36;
                            } else {
                                IAccessObj* access = nullptr;
                                hr = winrtSurf->QueryInterface(kIidDxgiAccess, reinterpret_cast<void**>(&access));
                                if (FAILED(hr) || !access) {
                                    std::fprintf(stderr, "[FAIL] QI(IDirect3DDxgiInterfaceAccess) on surface failed\n");
                                    exitCode = 37;
                                } else {
                                    IDXGISurface* unwrappedSurf = nullptr;
                                    hr = access->lpVtbl->GetInterface(access, __uuidof(IDXGISurface),
                                        reinterpret_cast<void**>(&unwrappedSurf));
                                    if (FAILED(hr) || !unwrappedSurf) {
                                        std::fprintf(stderr, "[FAIL] GetInterface(IDXGISurface) failed\n");
                                        exitCode = 38;
                                    } else {
                                        unwrappedSurf->Release();
                                    }
                                    access->lpVtbl->Release(access);
                                }
                                winrtSurf->Release();
                            }
                        }
                    }

                    if (exitCode == 0)
                        std::printf("[PASS] CreateDirect3D11DeviceFromDXGIDevice & CreateDirect3D11SurfaceFromDXGISurface verified\n");
                }
            }

            fallbackSwapChain->Release();
        }

        SyntheticCoreWindow::Bridge_Release(interopOnlyWindow);
    }

    factory2->Release();
    dxgiAdapter->Release();
    dxgiDevice->Release();
    context->Release();
    device->Release();
    DestroyWindow(hwnd);

    if (exitCode == 0)
        std::printf("=== ALL UWP COREWINDOW DX11 TESTS PASSED ===\n");
    return exitCode;
}
