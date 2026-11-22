/*
 * Proton-Scarlett — UWPHOST.dll CoreWindow & CoreDispatcher Implementation
 *
 * Implements:
 *   - Windows.UI.Core.CoreWindow (ICoreWindow, ICoreWindow2..5, ICoreWindowStatic,
 *     ICoreWindowInterop, IProtonScarlettCoreWindowBridge)
 *   - Windows.UI.Core.CoreDispatcher (ICoreDispatcher, ICoreAcceleratorKeys)
 *   - Real Win32 HWND creation via CreateWindowExW on STA/view thread
 *   - Win32 message loop & Win32 -> WinRT input/lifecycle event translation
 *   - uwp_core_* internal accessors declared in uwp_bridge_abi.h
 *
 * Under _WIN32 (PE64): Win32 APIs ONLY, zero POSIX/Linux calls.
 */

#include "uwphost_internal.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windowsx.h>
#endif

/* ============================================================================
 * Priority Work Queue Item for CoreDispatcher::RunAsync / RunIdleAsync
 * ============================================================================ */

typedef struct uwp_dispatch_item_s {
    struct uwp_dispatch_item_s *next;
    UwpCoreDispatcherPriority priority;
    int is_idle;
    UwpDispatchedHandler *callback;
    UwpIdleDispatchedHandler *idle_callback;
    UwpAsyncActionObj *async_action;
} uwp_dispatch_item_t;

/* ============================================================================
 * CoreWindow + CoreDispatcher Implementation Structs
 * ============================================================================ */

struct UwpCoreWindowObj {
    /* Multi-interface COM vtable headers */
    UwpICoreWindow                  core_window;
    UwpICoreWindow2                 core_window2;
    UwpICoreWindow3                 core_window3;
    UwpICoreWindow4                 core_window4;
    UwpICoreWindow5                 core_window5;
    UwpICoreWindowInterop           interop;
    IProtonScarlettCoreWindowBridge bridge;
    UwpICoreDispatcher              dispatcher;
    UwpICoreAcceleratorKeys         accel_keys;

    volatile int32_t ref;
    uwp_mutex_t lock;

    /* Native Win32 window state */
    HWND     hwnd;
    uint32_t owner_thread_id;
    int      headless;
    uint32_t client_width;
    uint32_t client_height;
    int32_t  is_fullscreen;
    uint8_t  visible;
    uint8_t  is_input_enabled;
    uint8_t  message_handled;
    uint8_t  pointer_entered;
    volatile int32_t closed;
    UwpCoreWindowFlowDirection flow_direction;
    UwpCoreWindowActivationMode activation_mode;
    UwpPoint pointer_pos;
    uint32_t pointer_frame_id;
    uint8_t  key_states[256]; /* host test key state table */
    wchar_t  title[256];

#ifdef _WIN32
    DWORD saved_style;
    DWORD saved_ex_style;
    RECT  saved_window_rect;
#endif

    /* Owned sub-objects */
    UwpIMap *custom_properties;
    IUnknown *pointer_cursor;
    IUnknown *swapchain_ref;

    /* Dispatcher priority queue */
    uwp_mutex_t queue_lock;
    uwp_dispatch_item_t *queue_head;

    /* CoreWindow & CoreDispatcher WinRT event listener lists */
    uwp_event_list_t ev_activated;
    uwp_event_list_t ev_automation;
    uwp_event_list_t ev_char_received;
    uwp_event_list_t ev_closed;
    uwp_event_list_t ev_input_enabled;
    uwp_event_list_t ev_key_down;
    uwp_event_list_t ev_key_up;
    uwp_event_list_t ev_pointer_capture_lost;
    uwp_event_list_t ev_pointer_entered;
    uwp_event_list_t ev_pointer_exited;
    uwp_event_list_t ev_pointer_moved;
    uwp_event_list_t ev_pointer_pressed;
    uwp_event_list_t ev_pointer_released;
    uwp_event_list_t ev_touch_hit_testing;
    uwp_event_list_t ev_pointer_wheel;
    uwp_event_list_t ev_size_changed;
    uwp_event_list_t ev_visibility_changed;
    uwp_event_list_t ev_closest_bounds;
    uwp_event_list_t ev_resize_started;
    uwp_event_list_t ev_resize_completed;
    uwp_event_list_t ev_accel_key;
};

/* ============================================================================
 * Global Subsystem State (Main Window + TLS Binding)
 * ============================================================================ */

static uwp_mutex_t g_cw_global_lock;
static int g_cw_global_lock_ready = 0;
static UwpCoreWindowObj *g_main_core_window = NULL;
static uint32_t g_default_width = 1280u;
static uint32_t g_default_height = 720u;

#ifdef _WIN32
static DWORD g_cw_tls_index = TLS_OUT_OF_INDEXES;
static volatile LONG g_wndclass_registered = 0;
static const wchar_t k_wndclass_name[] = L"ProtonScarlettUwpCoreWindow";
#else
static pthread_key_t g_cw_tls_key;
static int g_cw_tls_ready = 0;
#endif

static void uwp_cw_ensure_global_init(void) {
    if (!g_cw_global_lock_ready) {
        uwp_mutex_init(&g_cw_global_lock);
        g_cw_global_lock_ready = 1;
    }
#ifdef _WIN32
    if (g_cw_tls_index == TLS_OUT_OF_INDEXES) {
        g_cw_tls_index = TlsAlloc();
    }
#else
    if (!g_cw_tls_ready) {
        pthread_key_create(&g_cw_tls_key, NULL);
        g_cw_tls_ready = 1;
    }
#endif
}

void uwp_corewindow_subsystem_init(void) {
    uwp_cw_ensure_global_init();
}

void uwp_corewindow_subsystem_shutdown(void) {
    if (!g_cw_global_lock_ready) return;
    uwp_mutex_lock(&g_cw_global_lock);
    UwpCoreWindowObj *main_w = g_main_core_window;
    g_main_core_window = NULL;
    uwp_mutex_unlock(&g_cw_global_lock);
    if (main_w) {
        main_w->core_window.lpVtbl->Release(&main_w->core_window);
    }
}

void uwp_corewindow_set_default_size(uint32_t width, uint32_t height) {
    if (width > 0) g_default_width = width;
    if (height > 0) g_default_height = height;
}

void uwp_corewindow_bind_current_thread(UwpCoreWindowObj *win) {
    uwp_cw_ensure_global_init();
    if (win) {
        win->owner_thread_id = uwp_current_thread_id();
    }
#ifdef _WIN32
    if (g_cw_tls_index != TLS_OUT_OF_INDEXES) {
        TlsSetValue(g_cw_tls_index, win);
    }
#else
    if (g_cw_tls_ready) {
        pthread_setspecific(g_cw_tls_key, win);
    }
#endif
}

UwpCoreWindowObj *uwp_corewindow_get_for_current_thread(void) {
    uwp_cw_ensure_global_init();
#ifdef _WIN32
    if (g_cw_tls_index != TLS_OUT_OF_INDEXES) {
        UwpCoreWindowObj *tls_w = (UwpCoreWindowObj *)TlsGetValue(g_cw_tls_index);
        if (tls_w) return tls_w;
    }
#else
    if (g_cw_tls_ready) {
        UwpCoreWindowObj *tls_w = (UwpCoreWindowObj *)pthread_getspecific(g_cw_tls_key);
        if (tls_w) return tls_w;
    }
#endif
    /* Check if the calling thread is the owner of the main CoreWindow */
    uwp_mutex_lock(&g_cw_global_lock);
    UwpCoreWindowObj *res = NULL;
    if (g_main_core_window &&
        g_main_core_window->owner_thread_id == uwp_current_thread_id()) {
        res = g_main_core_window;
    }
    uwp_mutex_unlock(&g_cw_global_lock);
    return res;
}

UwpCoreWindowObj *uwp_corewindow_get_main(void) {
    uwp_cw_ensure_global_init();
    uwp_mutex_lock(&g_cw_global_lock);
    UwpCoreWindowObj *w = g_main_core_window;
    uwp_mutex_unlock(&g_cw_global_lock);
    return w;
}

/* ============================================================================
 * Helpers: Timestamps, PhysicalKeyStatus, VirtualKeyModifiers
 * ============================================================================ */

static uint64_t uwp_now_usec(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, cnt;
    if (QueryPerformanceFrequency(&freq) && freq.QuadPart > 0 && QueryPerformanceCounter(&cnt)) {
        return (uint64_t)((cnt.QuadPart * 1000000LL) / freq.QuadPart);
    }
    return (uint64_t)GetTickCount64() * 1000u;
#else
    return 0u;
#endif
}

static UwpCorePhysicalKeyStatus uwp_decode_key_status(LPARAM lParam, int is_keyup) {
    UwpCorePhysicalKeyStatus st;
    uintptr_t lp = (uintptr_t)lParam;
    st.RepeatCount   = (uint32_t)(lp & 0xFFFFu);
    st.ScanCode      = (uint32_t)((lp >> 16) & 0xFFu);
    st.IsExtendedKey = (uint8_t)((lp >> 24) & 1u);
    st.IsMenuKeyDown = (uint8_t)((lp >> 29) & 1u);
    st.WasKeyDown    = is_keyup ? 1u : (uint8_t)((lp >> 30) & 1u);
    st.IsKeyReleased = is_keyup ? 1u : (uint8_t)((lp >> 31) & 1u);
    return st;
}

static UwpVirtualKeyModifiers uwp_mods_from_wparam(WPARAM wParam) {
    uint32_t mods = UWP_MOD_NONE;
    if (wParam & MK_CONTROL) mods |= UWP_MOD_CONTROL;
    if (wParam & MK_SHIFT)   mods |= UWP_MOD_SHIFT;
#ifdef _WIN32
    if (GetKeyState(VK_MENU) < 0) mods |= UWP_MOD_MENU;
#endif
    return (UwpVirtualKeyModifiers)mods;
}

/* ============================================================================
 * CoreDispatcher Queue Helpers
 * ============================================================================ */

