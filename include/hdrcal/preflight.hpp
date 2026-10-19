// Evaluation of the five HDR preconditions from the project instructions.

#pragma once

#include "hdrcal/hypr.hpp"
#include "hdrcal/kms.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace hdrcal {

struct PreflightCheck {
    std::string name;
    bool passed = false;
    std::string detail;
};

struct PreflightResult {
    std::vector<PreflightCheck> checks;

    [[nodiscard]] bool allPassed() const;
};

// Evaluates the five preconditions from section 8.2 of the project
// instructions. Every check is reported, even when an earlier one fails, so the
// user can fix all problems in one pass.
//
// The ICC check is conservative: Hyprland 0.56.2 exposes no runtime query for
// the active monitor ICC, so the configuration directory is scanned for a
// non-empty monitor `icc` assignment. An unreadable configuration file is
// reported as a failure rather than silently accepted.
[[nodiscard]] PreflightResult checkHdrPreconditions(const HyprController& controller, const std::string& outputName, const KmsConnectorReader& kmsReader,
                                                    const std::filesystem::path& hyprlandConfigDirectory);

} // namespace hdrcal
