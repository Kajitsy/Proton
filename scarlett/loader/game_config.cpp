/*
 * Proton-Scarlett — MicrosoftGame.Config XML Parser & Metadata Extractor
 */

#include "game_config.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <utility>

namespace scarlett::loader {

namespace {

struct XmlNode {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::string text;
    std::vector<XmlNode> children;
};

std::string trim_ws(std::string_view sv) {
    size_t start = 0;
    while (start < sv.size() && std::isspace(static_cast<unsigned char>(sv[start]))) {
        ++start;
    }
    size_t end = sv.size();
    while (end > start && std::isspace(static_cast<unsigned char>(sv[end - 1]))) {
        --end;
    }
    return std::string(sv.substr(start, end - start));
}

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool contains_icase(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty()) {
        return true;
    }
    if (haystack.size() < needle.size()) {
        return false;
    }
    for (size_t i = 0; i <= haystack.size() - needle.size(); ++i) {
        if (iequals(haystack.substr(i, needle.size()), needle)) {
            return true;
        }
    }
    return false;
}

bool parse_bool_str(std::string_view sv, bool& out) noexcept {
    std::string trimmed = trim_ws(sv);
    if (iequals(trimmed, "true") || trimmed == "1") {
        out = true;
        return true;
    }
    if (iequals(trimmed, "false") || trimmed == "0") {
        out = false;
        return true;
    }
    return false;
}

bool decode_xml_entities(std::string_view in, std::string& out) {
    out.clear();
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '&') {
            out.push_back(in[i]);
            continue;
        }
        size_t semi = in.find(';', i + 1);
        if (semi == std::string_view::npos) {
            return false;
        }
        std::string_view ent = in.substr(i + 1, semi - (i + 1));
        if (ent == "amp") {
            out.push_back('&');
        } else if (ent == "lt") {
            out.push_back('<');
        } else if (ent == "gt") {
            out.push_back('>');
        } else if (ent == "quot") {
            out.push_back('"');
        } else if (ent == "apos") {
            out.push_back('\'');
        } else if (ent.size() >= 2 && ent[0] == '#') {
            uint32_t cp = 0;
            bool hex = (ent[1] == 'x' || ent[1] == 'X');
            size_t pos = hex ? 2 : 1;
            if (pos >= ent.size()) {
                return false;
            }
            for (; pos < ent.size(); ++pos) {
                char c = ent[pos];
                uint32_t d = 0;
                if (c >= '0' && c <= '9') {
                    d = static_cast<uint32_t>(c - '0');
                } else if (hex && c >= 'a' && c <= 'f') {
                    d = static_cast<uint32_t>(c - 'a' + 10);
                } else if (hex && c >= 'A' && c <= 'F') {
                    d = static_cast<uint32_t>(c - 'A' + 10);
                } else {
                    return false;
                }
                cp = cp * (hex ? 16u : 10u) + d;
                if (cp > 0x10FFFFu) {
                    return false;
                }
            }
            if (cp == 0) {
                return false;
            }
            if (cp <= 0x7Fu) {
                out.push_back(static_cast<char>(cp));
            } else if (cp <= 0x7FFu) {
                out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
                out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
            } else if (cp <= 0xFFFFu) {
                out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
                out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
                out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
            } else {
                out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
                out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
                out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
                out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
            }
        } else {
            return false;
        }
        i = semi;
    }
    return true;
}

class XmlParser {
public:
    explicit XmlParser(std::string_view src) : src_(src) {}

    bool parse(XmlNode& out_root) {
        // Strip optional UTF-8 BOM
        if (src_.size() >= 3 &&
            static_cast<unsigned char>(src_[0]) == 0xEF &&
            static_cast<unsigned char>(src_[1]) == 0xBB &&
            static_cast<unsigned char>(src_[2]) == 0xBF) {
            pos_ = 3;
        }

        if (!skip_prolog()) {
            return false;
        }
        if (pos_ >= src_.size() || src_[pos_] != '<') {
            return false;
        }
        if (!parse_element(out_root, 0)) {
            return false;
        }
        if (!skip_trailing_misc()) {
            return false;
        }
        return pos_ == src_.size();
    }

private:
    static constexpr int kMaxDepth = 64;

    bool is_name_start(char c) const noexcept {
        unsigned char uc = static_cast<unsigned char>(c);
        return std::isalpha(uc) || c == '_' || c == ':';
    }

