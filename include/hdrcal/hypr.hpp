// Injectable hyprctl wrapper and RAII guard for the temporary calibration rule.

#pragma once

#include "hdrcal/command.hpp"
#include "hdrcal/kms.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hdrcal {

// The temporary tone-mapping endpoints required during the visual test. They
// disable compositor tone mapping so the pattern reaches the panel unmodified.
inline constexpr double kCalibrationMinimumLuminanceNits = 0.0;
inline constexpr int kCalibrationMaximumLuminanceNits = 10000;
inline constexpr int kCalibrationMaximumFullFrameLuminanceNits = 10000;

// Version floor and audited ceiling for the Hyprland Lua configuration and
// `hyprctl eval` support used by this project.
inline constexpr int kMinimumHyprlandMajorVersion = 0;
inline constexpr int kMinimumHyprlandMinorVersion = 55;
inline constexpr int kAuditedHyprlandMajorVersion = 0;
inline constexpr int kAuditedHyprlandMinorVersion = 56;
inline constexpr int kAuditedHyprlandPatchVersion = 2;

struct HyprOptionValue {
    enum class Type { Integer, Boolean, FloatingPoint, String };

    Type type = Type::Integer;
    long long integerValue = 0;
    bool booleanValue = false;
    double floatingPointValue = 0.0;
    std::string stringValue;
};

struct HyprOptionReadResult {
    std::optional<HyprOptionValue> value;
    std::optional<std::string> error;
};

struct HyprMonitorSummary {
    std::string name;
    std::string currentFormat;
    std::string colorManagementPreset;
    int width = 0;
    int height = 0;
    bool enabled = false;
};

struct HyprMonitorListResult {
    std::optional<std::vector<HyprMonitorSummary>> value;
    std::optional<std::string> error;
};

struct HyprlandVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string raw;
};

struct HyprlandVersionResult {
    std::optional<HyprlandVersion> value;
    std::optional<std::string> error;
};

struct HyprConfigErrorsResult {
    std::optional<std::string> value;
    std::optional<std::string> error;
};

// Negative when left is older than right; zero when both versions are equal.
[[nodiscard]] int compareHyprlandVersions(const HyprlandVersion& left, const HyprlandVersion& right);

// HDR-relevant state observed before and after applying the temporary rule.
// Used to detect a silent fallback to SDR or 8-bit.
struct MonitorHdrState {
    int maximumBitsPerComponent = 0;
    std::optional<int> hdrEotf;
    std::string currentFormat;
};

// Thin, injectable wrapper around the `hyprctl` binary. All methods are
// read-only except reload() and applyMonitorLuminanceRule(), and every failure
// is returned as a user-facing (pt-BR) error string.
class HyprController {
public:
    explicit HyprController(CommandExecutor executor);

    // Uses the real process launcher.
    [[nodiscard]] static HyprController createWithSystemExecutor();

    [[nodiscard]] HyprOptionReadResult readOption(std::string_view optionName) const;
    [[nodiscard]] HyprMonitorListResult listMonitors() const;
    [[nodiscard]] HyprlandVersionResult readVersion() const;

    // Raw output of `hyprctl configerrors`, used by `hdrcal integrate` to
    // detect a syntax error introduced by the edit before keeping it.
    [[nodiscard]] HyprConfigErrorsResult readConfigErrors() const;

    // Asks Hyprland to re-read its configuration. Returns std::nullopt on
    // success.
    [[nodiscard]] std::optional<std::string> reload() const;

    // Applies the temporary calibration rule with `hyprctl eval`. This is a
    // runtime-only change; configuration persistence is done separately in
    // hdrcal.lua.
    [[nodiscard]] std::optional<std::string> applyMonitorLuminanceRule(std::string_view outputName) const;

private:
    CommandExecutor m_executor;
};

// RAII guard for the temporary calibration rule. Before touching the session it
// verifies that a monitor rule named exactly like the output exists, because
// Hyprland's partial `hl.monitor` merge only copies such a rule. After applying,
// it re-reads KMS and Hyprland state and restores immediately if HDR or 10-bit
// output degraded. The destructor restores the previous monitor configuration
// with `hyprctl reload`, so normal exits, exceptions and explicit cancel paths
// all restore the session. Signal and watchdog handling belongs to the
// interactive wizard (M2), which calls restore() before re-raising the signal.
class TemporaryLuminanceRule {
public:
    TemporaryLuminanceRule(const HyprController& controller, KmsConnectorReader kmsReader, std::filesystem::path hyprlandConfigDirectory) noexcept;
    ~TemporaryLuminanceRule();

    TemporaryLuminanceRule(const TemporaryLuminanceRule&) = delete;
    TemporaryLuminanceRule& operator=(const TemporaryLuminanceRule&) = delete;
    TemporaryLuminanceRule(TemporaryLuminanceRule&&) = delete;
    TemporaryLuminanceRule& operator=(TemporaryLuminanceRule&&) = delete;

    [[nodiscard]] std::optional<std::string> apply(std::string_view outputName);
    [[nodiscard]] std::optional<std::string> restore();

private:
    const HyprController* m_controller;
    KmsConnectorReader m_kmsReader;
    std::filesystem::path m_hyprlandConfigDirectory;
    bool m_applied = false;
};

} // namespace hdrcal
