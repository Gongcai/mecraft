#include "RenderDebugService.h"
#include "../../Diagnostics.h"

#include <algorithm>
#include <cstdio>

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
    for (auto& frame : m_shadowTimestampQueries) {
        for (auto& cascade : frame) {
            glGenQueries(static_cast<GLsizei>(cascade.size()), cascade.data());
        }
    }
    for (auto& issued : m_gpuTimerIssued) {
        issued.fill(false);
    }
    for (auto& frame : m_shadowTimestampIssued) {
        for (auto& cascade : frame) {
            cascade.fill(false);
        }
    }
    m_shadowFrameIssued.fill(false);
    m_shadowFrameStats.supported = m_gpuFrameStats.supported;
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
    for (auto& frame : m_shadowTimestampQueries) {
        for (auto& cascade : frame) {
            glDeleteQueries(static_cast<GLsizei>(cascade.size()), cascade.data());
            cascade.fill(0);
        }
    }
    for (auto& issued : m_gpuTimerIssued) {
        issued.fill(false);
    }
    for (auto& frame : m_shadowTimestampIssued) {
        for (auto& cascade : frame) {
            cascade.fill(false);
        }
    }
    m_shadowFrameIssued.fill(false);
    m_gpuTimersInitialized = false;
    m_gpuFrameStats.valid = false;
    m_shadowFrameStats.valid = false;
    m_shadowFrameActive = false;
}

void RenderDebugService::beginFrame() {
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled) {
        m_gpuFrameStats.supported = m_gpuTimersInitialized && m_gpuFrameStats.supported;
        m_gpuFrameStats.valid = false;
        m_shadowFrameStats.supported = m_gpuFrameStats.supported;
        m_shadowFrameStats.valid = false;
        m_gpuTimerCanIssueThisFrame = false;
        return;
    }

    m_shadowFrameStats.supported = true;

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
            m_gpuFrameStats.ssgiMs = readMs(GpuTimerPass::Ssgi);
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

    if (m_shadowFrameIssued[readIndex]) {
        bool allAvailable = true;
        const ShadowFrameStats& pendingStats = m_shadowFrameSlots[readIndex];
        const int cascadeCount = std::min(static_cast<int>(SHADOW_TIMER_CASCADE_COUNT),
                                          std::max(0, pendingStats.cascadeCount));
        for (int cascade = 0; cascade < cascadeCount && allAvailable; ++cascade) {
            for (size_t point = 0; point < SHADOW_TIMER_POINT_COUNT; ++point) {
                if (!m_shadowTimestampIssued[readIndex][cascade][point]) {
                    continue;
                }
                GLint available = GL_FALSE;
                glGetQueryObjectiv(m_shadowTimestampQueries[readIndex][cascade][point],
                                    GL_QUERY_RESULT_AVAILABLE, &available);
                if (available == GL_FALSE) {
                    allAvailable = false;
                    break;
                }
            }
        }

        if (allAvailable) {
            ShadowFrameStats stats = pendingStats;
            stats.supported = true;
            stats.valid = true;
            stats.gpuTotalMs = 0.0;
            for (int cascade = 0; cascade < cascadeCount; ++cascade) {
                auto readTimestamp = [&](ShadowTimestampPoint point) {
                    const size_t pointIndex = static_cast<size_t>(point);
                    if (!m_shadowTimestampIssued[readIndex][cascade][pointIndex]) {
                        return GLuint64{0};
                    }
                    GLuint64 timestamp = 0;
                    glGetQueryObjectui64v(m_shadowTimestampQueries[readIndex][cascade][pointIndex],
                                          GL_QUERY_RESULT, &timestamp);
                    return timestamp;
                };
                const GLuint64 start = readTimestamp(ShadowTimestampPoint::Start);
                const GLuint64 opaqueEnd = readTimestamp(ShadowTimestampPoint::OpaqueEnd);
                const GLuint64 end = readTimestamp(ShadowTimestampPoint::End);
                ShadowCascadeStats& cascadeStats = stats.cascades[static_cast<size_t>(cascade)];
                if (start > 0 && opaqueEnd >= start) {
                    cascadeStats.gpuOpaqueMs = static_cast<double>(opaqueEnd - start) / 1000000.0;
                }
                if (end > 0 && end >= opaqueEnd) {
                    cascadeStats.gpuTransparentMs = static_cast<double>(end - opaqueEnd) / 1000000.0;
                }
                if (end > 0 && end >= start) {
                    cascadeStats.gpuTotalMs = static_cast<double>(end - start) / 1000000.0;
                }
                stats.gpuTotalMs += cascadeStats.gpuTotalMs;
            }
            m_shadowFrameStats = stats;
            // Disabled verbose shadow stats logging
            // if (++m_shadowStatsPublishCount % 120 == 0) {
            //     MECRAFT_LOG_PRINTF("[shadow:csm:gpu] total=%.3fms res=%d submitted=%d culled=%d maxDist=%.1f cascades=%d\n",
            //                        stats.gpuTotalMs,
            //                        stats.shadowResolution,
            //                        stats.submitted,
            //                        stats.culled,
            //                        stats.maxCasterDistance,
            //                        stats.cascadeCount);
            //     for (int cascade = 0; cascade < cascadeCount; ++cascade) {
            //         const ShadowCascadeStats& cascadeStats = stats.cascades[static_cast<size_t>(cascade)];
            //         MECRAFT_LOG_PRINTF("[shadow:csm:gpu:c%d] total=%.3fms opaque=%.3fms transparent=%.3fms split=%.1f-%.1f texel=%.4f cmds(o=%zu c=%zu t=%zu) verts(o=%llu c=%llu t=%llu)\n",
            //                            cascade,
            //                            cascadeStats.gpuTotalMs,
            //                            cascadeStats.gpuOpaqueMs,
            //                            cascadeStats.gpuTransparentMs,
            //                            cascadeStats.splitNear,
            //                            cascadeStats.splitFar,
            //                            cascadeStats.texelWorldSize,
            //                            cascadeStats.opaqueCommands,
            //                            cascadeStats.cutoutCommands,
            //                            cascadeStats.transparentCommands,
            //                            static_cast<unsigned long long>(cascadeStats.opaqueVertices),
            //                            static_cast<unsigned long long>(cascadeStats.cutoutVertices),
            //                            static_cast<unsigned long long>(cascadeStats.transparentVertices));
            //     }
            // }
            for (auto& cascade : m_shadowTimestampIssued[readIndex]) {
                cascade.fill(false);
            }
            m_shadowFrameIssued[readIndex] = false;
        }
    }

    bool slotStillPending = false;
    for (const bool issued : m_gpuTimerIssued[readIndex]) {
        if (issued) {
            slotStillPending = true;
            break;
        }
    }
    if (m_shadowFrameIssued[readIndex]) {
        slotStillPending = true;
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
    for (auto& cascade : m_shadowTimestampIssued[m_gpuTimerWriteIndex]) {
        cascade.fill(false);
    }
    m_shadowFrameSlots[m_gpuTimerWriteIndex] = ShadowFrameStats{};
    m_shadowFrameIssued[m_gpuTimerWriteIndex] = false;
    m_shadowFrameActive = false;
}