static HRESULT uwp_enqueue_work(UwpCoreWindowObj *win,
                                UwpCoreDispatcherPriority priority,
                                int is_idle,
                                UwpDispatchedHandler *cb,
                                UwpIdleDispatchedHandler *idle_cb,
                                UwpIAsyncAction **out_action) {
    if (!win || (!cb && !idle_cb)) {
        return E_INVALIDARG;
    }
    if (out_action) *out_action = NULL;

    uwp_dispatch_item_t *item = (uwp_dispatch_item_t *)calloc(1, sizeof(*item));
    UwpAsyncActionObj *aa = uwp_async_action_create();
    if (!item || !aa) {
        free(item);
        if (aa) uwp_async_action_release(aa);
        return E_OUTOFMEMORY;
    }

    item->priority = priority;
    item->is_idle = is_idle;
    item->callback = cb;
    item->idle_callback = idle_cb;
    item->async_action = aa; /* retains ref 1 */
    if (cb) cb->lpVtbl->AddRef(cb);
    if (idle_cb) idle_cb->lpVtbl->AddRef(idle_cb);

    /* Priority-ordered insertion: Higher priority values run first; FIFO within same priority */
    uwp_mutex_lock(&win->queue_lock);
    uwp_dispatch_item_t **pp = &win->queue_head;
    while (*pp != NULL && (*pp)->priority >= priority) {
        pp = &(*pp)->next;
    }
    item->next = *pp;
    *pp = item;
    uwp_mutex_unlock(&win->queue_lock);

    if (out_action) {
        *out_action = uwp_async_action_iface(aa);
    }

#ifdef _WIN32
    if (win->hwnd && !win->headless) {
        PostMessageW(win->hwnd, WM_UWP_DISPATCH_WORK, 0, 0);
    }
#endif
    return S_OK;
}

uint32_t uwp_corewindow_drain_dispatched_work(UwpCoreWindowObj *win, uint32_t max_items) {
    if (!win) return 0u;
    uint32_t processed = 0u;
    while (max_items == 0u || processed < max_items) {
        uwp_mutex_lock(&win->queue_lock);
        uwp_dispatch_item_t *item = win->queue_head;
        if (item) {
            win->queue_head = item->next;
            item->next = NULL;
        }
        uwp_mutex_unlock(&win->queue_lock);

        if (!item) break;

        HRESULT hr = S_OK;
        if (item->is_idle && item->idle_callback) {
            UwpIIdleDispatchedHandlerArgs *idle_args = uwp_create_idle_dispatched_args(1u);
            hr = item->idle_callback->lpVtbl->Invoke(item->idle_callback, idle_args);
            if (idle_args) idle_args->lpVtbl->Release(idle_args);
            item->idle_callback->lpVtbl->Release(item->idle_callback);
        } else if (item->callback) {
            hr = item->callback->lpVtbl->Invoke(item->callback);
            item->callback->lpVtbl->Release(item->callback);
        }

        uwp_async_action_complete(item->async_action, hr);
        uwp_async_action_release(item->async_action);
        free(item);
        ++processed;
    }
    return processed;
}

static int uwp_has_dispatched_work(UwpCoreWindowObj *win) {
    if (!win) return 0;
    uwp_mutex_lock(&win->queue_lock);
    int has = (win->queue_head != NULL);
    uwp_mutex_unlock(&win->queue_lock);
    return has;
}

/* ============================================================================
 * Portable Win32 Message -> WinRT Event Translation
 * ============================================================================ */

static void uwp_fire_pointer_event(UwpCoreWindowObj *win,
                                   uwp_event_list_t *list,
                                   float x, float y,
                                   WPARAM wParam,
                                   UwpPointerUpdateKind update_kind,
                                   int32_t wheel_delta,
                                   uint8_t horizontal_wheel,
                                   int force_contact) {
    uwp_pointer_event_params_t p;
    memset(&p, 0, sizeof(p));
    p.pointer_id = 1u;
    p.frame_id = ++win->pointer_frame_id;
    p.timestamp_us = uwp_now_usec();
    p.x = x;
    p.y = y;
    p.left_button   = (wParam & MK_LBUTTON)  ? 1u : 0u;
    p.right_button  = (wParam & MK_RBUTTON)  ? 1u : 0u;
    p.middle_button = (wParam & MK_MBUTTON)  ? 1u : 0u;
    p.xbutton1      = (wParam & MK_XBUTTON1) ? 1u : 0u;
    p.xbutton2      = (wParam & MK_XBUTTON2) ? 1u : 0u;
    if (update_kind == UWP_POINTER_UPDATE_LEFT_BUTTON_PRESSED)    p.left_button = 1u;
    if (update_kind == UWP_POINTER_UPDATE_RIGHT_BUTTON_PRESSED)   p.right_button = 1u;
    if (update_kind == UWP_POINTER_UPDATE_MIDDLE_BUTTON_PRESSED)  p.middle_button = 1u;
    if (update_kind == UWP_POINTER_UPDATE_XBUTTON1_PRESSED)       p.xbutton1 = 1u;
    if (update_kind == UWP_POINTER_UPDATE_XBUTTON2_PRESSED)       p.xbutton2 = 1u;
    p.in_contact = (force_contact || p.left_button || p.right_button || p.middle_button ||
                    p.xbutton1 || p.xbutton2) ? 1u : 0u;
    p.wheel_delta = wheel_delta;
    p.horizontal_wheel = horizontal_wheel;
    p.update_kind = update_kind;
    p.key_modifiers = uwp_mods_from_wparam(wParam);

    win->pointer_pos.X = x;
    win->pointer_pos.Y = y;

    UwpIPointerEventArgs *args = uwp_create_pointer_event_args(&p);
    if (args) {
        uwp_event_list_invoke(list, &win->core_window, args);
        args->lpVtbl->Release(args);
    }
}

