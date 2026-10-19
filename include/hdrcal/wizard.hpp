// Pure state machine and session entry point for the three-phase calibration
// wizard.

#pragma once

#include "hdrcal/pattern.hpp"
#include "hdrcal/profile.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace hdrcal {

enum class WizardKey {
    Left,
    Right,
    CoarseLeft,
    CoarseRight,
    Enter,
    Escape,
};

struct PhaseResult {
    PatternPhase phase = PatternPhase::MinimumLuminance;
    std::uint16_t tenBitCode = 0;
    double luminanceNits = 0.0;
};

// Pure, interaction-free state machine. Keys arrive from any source (mpv FIFO
// in production, a scripted source in tests). It never touches mpv, the
// compositor or the filesystem.
class CalibrationWizard {
public:
    CalibrationWizard();

    [[nodiscard]] PatternPhase currentPhase() const;
    [[nodiscard]] std::size_t currentPhaseIndex() const;
    [[nodiscard]] std::uint16_t currentTenBitCode() const;
    [[nodiscard]] bool isFinished() const;
    [[nodiscard]] bool isAborted() const;
    [[nodiscard]] const std::vector<PhaseResult>& results() const;

    // Applies one key press. Ignored after finish or abort.
    void handleKey(WizardKey key);

private:
    void recordCurrentPhaseAndAdvance();

    std::size_t m_phaseIndex = 0;
    std::uint16_t m_currentTenBitCode = 0;
    std::vector<PhaseResult> m_results;
    bool m_finished = false;
    bool m_aborted = false;
};

} // namespace hdrcal
