#include "app/validation/TemporalResetCapture.h"

#include <iostream>

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[temporal_reset_capture_test] FAIL: " << message << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    app::validation::TemporalResetCapture capture;
    capture.reset(4u);
    capture.record(temporalResetReasonBit(TemporalResetReason::None));
    capture.record(temporalResetReasonBit(TemporalResetReason::PreExposure));
    capture.record(TemporalResetReason::CameraCut | TemporalResetReason::AssetRevision);
    capture.record(temporalResetReasonBit(TemporalResetReason::DenoiserMethod));

    const auto& samples = capture.sampleReasons();
    const auto& counts = capture.reasonCounts();
    if (!requireTrue(samples.size() == 4u && samples[0] == 0u &&
                         samples[2] == (TemporalResetReason::CameraCut | TemporalResetReason::AssetRevision),
                     "sample bitmasks must preserve exact frame order") ||
        !requireTrue(capture.nrdRestartFrameCount() == 2u && capture.nrdContinueFrameCount() == 2u,
                     "NRD restart totals must apply owner-specific filtering")) {
        return 1;
    }

    for (size_t index = 0u; index < temporalResetReasonDescriptors().size(); ++index) {
        const TemporalResetReason reason = temporalResetReasonDescriptors()[index].reason;
        const uint64_t expected =
            reason == TemporalResetReason::CameraCut || reason == TemporalResetReason::AssetRevision ||
                    reason == TemporalResetReason::PreExposure || reason == TemporalResetReason::DenoiserMethod
                ? 1u
                : 0u;
        if (!requireTrue(counts[index] == expected, "reason histogram must count every set bit independently")) {
            return 1;
        }
    }

    capture.reset(1u);
    return requireTrue(capture.sampleReasons().empty() && capture.nrdRestartFrameCount() == 0u &&
                           capture.nrdContinueFrameCount() == 0u,
                       "reset must clear sequence and all derived totals")
               ? 0
               : 1;
}