int uwp_corewindow_handle_message(UwpCoreWindowObj *win, uint32_t msg, WPARAM wParam, LPARAM lParam) {
    if (!win) return 0;

    switch (msg) {
        case WM_UWP_DISPATCH_WORK:
            uwp_corewindow_drain_dispatched_work(win, 0u);
            return 1;

        case WM_SIZE: {
            uint32_t w = (uint32_t)LOWORD(lParam);
            uint32_t h = (uint32_t)HIWORD(lParam);
            if (wParam == SIZE_MINIMIZED) {
                win->visible = 0u;
                UwpIVisibilityChangedEventArgs *vargs = uwp_create_visibility_changed_args(0u);
                if (vargs) {
                    uwp_event_list_invoke(&win->ev_visibility_changed, &win->core_window, vargs);
                    vargs->lpVtbl->Release(vargs);
                }
                return 1;
            }
            if (w > 0 && h > 0) {
                uwp_mutex_lock(&win->lock);
                win->client_width = w;
                win->client_height = h;
                uwp_mutex_unlock(&win->lock);

                UwpIWindowSizeChangedEventArgs *sargs =
                    uwp_create_size_changed_args((float)w, (float)h);
                if (sargs) {
                    uwp_event_list_invoke(&win->ev_size_changed, &win->core_window, sargs);
                    sargs->lpVtbl->Release(sargs);
                }
            }
            return 1;
        }

        case WM_ENTERSIZEMOVE:
            uwp_event_list_invoke(&win->ev_resize_started, &win->core_window, NULL);
            return 1;

        case WM_EXITSIZEMOVE:
            uwp_event_list_invoke(&win->ev_resize_completed, &win->core_window, NULL);
            return 1;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            int32_t vk = (int32_t)(wParam & 0xFFFFu);
            if (vk >= 0 && vk < 256) win->key_states[vk] |= UWP_KEYSTATE_DOWN;
            UwpCorePhysicalKeyStatus st = uwp_decode_key_status(lParam, 0);
            UwpIKeyEventArgs *kargs = uwp_create_key_event_args(vk, &st);
            if (kargs) {
                uwp_event_list_invoke(&win->ev_key_down, &win->core_window, kargs);
                kargs->lpVtbl->Release(kargs);
            }
            UwpCoreAcceleratorKeyEventType atype =
                (msg == WM_SYSKEYDOWN) ? UWP_ACCEL_SYSTEM_KEY_DOWN : UWP_ACCEL_KEY_DOWN;
            UwpIAcceleratorKeyEventArgs *aargs = uwp_create_accel_key_args(atype, vk, &st);
            if (aargs) {
                uwp_event_list_invoke(&win->ev_accel_key, &win->dispatcher, aargs);
                aargs->lpVtbl->Release(aargs);
            }
            return 1;
        }

        case WM_KEYUP:
        case WM_SYSKEYUP: {
            int32_t vk = (int32_t)(wParam & 0xFFFFu);
            if (vk >= 0 && vk < 256) win->key_states[vk] &= (uint8_t)~UWP_KEYSTATE_DOWN;
            UwpCorePhysicalKeyStatus st = uwp_decode_key_status(lParam, 1);
            UwpIKeyEventArgs *kargs = uwp_create_key_event_args(vk, &st);
            if (kargs) {
                uwp_event_list_invoke(&win->ev_key_up, &win->core_window, kargs);
                kargs->lpVtbl->Release(kargs);
            }
            UwpCoreAcceleratorKeyEventType atype =
                (msg == WM_SYSKEYUP) ? UWP_ACCEL_SYSTEM_KEY_UP : UWP_ACCEL_KEY_UP;
            UwpIAcceleratorKeyEventArgs *aargs = uwp_create_accel_key_args(atype, vk, &st);
            if (aargs) {
                uwp_event_list_invoke(&win->ev_accel_key, &win->dispatcher, aargs);
                aargs->lpVtbl->Release(aargs);
            }
            return 1;
        }

        case WM_CHAR:
        case WM_SYSCHAR: {
            uint32_t ch = (uint32_t)(wParam & 0xFFFFu);
            UwpCorePhysicalKeyStatus st = uwp_decode_key_status(lParam, 0);
            UwpICharacterReceivedEventArgs *cargs = uwp_create_char_received_args(ch, &st);
            if (cargs) {
                uwp_event_list_invoke(&win->ev_char_received, &win->core_window, cargs);
                cargs->lpVtbl->Release(cargs);
            }
            UwpCoreAcceleratorKeyEventType atype =
                (msg == WM_SYSCHAR) ? UWP_ACCEL_SYSTEM_CHARACTER : UWP_ACCEL_CHARACTER;
            UwpIAcceleratorKeyEventArgs *aargs = uwp_create_accel_key_args(atype, (int32_t)ch, &st);
            if (aargs) {
                uwp_event_list_invoke(&win->ev_accel_key, &win->dispatcher, aargs);
                aargs->lpVtbl->Release(aargs);
            }
            return 1;
        }

        case WM_MOUSEMOVE: {
            float x = (float)(int16_t)LOWORD(lParam);
            float y = (float)(int16_t)HIWORD(lParam);
            if (!win->pointer_entered) {
                win->pointer_entered = 1u;
                uwp_fire_pointer_event(win, &win->ev_pointer_entered, x, y, wParam,
                                       UWP_POINTER_UPDATE_OTHER, 0, 0u, 0);
            }
            uwp_fire_pointer_event(win, &win->ev_pointer_moved, x, y, wParam,
                                   UWP_POINTER_UPDATE_OTHER, 0, 0u, 0);
            return 1;
        }

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN: {
            float x = (float)(int16_t)LOWORD(lParam);
            float y = (float)(int16_t)HIWORD(lParam);
            UwpPointerUpdateKind uk = UWP_POINTER_UPDATE_LEFT_BUTTON_PRESSED;
            if (msg == WM_RBUTTONDOWN) uk = UWP_POINTER_UPDATE_RIGHT_BUTTON_PRESSED;
            else if (msg == WM_MBUTTONDOWN) uk = UWP_POINTER_UPDATE_MIDDLE_BUTTON_PRESSED;
            else if (msg == WM_XBUTTONDOWN) {
                uk = (HIWORD(wParam) == XBUTTON2) ? UWP_POINTER_UPDATE_XBUTTON2_PRESSED
                                                  : UWP_POINTER_UPDATE_XBUTTON1_PRESSED;
            }
            uwp_fire_pointer_event(win, &win->ev_pointer_pressed, x, y, wParam, uk, 0, 0u, 1);
            return 1;
        }

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_XBUTTONUP: {
            float x = (float)(int16_t)LOWORD(lParam);
            float y = (float)(int16_t)HIWORD(lParam);
            UwpPointerUpdateKind uk = UWP_POINTER_UPDATE_LEFT_BUTTON_RELEASED;
            if (msg == WM_RBUTTONUP) uk = UWP_POINTER_UPDATE_RIGHT_BUTTON_RELEASED;
            else if (msg == WM_MBUTTONUP) uk = UWP_POINTER_UPDATE_MIDDLE_BUTTON_RELEASED;
            else if (msg == WM_XBUTTONUP) {
                uk = (HIWORD(wParam) == XBUTTON2) ? UWP_POINTER_UPDATE_XBUTTON2_RELEASED
                                                  : UWP_POINTER_UPDATE_XBUTTON1_RELEASED;
            }
            uwp_fire_pointer_event(win, &win->ev_pointer_released, x, y, wParam, uk, 0, 0u, 0);
            return 1;
        }

        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL: {
            int32_t delta = (int32_t)(int16_t)HIWORD(wParam);
            int16_t sx = (int16_t)LOWORD(lParam);
            int16_t sy = (int16_t)HIWORD(lParam);
            float x = (float)sx;
            float y = (float)sy;
#ifdef _WIN32
            if (win->hwnd && !win->headless) {
                POINT pt = { sx, sy };
                if (ScreenToClient(win->hwnd, &pt)) {
                    x = (float)pt.x;
                    y = (float)pt.y;
                }
            }
#endif
            uwp_fire_pointer_event(win, &win->ev_pointer_wheel, x, y,
                                   (WPARAM)LOWORD(wParam),
                                   UWP_POINTER_UPDATE_OTHER,
                                   delta,
                                   (msg == WM_MOUSEHWHEEL) ? 1u : 0u,
                                   0);
            return 1;
        }

        case WM_ACTIVATE: {
            uint16_t active = LOWORD(wParam);
            UwpCoreWindowActivationState astate;
            if (active == WA_INACTIVE) {
                astate = UWP_ACTIVATION_DEACTIVATED;
                win->activation_mode = UWP_ACTIVATION_MODE_DEACTIVATED;
            } else if (active == WA_CLICKACTIVE) {
                astate = UWP_ACTIVATION_POINTER_ACTIVATED;
                win->activation_mode = UWP_ACTIVATION_MODE_ACTIVATED_IN_FOREGROUND;
            } else {
                astate = UWP_ACTIVATION_CODE_ACTIVATED;
                win->activation_mode = UWP_ACTIVATION_MODE_ACTIVATED_IN_FOREGROUND;
            }
            UwpIWindowActivatedEventArgs *args = uwp_create_activated_args(astate);
            if (args) {
                uwp_event_list_invoke(&win->ev_activated, &win->core_window, args);
                args->lpVtbl->Release(args);
            }
            return 1;
        }

        case WM_SHOWWINDOW: {
            uint8_t vis = (wParam != 0) ? 1u : 0u;
            win->visible = vis;
            UwpIVisibilityChangedEventArgs *vargs = uwp_create_visibility_changed_args(vis);
            if (vargs) {
                uwp_event_list_invoke(&win->ev_visibility_changed, &win->core_window, vargs);
                vargs->lpVtbl->Release(vargs);
            }
            return 1;
        }

        case WM_CLOSE: {
            win->closed = 1;
            UwpICoreWindowEventArgs *cargs = uwp_create_core_window_event_args();
            if (cargs) {
                uwp_event_list_invoke(&win->ev_closed, &win->core_window, cargs);
                cargs->lpVtbl->Release(cargs);
            }
#ifdef _WIN32
            if (win->hwnd && !win->headless) {
                PostQuitMessage(0);
            }
#endif
            return 1;
        }

        default:
            return 0;
    }
}

#ifdef _WIN32
static LRESULT CALLBACK uwp_corewindow_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    UwpCoreWindowObj *win = NULL;
    if (msg == WM_NCCREATE) {
        const CREATESTRUCTW *cs = (const CREATESTRUCTW *)lParam;
        win = (UwpCoreWindowObj *)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)win);
        if (win) {
            win->hwnd = hwnd;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    win = (UwpCoreWindowObj *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (win) {
        if (uwp_corewindow_handle_message(win, msg, wParam, lParam)) {
            if (msg == WM_CLOSE) {
                DestroyWindow(hwnd);
                win->hwnd = NULL;
                return 0;
            }
            if (msg == WM_UWP_DISPATCH_WORK) {
                return 0;
            }
        }
        if (msg == WM_DESTROY) {
            win->closed = 1;
            win->hwnd = NULL;
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static int uwp_ensure_wndclass_registered(void) {
    if (InterlockedCompareExchange(&g_wndclass_registered, 1, 0) == 0) {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = uwp_corewindow_wndproc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = k_wndclass_name;
        ATOM a = RegisterClassExW(&wc);
        if (a == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            uwp_bridge_log("corewindow", "RegisterClassExW failed err=%lu", (unsigned long)GetLastError());
            InterlockedExchange(&g_wndclass_registered, 0);
            return 0;
        }
        InterlockedExchange(&g_wndclass_registered, 2);
    } else {
        while (InterlockedCompareExchange(&g_wndclass_registered, 2, 2) != 2) {
            Sleep(0);
        }
    }
    return 1;
}
#endif /* _WIN32 */

/* ============================================================================
 * CoreWindow Lifetime & Unified QueryInterface
 * ============================================================================ */

static void cw_destroy(UwpCoreWindowObj *win) {
#ifdef _WIN32
    if (win->hwnd && !win->headless) {
        SetWindowLongPtrW(win->hwnd, GWLP_USERDATA, 0);
        DestroyWindow(win->hwnd);
        win->hwnd = NULL;
    }
#endif
    /* Cancel any remaining queued work items */
    uwp_mutex_lock(&win->queue_lock);
    uwp_dispatch_item_t *it = win->queue_head;
    win->queue_head = NULL;
    uwp_mutex_unlock(&win->queue_lock);
    while (it) {
        uwp_dispatch_item_t *next = it->next;
        if (it->callback) it->callback->lpVtbl->Release(it->callback);
        if (it->idle_callback) it->idle_callback->lpVtbl->Release(it->idle_callback);
        uwp_async_action_complete(it->async_action, E_ABORT);
        uwp_async_action_release(it->async_action);
        free(it);
        it = next;
    }

    if (win->custom_properties) win->custom_properties->lpVtbl->Release(win->custom_properties);
    if (win->pointer_cursor) win->pointer_cursor->lpVtbl->Release(win->pointer_cursor);
    if (win->swapchain_ref) win->swapchain_ref->lpVtbl->Release(win->swapchain_ref);

    uwp_event_list_destroy(&win->ev_activated);
    uwp_event_list_destroy(&win->ev_automation);
    uwp_event_list_destroy(&win->ev_char_received);
    uwp_event_list_destroy(&win->ev_closed);
    uwp_event_list_destroy(&win->ev_input_enabled);
    uwp_event_list_destroy(&win->ev_key_down);
    uwp_event_list_destroy(&win->ev_key_up);
    uwp_event_list_destroy(&win->ev_pointer_capture_lost);
    uwp_event_list_destroy(&win->ev_pointer_entered);
    uwp_event_list_destroy(&win->ev_pointer_exited);
    uwp_event_list_destroy(&win->ev_pointer_moved);
    uwp_event_list_destroy(&win->ev_pointer_pressed);
    uwp_event_list_destroy(&win->ev_pointer_released);
    uwp_event_list_destroy(&win->ev_touch_hit_testing);
    uwp_event_list_destroy(&win->ev_pointer_wheel);
    uwp_event_list_destroy(&win->ev_size_changed);
    uwp_event_list_destroy(&win->ev_visibility_changed);
    uwp_event_list_destroy(&win->ev_closest_bounds);
    uwp_event_list_destroy(&win->ev_resize_started);
    uwp_event_list_destroy(&win->ev_resize_completed);
    uwp_event_list_destroy(&win->ev_accel_key);

    uwp_mutex_destroy(&win->queue_lock);
    uwp_mutex_destroy(&win->lock);
    free(win);
}

static HRESULT cw_qi_impl(UwpCoreWindowObj *win, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (uwp_guid_equal(riid, &UWP_IID_IUnknown) ||
        uwp_guid_equal(riid, &UWP_IID_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(riid, &UWP_IID_ICoreWindow)) {
        uwp_atomic_inc(&win->ref);
        *ppv = &win->core_window;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ICoreWindow2)) {
        uwp_atomic_inc(&win->ref);
        *ppv = &win->core_window2;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ICoreWindow3)) {
        uwp_atomic_inc(&win->ref);
        *ppv = &win->core_window3;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ICoreWindow4)) {
        uwp_atomic_inc(&win->ref);
        *ppv = &win->core_window4;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ICoreWindow5)) {
        uwp_atomic_inc(&win->ref);
        *ppv = &win->core_window5;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &SCARLETT_IID_ICoreWindowInterop)) {
        uwp_atomic_inc(&win->ref);
        *ppv = &win->interop;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &SCARLETT_IID_IProtonScarlettCoreWindowBridge)) {
        uwp_atomic_inc(&win->ref);
        *ppv = &win->bridge;
        return S_OK;
    }
    return E_NOINTERFACE;
}

