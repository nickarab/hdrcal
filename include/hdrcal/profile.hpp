// Calibration profile model, validation, Lua rendering and atomic persistence.

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace hdrcal {

// Values read from the monitor OSD through DDC/CI. They describe the panel
// state at calibration time, not the calibration result itself. Divergence is
// reported later by the re-verification flow.
struct MonitorOsdSnapshot {
    int brightness = 0;
    int contrast = 0;
    std::string colorPreset;
};

// A calibration profile is the set of tone-mapping endpoints for one output.
// Minimum luminance is a fractional value in nits; Hyprland stores it as a
// float. Maximum and full-frame maximum are integer nits; Hyprland stores them
// as ints.
struct CalibrationProfile {
    std::string output;
    double minimumLuminanceNits = 0.0;
    int maximumLuminanceNits = 0;
    int maximumFullFrameLuminanceNits = 0;
    std::string timestampUtc;
    std::optional<MonitorOsdSnapshot> osdSnapshot;
};

// Returns the first validation failure, or std::nullopt when the profile is
// safe to persist. Error strings are user-facing (pt-BR).
[[nodiscard]] std::optional<std::string> findProfileValidationError(const CalibrationProfile& profile);

// Validates an output name against the characters Hyprland uses in monitor
// names (letters, digits, '-' and '_'). Returns the error, or std::nullopt.
[[nodiscard]] std::optional<std::string> findOutputNameValidationError(std::string_view output);

// Renders the Lua table consumed by ~/.config/hypr/hdrcal.lua. The result is
// locale-independent: decimal separator is always '.', and Hyprland integer
// fields are emitted as integers. On invalid input the content is empty and
// validationError explains why.
struct RenderedLuaTable {
    std::string content;
    std::optional<std::string> validationError;
};

[[nodiscard]] RenderedLuaTable renderProfileLuaTable(const CalibrationProfile& profile);

// Writes the profile atomically (temporary file in the same directory, fsync,
// rename). Missing parent directories are created. On success the destination
// either contains the complete new profile or the previous complete profile.
[[nodiscard]] std::optional<std::string> saveProfileToPath(const CalibrationProfile& profile, const std::filesystem::path& destinationPath);

// Formats a system-clock instant as UTC ISO 8601 with second precision, for
// example "2026-10-19T21:00:00Z". Uses gmtime_r and numeric formatting only, so
// the process locale cannot change the output.
[[nodiscard]] std::string formatUtcTimestamp(std::chrono::system_clock::time_point timestamp);

} // namespace hdrcal
