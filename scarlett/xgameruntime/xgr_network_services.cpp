/*
 * Proton-Scarlett — Network Policy (offline|lan|passthrough), libHttpClient,
 *                   XCurl, XSAPI & PlayFab Local Services
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "xgameruntime_internal.h"

#include <arpa/inet.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace scarlett::xgameruntime::unixlib {

namespace {

std::string to_lower_ascii(const char *s) {
    if (s == nullptr) return {};
    std::string out;
    for (size_t i = 0; s[i] != '\0'; ++i) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(s[i]))));
    }
    return out;
}

scarlett_net_mode_t resolve_net_mode(scarlett_net_mode_t fallback) {
    const char *env = std::getenv(SCARLETT_ENV_NET_MODE);
    if (env != nullptr && env[0] != '\0') {
        std::string lower = to_lower_ascii(env);
        if (lower == "lan") return SCARLETT_NET_MODE_LAN;
        if (lower == "passthrough") return SCARLETT_NET_MODE_PASSTHROUGH;
        if (lower == "offline") return SCARLETT_NET_MODE_OFFLINE;
    }
    return fallback;
}

bool parse_url_host_port(const std::string &input,
                         std::string &out_scheme,
                         std::string &out_host,
                         uint16_t &out_port,
                         std::string &out_path) {
    out_scheme = "http";
    out_host.clear();
    out_port = 80u;
    out_path = "/";

    std::string s = input;
    auto scheme_pos = s.find("://");
    if (scheme_pos != std::string::npos) {
        out_scheme = to_lower_ascii(s.substr(0, scheme_pos).c_str());
        s = s.substr(scheme_pos + 3);
        if (out_scheme == "https" || out_scheme == "wss") {
            out_port = 443u;
        }
    }

    auto slash_pos = s.find('/');
    std::string authority = (slash_pos == std::string::npos) ? s : s.substr(0, slash_pos);
    if (slash_pos != std::string::npos) {
        out_path = s.substr(slash_pos);
    }

    auto at_pos = authority.rfind('@');
    if (at_pos != std::string::npos) {
        authority = authority.substr(at_pos + 1);
    }
    if (authority.empty()) {
        return false;
    }

    if (authority.front() == '[') {
        auto close_br = authority.find(']');
        if (close_br == std::string::npos) {
            return false;
        }
        out_host = authority.substr(1, close_br - 1);
        if (close_br + 1 < authority.size() && authority[close_br + 1] == ':') {
            out_port = static_cast<uint16_t>(std::atoi(authority.substr(close_br + 2).c_str()));
        }
    } else {
        auto colon_pos = authority.rfind(':');
        if (colon_pos != std::string::npos) {
            out_host = authority.substr(0, colon_pos);
            out_port = static_cast<uint16_t>(std::atoi(authority.substr(colon_pos + 1).c_str()));
        } else {
            out_host = authority;
        }
    }
    return !out_host.empty();
}

bool is_lan_or_loopback_host(const std::string &host) {
    std::string lower = to_lower_ascii(host.c_str());
    if (lower == "localhost" || lower == "localhost.localdomain") {
        return true;
    }

    struct in_addr v4{};
    if (::inet_pton(AF_INET, lower.c_str(), &v4) == 1) {
        uint32_t addr = ntohl(v4.s_addr);
        /* 127.0.0.0/8 */
        if ((addr & 0xFF000000u) == 0x7F000000u) return true;
        /* 10.0.0.0/8 */
        if ((addr & 0xFF000000u) == 0x0A000000u) return true;
        /* 172.16.0.0/12 */
        if ((addr & 0xFFF00000u) == 0xAC100000u) return true;
        /* 192.168.0.0/16 */
        if ((addr & 0xFFFF0000u) == 0xC0A80000u) return true;
        /* 169.254.0.0/16 link-local */
        if ((addr & 0xFFFF0000u) == 0xA9FE0000u) return true;
        return false;
    }

    struct in6_addr v6{};
    /* Strip optional %zone_id */
    std::string clean_v6 = lower;
    auto pct = clean_v6.find('%');
    if (pct != std::string::npos) {
        clean_v6 = clean_v6.substr(0, pct);
    }
    if (::inet_pton(AF_INET6, clean_v6.c_str(), &v6) == 1) {
        if (IN6_IS_ADDR_LOOPBACK(&v6)) return true;
        if (IN6_IS_ADDR_LINKLOCAL(&v6)) return true;
        /* fc00::/7 ULA */
        if ((v6.s6_addr[0] & 0xFEu) == 0xFCu) return true;
        return false;
    }
    return false;
}

void resolve_ca_paths(std::string &out_file, std::string &out_dir) {
    out_file.clear();
    out_dir.clear();

    const char *env_file = std::getenv("SSL_CERT_FILE");
    if (env_file != nullptr && env_file[0] != '\0') {
        out_file = env_file;
    } else {
        static const char *const kCandidateFiles[] = {
            "/etc/ssl/certs/ca-certificates.crt",
            "/etc/pki/tls/certs/ca-bundle.crt",
            "/etc/ssl/ca-bundle.pem",
        };
        for (const char *p : kCandidateFiles) {
            if (std::filesystem::exists(p)) {
                out_file = p;
                break;
            }
        }
    }

    const char *env_dir = std::getenv("SSL_CERT_DIR");
    if (env_dir != nullptr && env_dir[0] != '\0') {
        out_dir = env_dir;
    } else if (std::filesystem::is_directory("/etc/ssl/certs")) {
        out_dir = "/etc/ssl/certs";
    }
}

