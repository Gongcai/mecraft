#include "DebugFrameProfiler.h"

#ifdef MECRAFT_DEBUG

void DebugFrameProfiler::publish(double frameTime) {
    m_timing.currentFixedInputMs = m_timing.frameFixedInputAccumMs;
    m_timing.currentFixedStateUpdateMs = m_timing.frameFixedStateAccumMs;
    m_timing.currentFixedParticleUpdateMs = m_timing.frameFixedParticleAccumMs;
    m_timing.currentFixedDropUpdateMs = m_timing.frameFixedDropAccumMs;
    m_timing.currentFixedWorldUpdateMs = m_timing.frameFixedWorldAccumMs;
    m_timing.currentFixedUpdateMs = m_timing.currentFixedInputMs + m_timing.currentFixedStateUpdateMs +
                                     m_timing.currentFixedParticleUpdateMs + m_timing.currentFixedDropUpdateMs +
                                     m_timing.currentFixedWorldUpdateMs;
    m_timing.currentAudioMs = m_timing.frameAudioAccumMs;
    m_timing.currentRenderMs = m_timing.frameRenderAccumMs;
    m_timing.currentPollEventsMs = m_timing.framePollEventsAccumMs;
    m_timing.currentAppUpdateDispatchMs = m_timing.frameAppUpdateDispatchAccumMs;
    m_timing.currentAppRenderDispatchMs = m_timing.frameAppRenderDispatchAccumMs;
    m_timing.currentPresentationAcquireMs =
        m_timing.framePresentationAcquireAccumMs;
    m_timing.currentRenderSnapshotMs = m_timing.frameRenderSnapshotAccumMs;
    m_timing.currentRenderSceneMs = m_timing.frameRenderSceneAccumMs;
    m_timing.currentRenderUiMs = m_timing.frameRenderUiAccumMs;
    m_timing.currentRenderDashboardMs = m_timing.frameRenderDashboardAccumMs;
    m_timing.currentSwapBuffersMs = m_timing.frameSwapBuffersAccumMs;
    m_timing.currentPollInputCallbackMs = m_timing.framePollInputCallbackAccumMs;
    m_timing.currentPollCursorPosCallbackMs = m_timing.framePollCursorPosCallbackAccumMs;
    m_timing.currentPollImguiCallbackMs = m_timing.framePollImguiCallbackAccumMs;
    m_timing.currentPollImguiCursorPosCallbackMs = m_timing.framePollImguiCursorPosCallbackAccumMs;
    m_timing.currentPollImguiCursorPosBackendMs = m_timing.framePollImguiCursorPosBackendAccumMs;
    m_timing.currentPollImguiWndProcMs = m_timing.framePollImguiWndProcAccumMs;
    m_timing.currentPollImguiWndProcSlowestMs = m_timing.framePollImguiWndProcSlowestMs;
    m_timing.currentPollImguiWndProcSlowestMsg = m_timing.framePollImguiWndProcSlowestMsg;
    m_timing.currentPollImguiWndProcCount = m_timing.framePollImguiWndProcCount;
    m_timing.currentPollEventCounts = m_timing.framePollEventCounts;

    m_frameHistoryAccumulator += frameTime;
    if (frameTime > 0.0 && m_frameHistoryAccumulator >= m_frameHistoryInterval) {
        m_frameHistoryAccumulator -= m_frameHistoryInterval;
        const size_t frameIdx = m_history.frameWriteIndex;
        m_history.fpsHistory[frameIdx] = static_cast<float>(1.0 / frameTime);
        m_history.frameWriteIndex = (frameIdx + 1) % kHistorySamples;
        if (m_history.frameCount < kHistorySamples) {
            ++m_history.frameCount;
        }
    }

    m_publishAccumulator += frameTime;
    if (m_publishAccumulator < m_publishInterval) {
        resetFrameAccumulators();
        return;
    }
    m_publishAccumulator -= m_publishInterval;

    // Compute averages from accumulators
    const double steps = static_cast<double>(m_timing.fixedStepCount);
    const auto avg = [steps](double accum) -> float {
        return steps > 0.0 ? static_cast<float>(accum / steps) : 0.0f;
    };

    m_timing.fixedUpdateMs = avg(m_timing.fixedInputAccumMs + m_timing.fixedStateAccumMs +
                                  m_timing.fixedParticleAccumMs + m_timing.fixedDropAccumMs +
                                  m_timing.fixedWorldAccumMs);
    m_timing.fixedInputMs = avg(m_timing.fixedInputAccumMs);
    m_timing.fixedStateUpdateMs = avg(m_timing.fixedStateAccumMs);
    m_timing.fixedParticleUpdateMs = avg(m_timing.fixedParticleAccumMs);
    m_timing.fixedDropUpdateMs = avg(m_timing.fixedDropAccumMs);
    m_timing.fixedWorldUpdateMs = avg(m_timing.fixedWorldAccumMs);
    const double frames = static_cast<double>(m_timing.frameSampleCount);
    m_timing.audioMs = frames > 0.0 ? m_timing.audioAccumMs / frames : 0.0;
    m_timing.renderMs = frames > 0.0 ? m_timing.renderAccumMs / frames : 0.0;

    // Push to history ring buffers
    const size_t idx = m_history.writeIndex;
    m_history.fixedUpdateHistory[idx] = static_cast<float>(m_timing.fixedUpdateMs);
    m_history.fixedInputHistory[idx] = static_cast<float>(m_timing.fixedInputMs);
    m_history.fixedStateHistory[idx] = static_cast<float>(m_timing.fixedStateUpdateMs);
    m_history.fixedParticleHistory[idx] = static_cast<float>(m_timing.fixedParticleUpdateMs);
    m_history.fixedDropHistory[idx] = static_cast<float>(m_timing.fixedDropUpdateMs);
    m_history.fixedWorldHistory[idx] = static_cast<float>(m_timing.fixedWorldUpdateMs);
    m_history.renderHistory[idx] = static_cast<float>(m_timing.renderMs);

    m_history.writeIndex = (idx + 1) % kHistorySamples;
    if (m_history.count < kHistorySamples) {
        ++m_history.count;
    }

    resetAccumulators();
    resetFrameAccumulators();
}

