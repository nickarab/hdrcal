// Unit tests for the pure three-phase calibration state machine.

#include "hdrcal/pq.hpp"
#include "hdrcal/wizard.hpp"
#include "test_framework.hpp"

#include <chrono>
#include <cstdint>

HDRCAL_TEST(testCalibrationWizard_StartsAtFirstPhaseWithKnownCode) {
    const hdrcal::CalibrationWizard wizard;
    HDRCAL_REQUIRE_EQ(wizard.currentPhaseIndex(), std::size_t{0});
    HDRCAL_REQUIRE_EQ(wizard.currentPhase(), hdrcal::PatternPhase::MinimumLuminance);
    HDRCAL_REQUIRE_EQ(wizard.currentTenBitCode(), std::uint16_t{100});
    HDRCAL_REQUIRE(!wizard.isFinished());
    HDRCAL_REQUIRE(!wizard.isAborted());
    HDRCAL_REQUIRE(wizard.results().empty());
}

HDRCAL_TEST(testCalibrationWizard_ArrowKeysStepByOneAndClamp) {
    hdrcal::CalibrationWizard wizard;
    wizard.handleKey(hdrcal::WizardKey::Right);
    HDRCAL_REQUIRE_EQ(wizard.currentTenBitCode(), std::uint16_t{101});
    wizard.handleKey(hdrcal::WizardKey::Left);
    HDRCAL_REQUIRE_EQ(wizard.currentTenBitCode(), std::uint16_t{100});

    for (int step = 0; step < 150; ++step)
        wizard.handleKey(hdrcal::WizardKey::CoarseLeft);
    HDRCAL_REQUIRE_EQ(wizard.currentTenBitCode(), std::uint16_t{0});

    for (int step = 0; step < 150; ++step)
        wizard.handleKey(hdrcal::WizardKey::CoarseRight);
    HDRCAL_REQUIRE_EQ(wizard.currentTenBitCode(), std::uint16_t{1023});
}

HDRCAL_TEST(testCalibrationWizard_CoarseKeysStepByTenAndClamp) {
    hdrcal::CalibrationWizard wizard;
    wizard.handleKey(hdrcal::WizardKey::CoarseLeft);
    HDRCAL_REQUIRE_EQ(wizard.currentTenBitCode(), std::uint16_t{90});
    wizard.handleKey(hdrcal::WizardKey::CoarseRight);
    HDRCAL_REQUIRE_EQ(wizard.currentTenBitCode(), std::uint16_t{100});
}

HDRCAL_TEST(testCalibrationWizard_EnterRecordsLuminanceAndAdvancesPhases) {
    hdrcal::CalibrationWizard wizard;
    wizard.handleKey(hdrcal::WizardKey::Right); // 101
    wizard.handleKey(hdrcal::WizardKey::Enter);

    HDRCAL_REQUIRE_EQ(wizard.results().size(), std::size_t{1});
    HDRCAL_REQUIRE_EQ(wizard.results().at(0).phase, hdrcal::PatternPhase::MinimumLuminance);
    HDRCAL_REQUIRE_EQ(wizard.results().at(0).tenBitCode, std::uint16_t{101});
    HDRCAL_REQUIRE_NEAR(wizard.results().at(0).luminanceNits, hdrcal::computeLuminanceFromPqCode(101.0 / 1023.0), 1e-12);

    HDRCAL_REQUIRE_EQ(wizard.currentPhaseIndex(), std::size_t{1});
    HDRCAL_REQUIRE_EQ(wizard.currentPhase(), hdrcal::PatternPhase::MaximumLuminance);
    HDRCAL_REQUIRE_EQ(wizard.currentTenBitCode(), std::uint16_t{700});

    wizard.handleKey(hdrcal::WizardKey::Enter);
    HDRCAL_REQUIRE_EQ(wizard.currentPhaseIndex(), std::size_t{2});
    HDRCAL_REQUIRE_EQ(wizard.currentPhase(), hdrcal::PatternPhase::MaximumFullFrameLuminance);
    HDRCAL_REQUIRE_EQ(wizard.currentTenBitCode(), std::uint16_t{592});

    wizard.handleKey(hdrcal::WizardKey::Enter);
    HDRCAL_REQUIRE(wizard.isFinished());
    HDRCAL_REQUIRE_EQ(wizard.results().size(), std::size_t{3});
    HDRCAL_REQUIRE_EQ(wizard.results().at(2).phase, hdrcal::PatternPhase::MaximumFullFrameLuminance);
}

HDRCAL_TEST(testCalibrationWizard_EscapeAbortsAndIgnoresFurtherKeys) {
    hdrcal::CalibrationWizard wizard;
    wizard.handleKey(hdrcal::WizardKey::Escape);
    HDRCAL_REQUIRE(wizard.isAborted());
    HDRCAL_REQUIRE(!wizard.isFinished());
    HDRCAL_REQUIRE(wizard.results().empty());

    wizard.handleKey(hdrcal::WizardKey::Enter);
    HDRCAL_REQUIRE(wizard.results().empty());
}

