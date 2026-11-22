/*
 * Proton-Scarlett — Host Unit Tests for CRT UWP Forwarder Runtime Helpers
 */

#include "crt_app_runtime.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int g_sink_called = 0;
static scarlett_stub_context_t g_captured_ctx;

static scarlett_stub_action_t test_stub_sink(
    const scarlett_stub_context_t *ctx,
    void *user_data) {
    int *allow_abort = (int *)user_data;
    ++g_sink_called;
    g_captured_ctx = *ctx;
    if (allow_abort && *allow_abort == 0) {
        return SCARLETT_STUB_ACTION_LOG_AND_FAIL;
    }
    return scarlett_stub_resolve_action(ctx);
}

static void test_diag_stub_trampoline(void) {
    static const scarlett_stub_descriptor_t desc = {
        SCARLETT_STUB_THUNK_ABI_VERSION,
        SCARLETT_STUB_FLAG_NONE,
        SCARLETT_STUB_ORDINAL_NONE,
        SCARLETT_STUB_SLOT_NONE,
        SCARLETT_E_NOTIMPL,
        0u,
        "vccorlib140_app.dll",
        "?GetIBoxVtable@Details@Platform@@YAPEAXPEAX@Z"
    };

    int allow_abort = 0;
    g_sink_called = 0;
    crt_app_stub_set_sink(test_stub_sink, &allow_abort);
    crt_app_stub_set_strict_override(0);

    scarlett_hresult_t hr = crt_app_stub_dispatch(
        &desc,
        (uint64_t)(uintptr_t)&test_diag_stub_trampoline,
        0x1111111122222222ULL,
        0x3333333344444444ULL,
        0x5555555566666666ULL,
        0x7777777788888888ULL);

    assert(hr == SCARLETT_E_NOTIMPL);
    assert(g_sink_called == 1);
    assert(strcmp(g_captured_ctx.dll_name, "vccorlib140_app.dll") == 0);
    assert(strcmp(g_captured_ctx.symbol_name, "?GetIBoxVtable@Details@Platform@@YAPEAXPEAX@Z") == 0);
    assert(g_captured_ctx.rcx == 0x1111111122222222ULL);
    assert(g_captured_ctx.rdx == 0x3333333344444444ULL);
    assert(g_captured_ctx.r8 == 0x5555555566666666ULL);
    assert(g_captured_ctx.r9 == 0x7777777788888888ULL);
    assert(g_captured_ctx.strict_mode == 0u);

    /* Verify strict mode forces SCARLETT_ERR_STRICT_STUB_ABORT */
    crt_app_stub_set_strict_override(1);
    hr = crt_app_stub_dispatch(&desc, 0x1234ULL, 1u, 2u, 3u, 4u);
    assert(g_captured_ctx.strict_mode == 1u);
    assert(g_captured_ctx.return_hr == SCARLETT_ERR_STRICT_STUB_ABORT);
    assert(hr == SCARLETT_ERR_STRICT_STUB_ABORT);

    crt_app_stub_set_sink(NULL, NULL);
    crt_app_stub_set_strict_override(-1);
}

