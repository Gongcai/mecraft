#ifndef MECRAFT_TEMPORAL_RESET_CAPTURE_H
#define MECRAFT_TEMPORAL_RESET_CAPTURE_H

#include "renderer/contracts/TemporalFrameContract.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace app::validation {

/// Accumulates exact temporal reset causes for a validation sample sequence.
/// Every accepted sample retains its source bitmask and contributes to the
/// per-reason histogram and NRD restart totals.
class TemporalResetCapture final {
public:
    /// Clear all samples and reserve storage for a known validation sequence.
    /// @param expectedSampleCount Number of frames expected in the next sequence.
    void reset(const size_t expectedSampleCount) {
        m_sampleReasons.clear();
        m_sampleReasons.reserve(expectedSampleCount);
        m_reasonCounts.fill(0u);
        m_nrdRestartFrameCount = 0u;
    }

    /// Record the reset causes used by one rendered validation sample.
    /// @param reasons Bitmask published by the frame that produced the sample.
    void record(const TemporalResetReasons reasons) {
        m_sampleReasons.push_back(reasons);
        if (ownerRequiresTemporalReset(TemporalHistoryOwner::NrdDiffuse, reasons)) {
            ++m_nrdRestartFrameCount;
        }
        for (size_t index = 0u; index < kTemporalResetReasonDescriptors.size(); ++index) {
            if (hasTemporalResetReason(reasons, kTemporalResetReasonDescriptors[index].reason)) {
                ++m_reasonCounts[index];
            }
        }
    }

    /// Return every recorded frame bitmask in sample order.
    /// @return Immutable reset-cause sequence with one entry per accepted frame.
    [[nodiscard]] const std::vector<TemporalResetReasons>& sampleReasons() const { return m_sampleReasons; }

    /// Return the number of frames containing each stable reset cause.
    /// @return Counts indexed identically to temporalResetReasonDescriptors().
    [[nodiscard]] const std::array<uint64_t, kTemporalResetReasonDescriptors.size()>& reasonCounts() const {
        return m_reasonCounts;
    }

    /// Return how many sampled frames restarted NRD diffuse history.
    /// @return Count after owner-specific reset filtering.
    [[nodiscard]] uint64_t nrdRestartFrameCount() const { return m_nrdRestartFrameCount; }

    /// Return how many sampled frames preserved NRD diffuse history.
    /// @return Recorded sample count minus the NRD restart count.
    [[nodiscard]] uint64_t nrdContinueFrameCount() const {
        return static_cast<uint64_t>(m_sampleReasons.size()) - m_nrdRestartFrameCount;
    }

private:
    std::vector<TemporalResetReasons> m_sampleReasons;
    std::array<uint64_t, kTemporalResetReasonDescriptors.size()> m_reasonCounts{};
    uint64_t m_nrdRestartFrameCount = 0u;
};

} // namespace app::validation

#endif // MECRAFT_TEMPORAL_RESET_CAPTURE_H