void DebugFrameProfiler::resetAccumulators() {
    m_timing.fixedInputAccumMs = 0.0;
    m_timing.fixedStateAccumMs = 0.0;
    m_timing.fixedParticleAccumMs = 0.0;
    m_timing.fixedDropAccumMs = 0.0;
    m_timing.fixedWorldAccumMs = 0.0;
    m_timing.audioAccumMs = 0.0;
    m_timing.renderAccumMs = 0.0;
    m_timing.pollEventsAccumMs = 0.0;
    m_timing.appUpdateDispatchAccumMs = 0.0;
    m_timing.appRenderDispatchAccumMs = 0.0;
    m_timing.fixedStepCount = 0;
    m_timing.frameSampleCount = 0;
}

void DebugFrameProfiler::resetFrameAccumulators() {
    m_timing.frameFixedInputAccumMs = 0.0;
    m_timing.frameFixedStateAccumMs = 0.0;
    m_timing.frameFixedParticleAccumMs = 0.0;
    m_timing.frameFixedDropAccumMs = 0.0;
    m_timing.frameFixedWorldAccumMs = 0.0;
    m_timing.frameAudioAccumMs = 0.0;
    m_timing.frameRenderAccumMs = 0.0;
    m_timing.framePollEventsAccumMs = 0.0;
    m_timing.frameAppUpdateDispatchAccumMs = 0.0;
    m_timing.frameAppRenderDispatchAccumMs = 0.0;
    m_timing.framePresentationAcquireAccumMs = 0.0;
    m_timing.frameRenderSnapshotAccumMs = 0.0;
    m_timing.frameRenderSceneAccumMs = 0.0;
    m_timing.frameRenderUiAccumMs = 0.0;
    m_timing.frameRenderDashboardAccumMs = 0.0;
    m_timing.frameSwapBuffersAccumMs = 0.0;
    m_timing.framePollInputCallbackAccumMs = 0.0;
    m_timing.framePollCursorPosCallbackAccumMs = 0.0;
    m_timing.framePollImguiCallbackAccumMs = 0.0;
    m_timing.framePollImguiCursorPosCallbackAccumMs = 0.0;
    m_timing.framePollImguiCursorPosBackendAccumMs = 0.0;
    m_timing.framePollImguiWndProcAccumMs = 0.0;
    m_timing.framePollImguiWndProcSlowestMs = 0.0;
    m_timing.framePollImguiWndProcSlowestMsg = 0;
    m_timing.framePollImguiWndProcCount = 0;
    m_timing.framePollEventCounts = {};
}

#endif // MECRAFT_DEBUG