static HRESULT disp_qi_impl(UwpCoreWindowObj *win, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (uwp_guid_equal(riid, &UWP_IID_IUnknown) ||
        uwp_guid_equal(riid, &UWP_IID_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(riid, &UWP_IID_ICoreDispatcher)) {
        uwp_atomic_inc(&win->ref);
        *ppv = &win->dispatcher;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ICoreAcceleratorKeys)) {
        uwp_atomic_inc(&win->ref);
        *ppv = &win->accel_keys;
        return S_OK;
    }
    return E_NOINTERFACE;
}

/* ============================================================================
 * ICoreWindow Methods
 * ============================================================================ */

static HRESULT UWP_CALL cw_QueryInterface(UwpICoreWindow *T, REFIID r, void **p) {
    return cw_qi_impl(UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window), r, p);
}
static ULONG UWP_CALL cw_AddRef(UwpICoreWindow *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window)->ref);
}
static ULONG UWP_CALL cw_Release(UwpICoreWindow *T) {
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window);
    int32_t r = uwp_atomic_dec(&w->ref);
    if (r == 0) cw_destroy(w);
    return (ULONG)r;
}
static HRESULT UWP_CALL cw_GetIids(UwpICoreWindow *T, ULONG *c, IID **i) {
    (void)T; if (!c || !i) return E_INVALIDARG; *c = 0; *i = NULL; return S_OK;
}
static HRESULT UWP_CALL cw_GetRuntimeClassName(UwpICoreWindow *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.CoreWindow", n);
}
static HRESULT UWP_CALL cw_GetTrustLevel(UwpICoreWindow *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL cw_get_AutomationHostProvider(UwpICoreWindow *T, UwpIInspectable **v) {
    (void)T; if (!v) return E_INVALIDARG; *v = NULL; return S_OK;
}
static HRESULT UWP_CALL cw_get_Bounds(UwpICoreWindow *T, UwpRect *v) {
    if (!v) return E_INVALIDARG;
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window);
#ifdef _WIN32
    if (w->hwnd && !w->headless) {
        RECT cr;
        if (GetClientRect(w->hwnd, &cr)) {
            POINT origin = { cr.left, cr.top };
            ClientToScreen(w->hwnd, &origin);
            v->X = (float)origin.x;
            v->Y = (float)origin.y;
            v->Width = (float)(cr.right - cr.left);
            v->Height = (float)(cr.bottom - cr.top);
            return S_OK;
        }
    }
#endif
    uwp_mutex_lock(&w->lock);
    v->X = 0.0f;
    v->Y = 0.0f;
    v->Width = (float)w->client_width;
    v->Height = (float)w->client_height;
    uwp_mutex_unlock(&w->lock);
    return S_OK;
}
static HRESULT UWP_CALL cw_get_CustomProperties(UwpICoreWindow *T, void **v) {
    if (!v) return E_INVALIDARG;
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window);
    uwp_mutex_lock(&w->lock);
    if (!w->custom_properties) {
        w->custom_properties = uwp_create_property_set();
    }
    *v = w->custom_properties;
    if (w->custom_properties) w->custom_properties->lpVtbl->AddRef(w->custom_properties);
    uwp_mutex_unlock(&w->lock);
    return *v ? S_OK : E_OUTOFMEMORY;
}
static HRESULT UWP_CALL cw_get_Dispatcher(UwpICoreWindow *T, void **v) {
    if (!v) return E_INVALIDARG;
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window);
    uwp_atomic_inc(&w->ref);
    *v = &w->dispatcher;
    return S_OK;
}
static HRESULT UWP_CALL cw_get_FlowDirection(UwpICoreWindow *T, UwpCoreWindowFlowDirection *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window)->flow_direction;
    return S_OK;
}
static HRESULT UWP_CALL cw_put_FlowDirection(UwpICoreWindow *T, UwpCoreWindowFlowDirection v) {
    UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window)->flow_direction = v;
    return S_OK;
}
static HRESULT UWP_CALL cw_get_IsInputEnabled(UwpICoreWindow *T, uint8_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window)->is_input_enabled;
    return S_OK;
}
static HRESULT UWP_CALL cw_put_IsInputEnabled(UwpICoreWindow *T, uint8_t v) {
    UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window)->is_input_enabled = v ? 1u : 0u;
    return S_OK;
}
static HRESULT UWP_CALL cw_get_PointerCursor(UwpICoreWindow *T, void **v) {
    if (!v) return E_INVALIDARG;
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window);
    uwp_mutex_lock(&w->lock);
    *v = w->pointer_cursor;
    if (w->pointer_cursor) w->pointer_cursor->lpVtbl->AddRef(w->pointer_cursor);
    uwp_mutex_unlock(&w->lock);
    return S_OK;
}
static HRESULT UWP_CALL cw_put_PointerCursor(UwpICoreWindow *T, void *v) {
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window);
    IUnknown *new_cur = (IUnknown *)v;
    uwp_mutex_lock(&w->lock);
    IUnknown *old_cur = w->pointer_cursor;
    if (new_cur) new_cur->lpVtbl->AddRef(new_cur);
    w->pointer_cursor = new_cur;
    uwp_mutex_unlock(&w->lock);
    if (old_cur) old_cur->lpVtbl->Release(old_cur);
#ifdef _WIN32
    if (!w->headless) {
        if (!new_cur) {
            SetCursor(NULL);
        } else {
            SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_ARROW));
        }
    }
#endif
    return S_OK;
}
static HRESULT UWP_CALL cw_get_PointerPosition(UwpICoreWindow *T, UwpPoint *v) {
    if (!v) return E_INVALIDARG;
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window);
#ifdef _WIN32
    if (!w->headless) {
        POINT pt;
        if (GetCursorPos(&pt)) {
            v->X = (float)pt.x;
            v->Y = (float)pt.y;
            return S_OK;
        }
    }
#endif
    *v = w->pointer_pos;
    return S_OK;
}
static HRESULT UWP_CALL cw_get_Visible(UwpICoreWindow *T, uint8_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window)->visible;
    return S_OK;
}
static HRESULT UWP_CALL cw_Activate(UwpICoreWindow *T) {
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window);
    w->visible = 1u;
    w->activation_mode = UWP_ACTIVATION_MODE_ACTIVATED_IN_FOREGROUND;
#ifdef _WIN32
    if (w->hwnd && !w->headless) {
        ShowWindow(w->hwnd, SW_SHOW);
        SetForegroundWindow(w->hwnd);
        UpdateWindow(w->hwnd);
    }
#endif
    UwpIVisibilityChangedEventArgs *vargs = uwp_create_visibility_changed_args(1u);
    if (vargs) {
        uwp_event_list_invoke(&w->ev_visibility_changed, &w->core_window, vargs);
        vargs->lpVtbl->Release(vargs);
    }
    UwpIWindowActivatedEventArgs *aargs = uwp_create_activated_args(UWP_ACTIVATION_CODE_ACTIVATED);
    if (aargs) {
        uwp_event_list_invoke(&w->ev_activated, &w->core_window, aargs);
        aargs->lpVtbl->Release(aargs);
    }
    return S_OK;
}
static HRESULT UWP_CALL cw_Close(UwpICoreWindow *T) {
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window);
    uwp_corewindow_request_close(w);
    return S_OK;
}
static HRESULT UWP_CALL cw_GetAsyncKeyState(UwpICoreWindow *T, int32_t vk, UwpCoreVirtualKeyStates *st) {
    if (!st) return E_INVALIDARG;
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window);
#ifdef _WIN32
    if (!w->headless) {
        SHORT s = GetAsyncKeyState(vk);
        uint32_t out = UWP_KEYSTATE_NONE;
        if (s & 0x8000) out |= UWP_KEYSTATE_DOWN;
        if (s & 0x0001) out |= UWP_KEYSTATE_LOCKED;
        *st = (UwpCoreVirtualKeyStates)out;
        return S_OK;
    }
#endif
    *st = (vk >= 0 && vk < 256) ? (UwpCoreVirtualKeyStates)w->key_states[vk] : UWP_KEYSTATE_NONE;
    return S_OK;
}
static HRESULT UWP_CALL cw_GetKeyState(UwpICoreWindow *T, int32_t vk, UwpCoreVirtualKeyStates *st) {
    if (!st) return E_INVALIDARG;
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window);
#ifdef _WIN32
    if (!w->headless) {
        SHORT s = GetKeyState(vk);
        uint32_t out = UWP_KEYSTATE_NONE;
        if (s & 0x8000) out |= UWP_KEYSTATE_DOWN;
        if (s & 0x0001) out |= UWP_KEYSTATE_LOCKED;
        *st = (UwpCoreVirtualKeyStates)out;
        return S_OK;
    }
