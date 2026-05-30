#include "RenderDebugService.h"

void RenderDebugService::init() {
    if (m_gpuTimersInitialized) {
        return;
    }

    m_gpuFrameStats.supported = GLAD_GL_VERSION_3_3 != 0;
    if (!m_gpuFrameStats.supported) {
        return;
    }

    for (auto& slot : m_gpuTimerQueries) {
        glGenQueries(static_cast<GLsizei>(slot.size()), slot.data());
    }
    for (auto& issued : m_gpuTimerIssued) {
        issued.fill(false);
    }
    m_gpuTimersInitialized = true;
}

void RenderDebugService::shutdown() {
    if (!m_gpuTimersInitialized) {
        return;
    }
    if (m_gpuTimerActive) {
        glEndQuery(GL_TIME_ELAPSED);
        m_gpuTimerActive = false;
    }

    for (auto& slot : m_gpuTimerQueries) {
        glDeleteQueries(static_cast<GLsizei>(slot.size()), slot.data());
        slot.fill(0);
    }
    for (auto& issued : m_gpuTimerIssued) {
        issued.fill(false);
    }
    m_gpuTimersInitialized = false;
    m_gpuFrameStats.valid = false;
}

void RenderDebugService::beginFrame() {
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled) {
        m_gpuFrameStats.supported = m_gpuTimersInitialized && m_gpuFrameStats.supported;
        m_gpuFrameStats.valid = false;
        m_gpuTimerCanIssueThisFrame = false;
        return;
    }

    const size_t readIndex = (m_gpuTimerWriteIndex + 1) % GPU_TIMER_RING_SIZE;
    bool allIssued = false;
    for (const bool issued : m_gpuTimerIssued[readIndex]) {
        allIssued = allIssued || issued;
    }

    if (allIssued) {
        bool allAvailable = true;
        for (size_t pass = 0; pass < static_cast<size_t>(GpuTimerPass::Count); ++pass) {
            if (!m_gpuTimerIssued[readIndex][pass]) {
                continue;
            }
            GLint available = GL_FALSE;
            glGetQueryObjectiv(m_gpuTimerQueries[readIndex][pass], GL_QUERY_RESULT_AVAILABLE, &available);
            if (available == GL_FALSE) {
                allAvailable = false;
                break;
            }
        }

        if (allAvailable) {
            auto readMs = [&](const GpuTimerPass pass) {
                const size_t passIndex = static_cast<size_t>(pass);
                if (!m_gpuTimerIssued[readIndex][passIndex]) {
                    return 0.0;
                }
                GLuint64 elapsedNs = 0;
                glGetQueryObjectui64v(m_gpuTimerQueries[readIndex][passIndex], GL_QUERY_RESULT, &elapsedNs);
                return static_cast<double>(elapsedNs) / 1000000.0;
            };

            m_gpuFrameStats.supported = true;
            m_gpuFrameStats.valid = true;
            m_gpuFrameStats.gbufferMs = readMs(GpuTimerPass::GBuffer);
            m_gpuFrameStats.shadowMs = readMs(GpuTimerPass::Shadow);
            m_gpuFrameStats.ssaoMs = readMs(GpuTimerPass::Ssao);
            m_gpuFrameStats.lightingMs = readMs(GpuTimerPass::Lighting);
            m_gpuFrameStats.transparentMs = readMs(GpuTimerPass::Transparent);
            m_gpuFrameStats.volumetricMs = readMs(GpuTimerPass::Volumetric);
            m_gpuFrameStats.reflectionMs = readMs(GpuTimerPass::Reflection);
            m_gpuFrameStats.cloudMs = readMs(GpuTimerPass::Cloud);
            m_gpuFrameStats.waterMs = readMs(GpuTimerPass::Water);
            m_gpuFrameStats.postMs = readMs(GpuTimerPass::Post);
            m_gpuTimerIssued[readIndex].fill(false);
        }
    }

    bool slotStillPending = false;
    for (const bool issued : m_gpuTimerIssued[readIndex]) {
        if (issued) {
            slotStillPending = true;
            break;
        }
    }
    m_gpuTimerCanIssueThisFrame = !slotStillPending;
    if (!m_gpuTimerCanIssueThisFrame) {
        return;
    }

    if (m_gpuTimerActive) {
        glEndQuery(GL_TIME_ELAPSED);
        m_gpuTimerActive = false;
    }
    m_gpuTimerWriteIndex = (m_gpuTimerWriteIndex + 1) % GPU_TIMER_RING_SIZE;
    m_gpuTimerIssued[m_gpuTimerWriteIndex].fill(false);
}

void RenderDebugService::beginGpuTimer(const GpuTimerPass pass) {
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled || !m_gpuTimerCanIssueThisFrame || m_gpuTimerActive) {
        return;
    }

    const size_t passIndex = static_cast<size_t>(pass);
    glBeginQuery(GL_TIME_ELAPSED, m_gpuTimerQueries[m_gpuTimerWriteIndex][passIndex]);
    m_gpuTimerActive = true;
    m_activeGpuTimerPass = pass;
}

void RenderDebugService::endGpuTimer(const GpuTimerPass pass) {
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled || !m_gpuTimerActive || m_activeGpuTimerPass != pass) {
        return;
    }

    glEndQuery(GL_TIME_ELAPSED);
    m_gpuTimerIssued[m_gpuTimerWriteIndex][static_cast<size_t>(pass)] = true;
    m_gpuTimerActive = false;
}

void RenderDebugService::resetCullingStats() {
    m_cullingStats = CullingFrameStats{};
}

void RenderDebugService::recordRegionCull(bool passed) {
    ++m_cullingStats.regionTests;
    if (passed) {
        ++m_cullingStats.regionPassed;
    }
}

void RenderDebugService::recordColumnCull(bool passed) {
    ++m_cullingStats.columnTests;
    if (passed) {
        ++m_cullingStats.columnPassed;
    }
}

void RenderDebugService::recordChunkCull(bool passed, FrustumPlane culledPlane) {
    ++m_cullingStats.chunkTests;
    if (passed) {
        ++m_cullingStats.chunkPassed;
    } else {
        ++m_cullingStats.chunkCulled;
        if (culledPlane != FrustumPlane::Count) {
            ++m_cullingStats.chunkCulledByPlane[static_cast<size_t>(culledPlane)];
        }
    }
}
