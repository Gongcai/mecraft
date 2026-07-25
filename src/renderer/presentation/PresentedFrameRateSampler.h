#ifndef MECRAFT_PRESENTED_FRAME_RATE_SAMPLER_H
#define MECRAFT_PRESENTED_FRAME_RATE_SAMPLER_H

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>

/// Samples the actual display rate from a monotonic presented-frame counter.
class PresentedFrameRateSampler {
public:
    static constexpr double kSampleIntervalSeconds = 0.5;

    /// Adds one elapsed-time observation for the current cumulative display count.
    /// @param displayedFrames Total frames confirmed as displayed by the presentation backend.
    /// @param elapsedSeconds Wall-clock duration since the preceding observation.
    /// @return A new frames-per-second sample when the sampling interval completes.
    [[nodiscard]] std::optional<double> update(const uint64_t displayedFrames,
                                               const double elapsedSeconds) {
        if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0) {
            std::abort();
        }
        if (!m_initialized) {
            m_previousDisplayedFrames = displayedFrames;
            m_initialized = true;
            return std::nullopt;
        }
        if (displayedFrames < m_previousDisplayedFrames) {
            std::abort();
        }

        m_elapsedSeconds += elapsedSeconds;
        if (m_elapsedSeconds < kSampleIntervalSeconds) {
            return std::nullopt;
        }

        const uint64_t displayedFrameDelta =
            displayedFrames - m_previousDisplayedFrames;
        const double presentedFps =
            static_cast<double>(displayedFrameDelta) / m_elapsedSeconds;
        m_previousDisplayedFrames = displayedFrames;
        m_elapsedSeconds = 0.0;
        return presentedFps;
    }

private:
    uint64_t m_previousDisplayedFrames = 0u;
    double m_elapsedSeconds = 0.0;
    bool m_initialized = false;
};

#endif // MECRAFT_PRESENTED_FRAME_RATE_SAMPLER_H