#endif
    *st = (vk >= 0 && vk < 256) ? (UwpCoreVirtualKeyStates)w->key_states[vk] : UWP_KEYSTATE_NONE;
    return S_OK;
}
static HRESULT UWP_CALL cw_ReleasePointerCapture(UwpICoreWindow *T) {
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window);
#ifdef _WIN32
    if (w->hwnd && !w->headless) {
        ReleaseCapture();
    }
#else
    (void)w;
#endif
    return S_OK;
}
static HRESULT UWP_CALL cw_SetPointerCapture(UwpICoreWindow *T) {
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window);
#ifdef _WIN32
    if (w->hwnd && !w->headless) {
        SetCapture(w->hwnd);
    }
#else
    (void)w;
#endif
    return S_OK;
}

#define DEFINE_CW_EVENT_METHODS(Name, Field)                                                      \
static HRESULT UWP_CALL cw_add_##Name(UwpICoreWindow *T, UwpTypedEventHandler *h,                 \
                                      UwpEventRegistrationToken *tok) {                           \
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window);                \
    return uwp_event_list_add(&w->Field, h, tok);                                                 \
}                                                                                                 \
static HRESULT UWP_CALL cw_remove_##Name(UwpICoreWindow *T, UwpEventRegistrationToken tok) {      \
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window);                \
    return uwp_event_list_remove(&w->Field, tok);                                                 \
}

DEFINE_CW_EVENT_METHODS(Activated, ev_activated)
DEFINE_CW_EVENT_METHODS(AutomationProviderRequested, ev_automation)
DEFINE_CW_EVENT_METHODS(CharacterReceived, ev_char_received)
DEFINE_CW_EVENT_METHODS(Closed, ev_closed)
DEFINE_CW_EVENT_METHODS(InputEnabled, ev_input_enabled)
DEFINE_CW_EVENT_METHODS(KeyDown, ev_key_down)
DEFINE_CW_EVENT_METHODS(KeyUp, ev_key_up)
DEFINE_CW_EVENT_METHODS(PointerCaptureLost, ev_pointer_capture_lost)
DEFINE_CW_EVENT_METHODS(PointerEntered, ev_pointer_entered)
DEFINE_CW_EVENT_METHODS(PointerExited, ev_pointer_exited)
DEFINE_CW_EVENT_METHODS(PointerMoved, ev_pointer_moved)
DEFINE_CW_EVENT_METHODS(PointerPressed, ev_pointer_pressed)
DEFINE_CW_EVENT_METHODS(PointerReleased, ev_pointer_released)
DEFINE_CW_EVENT_METHODS(TouchHitTesting, ev_touch_hit_testing)
DEFINE_CW_EVENT_METHODS(PointerWheelChanged, ev_pointer_wheel)
DEFINE_CW_EVENT_METHODS(SizeChanged, ev_size_changed)
DEFINE_CW_EVENT_METHODS(VisibilityChanged, ev_visibility_changed)

static const UwpICoreWindowVtbl g_cw_vtbl = {
    cw_QueryInterface, cw_AddRef, cw_Release,
    cw_GetIids, cw_GetRuntimeClassName, cw_GetTrustLevel,
    cw_get_AutomationHostProvider,
    cw_get_Bounds,
    cw_get_CustomProperties,
    cw_get_Dispatcher,
    cw_get_FlowDirection,
    cw_put_FlowDirection,
    cw_get_IsInputEnabled,
    cw_put_IsInputEnabled,
    cw_get_PointerCursor,
    cw_put_PointerCursor,
    cw_get_PointerPosition,
    cw_get_Visible,
    cw_Activate,
    cw_Close,
    cw_GetAsyncKeyState,
    cw_GetKeyState,
    cw_ReleasePointerCapture,
    cw_SetPointerCapture,
    cw_add_Activated, cw_remove_Activated,
    cw_add_AutomationProviderRequested, cw_remove_AutomationProviderRequested,
    cw_add_CharacterReceived, cw_remove_CharacterReceived,
    cw_add_Closed, cw_remove_Closed,
    cw_add_InputEnabled, cw_remove_InputEnabled,
    cw_add_KeyDown, cw_remove_KeyDown,
    cw_add_KeyUp, cw_remove_KeyUp,
    cw_add_PointerCaptureLost, cw_remove_PointerCaptureLost,
    cw_add_PointerEntered, cw_remove_PointerEntered,
    cw_add_PointerExited, cw_remove_PointerExited,
    cw_add_PointerMoved, cw_remove_PointerMoved,
    cw_add_PointerPressed, cw_remove_PointerPressed,
    cw_add_PointerReleased, cw_remove_PointerReleased,
    cw_add_TouchHitTesting, cw_remove_TouchHitTesting,
    cw_add_PointerWheelChanged, cw_remove_PointerWheelChanged,
    cw_add_SizeChanged, cw_remove_SizeChanged,
    cw_add_VisibilityChanged, cw_remove_VisibilityChanged
};

/* ============================================================================
 * ICoreWindow2..5 Methods
 * ============================================================================ */

static HRESULT UWP_CALL cw2_QI(UwpICoreWindow2 *T, REFIID r, void **p) {
    return cw_qi_impl(UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window2), r, p);
}
static ULONG UWP_CALL cw2_AddRef(UwpICoreWindow2 *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window2)->ref);
}
static ULONG UWP_CALL cw2_Release(UwpICoreWindow2 *T) {
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window2);
    int32_t r = uwp_atomic_dec(&w->ref);
    if (r == 0) cw_destroy(w);
    return (ULONG)r;
}
static HRESULT UWP_CALL cw2_GetIids(UwpICoreWindow2 *T, ULONG *c, IID **i) {
    (void)T; if (!c || !i) return E_INVALIDARG; *c = 0; *i = NULL; return S_OK;
}
static HRESULT UWP_CALL cw2_GetName(UwpICoreWindow2 *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.CoreWindow", n);
}
static HRESULT UWP_CALL cw2_GetTrust(UwpICoreWindow2 *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL cw2_put_PointerPosition(UwpICoreWindow2 *T, UwpPoint v) {
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window2);
    w->pointer_pos = v;
#ifdef _WIN32
    if (!w->headless) {
        SetCursorPos((int)v.X, (int)v.Y);
    }
#endif
    return S_OK;
}

static const UwpICoreWindow2Vtbl g_cw2_vtbl = {
    cw2_QI, cw2_AddRef, cw2_Release,
    cw2_GetIids, cw2_GetName, cw2_GetTrust,
    cw2_put_PointerPosition
};

static HRESULT UWP_CALL cw3_QI(UwpICoreWindow3 *T, REFIID r, void **p) {
    return cw_qi_impl(UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window3), r, p);
}
static ULONG UWP_CALL cw3_AddRef(UwpICoreWindow3 *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window3)->ref);
}
static ULONG UWP_CALL cw3_Release(UwpICoreWindow3 *T) {
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window3);
    int32_t r = uwp_atomic_dec(&w->ref);
    if (r == 0) cw_destroy(w);
    return (ULONG)r;
}
static HRESULT UWP_CALL cw3_GetIids(UwpICoreWindow3 *T, ULONG *c, IID **i) {
    (void)T; if (!c || !i) return E_INVALIDARG; *c = 0; *i = NULL; return S_OK;
}
static HRESULT UWP_CALL cw3_GetName(UwpICoreWindow3 *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.CoreWindow", n);
}
static HRESULT UWP_CALL cw3_GetTrust(UwpICoreWindow3 *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL cw3_add_Closest(UwpICoreWindow3 *T, UwpTypedEventHandler *h, UwpEventRegistrationToken *tok) {
    return uwp_event_list_add(&UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window3)->ev_closest_bounds, h, tok);
}
static HRESULT UWP_CALL cw3_remove_Closest(UwpICoreWindow3 *T, UwpEventRegistrationToken tok) {
    return uwp_event_list_remove(&UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window3)->ev_closest_bounds, tok);
}
static HRESULT UWP_CALL cw3_GetCurrentKeyEventDeviceId(UwpICoreWindow3 *T, UWP_HSTRING *v) {
    (void)T; return uwp_hstring_create_from_wstr(L"keyboard:0", v);
}

static const UwpICoreWindow3Vtbl g_cw3_vtbl = {
    cw3_QI, cw3_AddRef, cw3_Release,
    cw3_GetIids, cw3_GetName, cw3_GetTrust,
    cw3_add_Closest, cw3_remove_Closest, cw3_GetCurrentKeyEventDeviceId
};

static HRESULT UWP_CALL cw4_QI(UwpICoreWindow4 *T, REFIID r, void **p) {
    return cw_qi_impl(UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window4), r, p);
}
static ULONG UWP_CALL cw4_AddRef(UwpICoreWindow4 *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window4)->ref);
}
static ULONG UWP_CALL cw4_Release(UwpICoreWindow4 *T) {
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window4);
    int32_t r = uwp_atomic_dec(&w->ref);
    if (r == 0) cw_destroy(w);
    return (ULONG)r;
}
static HRESULT UWP_CALL cw4_GetIids(UwpICoreWindow4 *T, ULONG *c, IID **i) {
    (void)T; if (!c || !i) return E_INVALIDARG; *c = 0; *i = NULL; return S_OK;
}
static HRESULT UWP_CALL cw4_GetName(UwpICoreWindow4 *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.CoreWindow", n);
}
static HRESULT UWP_CALL cw4_GetTrust(UwpICoreWindow4 *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL cw4_add_RS(UwpICoreWindow4 *T, UwpTypedEventHandler *h, UwpEventRegistrationToken *tok) {
    return uwp_event_list_add(&UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window4)->ev_resize_started, h, tok);
}
static HRESULT UWP_CALL cw4_remove_RS(UwpICoreWindow4 *T, UwpEventRegistrationToken tok) {
    return uwp_event_list_remove(&UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window4)->ev_resize_started, tok);
}
static HRESULT UWP_CALL cw4_add_RC(UwpICoreWindow4 *T, UwpTypedEventHandler *h, UwpEventRegistrationToken *tok) {
    return uwp_event_list_add(&UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window4)->ev_resize_completed, h, tok);
}
static HRESULT UWP_CALL cw4_remove_RC(UwpICoreWindow4 *T, UwpEventRegistrationToken tok) {
    return uwp_event_list_remove(&UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window4)->ev_resize_completed, tok);
}