scarlett_hresult_t check_net_policy_for_url(scarlett_net_mode_t mode,
                                            const std::string &url_or_host,
                                            bool &out_allowed,
                                            std::string &out_scheme,
                                            std::string &out_host,
                                            uint16_t &out_port,
                                            std::string &out_path) {
    out_allowed = false;
    if (!parse_url_host_port(url_or_host, out_scheme, out_host, out_port, out_path)) {
        return SCARLETT_E_INVALIDARG;
    }
    if (mode == SCARLETT_NET_MODE_OFFLINE) {
        return SCARLETT_ERR_NET_OFFLINE_BLOCKED;
    }
    if (mode == SCARLETT_NET_MODE_LAN) {
        if (!is_lan_or_loopback_host(out_host)) {
            return SCARLETT_ERR_NET_LAN_POLICY_BLOCKED;
        }
        out_allowed = true;
        return SCARLETT_OK;
    }
    /* SCARLETT_NET_MODE_PASSTHROUGH */
    if (out_scheme == "https" || out_scheme == "wss") {
        std::string cf, cd;
        resolve_ca_paths(cf, cd);
        if (cf.empty() && cd.empty()) {
            return SCARLETT_ERR_NET_TLS_CERT_UNAVAILABLE;
        }
        if (!cf.empty() && !std::filesystem::exists(cf)) {
            return SCARLETT_ERR_NET_TLS_CERT_UNAVAILABLE;
        }
    }
    out_allowed = true;
    return SCARLETT_OK;
}

bool try_loopback_http_exchange(const std::string &method,
                                const std::string &host,
                                uint16_t port,
                                const std::string &path,
                                const std::vector<std::pair<std::string, std::string>> &headers,
                                const std::vector<uint8_t> &body,
                                uint32_t &out_http_status,
                                std::vector<uint8_t> &out_resp_body) {
    struct in_addr v4{};
    std::string connect_ip = host;
    if (to_lower_ascii(host.c_str()) == "localhost") {
        connect_ip = "127.0.0.1";
    }
    if (::inet_pton(AF_INET, connect_ip.c_str(), &v4) != 1) {
        return false;
    }
    /* Only attempt actual socket connect to loopback in automated/test environments */
    uint32_t addr = ntohl(v4.s_addr);
    if ((addr & 0xFF000000u) != 0x7F000000u) {
        return false;
    }

    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return false;
    }
    struct timeval tv{};
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr = v4;

    if (::connect(fd, reinterpret_cast<struct sockaddr *>(&sa), sizeof(sa)) != 0) {
        ::close(fd);
        return false;
    }

    std::ostringstream req;
    req << (method.empty() ? "GET" : method) << " " << (path.empty() ? "/" : path)
        << " HTTP/1.1\r\nHost: " << host << ":" << port << "\r\nConnection: close\r\n";
    bool has_len = false;
    for (const auto &h : headers) {
        if (to_lower_ascii(h.first.c_str()) == "content-length") {
            has_len = true;
        }
        req << h.first << ": " << h.second << "\r\n";
    }
    if (!body.empty() && !has_len) {
        req << "Content-Length: " << body.size() << "\r\n";
    }
    req << "\r\n";
    std::string hdrs = req.str();

    (void)::send(fd, hdrs.data(), hdrs.size(), 0);
    if (!body.empty()) {
        (void)::send(fd, body.data(), body.size(), 0);
    }

    std::string raw;
    char buf[4096];
    ssize_t n = 0;
    while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) {
        raw.append(buf, static_cast<size_t>(n));
    }
    ::close(fd);

    if (raw.empty()) {
        return false;
    }
    auto hdr_end = raw.find("\r\n\r\n");
    std::string status_line = raw.substr(0, raw.find("\r\n"));
    out_http_status = 200u;
    auto sp1 = status_line.find(' ');
    if (sp1 != std::string::npos) {
        out_http_status = static_cast<uint32_t>(std::atoi(status_line.c_str() + sp1 + 1));
    }
    if (hdr_end != std::string::npos) {
        std::string b = raw.substr(hdr_end + 4);
        out_resp_body.assign(b.begin(), b.end());
    }
    return true;
}

/* ============================================================================
 * Internal State Stores
 * ============================================================================ */

struct HcCallRecord {
    uint64_t handle{0u};
    uint32_t ref_count{1u};
    std::string method{"GET"};
    std::string url;
    std::vector<std::pair<std::string, std::string>> req_headers;
    std::vector<uint8_t> req_body;
    uint32_t timeout_sec{30u};
    uint32_t resp_status_code{0u};
    scarlett_hresult_t net_error_code{SCARLETT_OK};
    uint32_t platform_err{0u};
    std::string resp_str;
    std::vector<uint8_t> resp_bytes;
    std::map<std::string, std::string> resp_headers;
};

struct HcWsRecord {
    uint64_t handle{0u};
    bool connected{false};
    std::string uri;
    std::string subprotocol;
};

struct CurlEasyRecord {
    uint64_t handle{0u};
    std::string url;
    std::string post_fields;
    std::string cainfo;
    std::string capath;
    uint64_t timeout_sec{30u};
    uint64_t response_code{0u};
    uint64_t os_errno{0u};
    uint64_t write_fn{0u};
    uint64_t write_data{0u};
    uint64_t slist_ptr{0u};
    bool is_post{false};
};

struct CurlMultiRecord {
    uint64_t handle{0u};
    std::vector<uint64_t> easy_handles;
};

struct XblContextRecord {
    uint64_t handle{0u};
    uint32_t ref_count{1u};
    uint64_t user_handle{0u};
    uint64_t xuid{0u};
};