static void test_cyl_bessel_i(void) {
    /* Reference values for I_nu(x) */
    assert(fabs(crt_app_cyl_bessel_i(0.0, 0.0) - 1.0) < 1e-15);
    assert(fabs(crt_app_cyl_bessel_i(1.0, 0.0) - 0.0) < 1e-15);

    double i0_1 = crt_app_cyl_bessel_i(0.0, 1.0);
    assert(fabs(i0_1 - 1.2660658777520084) < 1e-12);

    double i0_5 = crt_app_cyl_bessel_i(0.0, 5.0);
    assert(fabs(i0_5 - 27.239871823604442) < 1e-10);

    double i1_2 = crt_app_cyl_bessel_i(1.0, 2.0);
    assert(fabs(i1_2 - 1.5906368546373291) < 1e-12);

    double i1_neg2 = crt_app_cyl_bessel_i(1.0, -2.0);
    assert(fabs(i1_neg2 + 1.5906368546373291) < 1e-12);

    /* Asymptotic region |x| > 25: I_0(30) = 7.816722978239775e+11 */
    double i0_30 = crt_app_cyl_bessel_i(0.0, 30.0);
    assert(fabs(i0_30 - 7.816722978239775e11) / 7.816722978239775e11 < 1e-12);

    /* Non-integer order nu = 2.5, x = 3.0 -> I_{2.5}(3) = 1.653197... */
    double i25_3 = crt_app_cyl_bessel_i(2.5, 3.0);
    double expected_25_3 =
        sqrt(2.0 / (3.14159265358979323846 * 3.0)) *
        ((3.0 / 9.0 + 1.0) * sinh(3.0) - cosh(3.0));
    assert(fabs(i25_3 - expected_25_3) < 1e-10);

    float fi0_1 = crt_app_cyl_bessel_if(0.0f, 1.0f);
    assert(fabsf(fi0_1 - 1.2660659f) < 1e-6f);
}

static void test_intptr_and_rect(void) {
    int64_t storage = 0;
    void *ret = crt_app_intptr_from_i32(&storage, -42);
    assert(ret == &storage);
    assert(storage == -42);
    assert(crt_app_intptr_to_i32(&storage) == -42);

    float rect[4] = { 10.0f, 20.0f, 100.0f, 50.0f };
    union { struct { float x; float y; } p; uint64_t u; } pt;
    pt.p.x = 50.0f;
    pt.p.y = 40.0f;
    assert(crt_app_rect_contains(rect, pt.u) == 1u);

    pt.p.x = 110.0f;
    pt.p.y = 70.0f;
    assert(crt_app_rect_contains(rect, pt.u) == 1u);

    pt.p.x = 110.5f;
    pt.p.y = 40.0f;
    assert(crt_app_rect_contains(rect, pt.u) == 0u);

    float bad_rect[4] = { 0.0f, 0.0f, -10.0f, 10.0f };
    pt.p.x = -5.0f;
    pt.p.y = 5.0f;
    assert(crt_app_rect_contains(bad_rect, pt.u) == 0u);
}

static void test_aligned_heap_and_object(void) {
    void *p1 = crt_app_aligned_alloc(64u, 32u);
    assert(p1 != NULL);
    assert(((uintptr_t)p1 & 31u) == 0u);
    memset(p1, 0xAB, 64u);
    crt_app_aligned_free(p1);
    crt_app_aligned_free(NULL);

    /* Untracked malloc pointer fallback */
    void *p2 = malloc(32u);
    assert(p2 != NULL);
    crt_app_aligned_free(p2);

    void *obj_mem[4] = { NULL, NULL, NULL, NULL };
    assert(crt_app_platform_object_ctor(obj_mem) == obj_mem);
    assert(obj_mem[0] != NULL);
}

/* Mock WRL ModuleBase / CreatorMap test */
typedef struct mock_factory_s {
    const void *lpVtbl;
    uint32_t refs;
} mock_factory_t;

static int32_t g_mock_factory_released = 0;

static int32_t mock_factory_qi(void *This, const crt_app_guid_t *riid, void **ppv) {
    (void)riid;
    mock_factory_t *f = (mock_factory_t *)This;
    ++f->refs;
    *ppv = This;
    return 0;
}
static uint32_t mock_factory_addref(void *This) {
    return ++((mock_factory_t *)This)->refs;
}
static uint32_t mock_factory_release(void *This) {
    mock_factory_t *f = (mock_factory_t *)This;
    if (--f->refs == 0u) {
        g_mock_factory_released = 1;
        return 0u;
    }
    return f->refs;
}

typedef struct mock_vtbl_s {
    int32_t (*qi)(void *, const crt_app_guid_t *, void **);
    uint32_t (*addref)(void *);
    uint32_t (*release)(void *);
} mock_vtbl_t;