bool RenderDebugService::beginGpuTimer(const GpuTimerPass pass) {
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled || !m_gpuTimerCanIssueThisFrame || m_gpuTimerActive) {
        return false;
    }

    const size_t passIndex = static_cast<size_t>(pass);
    if (m_gpuTimerIssued[m_gpuTimerWriteIndex][passIndex]) {
        return false;
    }
    glBeginQuery(GL_TIME_ELAPSED, m_gpuTimerQueries[m_gpuTimerWriteIndex][passIndex]);
    m_gpuTimerActive = true;
    m_activeGpuTimerPass = pass;
    return true;
}

void RenderDebugService::endGpuTimer(const GpuTimerPass pass) {
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled || !m_gpuTimerActive || m_activeGpuTimerPass != pass) {
        return;
    }

    glEndQuery(GL_TIME_ELAPSED);
    m_gpuTimerIssued[m_gpuTimerWriteIndex][static_cast<size_t>(pass)] = true;
    m_gpuTimerActive = false;
}

bool RenderDebugService::beginShadowFrame(const int cascadeCount, const int shadowResolution) {
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled || !m_gpuTimerCanIssueThisFrame || m_shadowFrameActive) {
        return false;
    }

    ShadowFrameStats stats;
    stats.supported = true;
    stats.valid = false;
    stats.cascadeCount = std::min(static_cast<int>(SHADOW_TIMER_CASCADE_COUNT), std::max(0, cascadeCount));
    stats.shadowResolution = shadowResolution;
    m_shadowFrameSlots[m_gpuTimerWriteIndex] = stats;
    for (auto& cascade : m_shadowTimestampIssued[m_gpuTimerWriteIndex]) {
        cascade.fill(false);
    }
    m_shadowFrameIssued[m_gpuTimerWriteIndex] = true;
    m_shadowFrameActive = true;
    return true;
}

void RenderDebugService::recordShadowCascadeStats(const int cascadeIndex,
                                                  const ShadowCascadeStats& stats) {
    if (!m_shadowFrameActive || cascadeIndex < 0 ||
        cascadeIndex >= static_cast<int>(SHADOW_TIMER_CASCADE_COUNT)) {
        return;
    }

    ShadowFrameStats& frame = m_shadowFrameSlots[m_gpuTimerWriteIndex];
    frame.cascades[static_cast<size_t>(cascadeIndex)] = stats;
}

void RenderDebugService::recordShadowFrameTotals(const int submitted, const int culled,
                                                 const float maxCasterDistance) {
    if (!m_shadowFrameActive) {
        return;
    }

    ShadowFrameStats& frame = m_shadowFrameSlots[m_gpuTimerWriteIndex];
    frame.submitted = submitted;
    frame.culled = culled;
    frame.maxCasterDistance = maxCasterDistance;
}

void RenderDebugService::markShadowTimestamp(const int cascadeIndex,
                                             const ShadowTimestampPoint point) {
    if (!m_shadowFrameActive || cascadeIndex < 0 ||
        cascadeIndex >= static_cast<int>(SHADOW_TIMER_CASCADE_COUNT)) {
        return;
    }
    const size_t pointIndex = static_cast<size_t>(point);
    if (pointIndex >= SHADOW_TIMER_POINT_COUNT) {
        return;
    }

    glQueryCounter(m_shadowTimestampQueries[m_gpuTimerWriteIndex][static_cast<size_t>(cascadeIndex)][pointIndex],
                   GL_TIMESTAMP);
    m_shadowTimestampIssued[m_gpuTimerWriteIndex][static_cast<size_t>(cascadeIndex)][pointIndex] = true;
}

void RenderDebugService::endShadowFrame() {
    m_shadowFrameActive = false;
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
