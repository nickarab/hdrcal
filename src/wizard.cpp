// Implements the pure three-phase calibration state machine.

#include "hdrcal/wizard.hpp"

#include "hdrcal/pq.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>

namespace hdrcal {
namespace {

constexpr std::uint16_t kMinimumTenBitCode = 0;
constexpr std::uint16_t kMaximumTenBitCode = 1023;
constexpr std::uint16_t kCoarseStep = 10;

// mpv emits repeats at --input-ar-rate (10/s, 100 ms after the initial 200 ms
// delay). A larger gap means a new physical press, not a continuation.
constexpr std::chrono::milliseconds kHoldContinuityGap{300};
// Holding an arrow for this long switches from the fine step to the existing
// coarse step. The requirement is "about one second".
constexpr std::chrono::milliseconds kCoarseStepHoldThreshold{1000};
// A repeated Enter/Escape within this window is treated as an input bounce.
// It is intentionally shorter than the coarse threshold so deliberate phase
// advances stay possible.
constexpr std::chrono::milliseconds kDiscreteKeyDebounce{250};
// Untimed callers get fresh presses separated by more than the debounce window.
constexpr std::chrono::milliseconds kUntimedKeySpacing{kDiscreteKeyDebounce.count() + 1};

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
    handleKeyAt(key, m_lastEventTime + kUntimedKeySpacing);
}

void CalibrationWizard::handleKeyAt(WizardKey key, std::chrono::milliseconds eventTime) {
    if (m_finished || m_aborted)
        return;

    if (eventTime < m_lastEventTime) {
        // A clock reset must not turn into a huge negative gap and must not
        // carry a stale debounce into the new timeline.
        resetHold();
        m_lastDiscreteKey.reset();
        m_lastEventTime = eventTime;
    }

    switch (key) {
    case WizardKey::Left:
    case WizardKey::Right: {
        // Same direction with a gap no larger than the repeat interval is a
        // held key. Any direction change or pause starts a new hold.
        const bool continuesHold = m_holdKey == key && m_holdStartTime.has_value() && eventTime - m_lastEventTime <= kHoldContinuityGap;
        if (!continuesHold) {
            m_holdKey = key;
            m_holdStartTime = eventTime;
        }

        const bool accelerated = eventTime - *m_holdStartTime >= kCoarseStepHoldThreshold;
        const int step = accelerated ? static_cast<int>(kCoarseStep) : 1;
        m_currentTenBitCode = addClamped(m_currentTenBitCode, key == WizardKey::Left ? -step : step);
        m_lastDiscreteKey.reset();
        break;
    }
    case WizardKey::CoarseLeft:
        resetHold();
        m_currentTenBitCode = addClamped(m_currentTenBitCode, -static_cast<int>(kCoarseStep));
        m_lastDiscreteKey.reset();
        break;
    case WizardKey::CoarseRight:
        resetHold();
        m_currentTenBitCode = addClamped(m_currentTenBitCode, static_cast<int>(kCoarseStep));
        m_lastDiscreteKey.reset();
        break;
    case WizardKey::Enter:
        resetHold();
        if (isDiscreteKeyDebounced(key, eventTime)) {
            // Keep extending the window while the burst continues, so a held
            // (auto-repeated) Enter never saves a second phase.
            m_lastDiscreteTime = eventTime;
            break;
        }
        m_lastDiscreteKey = key;
        m_lastDiscreteTime = eventTime;
        recordCurrentPhaseAndAdvance();
        break;
    case WizardKey::Escape:
        resetHold();
        if (isDiscreteKeyDebounced(key, eventTime)) {
            m_lastDiscreteTime = eventTime;
            break;
        }
        m_lastDiscreteKey = key;
        m_lastDiscreteTime = eventTime;
        m_aborted = true;
        break;
    }

    m_lastEventTime = eventTime;
}

void CalibrationWizard::resetHold() {
    m_holdKey.reset();
    m_holdStartTime.reset();
}

bool CalibrationWizard::isDiscreteKeyDebounced(WizardKey key, std::chrono::milliseconds eventTime) const {
    return m_lastDiscreteKey == key && eventTime - m_lastDiscreteTime < kDiscreteKeyDebounce;
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
