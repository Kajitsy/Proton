/*
 * Host unit tests for:
 *   1. Clean-room UWP Package Identity & AppPolicy resolution (uwp_pkg_identity.h)
 *   2. kernelx_pe.c exported GetCurrentPackage* / AppPolicy* functions
 *   3. Diagnostic auto-stub trampoline runtime (scarlett_app_stub_runtime.h)
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uwp_pkg_identity.h"
#include "scarlett_app_stub_runtime.h"

/* Declarations from kernelx_pe.c host build */
extern void kernelx_set_host_pkg_provider(const uwp_pkg_provider_t *provider);
extern int32_t GetCurrentPackageFullName(uint32_t *len, uint16_t *name);
extern int32_t GetCurrentPackageId(uint32_t *len, uint8_t *buf);
extern int32_t GetCurrentPackageFamilyName(uint32_t *len, uint16_t *name);
extern int32_t GetCurrentApplicationUserModelId(uint32_t *len, uint16_t *id);
extern int32_t GetPackagePathByFullName(const uint16_t *full_name, uint32_t *len, uint16_t *path);
extern int32_t GetCurrentPackagePath(uint32_t *len, uint16_t *path);
extern int32_t AppPolicyGetProcessTerminationMethod(void *token, uint32_t *policy);
extern int32_t AppPolicyGetWindowingModel(void *token, uint32_t *policy);

typedef struct mock_kv_s {
    const char *key;
    const char *val;
} mock_kv_t;

typedef struct mock_env_fs_s {
    const mock_kv_t *envs;
    size_t env_count;
    const char *exe_path;
    const char *manifest_path;
    const char *manifest_xml;
} mock_env_fs_t;

static int u16_eq_ascii(const uint16_t *u, const char *a) {
    if (!u || !a) return 0;
    size_t i = 0;
    while (u[i] != 0 && a[i] != '\0') {
        if (u[i] != (uint16_t)(uint8_t)a[i]) return 0;
        ++i;
    }
    return u[i] == 0 && a[i] == '\0';
}

static uint32_t mock_get_env(
    const uint16_t *name, uint16_t *buf, uint32_t cap, void *ud) {
    const mock_env_fs_t *m = (const mock_env_fs_t *)ud;
    for (size_t i = 0; i < m->env_count; ++i) {
        if (u16_eq_ascii(name, m->envs[i].key)) {
            uwp_u16_from_ascii(buf, cap, m->envs[i].val);
            return (uint32_t)uwp_u16_len(buf);
        }
    }
    return 0u;
}

static uint32_t mock_get_exe_path(uint16_t *buf, uint32_t cap, void *ud) {
    const mock_env_fs_t *m = (const mock_env_fs_t *)ud;
    if (!m->exe_path) return 0u;
    uwp_u16_from_ascii(buf, cap, m->exe_path);
    return (uint32_t)uwp_u16_len(buf);
}

static int mock_read_file(
    const uint16_t *path, uint8_t *buf, size_t cap, size_t *out_len, void *ud) {
    const mock_env_fs_t *m = (const mock_env_fs_t *)ud;
    if (!m->manifest_path || !m->manifest_xml) return 0;
    if (uwp_u16_icmp_ascii(path, m->manifest_path) != 0) return 0;
    size_t len = strlen(m->manifest_xml);
    if (len > cap) len = cap;
    memcpy(buf, m->manifest_xml, len);
    *out_len = len;
    return 1;
}

static void test_publisher_id_hash(void) {
    uint16_t pub[128];
    uint16_t pid[32];
    uwp_u16_from_ascii(
        pub, 128,
        "CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US");
    uwp_pkg_compute_publisher_id(pub, pid);
    assert(u16_eq_ascii(pid, "8wekyb3d8bbwe"));
    printf("[PASS] PublisherId SHA-256 + base32 matches Microsoft 8wekyb3d8bbwe\n");
}

static void test_bridge_disabled_returns_no_package(void) {
    mock_kv_t envs[] = {
        {"PROTON_UWP_BRIDGE", "0"},
        {"PROTON_UWP_PACKAGE_NAME", "ShouldBeIgnored"},
    };
    mock_env_fs_t mock = {envs, 2, "Z:\\game\\app.exe", NULL, NULL};
    uwp_pkg_provider_t prov = {mock_get_env, mock_get_exe_path, mock_read_file, &mock};

    uwp_pkg_resolved_t pkg;
    uwp_pkg_resolve_with_provider(&prov, 1, &pkg);
    assert(pkg.bridge_enabled == 0u);
    assert(pkg.valid == 0u);
    printf("[PASS] PROTON_UWP_BRIDGE=0 disables UWP package identity override\n");
}