static const UwpICoreWindow4Vtbl g_cw4_vtbl = {
    cw4_QI, cw4_AddRef, cw4_Release,
    cw4_GetIids, cw4_GetName, cw4_GetTrust,
    cw4_add_RS, cw4_remove_RS, cw4_add_RC, cw4_remove_RC
};

static HRESULT UWP_CALL cw5_QI(UwpICoreWindow5 *T, REFIID r, void **p) {
    return cw_qi_impl(UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window5), r, p);
}
static ULONG UWP_CALL cw5_AddRef(UwpICoreWindow5 *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window5)->ref);
}
static ULONG UWP_CALL cw5_Release(UwpICoreWindow5 *T) {
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window5);
    int32_t r = uwp_atomic_dec(&w->ref);
    if (r == 0) cw_destroy(w);
    return (ULONG)r;
}
static HRESULT UWP_CALL cw5_GetIids(UwpICoreWindow5 *T, ULONG *c, IID **i) {
    (void)T; if (!c || !i) return E_INVALIDARG; *c = 0; *i = NULL; return S_OK;
}
static HRESULT UWP_CALL cw5_GetName(UwpICoreWindow5 *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.CoreWindow", n);
}
static HRESULT UWP_CALL cw5_GetTrust(UwpICoreWindow5 *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL cw5_get_DispatcherQueue(UwpICoreWindow5 *T, void **v) {
    (void)T; if (v) *v = NULL;
    uwp_bridge_log("corewindow", "ICoreWindow5::get_DispatcherQueue not implemented");
    return E_NOTIMPL;
}
static HRESULT UWP_CALL cw5_get_ActivationMode(UwpICoreWindow5 *T, UwpCoreWindowActivationMode *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, core_window5)->activation_mode;
    return S_OK;
}

static const UwpICoreWindow5Vtbl g_cw5_vtbl = {
    cw5_QI, cw5_AddRef, cw5_Release,
    cw5_GetIids, cw5_GetName, cw5_GetTrust,
    cw5_get_DispatcherQueue, cw5_get_ActivationMode
};

/* ============================================================================
 * ICoreWindowInterop & IProtonScarlettCoreWindowBridge (DXVK/vkd3d-proton ABI)
 * ============================================================================ */

static HRESULT UWP_CALL interop_QI(void *T, REFIID r, void **p) {
    return cw_qi_impl(UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, interop), r, p);
}
static ULONG UWP_CALL interop_AddRef(void *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, interop)->ref);
}
static ULONG UWP_CALL interop_Release(void *T) {
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, interop);
    int32_t r = uwp_atomic_dec(&w->ref);
    if (r == 0) cw_destroy(w);
    return (ULONG)r;
}
static HRESULT UWP_CALL interop_get_WindowHandle(void *T, HWND *hwnd) {
    if (!hwnd) return E_INVALIDARG;
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, interop);
    *hwnd = w->hwnd;
    return S_OK;
}
static HRESULT UWP_CALL interop_put_MessageHandled(void *T, uint8_t value) {
    UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, interop)->message_handled = value ? 1u : 0u;
    return S_OK;
}

static const ICoreWindowInteropBridgeVtbl g_interop_vtbl = {
    interop_QI, interop_AddRef, interop_Release,
    interop_get_WindowHandle, interop_put_MessageHandled
};

static HRESULT UWP_CALL bridge_QI(void *T, REFIID r, void **p) {
    return cw_qi_impl(UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, bridge), r, p);
}
static ULONG UWP_CALL bridge_AddRef(void *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, bridge)->ref);
}
static ULONG UWP_CALL bridge_Release(void *T) {
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, bridge);
    int32_t r = uwp_atomic_dec(&w->ref);
    if (r == 0) cw_destroy(w);
    return (ULONG)r;
}
static HRESULT UWP_CALL bridge_GetHwnd(void *T, HWND *out_hwnd) {
    if (!out_hwnd) return E_INVALIDARG;
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, bridge);
    *out_hwnd = w->hwnd;
    return S_OK;
}
static HRESULT UWP_CALL bridge_GetClientSize(void *T, uint32_t *out_width, uint32_t *out_height) {
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, bridge);
    uint32_t cw = 0, ch = 0;
#ifdef _WIN32
    if (w->hwnd && !w->headless) {
        RECT cr;
        if (GetClientRect(w->hwnd, &cr)) {
            cw = (uint32_t)(cr.right - cr.left);
            ch = (uint32_t)(cr.bottom - cr.top);
        }
    }
#endif
    if (cw == 0 || ch == 0) {
        uwp_mutex_lock(&w->lock);
        cw = w->client_width;
        ch = w->client_height;
        uwp_mutex_unlock(&w->lock);
    }
    if (out_width) *out_width = cw;
    if (out_height) *out_height = ch;
    return S_OK;
}
static HRESULT UWP_CALL bridge_IsFullscreen(void *T, int32_t *out_is_fullscreen) {
    if (!out_is_fullscreen) return E_INVALIDARG;
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, bridge);
    uwp_mutex_lock(&w->lock);
    *out_is_fullscreen = w->is_fullscreen;
    uwp_mutex_unlock(&w->lock);
    return S_OK;
}
static HRESULT UWP_CALL bridge_NotifySwapChainCreated(void *T, IUnknown *swapchain) {
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, bridge);
    uwp_mutex_lock(&w->lock);
    IUnknown *old_sc = w->swapchain_ref;
    if (swapchain) swapchain->lpVtbl->AddRef(swapchain);
    w->swapchain_ref = swapchain;
    uint32_t cw = w->client_width;
    uint32_t ch = w->client_height;
    HWND hwnd = w->hwnd;
    uwp_mutex_unlock(&w->lock);
    if (old_sc) old_sc->lpVtbl->Release(old_sc);
    uwp_bridge_log("corewindow", "NotifySwapChainCreated swapchain=%p hwnd=%p size=%ux%u",
                   (void *)swapchain, (void *)hwnd, (unsigned)cw, (unsigned)ch);
    return S_OK;
}

static const IProtonScarlettCoreWindowBridgeVtbl g_bridge_vtbl = {
    bridge_QI, bridge_AddRef, bridge_Release,
    bridge_GetHwnd, bridge_GetClientSize, bridge_IsFullscreen, bridge_NotifySwapChainCreated
};

/* ============================================================================
 * ICoreDispatcher & ICoreAcceleratorKeys Methods
 * ============================================================================ */

static HRESULT UWP_CALL disp_QI(UwpICoreDispatcher *T, REFIID r, void **p) {
    return disp_qi_impl(UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, dispatcher), r, p);
}
static ULONG UWP_CALL disp_AddRef(UwpICoreDispatcher *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, dispatcher)->ref);
}
static ULONG UWP_CALL disp_Release(UwpICoreDispatcher *T) {
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, dispatcher);
    int32_t r = uwp_atomic_dec(&w->ref);
    if (r == 0) cw_destroy(w);
    return (ULONG)r;
}
static HRESULT UWP_CALL disp_GetIids(UwpICoreDispatcher *T, ULONG *c, IID **i) {
    (void)T; if (!c || !i) return E_INVALIDARG; *c = 0; *i = NULL; return S_OK;
}
static HRESULT UWP_CALL disp_GetName(UwpICoreDispatcher *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.CoreDispatcher", n);
}
static HRESULT UWP_CALL disp_GetTrust(UwpICoreDispatcher *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL disp_get_HasThreadAccess(UwpICoreDispatcher *T, uint8_t *v) {
    if (!v) return E_INVALIDARG;
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, dispatcher);
    *v = (w->owner_thread_id == uwp_current_thread_id()) ? 1u : 0u;
    return S_OK;
}
static HRESULT UWP_CALL disp_ProcessEvents(UwpICoreDispatcher *T, UwpCoreProcessEventsOption options) {
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, dispatcher);
#ifdef _WIN32
    if (!w->headless) {
        MSG msg;
        switch (options) {
            case UWP_PROCESS_ONE_IF_PRESENT:
                if (uwp_corewindow_drain_dispatched_work(w, 1u) > 0u) {
                    return S_OK;
                }
                if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                    if (msg.message == WM_QUIT) {
                        w->closed = 1;
                        return S_OK;
                    }
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                return S_OK;

            case UWP_PROCESS_ALL_IF_PRESENT:
                uwp_corewindow_drain_dispatched_work(w, 0u);
                while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                    if (msg.message == WM_QUIT) {
                        w->closed = 1;
                        break;
                    }
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                    uwp_corewindow_drain_dispatched_work(w, 0u);
                }
                return S_OK;

            case UWP_PROCESS_ONE_AND_ALL_PENDING:
                if (!uwp_has_dispatched_work(w) && !PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE)) {
                    if (!w->closed) {
                        BOOL gm = GetMessageW(&msg, NULL, 0, 0);
                        if (gm <= 0) {
                            w->closed = 1;
                            return S_OK;
                        }
                        TranslateMessage(&msg);
                        DispatchMessageW(&msg);
                    }
                }
                uwp_corewindow_drain_dispatched_work(w, 0u);
                while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                    if (msg.message == WM_QUIT) {
                        w->closed = 1;
                        break;
                    }
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                    uwp_corewindow_drain_dispatched_work(w, 0u);
                }
                return S_OK;

            case UWP_PROCESS_UNTIL_QUIT:
                while (!w->closed) {
                    uwp_corewindow_drain_dispatched_work(w, 0u);
                    if (w->closed) break;
                    BOOL gm = GetMessageW(&msg, NULL, 0, 0);
                    if (gm <= 0) {
                        w->closed = 1;
                        break;
                    }
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                uwp_corewindow_drain_dispatched_work(w, 0u);
                return S_OK;

            default:
                return E_INVALIDARG;
        }
    }