    bool is_name_char(char c) const noexcept {
        unsigned char uc = static_cast<unsigned char>(c);
        return std::isalnum(uc) || c == '_' || c == '-' || c == '.' || c == ':';
    }

    void skip_ws() noexcept {
        while (pos_ < src_.size() && std::isspace(static_cast<unsigned char>(src_[pos_]))) {
            ++pos_;
        }
    }

    bool starts_with(std::string_view prefix) const noexcept {
        return src_.substr(pos_, prefix.size()) == prefix;
    }

    bool skip_comment() noexcept {
        pos_ += 4; // skip "<!--"
        size_t end = src_.find("-->", pos_);
        if (end == std::string_view::npos) {
            return false;
        }
        pos_ = end + 3;
        return true;
    }

    bool skip_pi() noexcept {
        pos_ += 2; // skip "<?"
        size_t end = src_.find("?>", pos_);
        if (end == std::string_view::npos) {
            return false;
        }
        pos_ = end + 2;
        return true;
    }

    bool skip_prolog() noexcept {
        while (pos_ < src_.size()) {
            skip_ws();
            if (starts_with("<?")) {
                if (!skip_pi()) {
                    return false;
                }
            } else if (starts_with("<!--")) {
                if (!skip_comment()) {
                    return false;
                }
            } else {
                break;
            }
        }
        return true;
    }

    bool skip_trailing_misc() noexcept {
        while (pos_ < src_.size()) {
            skip_ws();
            if (pos_ >= src_.size()) {
                return true;
            }
            if (starts_with("<!--")) {
                if (!skip_comment()) {
                    return false;
                }
            } else if (starts_with("<?")) {
                if (!skip_pi()) {
                    return false;
                }
            } else {
                return false;
            }
        }
        return true;
    }

    bool parse_name(std::string& out_name) {
        if (pos_ >= src_.size() || !is_name_start(src_[pos_])) {
            return false;
        }
        size_t start = pos_++;
        while (pos_ < src_.size() && is_name_char(src_[pos_])) {
            ++pos_;
        }
        out_name.assign(src_.substr(start, pos_ - start));
        return true;
    }

    bool parse_element(XmlNode& node, int depth) {
        if (depth > kMaxDepth || pos_ >= src_.size() || src_[pos_] != '<') {
            return false;
        }
        ++pos_; // '<'
        if (pos_ >= src_.size() || src_[pos_] == '/' || src_[pos_] == '!' || src_[pos_] == '?') {
            return false;
        }
        if (!parse_name(node.name)) {
            return false;
        }

        // Parse attributes
        while (pos_ < src_.size()) {
            size_t before_ws = pos_;
            skip_ws();
            if (pos_ >= src_.size()) {
                return false;
            }
            if (starts_with("/>")) {
                pos_ += 2;
                return true;
            }
            if (src_[pos_] == '>') {
                ++pos_;
                break;
            }
            if (pos_ == before_ws) {
                return false; // missing whitespace before attribute
            }
            std::string attr_name;
            if (!parse_name(attr_name)) {
                return false;
            }
            skip_ws();
            if (pos_ >= src_.size() || src_[pos_] != '=') {
                return false;
            }
            ++pos_;
            skip_ws();
            if (pos_ >= src_.size() || (src_[pos_] != '"' && src_[pos_] != '\'')) {
                return false;
            }
            char quote = src_[pos_++];
            size_t val_start = pos_;
            while (pos_ < src_.size() && src_[pos_] != quote) {
                if (src_[pos_] == '<') {
                    return false;
                }
                ++pos_;
            }
            if (pos_ >= src_.size()) {
                return false;
            }
            std::string decoded_val;
            if (!decode_xml_entities(src_.substr(val_start, pos_ - val_start), decoded_val)) {
                return false;
            }
            ++pos_; // closing quote
            node.attrs.emplace_back(std::move(attr_name), std::move(decoded_val));
        }

        // Parse children / text until </name>
        while (pos_ < src_.size()) {
            if (starts_with("</")) {
                pos_ += 2;
                std::string close_name;
                if (!parse_name(close_name)) {
                    return false;
                }
                if (close_name != node.name) {
                    return false;
                }
                skip_ws();
                if (pos_ >= src_.size() || src_[pos_] != '>') {
                    return false;
                }
                ++pos_;
                return true;
            }
            if (starts_with("<!--")) {
                if (!skip_comment()) {
                    return false;
                }
                continue;
            }
            if (starts_with("<![CDATA[")) {
                pos_ += 9;
                size_t end = src_.find("]]>", pos_);
                if (end == std::string_view::npos) {
                    return false;
                }
                node.text.append(src_.substr(pos_, end - pos_));
                pos_ = end + 3;
                continue;
            }
            if (starts_with("<?")) {
                if (!skip_pi()) {
                    return false;
                }
                continue;
            }
            if (src_[pos_] == '<') {
                XmlNode child;
                if (!parse_element(child, depth + 1)) {
                    return false;
                }
                node.children.push_back(std::move(child));
                continue;
            }
            size_t text_start = pos_;
            while (pos_ < src_.size() && src_[pos_] != '<') {
                ++pos_;
            }
            std::string decoded_text;
            if (!decode_xml_entities(src_.substr(text_start, pos_ - text_start), decoded_text)) {
                return false;
            }
            node.text.append(decoded_text);
        }

        return false; // unclosed tag
    }

