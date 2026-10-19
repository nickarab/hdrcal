// Injectable hyprctl wrapper and RAII guard for the temporary calibration rule.

#pragma once

#include "hdrcal/command.hpp"

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
    bool enabled = false;
};

struct HyprMonitorListResult {
    std::optional<std::vector<HyprMonitorSummary>> value;
    std::optional<std::string> error;
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

// RAII guard for the temporary calibration rule. The destructor restores the
// previous monitor configuration with `hyprctl reload`, so normal exits,
// exceptions and explicit cancel paths all restore the session. Signal and
// watchdog handling belongs to the interactive wizard (M2), which will call
// restore() before re-raising the signal.
class TemporaryLuminanceRule {
public:
    explicit TemporaryLuminanceRule(const HyprController& controller) noexcept;
    ~TemporaryLuminanceRule();

    TemporaryLuminanceRule(const TemporaryLuminanceRule&) = delete;
    TemporaryLuminanceRule& operator=(const TemporaryLuminanceRule&) = delete;
    TemporaryLuminanceRule(TemporaryLuminanceRule&&) = delete;
    TemporaryLuminanceRule& operator=(TemporaryLuminanceRule&&) = delete;

    [[nodiscard]] std::optional<std::string> apply(std::string_view outputName);
    [[nodiscard]] std::optional<std::string> restore();

private:
    const HyprController* m_controller;
    bool m_applied = false;
};

} // namespace hdrcal
