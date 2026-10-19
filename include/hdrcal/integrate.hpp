// Explicit, transactional integration of the calibration profile into the
// user's Hyprland Lua monitor configuration.

#pragma once

#include "hdrcal/hypr_config.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace hdrcal {

struct ConfigFileContent {
    std::string content;
    std::optional<std::string> error;
};

// Reads a configuration file as raw bytes so comment stripping used by the
// scanner cannot shift the offsets needed for editing.
[[nodiscard]] ConfigFileContent readConfigFileContent(const std::filesystem::path& filePath);

struct IntegrationPlan {
    std::string updatedContent;
    std::string diff;
    bool alreadyIntegrated = false;
    std::optional<std::string> error;
};

// Pure editor: given the raw text of one Lua config file and an output name,
// finds the single `hl.monitor` rule for that output and inserts a nil-safe
// profile lookup plus the three luminance fields. It preserves every existing
// field and returns alreadyIntegrated=true (with an empty diff) when the three
// fields are already present, which makes repeated runs idempotent. The
// multi-file ambiguity check belongs to findMonitorRuleMatches().
[[nodiscard]] IntegrationPlan planMonitorRuleIntegration(std::string_view fileContent, std::string_view outputName);

// Writes a backup next to the file, replaces the file atomically and rolls the
// original content back if validation fails or the transaction is destroyed
// before commit. The backup is kept after both success and rollback.
class ConfigEditTransaction {
public:
    ConfigEditTransaction(std::filesystem::path filePath, std::string originalContent, std::string updatedContent);
    ~ConfigEditTransaction();

    ConfigEditTransaction(const ConfigEditTransaction&) = delete;
    ConfigEditTransaction& operator=(const ConfigEditTransaction&) = delete;
    ConfigEditTransaction(ConfigEditTransaction&&) = delete;
    ConfigEditTransaction& operator=(ConfigEditTransaction&&) = delete;

    // Checks write permission, creates the timestamped backup and writes the
    // updated content with fsync + rename. Returns a pt-BR error on failure.
    [[nodiscard]] std::optional<std::string> write();

    // Runs validation after write(). On error, restores the original content
    // and returns a pt-BR error that mentions the backup. On success the edit
    // is committed and the backup remains.
    [[nodiscard]] std::optional<std::string> commitAfterValidation(const std::function<std::optional<std::string>()>& validation);

    [[nodiscard]] const std::filesystem::path& backupPath() const;

private:
    [[nodiscard]] std::optional<std::string> restoreOriginal();
    [[nodiscard]] std::filesystem::path buildBackupPath() const;

    std::filesystem::path m_filePath;
    std::filesystem::path m_backupPath;
    std::string m_originalContent;
    std::string m_updatedContent;
    std::filesystem::perms m_originalPermissions{};
    bool m_hasOriginalPermissions = false;
    bool m_written = false;
    bool m_committed = false;
};

} // namespace hdrcal