static void test_env_var_identity_and_kernelx_exports(void) {
    mock_kv_t envs[] = {
        {"PROTON_UWP_BRIDGE", "1"},
        {"PROTON_UWP_PACKAGE_NAME", "HedgePhysics"},
        {"PROTON_UWP_PACKAGE_PUBLISHER", "CN=..."},
        {"PROTON_UWP_PACKAGE_VERSION", "1.2.3.0"},
        {"PROTON_UWP_PACKAGE_FULL_NAME", "HedgePhysics_1.2.3.0_x64__6033t3g2k8j10"},
        {"PROTON_UWP_PACKAGE_FAMILY_NAME", "HedgePhysics_6033t3g2k8j10"},
        {"PROTON_UWP_AUMID", "HedgePhysics_6033t3g2k8j10!App"},
        {"PROTON_UWP_PACKAGE_DIR", "Z:\\games\\HedgePhysics"},
    };
    mock_env_fs_t mock = {envs, 8, "Z:\\games\\HedgePhysics\\HedgePhysics.exe", NULL, NULL};
    uwp_pkg_provider_t prov = {mock_get_env, mock_get_exe_path, mock_read_file, &mock};

    kernelx_set_host_pkg_provider(&prov);

    uint16_t buf[260];
    uint32_t len = 0;

    /* Buffer size query */
    assert(GetCurrentPackageFullName(&len, NULL) == UWP_ERROR_INSUFFICIENT_BUFFER);
    assert(len == strlen("HedgePhysics_1.2.3.0_x64__6033t3g2k8j10") + 1);
    assert(GetCurrentPackageFullName(&len, buf) == UWP_ERROR_SUCCESS);
    assert(u16_eq_ascii(buf, "HedgePhysics_1.2.3.0_x64__6033t3g2k8j10"));

    len = 260;
    assert(GetCurrentPackageFamilyName(&len, buf) == UWP_ERROR_SUCCESS);
    assert(u16_eq_ascii(buf, "HedgePhysics_6033t3g2k8j10"));

    len = 260;
    assert(GetCurrentApplicationUserModelId(&len, buf) == UWP_ERROR_SUCCESS);
    assert(u16_eq_ascii(buf, "HedgePhysics_6033t3g2k8j10!App"));

    len = 260;
    assert(GetCurrentPackagePath(&len, buf) == UWP_ERROR_SUCCESS);
    assert(u16_eq_ascii(buf, "Z:\\games\\HedgePhysics"));

    uint16_t query_name[260];
    uwp_u16_from_ascii(query_name, 260, "hedgephysics_1.2.3.0_x64__6033t3g2k8j10");
    len = 260;
    assert(GetPackagePathByFullName(query_name, &len, buf) == UWP_ERROR_SUCCESS);
    assert(u16_eq_ascii(buf, "Z:\\games\\HedgePhysics"));

    uwp_u16_from_ascii(query_name, 260, "WrongPackage_1.0.0.0_x64__8wekyb3d8bbwe");
    len = 260;
    assert(GetPackagePathByFullName(query_name, &len, buf) == UWP_APPMODEL_ERROR_NO_PACKAGE);

    /* PACKAGE_ID check */
    uint8_t id_buf[1024];
    uint32_t id_len = 0;
    assert(GetCurrentPackageId(&id_len, NULL) == UWP_ERROR_INSUFFICIENT_BUFFER);
    assert(id_len > sizeof(uwp_pkg_id_t) && id_len < sizeof(id_buf));
    assert(GetCurrentPackageId(&id_len, id_buf) == UWP_ERROR_SUCCESS);
    const uwp_pkg_id_t *id = (const uwp_pkg_id_t *)id_buf;
    assert(id->processorArchitecture == UWP_PROC_ARCH_AMD64);
    assert(id->version.Major == 1 && id->version.Minor == 2 &&
           id->version.Build == 3 && id->version.Revision == 0);
    assert(u16_eq_ascii(id->name, "HedgePhysics"));
    assert(u16_eq_ascii(id->publisher, "CN=..."));
    assert(u16_eq_ascii(id->publisherId, "6033t3g2k8j10"));

    /* AppPolicy */
    uint32_t policy = 0;
    assert(AppPolicyGetWindowingModel(NULL, &policy) == UWP_ERROR_SUCCESS);
    assert(policy == UWP_APPPOLICY_WIN_UNIVERSAL);
    policy = 0;
    assert(AppPolicyGetProcessTerminationMethod(NULL, &policy) == UWP_ERROR_SUCCESS);
    assert(policy == UWP_APPPOLICY_TERM_TERMINATE_PROCESS);

    kernelx_set_host_pkg_provider(NULL);
    printf("[PASS] Env var Package Identity, PACKAGE_ID, GetPackagePathByFullName & AppPolicy\n");
}