struct XblAchResultRecord {
    uint64_t handle{0u};
    std::vector<XblAchievement> items;
};

struct PfServiceConfigRecord {
    uint64_t handle{0u};
    std::string conn_str;
    std::string title_id;
};

struct PfEntityRecord {
    uint64_t handle{0u};
    uint64_t cfg_handle{0u};
    uint64_t user_handle{0u};
    uint64_t xuid{0u};
};

struct NetState {
    std::mutex mutex;
    scarlett_net_mode_t default_mode{SCARLETT_NET_MODE_OFFLINE};
    uint64_t next_handle{0x9000u};

    bool hc_initialized{false};
    std::unordered_map<uint64_t, HcCallRecord> hc_calls;
    std::unordered_map<uint64_t, HcWsRecord> hc_ws;

    bool curl_initialized{false};
    std::unordered_map<uint64_t, CurlEasyRecord> curl_easy;
    std::unordered_map<uint64_t, CurlMultiRecord> curl_multi;

    bool xbl_initialized{false};
    std::string xbl_scid;
    std::unordered_map<uint64_t, XblContextRecord> xbl_ctxs;
    std::unordered_map<std::string, XblAchievement> xbl_achievements;
    std::unordered_map<uint64_t, XblAchResultRecord> xbl_ach_results;
    std::unordered_map<std::string, std::string> xbl_stats;

    bool pf_initialized{false};
    std::unordered_map<uint64_t, PfServiceConfigRecord> pf_cfgs;
    std::unordered_map<uint64_t, PfEntityRecord> pf_entities;
    std::unordered_map<std::string, std::string> pf_title_data;
    std::unordered_map<std::string, std::string> pf_player_data;

    NetState() {
        xbl_achievements["1"] = XblAchievement{"1", "First Boot", 2u, 0u};
        xbl_achievements["2"] = XblAchievement{"2", "Scarlett Explorer", 3u, 50u};
        xbl_stats["GamesPlayed"] = "1";
        pf_title_data["motd"] = "{\"message\":\"Offline Scarlett Title State\"}";
        pf_player_data["save_slot"] = "{\"slot\":1}";
    }
};

NetState &nstate() {
    static NetState s;
    return s;
}

scarlett_hresult_t copy_str_to_buf(uint64_t addr,
                                     uint32_t cap,
                                     const std::string &src,
                                     uint32_t *out_used = nullptr) {
    const uint32_t need = static_cast<uint32_t>(src.size()) + 1u;
    if (out_used != nullptr) {
        *out_used = need;
    }
    char *dst = reinterpret_cast<char *>(static_cast<uintptr_t>(addr));
    if (dst == nullptr) {
        return SCARLETT_E_POINTER;
    }
    if (cap < need) {
        if (cap > 0u) {
            std::memcpy(dst, src.data(), cap - 1u);
            dst[cap - 1u] = '\0';
        }
        return SCARLETT_HRESULT_C(0x8007007Au);
    }
    std::memcpy(dst, src.c_str(), need);
    return SCARLETT_OK;
}

} // namespace

NetworkServicesManager &NetworkServicesManager::instance() {
    static NetworkServicesManager mgr;
    return mgr;
}

void NetworkServicesManager::initialize(scarlett_net_mode_t net_mode) {
    std::lock_guard<std::mutex> lock(nstate().mutex);
    nstate().default_mode = net_mode;
}

void NetworkServicesManager::shutdown() {
    std::lock_guard<std::mutex> lock(nstate().mutex);
    nstate().hc_calls.clear();
    nstate().hc_ws.clear();
    nstate().curl_easy.clear();
    nstate().curl_multi.clear();
    nstate().xbl_ctxs.clear();
    nstate().xbl_ach_results.clear();
    nstate().pf_cfgs.clear();
    nstate().pf_entities.clear();
}

scarlett_hresult_t NetworkServicesManager::net_config(scarlett_xgr_net_config_params_t &p) {
    std::lock_guard<std::mutex> lock(nstate().mutex);
    const scarlett_net_mode_t mode = resolve_net_mode(nstate().default_mode);
    p.out_net_mode = static_cast<uint32_t>(mode);

    switch (static_cast<scarlett_xgr_net_config_subop_t>(p.subop)) {
        case SCARLETT_XGR_NET_GET_POLICY:
            p.out_allowed = (mode != SCARLETT_NET_MODE_OFFLINE) ? 1u : 0u;
            p.out_status = SCARLETT_OK;
            break;

        case SCARLETT_XGR_NET_CHECK_HOST: {
            const char *host = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.host_addr));
            if (host == nullptr || host[0] == '\0') {
                p.out_allowed = 0u;
                p.out_status = SCARLETT_E_INVALIDARG;
                break;
            }
            bool allowed = false;
            std::string sc, h, pt;
            uint16_t port = 0u;
            p.out_status = check_net_policy_for_url(mode, host, allowed, sc, h, port, pt);
            p.out_allowed = allowed ? 1u : 0u;
            break;
        }

        case SCARLETT_XGR_NET_GET_CA_PATHS: {
            std::string cf, cd;
            resolve_ca_paths(cf, cd);
            if (p.cert_file_addr != 0u && p.cert_file_cap > 0u) {
                (void)copy_str_to_buf(p.cert_file_addr, p.cert_file_cap, cf);
            }
            if (p.cert_dir_addr != 0u && p.cert_dir_cap > 0u) {
                (void)copy_str_to_buf(p.cert_dir_addr, p.cert_dir_cap, cd);
            }
            p.out_status = SCARLETT_OK;
            break;
        }

        case SCARLETT_XGR_NET_REFRESH_ENV:
            p.out_status = SCARLETT_OK;
            break;

        default:
            p.out_status = SCARLETT_E_INVALIDARG;
            break;
    }
    return p.out_status;
}