HDRCAL_TEST(testCalibrationWizard_KeysAfterFinishAreIgnored) {
    hdrcal::CalibrationWizard wizard;
    wizard.handleKey(hdrcal::WizardKey::Enter);
    wizard.handleKey(hdrcal::WizardKey::Enter);
    wizard.handleKey(hdrcal::WizardKey::Enter);
    HDRCAL_REQUIRE(wizard.isFinished());

    const std::uint16_t codeBefore = wizard.currentTenBitCode();
    wizard.handleKey(hdrcal::WizardKey::Right);
    HDRCAL_REQUIRE_EQ(wizard.currentTenBitCode(), codeBefore);
    HDRCAL_REQUIRE_EQ(wizard.results().size(), std::size_t{3});
}

HDRCAL_TEST(testCalibrationWizard_HeldArrowAcceleratesToCoarseStepAfterOneSecond) {
    hdrcal::CalibrationWizard wizard;
    const auto right = hdrcal::WizardKey::Right;

    wizard.handleKeyAt(right, std::chrono::milliseconds{0});
    HDRCAL_REQUIRE_EQ(wizard.currentTenBitCode(), std::uint16_t{101});
    for (int time = 100; time < 1000; time += 100)
        wizard.handleKeyAt(right, std::chrono::milliseconds{time});
    HDRCAL_REQUIRE_EQ(wizard.currentTenBitCode(), std::uint16_t{110});
    wizard.handleKeyAt(right, std::chrono::milliseconds{1000});
    HDRCAL_REQUIRE_EQ(wizard.currentTenBitCode(), std::uint16_t{120});
    wizard.handleKeyAt(right, std::chrono::milliseconds{1066});
    HDRCAL_REQUIRE_EQ(wizard.currentTenBitCode(), std::uint16_t{130});
}

HDRCAL_TEST(testCalibrationWizard_PausingOrChangingDirectionRestartsFineStep) {
    hdrcal::CalibrationWizard wizard;

    wizard.handleKeyAt(hdrcal::WizardKey::Right, std::chrono::milliseconds{0});
    wizard.handleKeyAt(hdrcal::WizardKey::Right, std::chrono::milliseconds{900});
    HDRCAL_REQUIRE_EQ(wizard.currentTenBitCode(), std::uint16_t{102});

    // A gap larger than the repeat interval is a new press, so the step is
    // fine again even though the previous hold was long.
    wizard.handleKeyAt(hdrcal::WizardKey::Right, std::chrono::milliseconds{2000});
    HDRCAL_REQUIRE_EQ(wizard.currentTenBitCode(), std::uint16_t{103});

    // Direction change also restarts the hold: the next repeats are fine again.
    wizard.handleKeyAt(hdrcal::WizardKey::Left, std::chrono::milliseconds{2500});
    HDRCAL_REQUIRE_EQ(wizard.currentTenBitCode(), std::uint16_t{102});
    for (int time = 2600; time <= 3400; time += 100)
        wizard.handleKeyAt(hdrcal::WizardKey::Left, std::chrono::milliseconds{time});
    HDRCAL_REQUIRE_EQ(wizard.currentTenBitCode(), std::uint16_t{93});

    // 1000 ms after the new hold started, the coarse step applies.
    wizard.handleKeyAt(hdrcal::WizardKey::Left, std::chrono::milliseconds{3500});
    HDRCAL_REQUIRE_EQ(wizard.currentTenBitCode(), std::uint16_t{83});
}

HDRCAL_TEST(testCalibrationWizard_RepeatedEnterInBurstAdvancesOnlyOnce) {
    hdrcal::CalibrationWizard wizard;
    const auto enter = hdrcal::WizardKey::Enter;

    wizard.handleKeyAt(enter, std::chrono::milliseconds{0});
    HDRCAL_REQUIRE_EQ(wizard.results().size(), std::size_t{1});
    HDRCAL_REQUIRE_EQ(wizard.currentPhaseIndex(), std::size_t{1});

    const std::chrono::milliseconds burstTimes[] = {std::chrono::milliseconds{100}, std::chrono::milliseconds{200}, std::chrono::milliseconds{300},
                                                    std::chrono::milliseconds{366}, std::chrono::milliseconds{432}};
    for (const std::chrono::milliseconds time : burstTimes) {
        wizard.handleKeyAt(enter, time);
        HDRCAL_REQUIRE_EQ(wizard.results().size(), std::size_t{1});
        HDRCAL_REQUIRE_EQ(wizard.currentPhaseIndex(), std::size_t{1});
    }

    // After the burst stops for the debounce window, a new press is accepted.
    wizard.handleKeyAt(enter, std::chrono::milliseconds{800});
    HDRCAL_REQUIRE_EQ(wizard.results().size(), std::size_t{2});
    HDRCAL_REQUIRE_EQ(wizard.currentPhaseIndex(), std::size_t{2});
}

HDRCAL_TEST(testCalibrationWizard_ClockResetDoesNotAccelerateNextPress) {
    hdrcal::CalibrationWizard wizard;
    const auto right = hdrcal::WizardKey::Right;

    wizard.handleKeyAt(right, std::chrono::milliseconds{5000});
    HDRCAL_REQUIRE_EQ(wizard.currentTenBitCode(), std::uint16_t{101});

    wizard.handleKeyAt(right, std::chrono::milliseconds{10});
    HDRCAL_REQUIRE_EQ(wizard.currentTenBitCode(), std::uint16_t{102});
}
