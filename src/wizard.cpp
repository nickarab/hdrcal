// Implements the pure three-phase calibration state machine.

#include "hdrcal/wizard.hpp"

#include "hdrcal/pq.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace hdrcal {
namespace {

constexpr std::uint16_t kMinimumTenBitCode = 0;
constexpr std::uint16_t kMaximumTenBitCode = 1023;
constexpr std::uint16_t kCoarseStep = 10;

constexpr std::array<std::uint16_t, 3> kInitialCodes = {100, 700, 592};
constexpr std::array<PatternPhase, 3> kPhases = {PatternPhase::MinimumLuminance, PatternPhase::MaximumLuminance, PatternPhase::MaximumFullFrameLuminance};

std::uint16_t addClamped(std::uint16_t value, int delta) {
    const int adjusted = static_cast<int>(value) + delta;
    return static_cast<std::uint16_t>(std::clamp(adjusted, static_cast<int>(kMinimumTenBitCode), static_cast<int>(kMaximumTenBitCode)));
}

} // namespace

CalibrationWizard::CalibrationWizard() : m_currentTenBitCode(kInitialCodes.front()) {}

PatternPhase CalibrationWizard::currentPhase() const {
    return kPhases.at(std::min(m_phaseIndex, kPhases.size() - 1));
}

std::size_t CalibrationWizard::currentPhaseIndex() const {
    return m_phaseIndex;
}

std::uint16_t CalibrationWizard::currentTenBitCode() const {
    return m_currentTenBitCode;
}

bool CalibrationWizard::isFinished() const {
    return m_finished;
}

bool CalibrationWizard::isAborted() const {
    return m_aborted;
}

const std::vector<PhaseResult>& CalibrationWizard::results() const {
    return m_results;
}

void CalibrationWizard::handleKey(WizardKey key) {
    if (m_finished || m_aborted)
        return;

    switch (key) {
    case WizardKey::Left:
        m_currentTenBitCode = addClamped(m_currentTenBitCode, -1);
        break;
    case WizardKey::Right:
        m_currentTenBitCode = addClamped(m_currentTenBitCode, 1);
        break;
    case WizardKey::CoarseLeft:
        m_currentTenBitCode = addClamped(m_currentTenBitCode, -static_cast<int>(kCoarseStep));
        break;
    case WizardKey::CoarseRight:
        m_currentTenBitCode = addClamped(m_currentTenBitCode, static_cast<int>(kCoarseStep));
        break;
    case WizardKey::Enter:
        recordCurrentPhaseAndAdvance();
        break;
    case WizardKey::Escape:
        m_aborted = true;
        break;
    }
}

void CalibrationWizard::recordCurrentPhaseAndAdvance() {
    const double luminanceNits = computeLuminanceFromPqCode(computePqCodeFromTenBitSample(m_currentTenBitCode));
    m_results.push_back(PhaseResult{.phase = currentPhase(), .tenBitCode = m_currentTenBitCode, .luminanceNits = luminanceNits});

    if (m_phaseIndex + 1 >= kPhases.size()) {
        m_finished = true;
        return;
    }

    ++m_phaseIndex;
    m_currentTenBitCode = kInitialCodes.at(m_phaseIndex);
}

} // namespace hdrcal
