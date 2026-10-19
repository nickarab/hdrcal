// Unit tests for the pure three-phase calibration state machine.

#include "hdrcal/pq.hpp"
#include "hdrcal/wizard.hpp"
#include "test_framework.hpp"

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