static const mock_vtbl_t g_mock_factory_vtbl = {
    mock_factory_qi, mock_factory_addref, mock_factory_release
};

static mock_factory_t g_mock_factory = { &g_mock_factory_vtbl, 0u };
static crt_wrl_factory_cache_t g_mock_cache = { NULL, { 0u } };
static uint32_t g_mock_obj_count = 0u;

static const wchar_t *mock_get_runtime_name(void) {
    return L"UnityEngineDelegates.PlatformInvoke";
}

static int32_t mock_factory_creator(
    unsigned int *flags,
    const crt_wrl_creator_map_t *entry,
    const crt_app_guid_t *riid,
    void **ppFactory) {
    (void)flags;
    (void)riid;
    g_mock_factory.refs = 1u;
    entry->factory_cache->factory = &g_mock_factory;
    *ppFactory = &g_mock_factory;
    ++g_mock_factory.refs;
    return 0;
}

static const crt_wrl_creator_map_t g_mock_cm = {
    mock_factory_creator,
    { .get_runtime_name = mock_get_runtime_name },
    NULL,
    &g_mock_cache,
    NULL
};

static const crt_wrl_creator_map_t *g_mock_entries[3] = {
    NULL,      /* sentinel (mid) */
    &g_mock_cm,
    NULL
};

static uint32_t mock_mod_get_count(void *self) { (void)self; return g_mock_obj_count; }
static const crt_wrl_creator_map_t **mock_mod_first(void *self) { (void)self; return &g_mock_entries[0]; }
static const crt_wrl_creator_map_t **mock_mod_mid(void *self) { (void)self; return &g_mock_entries[0]; }
static const crt_wrl_creator_map_t **mock_mod_last(void *self) { (void)self; return &g_mock_entries[2]; }
static void *mock_mod_lock(void *self) { (void)self; return NULL; }

static const crt_wrl_module_vtbl_t g_mock_mod_vtbl = {
    NULL, NULL, NULL,
    mock_mod_get_count,
    mock_mod_first,
    mock_mod_mid,
    mock_mod_last,
    mock_mod_lock
};

static const wchar_t *mock_get_hstring_buf(void *hstr, uint32_t *out_len) {
    const wchar_t *s = (const wchar_t *)hstr;
    if (out_len) *out_len = s ? (uint32_t)wcslen(s) : 0u;
    return s;
}

static void test_wrl_module_activation(void) {
    crt_wrl_module_base_t mod = { &g_mock_mod_vtbl };
    void *factory = NULL;

    int32_t hr = crt_app_wrl_get_activation_factory(
        &mod, (void *)L"Unknown.Class", &factory, mock_get_hstring_buf);
    assert(hr == (int32_t)0x80040111u); /* CLASS_E_CLASSNOTAVAILABLE */
    assert(factory == NULL);

    hr = crt_app_wrl_get_activation_factory(
        &mod, (void *)L"UnityEngineDelegates.PlatformInvoke", &factory, mock_get_hstring_buf);
    assert(hr == 0);
    assert(factory == &g_mock_factory);

    /* Second lookup uses factory_cache */
    void *factory2 = NULL;
    hr = crt_app_wrl_get_activation_factory(
        &mod, (void *)L"UnityEngineDelegates.PlatformInvoke", &factory2, mock_get_hstring_buf);
    assert(hr == 0);
    assert(factory2 == &g_mock_factory);

    mock_factory_release(factory);
    mock_factory_release(factory2);
    g_mock_obj_count = 0u;
    g_mock_factory_released = 0;
    assert(crt_app_wrl_terminate_module(&mod) == 1u);
    assert(g_mock_factory_released == 1);
    assert(g_mock_cache.factory == NULL);
}

int main(void) {
    test_diag_stub_trampoline();
    test_cyl_bessel_i();
    test_intptr_and_rect();
    test_aligned_heap_and_object();
    test_wrl_module_activation();
    printf("[PASS] All CRT app forwarder runtime unit tests passed.\n");
    return 0;
}
