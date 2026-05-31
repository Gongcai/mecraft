#include "DebugFrameProfiler.h"

#ifdef MECRAFT_DEBUG

void DebugFrameProfiler::publish(double frameTime) {
    m_publishAccumulator += frameTime;
    if (m_publishAccumulator < m_publishInterval) {
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

    // Push to history ring buffers
    const size_t idx = m_history.writeIndex;
    m_history.fixedUpdateHistory[idx] = static_cast<float>(m_timing.fixedUpdateMs);
    m_history.fixedInputHistory[idx] = static_cast<float>(m_timing.fixedInputMs);
    m_history.fixedStateHistory[idx] = static_cast<float>(m_timing.fixedStateUpdateMs);
    m_history.fixedParticleHistory[idx] = static_cast<float>(m_timing.fixedParticleUpdateMs);
    m_history.fixedDropHistory[idx] = static_cast<float>(m_timing.fixedDropUpdateMs);
    m_history.fixedWorldHistory[idx] = static_cast<float>(m_timing.fixedWorldUpdateMs);

    m_history.writeIndex = (idx + 1) % kHistorySamples;
    if (m_history.count < kHistorySamples) {
        ++m_history.count;
    }

    resetAccumulators();
}

void DebugFrameProfiler::resetAccumulators() {
    m_timing.fixedInputAccumMs = 0.0;
    m_timing.fixedStateAccumMs = 0.0;
    m_timing.fixedParticleAccumMs = 0.0;
    m_timing.fixedDropAccumMs = 0.0;
    m_timing.fixedWorldAccumMs = 0.0;
    m_timing.fixedStepCount = 0;
}

#endif // MECRAFT_DEBUG
