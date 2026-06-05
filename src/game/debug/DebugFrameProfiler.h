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
        struct PollEventCounts {
            unsigned keyEvents = 0;
            unsigned mouseButtonEvents = 0;
            unsigned cursorPosEvents = 0;
            unsigned scrollEvents = 0;
            unsigned charEvents = 0;

            [[nodiscard]] unsigned total() const {
                return keyEvents + mouseButtonEvents + cursorPosEvents + scrollEvents + charEvents;
            }
        };

        double fixedUpdateMs = 0.0;
        double fixedInputMs = 0.0;
        double fixedStateUpdateMs = 0.0;
        double fixedParticleUpdateMs = 0.0;
        double fixedDropUpdateMs = 0.0;
        double fixedWorldUpdateMs = 0.0;
        double audioMs = 0.0;
        double renderMs = 0.0;

        double currentFixedUpdateMs = 0.0;
        double currentFixedInputMs = 0.0;
        double currentFixedStateUpdateMs = 0.0;
        double currentFixedParticleUpdateMs = 0.0;
        double currentFixedDropUpdateMs = 0.0;
        double currentFixedWorldUpdateMs = 0.0;
        double currentAudioMs = 0.0;
        double currentRenderMs = 0.0;
        double currentPollEventsMs = 0.0;
        double currentAppUpdateDispatchMs = 0.0;
        double currentAppRenderDispatchMs = 0.0;
        double currentRenderSnapshotMs = 0.0;
        double currentRenderSceneMs = 0.0;
        double currentRenderUiMs = 0.0;
        double currentRenderDashboardMs = 0.0;
        double currentSwapBuffersMs = 0.0;
        PollEventCounts currentPollEventCounts{};

        double fixedInputAccumMs = 0.0;
        double fixedStateAccumMs = 0.0;
        double fixedParticleAccumMs = 0.0;
        double fixedDropAccumMs = 0.0;
        double fixedWorldAccumMs = 0.0;
        double audioAccumMs = 0.0;
        double renderAccumMs = 0.0;
        double pollEventsAccumMs = 0.0;
        double appUpdateDispatchAccumMs = 0.0;
        double appRenderDispatchAccumMs = 0.0;
        size_t fixedStepCount = 0;
        size_t frameSampleCount = 0;

        double frameFixedInputAccumMs = 0.0;
        double frameFixedStateAccumMs = 0.0;
        double frameFixedParticleAccumMs = 0.0;
        double frameFixedDropAccumMs = 0.0;
        double frameFixedWorldAccumMs = 0.0;
        double frameAudioAccumMs = 0.0;
        double frameRenderAccumMs = 0.0;
        double framePollEventsAccumMs = 0.0;
        double frameAppUpdateDispatchAccumMs = 0.0;
        double frameAppRenderDispatchAccumMs = 0.0;
        double frameRenderSnapshotAccumMs = 0.0;
        double frameRenderSceneAccumMs = 0.0;
        double frameRenderUiAccumMs = 0.0;
        double frameRenderDashboardAccumMs = 0.0;
        double frameSwapBuffersAccumMs = 0.0;
        PollEventCounts framePollEventCounts{};
    };

    struct HistoryData {
        size_t count = 0;
        size_t writeIndex = 0;
        size_t frameCount = 0;
        size_t frameWriteIndex = 0;
        std::array<float, kHistorySamples> fpsHistory{};
        std::array<float, kHistorySamples> renderHistory{};
        std::array<float, kHistorySamples> fixedUpdateHistory{};
        std::array<float, kHistorySamples> fixedInputHistory{};
        std::array<float, kHistorySamples> fixedStateHistory{};
        std::array<float, kHistorySamples> fixedParticleHistory{};
        std::array<float, kHistorySamples> fixedDropHistory{};
        std::array<float, kHistorySamples> fixedWorldHistory{};
    };

    /// Record timing for a fixed update sub-stage.
    void recordFixedInput(double ms) {
        m_timing.fixedInputAccumMs += ms;
        m_timing.frameFixedInputAccumMs += ms;
    }
    void recordFixedState(double ms) {
        m_timing.fixedStateAccumMs += ms;
        m_timing.frameFixedStateAccumMs += ms;
    }
    void recordFixedParticle(double ms) {
        m_timing.fixedParticleAccumMs += ms;
        m_timing.frameFixedParticleAccumMs += ms;
    }
    void recordFixedDrop(double ms) {
        m_timing.fixedDropAccumMs += ms;
        m_timing.frameFixedDropAccumMs += ms;
    }
    void recordFixedWorld(double ms) {
        m_timing.fixedWorldAccumMs += ms;
        m_timing.frameFixedWorldAccumMs += ms;
    }
    void recordAudio(double ms) {
        m_timing.audioAccumMs += ms;
        m_timing.frameAudioAccumMs += ms;
    }
    void recordRender(double ms) {
        m_timing.renderAccumMs += ms;
        m_timing.frameRenderAccumMs += ms;
        ++m_timing.frameSampleCount;
    }
    void recordPollEvents(double ms,
                          unsigned keyEvents,
                          unsigned mouseButtonEvents,
                          unsigned cursorPosEvents,
                          unsigned scrollEvents,
                          unsigned charEvents) {
        m_timing.pollEventsAccumMs += ms;
        m_timing.framePollEventsAccumMs += ms;
        m_timing.framePollEventCounts.keyEvents += keyEvents;
        m_timing.framePollEventCounts.mouseButtonEvents += mouseButtonEvents;
        m_timing.framePollEventCounts.cursorPosEvents += cursorPosEvents;
        m_timing.framePollEventCounts.scrollEvents += scrollEvents;
        m_timing.framePollEventCounts.charEvents += charEvents;
    }
    void recordAppUpdateDispatch(double ms) {
        m_timing.appUpdateDispatchAccumMs += ms;
        m_timing.frameAppUpdateDispatchAccumMs += ms;
    }
    void recordAppRenderDispatch(double ms) {
        m_timing.appRenderDispatchAccumMs += ms;
        m_timing.frameAppRenderDispatchAccumMs += ms;
    }
    void recordRenderSnapshot(double ms) { m_timing.frameRenderSnapshotAccumMs += ms; }
    void recordRenderScene(double ms) { m_timing.frameRenderSceneAccumMs += ms; }
    void recordRenderUi(double ms) { m_timing.frameRenderUiAccumMs += ms; }
    void recordRenderDashboard(double ms) { m_timing.frameRenderDashboardAccumMs += ms; }
    void recordSwapBuffers(double ms) { m_timing.frameSwapBuffersAccumMs += ms; }

    /// Increment the fixed step counter.
    void incrementFixedStep() { ++m_timing.fixedStepCount; }

    /// Publish accumulated timing data to history ring buffers.
    /// Called once per frame after all fixed steps are complete.
    void publish(double frameTime);

    /// Reset per-frame accumulators (called after publish).
    void resetAccumulators();
    void resetFrameAccumulators();

    // Accessors
    [[nodiscard]] const TimingData& timing() const { return m_timing; }
    [[nodiscard]] const HistoryData& history() const { return m_history; }

private:
    TimingData m_timing{};
    HistoryData m_history{};
    double m_frameHistoryAccumulator = 0.0;
    double m_frameHistoryInterval = 0.2;
    double m_publishAccumulator = 0.0;
    double m_publishInterval = 0.25;
};

#endif // MECRAFT_DEBUG

#endif // MECRAFT_DEBUG_FRAME_PROFILER_H
