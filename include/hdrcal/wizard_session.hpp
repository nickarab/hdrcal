// Interactive three-phase calibration session: preflight, mpv, keys,
// persistence and guaranteed restoration.

#pragma once

#include "hdrcal/wizard.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace hdrcal {

struct WizardOptions {
    std::string outputName;
    std::filesystem::path profilePath;
    std::filesystem::path hyprlandConfigDirectory;
};

struct WizardOutcome {
    bool completed = false;
    bool aborted = false;
    int terminationSignal = 0;
    std::vector<PhaseResult> phaseResults;
    std::optional<std::string> error;
    std::filesystem::path savedProfilePath;
};

// Runs the full interactive calibration. The terminal is used only for
// instructions and the final summary; the visual feedback is rendered by mpv
// in fullscreen. On every exit path (success, Esc, error, SIGINT/SIGTERM,
// process crash) the temporary monitor rule is restored.
[[nodiscard]] WizardOutcome runCalibrationWizard(const WizardOptions& options);

} // namespace hdrcal