#endif
    /* Headless / host-test mode */
    switch (options) {
        case UWP_PROCESS_ONE_IF_PRESENT:
            uwp_corewindow_drain_dispatched_work(w, 1u);
            return S_OK;
        case UWP_PROCESS_ALL_IF_PRESENT:
        case UWP_PROCESS_ONE_AND_ALL_PENDING:
            uwp_corewindow_drain_dispatched_work(w, 0u);
            return S_OK;
        case UWP_PROCESS_UNTIL_QUIT:
            while (!w->closed && uwp_has_dispatched_work(w)) {
                uwp_corewindow_drain_dispatched_work(w, 0u);
            }
            return S_OK;
        default:
            return E_INVALIDARG;
    }
}
static HRESULT UWP_CALL disp_RunAsync(UwpICoreDispatcher *T, UwpCoreDispatcherPriority priority,
                                      UwpDispatchedHandler *callback, UwpIAsyncAction **action) {
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, dispatcher);
    return uwp_enqueue_work(w, priority, 0, callback, NULL, action);
}
static HRESULT UWP_CALL disp_RunIdleAsync(UwpICoreDispatcher *T, UwpIdleDispatchedHandler *callback,
                                          UwpIAsyncAction **action) {
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, dispatcher);
    return uwp_enqueue_work(w, UWP_PRIORITY_IDLE, 1, NULL, callback, action);
}

static const UwpICoreDispatcherVtbl g_disp_vtbl = {
    disp_QI, disp_AddRef, disp_Release,
    disp_GetIids, disp_GetName, disp_GetTrust,
    disp_get_HasThreadAccess,
    disp_ProcessEvents,
    disp_RunAsync,
    disp_RunIdleAsync
};

static HRESULT UWP_CALL accel_QI(UwpICoreAcceleratorKeys *T, REFIID r, void **p) {
    return disp_qi_impl(UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, accel_keys), r, p);
}
static ULONG UWP_CALL accel_AddRef(UwpICoreAcceleratorKeys *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, accel_keys)->ref);
}
static ULONG UWP_CALL accel_Release(UwpICoreAcceleratorKeys *T) {
    UwpCoreWindowObj *w = UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, accel_keys);
    int32_t r = uwp_atomic_dec(&w->ref);
    if (r == 0) cw_destroy(w);
    return (ULONG)r;
}
static HRESULT UWP_CALL accel_GetIids(UwpICoreAcceleratorKeys *T, ULONG *c, IID **i) {
    (void)T; if (!c || !i) return E_INVALIDARG; *c = 0; *i = NULL; return S_OK;
}
static HRESULT UWP_CALL accel_GetName(UwpICoreAcceleratorKeys *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.CoreDispatcher", n);
}
static HRESULT UWP_CALL accel_GetTrust(UwpICoreAcceleratorKeys *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL accel_add(UwpICoreAcceleratorKeys *T, UwpTypedEventHandler *h,
                                  UwpEventRegistrationToken *tok) {
    return uwp_event_list_add(&UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, accel_keys)->ev_accel_key, h, tok);
}
static HRESULT UWP_CALL accel_remove(UwpICoreAcceleratorKeys *T, UwpEventRegistrationToken tok) {
    return uwp_event_list_remove(&UWP_CONTAINING_RECORD(T, UwpCoreWindowObj, accel_keys)->ev_accel_key, tok);
}

static const UwpICoreAcceleratorKeysVtbl g_accel_vtbl = {
    accel_QI, accel_AddRef, accel_Release,
    accel_GetIids, accel_GetName, accel_GetTrust,
    accel_add, accel_remove
};

/* ============================================================================
 * CoreWindow Creation & Lifecycle Helpers
 * ============================================================================ */

UwpCoreWindowObj *uwp_corewindow_create(uint32_t width, uint32_t height,
                                        const wchar_t *title, int headless) {
    uwp_cw_ensure_global_init();

    UwpCoreWindowObj *w = (UwpCoreWindowObj *)calloc(1, sizeof(*w));
    if (!w) return NULL;

    w->core_window.lpVtbl  = &g_cw_vtbl;
    w->core_window2.lpVtbl = &g_cw2_vtbl;
    w->core_window3.lpVtbl = &g_cw3_vtbl;
    w->core_window4.lpVtbl = &g_cw4_vtbl;
    w->core_window5.lpVtbl = &g_cw5_vtbl;
    w->interop.lpVtbl      = &g_interop_vtbl;
    w->bridge.lpVtbl       = &g_bridge_vtbl;
    w->dispatcher.lpVtbl   = &g_disp_vtbl;
    w->accel_keys.lpVtbl   = &g_accel_vtbl;

    w->ref = 1;
    uwp_mutex_init(&w->lock);
    uwp_mutex_init(&w->queue_lock);

    w->owner_thread_id = uwp_current_thread_id();
    w->headless = headless ? 1 : 0;
    w->client_width = width ? width : g_default_width;
    w->client_height = height ? height : g_default_height;
    w->is_input_enabled = 1u;
    w->visible = 0u;
    w->flow_direction = UWP_FLOW_LEFT_TO_RIGHT;
    w->activation_mode = UWP_ACTIVATION_MODE_NONE;

    if (title && title[0] != L'\0') {
        wcsncpy(w->title, title, sizeof(w->title) / sizeof(wchar_t) - 1);
    } else {
        const uwp_package_identity_t *id = uwp_core_get_package_identity();
        if (id && id->display_name[0] != L'\0') {
            wcsncpy(w->title, id->display_name, sizeof(w->title) / sizeof(wchar_t) - 1);
        } else {
            wcscpy(w->title, L"Proton-Scarlett UWP");
        }
    }

    uwp_event_list_init(&w->ev_activated);
    uwp_event_list_init(&w->ev_automation);
    uwp_event_list_init(&w->ev_char_received);
    uwp_event_list_init(&w->ev_closed);
    uwp_event_list_init(&w->ev_input_enabled);
    uwp_event_list_init(&w->ev_key_down);
    uwp_event_list_init(&w->ev_key_up);
    uwp_event_list_init(&w->ev_pointer_capture_lost);
    uwp_event_list_init(&w->ev_pointer_entered);
    uwp_event_list_init(&w->ev_pointer_exited);
    uwp_event_list_init(&w->ev_pointer_moved);
    uwp_event_list_init(&w->ev_pointer_pressed);
    uwp_event_list_init(&w->ev_pointer_released);
    uwp_event_list_init(&w->ev_touch_hit_testing);
    uwp_event_list_init(&w->ev_pointer_wheel);
    uwp_event_list_init(&w->ev_size_changed);
    uwp_event_list_init(&w->ev_visibility_changed);
    uwp_event_list_init(&w->ev_closest_bounds);
    uwp_event_list_init(&w->ev_resize_started);
    uwp_event_list_init(&w->ev_resize_completed);
    uwp_event_list_init(&w->ev_accel_key);

#ifdef _WIN32
    if (!w->headless && uwp_ensure_wndclass_registered()) {
        DWORD style = WS_OVERLAPPEDWINDOW;
        DWORD ex_style = WS_EX_APPWINDOW;
        RECT rc = { 0, 0, (LONG)w->client_width, (LONG)w->client_height };
        AdjustWindowRectEx(&rc, style, FALSE, ex_style);

        HWND hwnd = CreateWindowExW(
            ex_style,
            k_wndclass_name,
            w->title,
            style,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left,
            rc.bottom - rc.top,
            NULL, NULL,
            GetModuleHandleW(NULL),
            w);
        if (hwnd) {
            w->hwnd = hwnd;
            w->saved_style = style;
            w->saved_ex_style = ex_style;
            GetWindowRect(hwnd, &w->saved_window_rect);
            uwp_bridge_log("corewindow", "Created real Win32 HWND=%p (%ux%u)",
                           (void *)hwnd, (unsigned)w->client_width, (unsigned)w->client_height);
        } else {
            uwp_bridge_log("corewindow", "CreateWindowExW failed err=%lu", (unsigned long)GetLastError());
        }
    }
#else
    /* Non-Win32 host synthetic handle for unit tests */
    w->hwnd = (HWND)(uintptr_t)0x00005CA700000001ULL;
#endif

    uwp_corewindow_bind_current_thread(w);

    uwp_mutex_lock(&g_cw_global_lock);
    if (!g_main_core_window) {
        uwp_atomic_inc(&w->ref);
        g_main_core_window = w;
    }
    uwp_mutex_unlock(&g_cw_global_lock);

    return w;
}

UwpICoreWindow *uwp_corewindow_iface(UwpCoreWindowObj *win) {
    if (!win) return NULL;
    uwp_atomic_inc(&win->ref);
    return &win->core_window;
}

UwpICoreDispatcher *uwp_corewindow_dispatcher_iface(UwpCoreWindowObj *win) {
    if (!win) return NULL;
    uwp_atomic_inc(&win->ref);
    return &win->dispatcher;
}

void uwp_corewindow_request_close(UwpCoreWindowObj *win) {
    if (!win) return;
    if (!win->closed) {
        win->closed = 1;
        UwpICoreWindowEventArgs *cargs = uwp_create_core_window_event_args();
        if (cargs) {
            uwp_event_list_invoke(&win->ev_closed, &win->core_window, cargs);
            cargs->lpVtbl->Release(cargs);
        }
    }
#ifdef _WIN32
    if (win->hwnd && !win->headless) {
        PostMessageW(win->hwnd, WM_CLOSE, 0, 0);
    }
#endif
}

int uwp_corewindow_is_closed(UwpCoreWindowObj *win) {
    return win ? (win->closed != 0) : 1;
}

/* ============================================================================
 * CoreWindow & CoreDispatcher Activation Factories
 * ============================================================================ */

