/*
 * Proton-Scarlett — MicrosoftGame.Config XML Parser & Metadata Extractor
 */

#ifndef SCARLETT_LOADER_GAME_CONFIG_HPP
#define SCARLETT_LOADER_GAME_CONFIG_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "loader_unix_abi.h"
#include "scarlett_errors.h"

namespace scarlett::loader {

struct GameIdentity {
    std::string name;
    std::string publisher;
    std::string version;
    std::string resource_language;
};

struct GameExecutable {
    std::string name;
    std::string id;
    std::string target_device_family;
    std::string alias;
    std::string override_display_name;
    bool is_dev_only = false;
    bool executable_is_hosted = false;
    bool is_scarlett = false;
};

struct ShellVisuals {
    std::string default_display_name;
    std::string publisher_display_name;
    std::string description;
    std::string store_logo;
    std::string square150x150_logo;
    std::string square44x44_logo;
    std::string splash_screen_image;
    std::string background_color;
    std::string foreground_text;
};

struct VirtualMachineConfig {
    bool present = false;
    bool enabled = false;
    std::string virtual_machine_id;
};

struct DesktopRegistrationConfig {
    bool present = false;
    bool disable_registry_virtualization = false;
    bool enable_reserved_drive_letters = false;
    std::string mod_folder;
    std::vector<std::string> custom_install_actions;
    std::vector<std::string> dependencies;
};

struct XSystemMetadata {
    uint32_t title_id = 0;
    std::string title_id_hex;
    std::string msa_app_id;
    std::string sandbox_id;
    scarlett_console_model_t console_model = SCARLETT_MODEL_ANACONDA;
    bool is_scarlett = false;
    bool advanced_user_model = false;
};

struct XPackageMetadata {
    std::string package_full_name;
    std::string package_family_name;
    std::string identity_name;
    std::string publisher;
    std::string version;
    std::string display_name;
    std::string selected_executable;
    std::string target_device_family;
};

struct MicrosoftGameConfig {
    std::string config_version;
    GameIdentity identity;
    std::vector<GameExecutable> executables;
    std::vector<std::string> standalone_target_device_families;
    ShellVisuals shell_visuals;
    bool advanced_user_model = false;
    std::string title_id_raw;
    uint32_t title_id = 0;
    std::string msa_app_id;
    VirtualMachineConfig virtual_machine;
    DesktopRegistrationConfig desktop_registration;
    bool is_scarlett = false;
    std::string selected_executable;
    std::string selected_target_device_family;
};

bool target_device_family_is_scarlett(std::string_view family) noexcept;

bool is_valid_pe64_file(const std::filesystem::path& exe_path) noexcept;

scarlett_hresult_t parse_microsoft_game_config(
    std::string_view xml_text,
    MicrosoftGameConfig& out_config) noexcept;

scarlett_hresult_t parse_microsoft_game_config_file(
    const std::filesystem::path& config_path,
    MicrosoftGameConfig& out_config) noexcept;

scarlett_hresult_t select_scarlett_pe64_executable(
    MicrosoftGameConfig& config,
    const std::filesystem::path& game_dir) noexcept;

void extract_xsystem_xpackage_metadata(
    const MicrosoftGameConfig& config,
    scarlett_console_model_t console_model,
    XSystemMetadata& out_xsystem,
    XPackageMetadata& out_xpackage) noexcept;

void populate_loader_title_metadata(
    const MicrosoftGameConfig& config,
    scarlett_console_model_t console_model,
    scarlett_loader_title_metadata_t& out_meta) noexcept;

} // namespace scarlett::loader

#endif /* SCARLETT_LOADER_GAME_CONFIG_HPP */
