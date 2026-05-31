#ifndef MECRAFT_DEBUG_FRAME_PROFILER_H
#define MECRAFT_DEBUG_FRAME_PROFILER_H

#include <array>
#include <cstddef>

#ifdef MECRAFT_DEBUG

/// Debug frame profiler — collects timing data for fixed update sub-stages.
/// Extracted from Game to reduce its responsibilities.
/// Only compiled in debug builds.
class DebugFrameProfiler {
public:
    static constexpr size_t kHistorySamples = 120;

    struct TimingData {
        double fixedUpdateMs = 0.0;
        double fixedInputMs = 0.0;
        double fixedStateUpdateMs = 0.0;
        double fixedParticleUpdateMs = 0.0;
        double fixedDropUpdateMs = 0.0;
        double fixedWorldUpdateMs = 0.0;
        double audioMs = 0.0;
        double renderMs = 0.0;

        double fixedInputAccumMs = 0.0;
        double fixedStateAccumMs = 0.0;
        double fixedParticleAccumMs = 0.0;
        double fixedDropAccumMs = 0.0;
        double fixedWorldAccumMs = 0.0;
        size_t fixedStepCount = 0;
    };

    struct HistoryData {
        size_t count = 0;
        size_t writeIndex = 0;
        std::array<float, kHistorySamples> fixedUpdateHistory{};
        std::array<float, kHistorySamples> fixedInputHistory{};
        std::array<float, kHistorySamples> fixedStateHistory{};
        std::array<float, kHistorySamples> fixedParticleHistory{};
        std::array<float, kHistorySamples> fixedDropHistory{};
        std::array<float, kHistorySamples> fixedWorldHistory{};
    };

    /// Record timing for a fixed update sub-stage.
    void recordFixedInput(double ms) { m_timing.fixedInputAccumMs += ms; }
    void recordFixedState(double ms) { m_timing.fixedStateAccumMs += ms; }
    void recordFixedParticle(double ms) { m_timing.fixedParticleAccumMs += ms; }
    void recordFixedDrop(double ms) { m_timing.fixedDropAccumMs += ms; }
    void recordFixedWorld(double ms) { m_timing.fixedWorldAccumMs += ms; }

    /// Increment the fixed step counter.
    void incrementFixedStep() { ++m_timing.fixedStepCount; }

    /// Publish accumulated timing data to history ring buffers.
    /// Called once per frame after all fixed steps are complete.
    void publish(double frameTime);

    /// Reset per-frame accumulators (called after publish).
    void resetAccumulators();

    // Accessors
    [[nodiscard]] const TimingData& timing() const { return m_timing; }
    [[nodiscard]] const HistoryData& history() const { return m_history; }

private:
    TimingData m_timing{};
    HistoryData m_history{};
    double m_publishAccumulator = 0.0;
    double m_publishInterval = 0.25;
};

#endif // MECRAFT_DEBUG

#endif // MECRAFT_DEBUG_FRAME_PROFILER_H