static void test_appxmanifest_and_microsoftgame_config_fallback(void) {
    static const char *const appx_xml =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<Package xmlns=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10\">\n"
        "  <!-- <Identity Name=\"IgnoredComment\"/> -->\n"
        "  <Identity Name=\"d19ff9e2-fbcc-4ab7-969e-c3d7d7b842f3\" "
        "Publisher=\"CN=aerisarn\" Version=\"1.0.7.0\" ProcessorArchitecture=\"x64\"/>\n"
        "  <Applications><Application Id=\"GZDoom\" Executable=\"GZDoom.exe\"/></Applications>\n"
        "</Package>\n";

    mock_kv_t envs[] = { {"PROTON_UWP_BRIDGE", "1"} };
    mock_env_fs_t mock = {
        envs, 1,
        "Z:\\zdoom\\gzdoom.exe",
        "Z:\\zdoom\\AppxManifest.xml",
        appx_xml
    };
    uwp_pkg_provider_t prov = {mock_get_env, mock_get_exe_path, mock_read_file, &mock};

    uwp_pkg_resolved_t pkg;
    uwp_pkg_resolve_with_provider(&prov, 1, &pkg);
    assert(pkg.valid == 1u);
    assert(u16_eq_ascii(pkg.name, "d19ff9e2-fbcc-4ab7-969e-c3d7d7b842f3"));
    assert(u16_eq_ascii(pkg.publisher, "CN=aerisarn"));
    assert(pkg.version.Major == 1 && pkg.version.Minor == 0 &&
           pkg.version.Build == 7 && pkg.version.Revision == 0);
    assert(uwp_u16_len(pkg.publisher_id) == 13u);
    assert(u16_eq_ascii(pkg.app_id, "GZDoom"));
    assert(u16_eq_ascii(pkg.package_dir, "Z:\\zdoom"));

    static const char *const mgc_xml =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<Game configVersion=\"1\">\n"
        "  <Identity Name=\"ScarlettTestTitle\" Publisher=\"CN=Test\" Version=\"2.4.6.8\"/>\n"
        "  <ExecutableList><Executable Name=\"Game.exe\" Id=\"Game\"/></ExecutableList>\n"
        "</Game>\n";
    mock.exe_path = "Z:\\gdk\\Game.exe";
    mock.manifest_path = "Z:\\gdk\\MicrosoftGame.Config";
    mock.manifest_xml = mgc_xml;
    uwp_pkg_resolve_with_provider(&prov, 1, &pkg);
    assert(pkg.valid == 1u);
    assert(u16_eq_ascii(pkg.name, "ScarlettTestTitle"));
    assert(pkg.version.Major == 2 && pkg.version.Minor == 4 &&
           pkg.version.Build == 6 && pkg.version.Revision == 8);
    assert(u16_eq_ascii(pkg.app_id, "Game"));
    printf("[PASS] AppxManifest.xml and MicrosoftGame.Config fallback parsing\n");
}

SCARLETT_APP_DEFINE_STUB("KERNEL32_APP.dll", TestMissingUwpApi, 0u, SCARLETT_E_NOTIMPL)

static scarlett_stub_action_t test_sink(const scarlett_stub_context_t *ctx, void *ud) {
    int *called = (int *)ud;
    *called += 1;
    assert(strcmp(ctx->dll_name, "KERNEL32_APP.dll") == 0);
    assert(strcmp(ctx->symbol_name, "TestMissingUwpApi") == 0);
    return scarlett_stub_resolve_action(ctx);
}

static void test_diagnostic_stub_trampoline(void) {
    int sink_calls = 0;
    scarlett_app_stub_reset_stats();
    scarlett_app_stub_set_sink(test_sink, &sink_calls);
    scarlett_app_stub_set_strict_override(0);

    scarlett_hresult_t hr = scarlett_stub_fn_TestMissingUwpApi(
        0x1111u, 0x2222u, 0x3333u, 0x4444u);
    assert(hr == SCARLETT_E_NOTIMPL);
    assert(hr < 0);
    assert(sink_calls == 1);

    scarlett_stub_context_t ctx;
    scarlett_app_stub_get_last_context(&ctx);
    assert(ctx.rcx == 0x1111u && ctx.rdx == 0x2222u &&
           ctx.r8 == 0x3333u && ctx.r9 == 0x4444u);
    assert(ctx.strict_mode == 0u);
    assert(ctx.return_address != 0u);

    /* Strict mode must return SCARLETT_ERR_STRICT_STUB_ABORT */
    scarlett_app_stub_set_strict_override(1);
    hr = scarlett_stub_fn_TestMissingUwpApi(1u, 2u, 3u, 4u);
    assert(hr == SCARLETT_ERR_STRICT_STUB_ABORT);
    assert(sink_calls == 2);

    scarlett_app_stub_set_sink(NULL, NULL);
    scarlett_app_stub_set_strict_override(-1);
    printf("[PASS] Diagnostic auto-stub trampoline captures args & honors PROTON_XBOX_STRICT\n");
}

int main(void) {
    test_publisher_id_hash();
    test_bridge_disabled_returns_no_package();
    test_env_var_identity_and_kernelx_exports();
    test_appxmanifest_and_microsoftgame_config_fallback();
    test_diagnostic_stub_trampoline();
    return 0;
}