typedef struct UwpCoreWindowFactoryObj {
    UwpIActivationFactory factory;
    UwpICoreWindowStatic  statics;
} UwpCoreWindowFactoryObj;

static HRESULT UWP_CALL cwf_qi_impl(UwpCoreWindowFactoryObj *o, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (uwp_guid_equal(riid, &UWP_IID_IUnknown) ||
        uwp_guid_equal(riid, &UWP_IID_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(riid, &UWP_IID_IActivationFactory)) {
        *ppv = &o->factory;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ICoreWindowStatic)) {
        *ppv = &o->statics;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static HRESULT UWP_CALL cwf_QI(UwpIActivationFactory *T, REFIID r, void **p) {
    return cwf_qi_impl(UWP_CONTAINING_RECORD(T, UwpCoreWindowFactoryObj, factory), r, p);
}
static ULONG UWP_CALL cwf_AddRef(UwpIActivationFactory *T) { (void)T; return 1u; }
static ULONG UWP_CALL cwf_Release(UwpIActivationFactory *T) { (void)T; return 1u; }
static HRESULT UWP_CALL cwf_GetIids(UwpIActivationFactory *T, ULONG *c, IID **i) {
    (void)T; if (!c || !i) return E_INVALIDARG; *c = 0; *i = NULL; return S_OK;
}
static HRESULT UWP_CALL cwf_GetName(UwpIActivationFactory *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.CoreWindow", n);
}
static HRESULT UWP_CALL cwf_GetTrust(UwpIActivationFactory *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL cwf_ActivateInstance(UwpIActivationFactory *T, UwpIInspectable **inst) {
    (void)T; if (inst) *inst = NULL;
    return E_NOTIMPL; /* CoreWindow is not directly activatable */
}

static HRESULT UWP_CALL cws_QI(UwpICoreWindowStatic *T, REFIID r, void **p) {
    return cwf_qi_impl(UWP_CONTAINING_RECORD(T, UwpCoreWindowFactoryObj, statics), r, p);
}
static ULONG UWP_CALL cws_AddRef(UwpICoreWindowStatic *T) { (void)T; return 1u; }
static ULONG UWP_CALL cws_Release(UwpICoreWindowStatic *T) { (void)T; return 1u; }
static HRESULT UWP_CALL cws_GetIids(UwpICoreWindowStatic *T, ULONG *c, IID **i) {
    (void)T; if (!c || !i) return E_INVALIDARG; *c = 0; *i = NULL; return S_OK;
}
static HRESULT UWP_CALL cws_GetName(UwpICoreWindowStatic *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.CoreWindow", n);
}
static HRESULT UWP_CALL cws_GetTrust(UwpICoreWindowStatic *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL cws_GetForCurrentThread(UwpICoreWindowStatic *T, UwpICoreWindow **window) {
    (void)T;
    if (!window) return E_INVALIDARG;
    UwpCoreWindowObj *w = uwp_corewindow_get_for_current_thread();
    *window = w ? uwp_corewindow_iface(w) : NULL;
    return S_OK;
}

static const UwpIActivationFactoryVtbl g_cwf_vtbl = {
    cwf_QI, cwf_AddRef, cwf_Release,
    cwf_GetIids, cwf_GetName, cwf_GetTrust,
    cwf_ActivateInstance
};
static const UwpICoreWindowStaticVtbl g_cws_vtbl = {
    cws_QI, cws_AddRef, cws_Release,
    cws_GetIids, cws_GetName, cws_GetTrust,
    cws_GetForCurrentThread
};
static UwpCoreWindowFactoryObj g_cw_factory = {
    { &g_cwf_vtbl },
    { &g_cws_vtbl }
};

HRESULT uwp_corewindow_get_activation_factory(REFIID iid, void **out_factory) {
    return cwf_qi_impl(&g_cw_factory, iid, out_factory);
}

static HRESULT UWP_CALL df_QI(UwpIActivationFactory *T, REFIID r, void **p) {
    if (!r || !p) return E_INVALIDARG;
    *p = NULL;
    if (uwp_guid_equal(r, &UWP_IID_IUnknown) ||
        uwp_guid_equal(r, &UWP_IID_IInspectable) ||
        uwp_guid_equal(r, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(r, &UWP_IID_IActivationFactory)) {
        *p = T;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG UWP_CALL df_AddRef(UwpIActivationFactory *T) { (void)T; return 1u; }
static ULONG UWP_CALL df_Release(UwpIActivationFactory *T) { (void)T; return 1u; }
static HRESULT UWP_CALL df_GetIids(UwpIActivationFactory *T, ULONG *c, IID **i) {
    (void)T; if (!c || !i) return E_INVALIDARG; *c = 0; *i = NULL; return S_OK;
}
static HRESULT UWP_CALL df_GetName(UwpIActivationFactory *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.CoreDispatcher", n);
}
static HRESULT UWP_CALL df_GetTrust(UwpIActivationFactory *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL df_ActivateInstance(UwpIActivationFactory *T, UwpIInspectable **inst) {
    (void)T; if (inst) *inst = NULL;
    return E_NOTIMPL;
}
static const UwpIActivationFactoryVtbl g_df_vtbl = {
    df_QI, df_AddRef, df_Release,
    df_GetIids, df_GetName, df_GetTrust,
    df_ActivateInstance
};
static UwpIActivationFactory g_disp_factory = { &g_df_vtbl };

HRESULT uwp_coredispatcher_get_activation_factory(REFIID iid, void **out_factory) {
    return df_QI(&g_disp_factory, iid, out_factory);
}

/* ============================================================================
 * Internal CoreWindow Accessors (Declared in uwp_bridge_abi.h)
 * ============================================================================ */

HWND uwp_core_get_main_hwnd(void) {
    UwpCoreWindowObj *w = uwp_corewindow_get_main();
    return w ? w->hwnd : NULL;
}

void uwp_core_get_client_size(uint32_t *out_width, uint32_t *out_height) {
    UwpCoreWindowObj *w = uwp_corewindow_get_main();
    if (!w) {
        if (out_width) *out_width = g_default_width;
        if (out_height) *out_height = g_default_height;
        return;
    }
    bridge_GetClientSize(&w->bridge, out_width, out_height);
}

int32_t uwp_core_is_fullscreen(void) {
    UwpCoreWindowObj *w = uwp_corewindow_get_main();
    if (!w) return 0;
    uwp_mutex_lock(&w->lock);
    int32_t fs = w->is_fullscreen;
    uwp_mutex_unlock(&w->lock);
    return fs;
}

int32_t uwp_core_set_fullscreen(int32_t fullscreen) {
    UwpCoreWindowObj *w = uwp_corewindow_get_main();
    if (!w) return 0;
    int32_t target = fullscreen ? 1 : 0;

    uwp_mutex_lock(&w->lock);
    if (w->is_fullscreen == target) {
        uwp_mutex_unlock(&w->lock);
        return 1;
    }
    w->is_fullscreen = target;
#ifdef _WIN32
    HWND hwnd = w->hwnd;
    int headless = w->headless;
    DWORD saved_style = w->saved_style;
    DWORD saved_ex = w->saved_ex_style;
    RECT saved_rect = w->saved_window_rect;
    if (target && hwnd && !headless) {
        w->saved_style = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
        w->saved_ex_style = (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        GetWindowRect(hwnd, &w->saved_window_rect);
    }
#endif
    uwp_mutex_unlock(&w->lock);

#ifdef _WIN32
    if (hwnd && !headless) {
        if (target) {
            MONITORINFO mi;
            memset(&mi, 0, sizeof(mi));
            mi.cbSize = sizeof(mi);
            HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            if (GetMonitorInfoW(mon, &mi)) {
                SetWindowLongPtrW(hwnd, GWL_STYLE,
                                  (LONG_PTR)((w->saved_style & ~(WS_CAPTION | WS_THICKFRAME)) | WS_POPUP));
                SetWindowPos(hwnd, HWND_TOP,
                             mi.rcMonitor.left, mi.rcMonitor.top,
                             mi.rcMonitor.right - mi.rcMonitor.left,
                             mi.rcMonitor.bottom - mi.rcMonitor.top,
                             SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
            }
        } else {
            SetWindowLongPtrW(hwnd, GWL_STYLE, (LONG_PTR)(saved_style ? saved_style : WS_OVERLAPPEDWINDOW));
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, (LONG_PTR)saved_ex);
            int rw = saved_rect.right - saved_rect.left;
            int rh = saved_rect.bottom - saved_rect.top;
            if (rw <= 0 || rh <= 0) {
                rw = (int)g_default_width;
                rh = (int)g_default_height;
            }
            SetWindowPos(hwnd, HWND_NOTOPMOST,
                         saved_rect.left, saved_rect.top, rw, rh,
                         SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        }
    }
#endif
    uwp_bridge_log("corewindow", "Fullscreen mode -> %d", (int)target);
    return 1;
}

void uwp_core_set_window_title(const wchar_t *title) {
    UwpCoreWindowObj *w = uwp_corewindow_get_main();
    if (!w || !title) return;
    uwp_mutex_lock(&w->lock);
    wcsncpy(w->title, title, sizeof(w->title) / sizeof(wchar_t) - 1);
    w->title[sizeof(w->title) / sizeof(wchar_t) - 1] = L'\0';
#ifdef _WIN32
    HWND hwnd = w->hwnd;
    int headless = w->headless;
#endif
    uwp_mutex_unlock(&w->lock);
#ifdef _WIN32
    if (hwnd && !headless) {
        SetWindowTextW(hwnd, title);
    }
#endif
}

void uwp_core_get_window_title(wchar_t *buf, uint32_t buf_chars) {
    if (!buf || buf_chars == 0) return;
    buf[0] = L'\0';
    UwpCoreWindowObj *w = uwp_corewindow_get_main();
    if (!w) return;
    uwp_mutex_lock(&w->lock);
    wcsncpy(buf, w->title, buf_chars - 1);
    buf[buf_chars - 1] = L'\0';
    uwp_mutex_unlock(&w->lock);
}