scarlett_hresult_t NetworkServicesManager::hc_op(scarlett_xgr_hc_params_t &p) {
    std::lock_guard<std::mutex> lock(nstate().mutex);
    const scarlett_net_mode_t mode = resolve_net_mode(nstate().default_mode);

    switch (static_cast<scarlett_xgr_hc_subop_t>(p.subop)) {
        case SCARLETT_XGR_HC_INIT:
            nstate().hc_initialized = true;
            p.out_status = SCARLETT_OK;
            break;
        case SCARLETT_XGR_HC_CLEANUP:
            nstate().hc_initialized = false;
            nstate().hc_calls.clear();
            nstate().hc_ws.clear();
            p.out_status = SCARLETT_OK;
            break;
        case SCARLETT_XGR_HC_CALL_CREATE: {
            HcCallRecord rec{};
            rec.handle = ++nstate().next_handle;
            rec.ref_count = 1u;
            nstate().hc_calls[rec.handle] = rec;
            p.out_u64 = rec.handle;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_HC_CALL_DUP: {
            auto it = nstate().hc_calls.find(p.handle);
            if (it == nstate().hc_calls.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            ++it->second.ref_count;
            p.out_u64 = it->second.handle;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_HC_CALL_CLOSE: {
            auto it = nstate().hc_calls.find(p.handle);
            if (it == nstate().hc_calls.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            if (--it->second.ref_count == 0u) {
                nstate().hc_calls.erase(it);
            }
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_HC_SET_URL: {
            auto it = nstate().hc_calls.find(p.handle);
            if (it == nstate().hc_calls.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            const char *m = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_a_addr));
            const char *u = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_b_addr));
            if (m == nullptr || u == nullptr) {
                p.out_status = SCARLETT_E_POINTER;
                break;
            }
            it->second.method = m;
            it->second.url = u;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_HC_SET_BODY: {
            auto it = nstate().hc_calls.find(p.handle);
            if (it == nstate().hc_calls.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            const auto *b = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(p.buf_addr));
            if (b != nullptr && p.buf_size > 0u) {
                it->second.req_body.assign(b, b + p.buf_size);
            } else {
                it->second.req_body.clear();
            }
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_HC_SET_HEADER: {
            auto it = nstate().hc_calls.find(p.handle);
            if (it == nstate().hc_calls.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            const char *hn = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_a_addr));
            const char *hv = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_b_addr));
            if (hn == nullptr || hv == nullptr) {
                p.out_status = SCARLETT_E_POINTER;
                break;
            }
            it->second.req_headers.emplace_back(hn, hv);
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_HC_SET_TIMEOUT: {
            auto it = nstate().hc_calls.find(p.handle);
            if (it == nstate().hc_calls.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            it->second.timeout_sec = p.u32_a;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_HC_PERFORM: {
            auto it = nstate().hc_calls.find(p.handle);
            if (it == nstate().hc_calls.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            bool allowed = false;
            std::string scheme, host, path;
            uint16_t port = 0u;
            scarlett_hresult_t phr = check_net_policy_for_url(
                mode, it->second.url, allowed, scheme, host, port, path);

            if (!allowed) {
                it->second.net_error_code = phr;
                it->second.platform_err = static_cast<uint32_t>(phr);
                it->second.resp_status_code = 0u;
                it->second.resp_str = (mode == SCARLETT_NET_MODE_OFFLINE)
                                          ? "{\"scarlett_net_mode\":\"offline\",\"cached\":true}"
                                          : "{\"scarlett_net_mode\":\"lan_blocked\"}";
                it->second.resp_bytes.assign(it->second.resp_str.begin(), it->second.resp_str.end());
                it->second.resp_headers["X-Scarlett-Net-Policy"] =
                    (mode == SCARLETT_NET_MODE_OFFLINE) ? "offline" : "lan-blocked";
                p.out_status = phr;
                break;
            }

            uint32_t http_code = 200u;
            std::vector<uint8_t> rbody;
            if (!try_loopback_http_exchange(it->second.method, host, port, path,
                                            it->second.req_headers, it->second.req_body,
                                            http_code, rbody)) {
                /* Deterministic simulated response when no local test server is bound */
                std::string cf, cd;
                resolve_ca_paths(cf, cd);
                std::ostringstream oss;
                oss << "{\"scarlett_net_mode\":\""
                    << (mode == SCARLETT_NET_MODE_LAN ? "lan" : "passthrough")
                    << "\",\"host\":\"" << host << "\",\"ssl_cert_file\":\"" << cf << "\"}";
                std::string s = oss.str();
                rbody.assign(s.begin(), s.end());
            }
            it->second.net_error_code = SCARLETT_OK;
            it->second.platform_err = 0u;
            it->second.resp_status_code = http_code;
            it->second.resp_bytes = std::move(rbody);
            it->second.resp_str.assign(it->second.resp_bytes.begin(), it->second.resp_bytes.end());
            it->second.resp_headers["X-Scarlett-Net-Policy"] =
                (mode == SCARLETT_NET_MODE_LAN) ? "lan" : "passthrough";
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_HC_GET_STATUS_CODE: {
            auto it = nstate().hc_calls.find(p.handle);
            if (it == nstate().hc_calls.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            p.out_u32 = it->second.resp_status_code;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_HC_GET_NET_ERR: {
            auto it = nstate().hc_calls.find(p.handle);
            if (it == nstate().hc_calls.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            p.out_u64 = static_cast<uint64_t>(static_cast<uint32_t>(it->second.net_error_code));
            p.out_u32 = it->second.platform_err;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_HC_GET_RESP_STR: {
            auto it = nstate().hc_calls.find(p.handle);
            if (it == nstate().hc_calls.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            p.out_u64 = reinterpret_cast<uint64_t>(it->second.resp_str.c_str());
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_HC_GET_RESP_BYTES: {
            auto it = nstate().hc_calls.find(p.handle);
            if (it == nstate().hc_calls.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            p.out_u64 = it->second.resp_bytes.size();
            if (p.buf_addr != 0u) {
                uint64_t n = std::min<uint64_t>(p.buf_size, it->second.resp_bytes.size());
                if (n > 0u) {
                    std::memcpy(reinterpret_cast<void *>(static_cast<uintptr_t>(p.buf_addr)),
                                it->second.resp_bytes.data(), n);
                }
                if (p.buf_size < it->second.resp_bytes.size()) {
                    p.out_status = SCARLETT_HRESULT_C(0x8007007Au);
                    break;
                }
            }
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_HC_GET_RESP_HEADER: {
            auto it = nstate().hc_calls.find(p.handle);
            if (it == nstate().hc_calls.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            const char *hn = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_a_addr));
            if (hn == nullptr) {
                p.out_status = SCARLETT_E_POINTER;
                break;
            }
            auto hit = it->second.resp_headers.find(hn);
            p.out_u64 = (hit != it->second.resp_headers.end())
                            ? reinterpret_cast<uint64_t>(hit->second.c_str())
                            : 0u;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_HC_WS_CREATE: {
            HcWsRecord ws{};
            ws.handle = ++nstate().next_handle;
            nstate().hc_ws[ws.handle] = ws;
            p.out_u64 = ws.handle;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_HC_WS_CONNECT: {
            auto it = nstate().hc_ws.find(p.handle);
            if (it == nstate().hc_ws.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            const char *uri = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_a_addr));
            if (uri == nullptr) {
                p.out_status = SCARLETT_E_POINTER;
                break;
            }
            bool allowed = false;
            std::string sc, h, pt;
            uint16_t port = 0u;
            scarlett_hresult_t phr = check_net_policy_for_url(mode, uri, allowed, sc, h, port, pt);
            if (!allowed) {
                p.out_status = phr;
                break;
            }
            it->second.uri = uri;
            it->second.connected = true;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_HC_WS_SEND: {
            auto it = nstate().hc_ws.find(p.handle);
            if (it == nstate().hc_ws.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            p.out_status = it->second.connected ? SCARLETT_OK : SCARLETT_ERR_NET_OFFLINE_BLOCKED;
            break;
        }
        case SCARLETT_XGR_HC_WS_DISCONNECT: {
            auto it = nstate().hc_ws.find(p.handle);
            if (it != nstate().hc_ws.end()) {
                it->second.connected = false;
            }
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_HC_WS_CLOSE: {
            nstate().hc_ws.erase(p.handle);
            p.out_status = SCARLETT_OK;
            break;
        }
        default:
            p.out_status = SCARLETT_E_INVALIDARG;
            break;
    }
    return p.out_status;
}

scarlett_hresult_t NetworkServicesManager::xcurl_op(scarlett_xgr_xcurl_params_t &p) {
    std::lock_guard<std::mutex> lock(nstate().mutex);
    const scarlett_net_mode_t mode = resolve_net_mode(nstate().default_mode);

    switch (static_cast<scarlett_xgr_xcurl_subop_t>(p.subop)) {
        case SCARLETT_XGR_XCURL_GLOBAL_INIT:
            nstate().curl_initialized = true;
            p.out_curl_code = CURLE_OK;
            p.out_status = SCARLETT_OK;
            break;
        case SCARLETT_XGR_XCURL_GLOBAL_CLEANUP:
            nstate().curl_initialized = false;
            nstate().curl_easy.clear();
            nstate().curl_multi.clear();
            p.out_curl_code = CURLE_OK;
            p.out_status = SCARLETT_OK;
            break;
        case SCARLETT_XGR_XCURL_EASY_INIT: {
            CurlEasyRecord rec{};
            rec.handle = ++nstate().next_handle;
            resolve_ca_paths(rec.cainfo, rec.capath);
            nstate().curl_easy[rec.handle] = rec;
            p.handle = rec.handle;
            p.out_curl_code = CURLE_OK;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XCURL_EASY_CLEANUP:
            nstate().curl_easy.erase(p.handle);
            p.out_curl_code = CURLE_OK;
            p.out_status = SCARLETT_OK;
            break;
        case SCARLETT_XGR_XCURL_EASY_SETOPT_STR: {
            auto it = nstate().curl_easy.find(p.handle);
            if (it == nstate().curl_easy.end()) {
                p.out_curl_code = CURLE_BAD_FUNCTION_ARGUMENT;
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            const char *sv = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_addr));
            std::string val = (sv != nullptr) ? sv : "";
            switch (p.option_id) {
                case CURLOPT_URL: it->second.url = val; break;
                case CURLOPT_POSTFIELDS: it->second.post_fields = val; break;
                case CURLOPT_CAINFO: it->second.cainfo = val; break;
                case CURLOPT_CAPATH: it->second.capath = val; break;
                default: break;
            }
            p.out_curl_code = CURLE_OK;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XCURL_EASY_SETOPT_U64: {
            auto it = nstate().curl_easy.find(p.handle);
            if (it == nstate().curl_easy.end()) {
                p.out_curl_code = CURLE_BAD_FUNCTION_ARGUMENT;
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            switch (p.option_id) {
                case CURLOPT_TIMEOUT: it->second.timeout_sec = p.value_u64; break;
                case CURLOPT_POST: it->second.is_post = (p.value_u64 != 0u); break;
                case CURLOPT_WRITEFUNCTION: it->second.write_fn = p.value_u64; break;
                case CURLOPT_WRITEDATA: it->second.write_data = p.value_u64; break;
                case CURLOPT_HTTPHEADER: it->second.slist_ptr = p.value_u64; break;
                default: break;
            }
            p.out_curl_code = CURLE_OK;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XCURL_EASY_PERFORM: {
            auto it = nstate().curl_easy.find(p.handle);
            if (it == nstate().curl_easy.end()) {
                p.out_curl_code = CURLE_BAD_FUNCTION_ARGUMENT;
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            if (it->second.url.empty()) {
                p.out_curl_code = CURLE_URL_MALFORMAT;
                p.out_policy_hr = SCARLETT_E_INVALIDARG;
                p.out_status = SCARLETT_E_INVALIDARG;
                break;
            }
            /* Refresh CA paths from env in passthrough if not explicitly overridden */
            if (it->second.cainfo.empty() && it->second.capath.empty()) {
                resolve_ca_paths(it->second.cainfo, it->second.capath);
            }

            bool allowed = false;
            std::string scheme, host, path;
            uint16_t port = 0u;
            scarlett_hresult_t phr = check_net_policy_for_url(
                mode, it->second.url, allowed, scheme, host, port, path);
            p.out_policy_hr = phr;

            if (!allowed) {
                it->second.response_code = 0u;
                it->second.os_errno = static_cast<uint64_t>(static_cast<uint32_t>(phr));
                p.out_curl_code = (phr == SCARLETT_ERR_NET_TLS_CERT_UNAVAILABLE)
                                      ? CURLE_SSL_CACERT_BADFILE
                                      : CURLE_COULDNT_CONNECT;
                p.out_status = phr;
                break;
            }

            uint32_t http_code = 200u;
            std::vector<uint8_t> rbody;
            std::vector<std::pair<std::string, std::string>> hdrs;
            std::vector<uint8_t> req_body(it->second.post_fields.begin(), it->second.post_fields.end());
            if (!try_loopback_http_exchange(it->second.is_post ? "POST" : "GET",
                                            host, port, path, hdrs, req_body, http_code, rbody)) {
                std::string mock = "{\"xcurl_mode\":\"" +
                                   std::string(mode == SCARLETT_NET_MODE_LAN ? "lan" : "passthrough") +
                                   "\",\"cainfo\":\"" + it->second.cainfo + "\"}";
                rbody.assign(mock.begin(), mock.end());
            }

            it->second.response_code = http_code;
            it->second.os_errno = 0u;

            if (it->second.write_fn != 0u && !rbody.empty()) {
                using curl_write_cb = size_t (*)(char *, size_t, size_t, void *);
                auto cb = reinterpret_cast<curl_write_cb>(static_cast<uintptr_t>(it->second.write_fn));
                (void)cb(reinterpret_cast<char *>(rbody.data()),
                         1u,
                         rbody.size(),
                         reinterpret_cast<void *>(static_cast<uintptr_t>(it->second.write_data)));
            }

            p.out_curl_code = CURLE_OK;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XCURL_EASY_GETINFO_U64: {
            auto it = nstate().curl_easy.find(p.handle);
            if (it == nstate().curl_easy.end()) {
                p.out_curl_code = CURLE_BAD_FUNCTION_ARGUMENT;
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            if (p.option_id == CURLINFO_RESPONSE_CODE) {
                p.value_u64 = it->second.response_code;
            } else if (p.option_id == CURLINFO_OS_ERRNO) {
                p.value_u64 = it->second.os_errno;
            } else {
                p.value_u64 = 0u;
            }
            p.out_curl_code = CURLE_OK;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XCURL_EASY_GETINFO_STR: {
            auto it = nstate().curl_easy.find(p.handle);
            if (it == nstate().curl_easy.end()) {
                p.out_curl_code = CURLE_BAD_FUNCTION_ARGUMENT;
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            if (p.option_id == CURLINFO_CAINFO) {
                if (it->second.cainfo.empty()) {
                    resolve_ca_paths(it->second.cainfo, it->second.capath);
                }
                p.value_u64 = reinterpret_cast<uint64_t>(it->second.cainfo.c_str());
            } else if (p.option_id == CURLINFO_CAPATH) {
                if (it->second.capath.empty()) {
                    resolve_ca_paths(it->second.cainfo, it->second.capath);
                }
                p.value_u64 = reinterpret_cast<uint64_t>(it->second.capath.c_str());
            } else if (p.option_id == CURLINFO_EFFECTIVE_URL) {
                p.value_u64 = reinterpret_cast<uint64_t>(it->second.url.c_str());
            } else {
                p.value_u64 = 0u;
            }
            p.out_curl_code = CURLE_OK;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XCURL_MULTI_INIT: {
            CurlMultiRecord m{};
            m.handle = ++nstate().next_handle;
            nstate().curl_multi[m.handle] = m;
            p.handle = m.handle;
            p.out_curl_code = CURLM_OK;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XCURL_MULTI_CLEANUP:
            nstate().curl_multi.erase(p.handle);
            p.out_curl_code = CURLM_OK;
            p.out_status = SCARLETT_OK;
            break;
        case SCARLETT_XGR_XCURL_MULTI_ADD: {
            auto it = nstate().curl_multi.find(p.handle);
            if (it == nstate().curl_multi.end()) {
                p.out_curl_code = CURLM_BAD_HANDLE;
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            it->second.easy_handles.push_back(p.aux_handle);
            p.out_curl_code = CURLM_OK;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XCURL_MULTI_REMOVE: {
            auto it = nstate().curl_multi.find(p.handle);
            if (it == nstate().curl_multi.end()) {
                p.out_curl_code = CURLM_BAD_HANDLE;
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            auto &v = it->second.easy_handles;
            for (auto vit = v.begin(); vit != v.end();) {
                if (*vit == p.aux_handle) vit = v.erase(vit);
                else ++vit;
            }
            p.out_curl_code = CURLM_OK;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XCURL_MULTI_PERFORM: {
            auto it = nstate().curl_multi.find(p.handle);
            if (it == nstate().curl_multi.end()) {
                p.out_curl_code = CURLM_BAD_HANDLE;
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            p.value_u64 = 0u; /* 0 running handles remaining */
            p.out_curl_code = CURLM_OK;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XCURL_MULTI_POLL:
            p.value_u64 = 0u;
            p.out_curl_code = CURLM_OK;
            p.out_status = SCARLETT_OK;
            break;
        case SCARLETT_XGR_XCURL_SLIST_APPEND: {
            auto *head = reinterpret_cast<struct curl_slist *>(static_cast<uintptr_t>(p.handle));
            const char *txt = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_addr));
            auto *node = new curl_slist{};
            if (txt != nullptr) {
                size_t len = std::strlen(txt) + 1u;
                node->data = new char[len];
                std::memcpy(node->data, txt, len);
            }
            node->next = nullptr;
            if (head == nullptr) {
                p.value_u64 = reinterpret_cast<uint64_t>(node);
            } else {
                struct curl_slist *cur = head;
                while (cur->next != nullptr) {
                    cur = cur->next;
                }
                cur->next = node;
                p.value_u64 = reinterpret_cast<uint64_t>(head);
            }
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XCURL_SLIST_FREE: {
            auto *cur = reinterpret_cast<struct curl_slist *>(static_cast<uintptr_t>(p.handle));
            while (cur != nullptr) {
                auto *next = cur->next;
                delete[] cur->data;
                delete cur;
                cur = next;
            }
            p.out_status = SCARLETT_OK;
            break;
        }
        default:
            p.out_curl_code = CURLE_BAD_FUNCTION_ARGUMENT;
            p.out_status = SCARLETT_E_INVALIDARG;
            break;
    }

    return p.out_status;
}

scarlett_hresult_t NetworkServicesManager::xsapi_op(scarlett_xgr_xsapi_params_t &p) {
    std::lock_guard<std::mutex> lock(nstate().mutex);
    switch (static_cast<scarlett_xgr_xsapi_subop_t>(p.subop)) {
        case SCARLETT_XGR_XSAPI_INIT: {
            const char *scid = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_a_addr));
            if (scid == nullptr || scid[0] == '\0') {
                p.out_status = SCARLETT_E_INVALIDARG;
                break;
            }
            nstate().xbl_initialized = true;
            nstate().xbl_scid = scid;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XSAPI_CLEANUP:
            nstate().xbl_initialized = false;
            nstate().xbl_ctxs.clear();
            nstate().xbl_ach_results.clear();
            p.out_status = SCARLETT_OK;
            break;
        case SCARLETT_XGR_XSAPI_CTX_CREATE: {
            XblContextRecord ctx{};
            ctx.handle = ++nstate().next_handle;
            ctx.user_handle = p.handle;
            ctx.xuid = PlatformManager::instance().get_user_xuid(p.handle);
            nstate().xbl_ctxs[ctx.handle] = ctx;
            p.out_u64 = ctx.handle;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XSAPI_CTX_CLOSE: {
            auto it = nstate().xbl_ctxs.find(p.handle);
            if (it == nstate().xbl_ctxs.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            if (--it->second.ref_count == 0u) {
                nstate().xbl_ctxs.erase(it);
            }
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XSAPI_CTX_DUP: {
            auto it = nstate().xbl_ctxs.find(p.handle);
            if (it == nstate().xbl_ctxs.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            ++it->second.ref_count;
            p.out_u64 = it->second.handle;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XSAPI_CTX_GET_USER: {
            auto it = nstate().xbl_ctxs.find(p.handle);
            if (it == nstate().xbl_ctxs.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            p.out_u64 = it->second.user_handle;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XSAPI_ACH_GET: {
            if (nstate().xbl_ctxs.count(p.handle) == 0u) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            XblAchResultRecord res{};
            res.handle = ++nstate().next_handle;
            for (const auto &kv : nstate().xbl_achievements) {
                if (p.u32_a != 0u && kv.second.progressState != 1u) {
                    continue;
                }
                res.items.push_back(kv.second);
            }
            nstate().xbl_ach_results[res.handle] = res;
            p.out_u64 = res.handle;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XSAPI_ACH_UPDATE: {
            if (nstate().xbl_ctxs.count(p.handle) == 0u) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            const char *aid = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_a_addr));
            if (aid == nullptr || aid[0] == '\0') {
                p.out_status = SCARLETT_E_INVALIDARG;
                break;
            }
            auto &ach = nstate().xbl_achievements[aid];
            std::snprintf(ach.id, sizeof(ach.id), "%s", aid);
            if (ach.name[0] == '\0') {
                std::snprintf(ach.name, sizeof(ach.name), "Achievement_%s", aid);
            }
            ach.percentComplete = std::min<uint32_t>(100u, p.u32_a);
            ach.progressState = (ach.percentComplete >= 100u) ? 1u : (ach.percentComplete > 0u ? 3u : 2u);
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XSAPI_ACH_READ: {
            auto it = nstate().xbl_ach_results.find(p.handle);
            if (it == nstate().xbl_ach_results.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            p.out_u64 = reinterpret_cast<uint64_t>(it->second.items.data());
            p.out_u32 = static_cast<uint32_t>(it->second.items.size());
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XSAPI_ACH_CLOSE:
            nstate().xbl_ach_results.erase(p.handle);
            p.out_status = SCARLETT_OK;
            break;
        case SCARLETT_XGR_XSAPI_STATS_GET: {
            const char *sname = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_b_addr));
            if (sname == nullptr) {
                p.out_status = SCARLETT_E_POINTER;
                break;
            }
            auto it = nstate().xbl_stats.find(sname);
            std::string val = (it != nstate().xbl_stats.end()) ? it->second : "0";
            uint32_t used = 0u;
            p.out_status = copy_str_to_buf(p.out_buf_addr, p.u32_a, val, &used);
            p.out_u32 = used;
            break;
        }
        case SCARLETT_XGR_XSAPI_STATS_SET: {
            const char *sname = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_a_addr));
            const char *sval = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_b_addr));
            if (sname != nullptr && sval != nullptr) {
                nstate().xbl_stats[sname] = sval;
            }
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XSAPI_SOCIAL_GET:
            p.out_u32 = 0u; /* Offline autonomous: 0 friends */
            p.out_status = SCARLETT_OK;
            break;
        default:
            p.out_status = SCARLETT_E_INVALIDARG;
            break;
    }
    return p.out_status;
}

scarlett_hresult_t NetworkServicesManager::playfab_op(scarlett_xgr_playfab_params_t &p) {
    std::lock_guard<std::mutex> lock(nstate().mutex);
    switch (static_cast<scarlett_xgr_playfab_subop_t>(p.subop)) {
        case SCARLETT_XGR_PF_INIT:
            nstate().pf_initialized = true;
            p.out_status = SCARLETT_OK;
            break;
        case SCARLETT_XGR_PF_UNINIT:
            nstate().pf_initialized = false;
            nstate().pf_cfgs.clear();
            nstate().pf_entities.clear();
            p.out_status = SCARLETT_OK;
            break;
        case SCARLETT_XGR_PF_CFG_CREATE: {
            const char *cs = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_a_addr));
            const char *tid = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_b_addr));
            if (tid == nullptr || tid[0] == '\0') {
                p.out_status = SCARLETT_E_INVALIDARG;
                break;
            }
            PfServiceConfigRecord cfg{};
            cfg.handle = ++nstate().next_handle;
            cfg.conn_str = (cs != nullptr) ? cs : "";
            cfg.title_id = tid;
            nstate().pf_cfgs[cfg.handle] = cfg;
            p.out_u64 = cfg.handle;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_PF_CFG_CLOSE:
            nstate().pf_cfgs.erase(p.handle);
            p.out_status = SCARLETT_OK;
            break;
        case SCARLETT_XGR_PF_LOGIN_XBOX: {
            if (nstate().pf_cfgs.count(p.handle) == 0u) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            PfEntityRecord ent{};
            ent.handle = ++nstate().next_handle;
            ent.cfg_handle = p.handle;
            ent.user_handle = p.aux_handle;
            ent.xuid = PlatformManager::instance().get_user_xuid(p.aux_handle);
            nstate().pf_entities[ent.handle] = ent;
            p.out_u64 = ent.handle;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_PF_ENTITY_CLOSE:
            nstate().pf_entities.erase(p.handle);
            p.out_status = SCARLETT_OK;
            break;
        case SCARLETT_XGR_PF_TITLE_DATA_GET: {
            if (nstate().pf_entities.count(p.handle) == 0u) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            const char *key = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_a_addr));
            if (key == nullptr) {
                p.out_status = SCARLETT_E_POINTER;
                break;
            }
            auto it = nstate().pf_title_data.find(key);
            std::string val = (it != nstate().pf_title_data.end()) ? it->second : "{}";
            p.out_status = copy_str_to_buf(p.out_buf_addr, p.buf_cap, val, &p.out_used);
            break;
        }
        case SCARLETT_XGR_PF_TITLE_DATA_SET: {
            const char *key = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_a_addr));
            const char *val = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_b_addr));
            if (key != nullptr && val != nullptr) {
                nstate().pf_title_data[key] = val;
            }
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_PF_PLAYER_DATA_GET: {
            if (nstate().pf_entities.count(p.handle) == 0u) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            const char *key = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_a_addr));
            if (key == nullptr) {
                p.out_status = SCARLETT_E_POINTER;
                break;
            }
            auto it = nstate().pf_player_data.find(key);
            std::string val = (it != nstate().pf_player_data.end()) ? it->second : "{}";
            p.out_status = copy_str_to_buf(p.out_buf_addr, p.buf_cap, val, &p.out_used);
            break;
        }
        case SCARLETT_XGR_PF_PLAYER_DATA_SET: {
            if (nstate().pf_entities.count(p.handle) == 0u) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            const char *key = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_a_addr));
            const char *val = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_b_addr));
            if (key == nullptr || val == nullptr) {
                p.out_status = SCARLETT_E_POINTER;
                break;
            }
            nstate().pf_player_data[key] = val;
            p.out_status = SCARLETT_OK;
            break;
        }
        default:
            p.out_status = SCARLETT_E_INVALIDARG;
            break;
    }
    return p.out_status;
}

} // namespace scarlett::xgameruntime::unixlib