    std::string_view src_;
    size_t pos_ = 0;
};

const std::string* find_attr(const XmlNode& node, std::string_view attr_name) noexcept {
    for (const auto& [k, v] : node.attrs) {
        if (iequals(k, attr_name)) {
            return &v;
        }
    }
    return nullptr;
}

std::string attr_or_empty(const XmlNode& node, std::string_view attr_name) {
    const std::string* val = find_attr(node, attr_name);
    return val ? *val : std::string{};
}

bool parse_title_id_u32(std::string_view sv, uint32_t& out_id) noexcept {
    std::string s = trim_ws(sv);
    if (s.empty()) {
        return false;
    }
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s.erase(0, 2);
    }
    if (s.empty() || s.size() > 8) {
        return false;
    }
    uint32_t val = 0;
    for (char c : s) {
        uint32_t d = 0;
        if (c >= '0' && c <= '9') {
            d = static_cast<uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            d = static_cast<uint32_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            d = static_cast<uint32_t>(c - 'A' + 10);
        } else {
            return false;
        }
        val = (val << 4) | d;
    }
    out_id = val;
    return true;
}

std::string build_publisher_token(std::string_view publisher) {
    if (publisher.empty()) {
        return "0000000000000";
    }
    // Deterministic Crockford-like 13-char token derived from publisher string
    static constexpr char kAlphabet[] = "0123456789abcdefghjkmnpqrstvwxyz";
    uint64_t h1 = UINT64_C(0xCBF29CE484222325);
    uint64_t h2 = UINT64_C(0x9E3779B97F4A7C15);
    for (unsigned char c : publisher) {
        h1 ^= c;
        h1 *= UINT64_C(0x100000001B3);
        h2 = (h2 << 7) | (h2 >> 57);
        h2 ^= static_cast<uint64_t>(c) * UINT64_C(0x517CC1B727220A95);
    }
    std::string out(13, '0');
    uint64_t combined = h1 ^ h2;
    for (size_t i = 0; i < 13; ++i) {
        out[i] = kAlphabet[combined & 0x1Fu];
        combined >>= 5;
        if (i == 7) {
            combined ^= h1;
        }
    }
    return out;
}

std::filesystem::path find_case_insensitive_file(
    const std::filesystem::path& base_dir,
    std::string_view rel_win_path) {
    if (base_dir.empty() || rel_win_path.empty()) {
        return {};
    }
    std::filesystem::path cur = base_dir;
    size_t pos = 0;
    while (pos < rel_win_path.size()) {
        while (pos < rel_win_path.size() &&
               (rel_win_path[pos] == '\\' || rel_win_path[pos] == '/')) {
            ++pos;
        }
        if (pos >= rel_win_path.size()) {
            break;
        }
        size_t next = pos;
        while (next < rel_win_path.size() &&
               rel_win_path[next] != '\\' && rel_win_path[next] != '/') {
            ++next;
        }
        std::string_view comp = rel_win_path.substr(pos, next - pos);
        pos = next;
        if (comp == ".") {
            continue;
        }
        if (comp == "..") {
            return {};
        }

        std::filesystem::path exact = cur / std::string(comp);
        std::error_code ec;
        if (std::filesystem::exists(exact, ec)) {
            cur = exact;
            continue;
        }

        bool matched = false;
        for (const auto& entry : std::filesystem::directory_iterator(cur, ec)) {
            std::string name = entry.path().filename().string();
            if (iequals(name, comp)) {
                cur = entry.path();
                matched = true;
                break;
            }
        }
        if (!matched) {
            return {};
        }
    }
    return cur;
}

void copy_cstr(char* dst, size_t cap, std::string_view src) noexcept {
    if (cap == 0) {
        return;
    }
    size_t n = std::min(cap - 1, src.size());
    if (n > 0) {
        std::memcpy(dst, src.data(), n);
    }
    dst[n] = '\0';
}

} // namespace

bool target_device_family_is_scarlett(std::string_view family) noexcept {
    return contains_icase(family, "Scarlett") || contains_icase(family, "XboxGen9");
}

bool is_valid_pe64_file(const std::filesystem::path& exe_path) noexcept {
    std::ifstream in(exe_path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    std::array<uint8_t, 0x40> dos{};
    in.read(reinterpret_cast<char*>(dos.data()), static_cast<std::streamsize>(dos.size()));
    if (in.gcount() != static_cast<std::streamsize>(dos.size())) {
        return false;
    }
    if (dos[0] != 'M' || dos[1] != 'Z') {
        return false;
    }
    uint32_t pe_offset =
        static_cast<uint32_t>(dos[0x3C]) |
        (static_cast<uint32_t>(dos[0x3D]) << 8) |
        (static_cast<uint32_t>(dos[0x3E]) << 16) |
        (static_cast<uint32_t>(dos[0x3F]) << 24);
    if (pe_offset < 0x40u || pe_offset > 0x100000u) {
        return false;
    }
    in.seekg(static_cast<std::streamoff>(pe_offset), std::ios::beg);
    std::array<uint8_t, 26> pe_hdr{};
    in.read(reinterpret_cast<char*>(pe_hdr.data()), static_cast<std::streamsize>(pe_hdr.size()));
    if (in.gcount() != static_cast<std::streamsize>(pe_hdr.size())) {
        return false;
    }
    if (pe_hdr[0] != 'P' || pe_hdr[1] != 'E' || pe_hdr[2] != 0 || pe_hdr[3] != 0) {
        return false;
    }
    uint16_t machine = static_cast<uint16_t>(pe_hdr[4]) |
                       (static_cast<uint16_t>(pe_hdr[5]) << 8);
    uint16_t opt_magic = static_cast<uint16_t>(pe_hdr[24]) |
                         (static_cast<uint16_t>(pe_hdr[25]) << 8);
    return machine == 0x8664u && opt_magic == 0x020Bu;
}

scarlett_hresult_t parse_microsoft_game_config(
    std::string_view xml_text,
    MicrosoftGameConfig& out_config) noexcept {
    if (xml_text.empty()) {
        return SCARLETT_ERR_CONFIG_PARSE_FAILED;
    }

    XmlNode root;
    try {
        XmlParser parser(xml_text);
        if (!parser.parse(root)) {
            return SCARLETT_ERR_CONFIG_PARSE_FAILED;
        }
    } catch (...) {
        return SCARLETT_ERR_CONFIG_PARSE_FAILED;
    }

    if (!iequals(root.name, "Game")) {
        return SCARLETT_ERR_CONFIG_PARSE_FAILED;
    }

    MicrosoftGameConfig cfg;
    cfg.config_version = attr_or_empty(root, "configVersion");

    for (const auto& child : root.children) {
        if (iequals(child.name, "Identity")) {
            cfg.identity.name = attr_or_empty(child, "Name");
            cfg.identity.publisher = attr_or_empty(child, "Publisher");
            cfg.identity.version = attr_or_empty(child, "Version");
            cfg.identity.resource_language = attr_or_empty(child, "ResourceLanguage");
            if (cfg.identity.name.empty()) {
                return SCARLETT_ERR_CONFIG_PARSE_FAILED;
            }
        } else if (iequals(child.name, "ExecutableList")) {
            for (const auto& exe_node : child.children) {
                if (!iequals(exe_node.name, "Executable")) {
                    continue;
                }
                GameExecutable exe;
                exe.name = attr_or_empty(exe_node, "Name");
                exe.id = attr_or_empty(exe_node, "Id");
                exe.target_device_family = attr_or_empty(exe_node, "TargetDeviceFamily");
                exe.alias = attr_or_empty(exe_node, "Alias");
                exe.override_display_name = attr_or_empty(exe_node, "OverrideDisplayName");

                std::string dev_only = attr_or_empty(exe_node, "IsDevOnly");
                if (!dev_only.empty()) {
                    if (!parse_bool_str(dev_only, exe.is_dev_only)) {
                        return SCARLETT_ERR_CONFIG_PARSE_FAILED;
                    }
                }
                std::string hosted = attr_or_empty(exe_node, "ExecutableIsHosted");
                if (!hosted.empty()) {
                    if (!parse_bool_str(hosted, exe.executable_is_hosted)) {
                        return SCARLETT_ERR_CONFIG_PARSE_FAILED;
                    }
                }
                if (exe.name.empty()) {
                    return SCARLETT_ERR_CONFIG_PARSE_FAILED;
                }
                exe.is_scarlett = target_device_family_is_scarlett(exe.target_device_family);
                if (exe.is_scarlett) {
                    cfg.is_scarlett = true;
                }
                cfg.executables.push_back(std::move(exe));
            }
        } else if (iequals(child.name, "TargetDeviceFamily")) {
            std::string fam = attr_or_empty(child, "Name");
            if (fam.empty()) {
                fam = trim_ws(child.text);
            }
            if (!fam.empty()) {
                if (target_device_family_is_scarlett(fam)) {
                    cfg.is_scarlett = true;
                }
                cfg.standalone_target_device_families.push_back(std::move(fam));
            }
        } else if (iequals(child.name, "ShellVisuals")) {
            cfg.shell_visuals.default_display_name = attr_or_empty(child, "DefaultDisplayName");
            cfg.shell_visuals.publisher_display_name = attr_or_empty(child, "PublisherDisplayName");
            cfg.shell_visuals.description = attr_or_empty(child, "Description");
            cfg.shell_visuals.store_logo = attr_or_empty(child, "StoreLogo");
            cfg.shell_visuals.square150x150_logo = attr_or_empty(child, "Square150x150Logo");
            cfg.shell_visuals.square44x44_logo = attr_or_empty(child, "Square44x44Logo");
            cfg.shell_visuals.splash_screen_image = attr_or_empty(child, "SplashScreenImage");
            cfg.shell_visuals.background_color = attr_or_empty(child, "BackgroundColor");
            cfg.shell_visuals.foreground_text = attr_or_empty(child, "ForegroundText");
        } else if (iequals(child.name, "AdvancedUserModel")) {
            std::string val = trim_ws(child.text);
            if (val.empty()) {
                val = attr_or_empty(child, "Enabled");
            }
            if (!val.empty() && !parse_bool_str(val, cfg.advanced_user_model)) {
                return SCARLETT_ERR_CONFIG_PARSE_FAILED;
            }
        } else if (iequals(child.name, "TitleId")) {
            cfg.title_id_raw = trim_ws(child.text);
            if (!cfg.title_id_raw.empty()) {
                if (!parse_title_id_u32(cfg.title_id_raw, cfg.title_id)) {
                    return SCARLETT_ERR_CONFIG_PARSE_FAILED;
                }
            }
        } else if (iequals(child.name, "MSAAppId")) {
            cfg.msa_app_id = trim_ws(child.text);
        } else if (iequals(child.name, "VirtualMachine")) {
            cfg.virtual_machine.present = true;
            cfg.virtual_machine.enabled = true;
            std::string en = attr_or_empty(child, "Enabled");
            if (!en.empty()) {
                if (!parse_bool_str(en, cfg.virtual_machine.enabled)) {
                    return SCARLETT_ERR_CONFIG_PARSE_FAILED;
                }
            }
            cfg.virtual_machine.virtual_machine_id = attr_or_empty(child, "VirtualMachineId");
            std::string txt = trim_ws(child.text);
            if (!txt.empty() && en.empty()) {
                bool txt_bool = false;
                if (parse_bool_str(txt, txt_bool)) {
                    cfg.virtual_machine.enabled = txt_bool;
                }
            }
            for (const auto& vm_child : child.children) {
                if (iequals(vm_child.name, "EnableVirtualMachine")) {
                    bool v = false;
                    if (!parse_bool_str(vm_child.text, v)) {
                        return SCARLETT_ERR_CONFIG_PARSE_FAILED;
                    }
                    cfg.virtual_machine.enabled = v;
                } else if (iequals(vm_child.name, "VirtualMachineId")) {
                    cfg.virtual_machine.virtual_machine_id = trim_ws(vm_child.text);
                }
            }
        } else if (iequals(child.name, "DesktopRegistration")) {
            cfg.desktop_registration.present = true;
            std::string drv = attr_or_empty(child, "DisableRegistryVirtualization");
            if (!drv.empty()) {
                if (!parse_bool_str(drv, cfg.desktop_registration.disable_registry_virtualization)) {
                    return SCARLETT_ERR_CONFIG_PARSE_FAILED;
                }
            }
            std::string rdl = attr_or_empty(child, "EnableReservedDriveLetters");
            if (!rdl.empty()) {
                if (!parse_bool_str(rdl, cfg.desktop_registration.enable_reserved_drive_letters)) {
                    return SCARLETT_ERR_CONFIG_PARSE_FAILED;
                }
            }
            cfg.desktop_registration.mod_folder = attr_or_empty(child, "ModFolder");
            for (const auto& dr_child : child.children) {
                if (iequals(dr_child.name, "ModFolder")) {
                    std::string mf = attr_or_empty(dr_child, "Name");
                    if (mf.empty()) {
                        mf = trim_ws(dr_child.text);
                    }
                    cfg.desktop_registration.mod_folder = std::move(mf);
                } else if (iequals(dr_child.name, "DisableRegistryVirtualization")) {
                    if (!parse_bool_str(dr_child.text,
                                        cfg.desktop_registration.disable_registry_virtualization)) {
                        return SCARLETT_ERR_CONFIG_PARSE_FAILED;
                    }
                } else if (iequals(dr_child.name, "EnableReservedDriveLetters")) {
                    if (!parse_bool_str(dr_child.text,
                                        cfg.desktop_registration.enable_reserved_drive_letters)) {
                        return SCARLETT_ERR_CONFIG_PARSE_FAILED;
                    }
                } else if (iequals(dr_child.name, "CustomInstallActions")) {
                    for (const auto& act : dr_child.children) {
                        std::string name = attr_or_empty(act, "Name");
                        if (name.empty()) {
                            name = act.name;
                        }
                        cfg.desktop_registration.custom_install_actions.push_back(std::move(name));
                    }
                } else if (iequals(dr_child.name, "DependencyList")) {
                    for (const auto& dep : dr_child.children) {
                        std::string name = attr_or_empty(dep, "Name");
                        if (!name.empty()) {
                            cfg.desktop_registration.dependencies.push_back(std::move(name));
                        }
                    }
                }
            }
        }
    }

    // Select default Scarlett executable if present
    const GameExecutable* chosen = nullptr;
    for (const auto& exe : cfg.executables) {
        if (exe.is_scarlett && !exe.is_dev_only) {
            chosen = &exe;
            break;
        }
    }
    if (!chosen) {
        for (const auto& exe : cfg.executables) {
            if (exe.is_scarlett) {
                chosen = &exe;
                break;
            }
        }
    }
    if (chosen) {
        cfg.selected_executable = chosen->name;
        cfg.selected_target_device_family = chosen->target_device_family;
    } else if (!cfg.standalone_target_device_families.empty()) {
        for (const auto& fam : cfg.standalone_target_device_families) {
            if (target_device_family_is_scarlett(fam)) {
                cfg.selected_target_device_family = fam;
                break;
            }
        }
    }

    out_config = std::move(cfg);
    return SCARLETT_OK;
}

scarlett_hresult_t parse_microsoft_game_config_file(
    const std::filesystem::path& config_path,
    MicrosoftGameConfig& out_config) noexcept {
    std::ifstream in(config_path, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        return SCARLETT_ERR_CONFIG_PARSE_FAILED;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return parse_microsoft_game_config(ss.str(), out_config);
}

scarlett_hresult_t select_scarlett_pe64_executable(
    MicrosoftGameConfig& config,
    const std::filesystem::path& game_dir) noexcept {
    if (!config.is_scarlett) {
        return SCARLETT_ERR_CONFIG_PARSE_FAILED;
    }

    std::vector<const GameExecutable*> candidates;
    for (const auto& exe : config.executables) {
        if (exe.is_scarlett && !exe.is_dev_only) {
            candidates.push_back(&exe);
        }
    }
    for (const auto& exe : config.executables) {
        if (exe.is_scarlett && exe.is_dev_only) {
            candidates.push_back(&exe);
        }
    }

    if (candidates.empty()) {
        return SCARLETT_ERR_CONFIG_PARSE_FAILED;
    }

    if (game_dir.empty()) {
        config.selected_executable = candidates.front()->name;
        config.selected_target_device_family = candidates.front()->target_device_family;
        return SCARLETT_OK;
    }

    for (const GameExecutable* cand : candidates) {
        std::filesystem::path resolved = find_case_insensitive_file(game_dir, cand->name);
        if (!resolved.empty() && is_valid_pe64_file(resolved)) {
            config.selected_executable = cand->name;
            config.selected_target_device_family = cand->target_device_family;
            return SCARLETT_OK;
        }
    }

    return SCARLETT_ERR_CONFIG_PARSE_FAILED;
}

void extract_xsystem_xpackage_metadata(
    const MicrosoftGameConfig& config,
    scarlett_console_model_t console_model,
    XSystemMetadata& out_xsystem,
    XPackageMetadata& out_xpackage) noexcept {
    out_xsystem = XSystemMetadata{};
    out_xsystem.title_id = config.title_id;
    out_xsystem.title_id_hex = config.title_id_raw;
    out_xsystem.msa_app_id = config.msa_app_id;
    out_xsystem.sandbox_id = "RETAIL";
    out_xsystem.console_model = console_model;
    out_xsystem.is_scarlett = config.is_scarlett;
    out_xsystem.advanced_user_model = config.advanced_user_model;

    out_xpackage = XPackageMetadata{};
    out_xpackage.identity_name = config.identity.name;
    out_xpackage.publisher = config.identity.publisher;
    out_xpackage.version = config.identity.version.empty() ? "1.0.0.0" : config.identity.version;
    out_xpackage.display_name = config.shell_visuals.default_display_name.empty()
                                    ? config.identity.name
                                    : config.shell_visuals.default_display_name;
    out_xpackage.selected_executable = config.selected_executable;
    out_xpackage.target_device_family = config.selected_target_device_family;

    if (!config.identity.name.empty()) {
        std::string pub_tok = build_publisher_token(config.identity.publisher);
        out_xpackage.package_family_name = config.identity.name + "_" + pub_tok;
        out_xpackage.package_full_name =
            config.identity.name + "_" + out_xpackage.version + "_x64__" + pub_tok;
    }
}

void populate_loader_title_metadata(
    const MicrosoftGameConfig& config,
    scarlett_console_model_t console_model,
    scarlett_loader_title_metadata_t& out_meta) noexcept {
    std::memset(&out_meta, 0, sizeof(out_meta));

    XSystemMetadata xsys;
    XPackageMetadata xpkg;
    extract_xsystem_xpackage_metadata(config, console_model, xsys, xpkg);

    out_meta.abi_version = SCARLETT_CONTRACT_ABI_VERSION;
    out_meta.is_scarlett = config.is_scarlett ? 1u : 0u;
    out_meta.title_id = xsys.title_id;
    out_meta.advanced_user_model = xsys.advanced_user_model ? 1u : 0u;
    out_meta.vm_enabled = (config.virtual_machine.present && config.virtual_machine.enabled) ? 1u : 0u;
    out_meta.desktop_reg_present = config.desktop_registration.present ? 1u : 0u;
    out_meta.console_model = static_cast<uint32_t>(console_model);

    copy_cstr(out_meta.identity_name, sizeof(out_meta.identity_name), xpkg.identity_name);
    copy_cstr(out_meta.publisher, sizeof(out_meta.publisher), xpkg.publisher);
    copy_cstr(out_meta.version, sizeof(out_meta.version), xpkg.version);
    copy_cstr(out_meta.msa_app_id, sizeof(out_meta.msa_app_id), xsys.msa_app_id);
    copy_cstr(out_meta.display_name, sizeof(out_meta.display_name), xpkg.display_name);
    copy_cstr(out_meta.selected_exe, sizeof(out_meta.selected_exe), xpkg.selected_executable);
    copy_cstr(out_meta.target_device_family, sizeof(out_meta.target_device_family),
              xpkg.target_device_family);
    copy_cstr(out_meta.package_full_name, sizeof(out_meta.package_full_name),
              xpkg.package_full_name);
}

} // namespace scarlett::loader
