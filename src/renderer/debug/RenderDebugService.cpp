#include "RenderDebugService.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace {

double gpuTimerPassMilliseconds(const GpuFrameStats& stats,
                                const GpuTimerPass pass) {
    switch (pass) {
        case GpuTimerPass::GBuffer: return stats.gbufferMs;
        case GpuTimerPass::Shadow: return stats.shadowMs;
        case GpuTimerPass::Ssao: return stats.ssaoMs;
        case GpuTimerPass::Ssgi: return stats.ssgiMs;
        case GpuTimerPass::Lighting: return stats.lightingMs;
        case GpuTimerPass::Transparent: return stats.transparentMs;
        case GpuTimerPass::Volumetric: return stats.volumetricMs;
        case GpuTimerPass::Reflection: return stats.reflectionMs;
        case GpuTimerPass::Cloud: return stats.cloudMs;
        case GpuTimerPass::Water: return stats.waterMs;
        case GpuTimerPass::Post: return stats.postMs;
        case GpuTimerPass::Count: break;
    }
    std::abort();
}

GpuTimingPercentiles calculatePercentiles(
    const std::array<double, GpuTimingHistory::kCapacity>& samples,
    const size_t sampleCount) {
    if (sampleCount == 0u) {
        return {};
    }
    std::array<double, GpuTimingHistory::kCapacity> sortedSamples{};
    std::copy_n(samples.begin(), sampleCount, sortedSamples.begin());
    std::sort(sortedSamples.begin(), sortedSamples.begin() + sampleCount);
    const auto nearestRank = [&](const size_t percentile) {
        const size_t rank = (sampleCount * percentile + 99u) / 100u;
        return sortedSamples[rank - 1u];
    };
    return {
        nearestRank(50u),
        nearestRank(95u),
        nearestRank(99u)
    };
}

} // namespace

const char* gpuTimerPassName(const GpuTimerPass pass) {
    switch (pass) {
        case GpuTimerPass::GBuffer: return "GBuffer";
        case GpuTimerPass::Shadow: return "Shadow";
        case GpuTimerPass::Ssao: return "SSAO";
        case GpuTimerPass::Ssgi: return "SSGI";
        case GpuTimerPass::Lighting: return "Lighting";
        case GpuTimerPass::Transparent: return "Transparent";
        case GpuTimerPass::Volumetric: return "Volumetric";
        case GpuTimerPass::Reflection: return "Reflection";
        case GpuTimerPass::Cloud: return "Cloud";
        case GpuTimerPass::Water: return "Water";
        case GpuTimerPass::Post: return "Post";
        case GpuTimerPass::Count: break;
    }
    std::abort();
}

void GpuTimingHistory::reset() {
    m_nextSample = 0u;
    m_sampleCount = 0u;
    m_observedSampleCount = 0u;
    m_lastSequence = 0u;
}

bool GpuTimingHistory::record(const GpuFrameStats& stats) {
    if (!stats.supported || !stats.valid || stats.sequence == 0u ||
        stats.sequence <= m_lastSequence) {
        return false;
    }

    std::array<double, static_cast<size_t>(GpuTimerPass::Count)> passValues{};
    double totalMs = 0.0;
    for (size_t passIndex = 0u;
         passIndex < static_cast<size_t>(GpuTimerPass::Count);
         ++passIndex) {
        const auto pass = static_cast<GpuTimerPass>(passIndex);
        const double milliseconds = gpuTimerPassMilliseconds(stats, pass);
        if (!std::isfinite(milliseconds) || milliseconds < 0.0) {
            return false;
        }
        passValues[passIndex] = milliseconds;
        totalMs += milliseconds;
    }
    for (size_t passIndex = 0u; passIndex < passValues.size(); ++passIndex) {
        m_passSamples[passIndex][m_nextSample] = passValues[passIndex];
    }
    m_totalSamples[m_nextSample] = totalMs;
    m_nextSample = (m_nextSample + 1u) % kCapacity;
    m_sampleCount = std::min(m_sampleCount + 1u, kCapacity);
    ++m_observedSampleCount;
    m_lastSequence = stats.sequence;
    return true;
}

GpuTimingWindowStats GpuTimingHistory::snapshot() const {
    GpuTimingWindowStats stats;
    stats.valid = m_sampleCount > 0u;
    stats.sampleCount = m_sampleCount;
    stats.capacity = kCapacity;
    stats.observedSampleCount = m_observedSampleCount;
    stats.totalTrackedGpuMs = calculatePercentiles(m_totalSamples, m_sampleCount);
    for (size_t passIndex = 0u;
         passIndex < static_cast<size_t>(GpuTimerPass::Count);
         ++passIndex) {
        GpuTimerPassWindowStats& passStats = stats.passes[passIndex];
        passStats.pass = static_cast<GpuTimerPass>(passIndex);
        passStats.gpuMs = calculatePercentiles(
            m_passSamples[passIndex], m_sampleCount);
    }
    return stats;
}

uint32_t RenderDebugService::gpuTimerQueryIndex(const size_t frameIndex,
                                                const size_t passIndex,
                                                const size_t segmentIndex,
                                                const size_t pointIndex) {
    return static_cast<uint32_t>(
        (((frameIndex * GPU_TIMER_PASS_COUNT + passIndex) *
              GPU_TIMER_MAX_SEGMENTS_PER_PASS +
          segmentIndex) *
             GPU_TIMER_POINTS_PER_SEGMENT) +
        pointIndex);
}

uint32_t RenderDebugService::shadowQueryIndex(const size_t frameIndex,
                                              const size_t cascadeIndex,
                                              const size_t pointIndex) {
    return static_cast<uint32_t>(
        (frameIndex * SHADOW_TIMER_CASCADE_COUNT + cascadeIndex) *
            SHADOW_TIMER_POINT_COUNT +
        pointIndex);
}

void RenderDebugService::init(RhiDevice& rhiDevice) {
    if (m_gpuTimersInitialized) {
        return;
    }

    RhiQueryPoolDesc gpuTimerPoolDesc;
    gpuTimerPoolDesc.debugName = "RenderDebug.PassTimestamps";
    gpuTimerPoolDesc.queryCount = GPU_TIMER_QUERY_COUNT;
    m_gpuTimerQueryPool = rhiDevice.createQueryPool(gpuTimerPoolDesc);
    if (!m_gpuTimerQueryPool.isValid()) {
        std::abort();
    }

    RhiQueryPoolDesc shadowPoolDesc;
    shadowPoolDesc.debugName = "RenderDebug.ShadowTimestamps";
    shadowPoolDesc.queryCount = SHADOW_TIMER_QUERY_COUNT;
    m_shadowTimestampQueryPool = rhiDevice.createQueryPool(shadowPoolDesc);
    if (!m_shadowTimestampQueryPool.isValid()) {
        std::abort();
    }
    m_rhiDevice = &rhiDevice;
    for (auto& counts : m_gpuTimerAllocatedSegmentCounts) {
        counts.fill(0u);
    }
    for (auto& frame : m_gpuTimerSegmentStates) {
        for (auto& pass : frame) {
            pass.fill(GpuTimerSegmentState::Unused);
        }
    }
    for (auto& frame : m_shadowTimestampIssued) {
        for (auto& cascade : frame) {
            cascade.fill(false);
        }
    }
    m_shadowFrameIssued.fill(false);
    m_gpuFrameStats = {};
    m_gpuFrameStats.supported = true;
    m_gpuFrameSequence = 0u;
    m_gpuTimingHistory.reset();
    m_shadowFrameStats.supported = true;
    m_gpuTimersInitialized = true;
}

void RenderDebugService::shutdown() {
    if (!m_gpuTimersInitialized) {
        return;
    }
    if (m_rhiDevice != nullptr && m_gpuTimerQueryPool.isValid()) {
        m_rhiDevice->destroyQueryPool(m_gpuTimerQueryPool);
    }
    m_gpuTimerQueryPool = {};
    if (m_rhiDevice != nullptr && m_shadowTimestampQueryPool.isValid()) {
        m_rhiDevice->destroyQueryPool(m_shadowTimestampQueryPool);
    }
    m_shadowTimestampQueryPool = {};
    m_rhiDevice = nullptr;
    for (auto& counts : m_gpuTimerAllocatedSegmentCounts) {
        counts.fill(0u);
    }
    for (auto& frame : m_gpuTimerSegmentStates) {
        for (auto& pass : frame) {
            pass.fill(GpuTimerSegmentState::Unused);
        }
    }
    for (auto& frame : m_shadowTimestampIssued) {
        for (auto& cascade : frame) {
            cascade.fill(false);
        }
    }
    m_shadowFrameIssued.fill(false);
    m_gpuTimersInitialized = false;
    m_gpuFrameStats = {};
    m_gpuFrameSequence = 0u;
    m_gpuTimingHistory.reset();
    m_shadowFrameStats.valid = false;
    m_shadowFrameActive = false;
}

void RenderDebugService::beginFrame(RhiCommandList& commandList) {
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
    bool anyPassIssued = false;
    for (const auto& pass : m_gpuTimerSegmentStates[readIndex]) {
        for (const GpuTimerSegmentState state : pass) {
            anyPassIssued = anyPassIssued || state == GpuTimerSegmentState::Issued;
        }
    }

    if (anyPassIssued) {
        bool allAvailable = true;
        for (size_t pass = 0; pass < GPU_TIMER_PASS_COUNT; ++pass) {
            for (size_t segment = 0; segment < GPU_TIMER_MAX_SEGMENTS_PER_PASS; ++segment) {
                if (m_gpuTimerSegmentStates[readIndex][pass][segment] !=
                    GpuTimerSegmentState::Issued) {
                    continue;
                }
                const uint32_t firstQuery = gpuTimerQueryIndex(readIndex, pass, segment, 0u);
                if (!m_rhiDevice->areQueryResultsAvailable(
                        m_gpuTimerQueryPool, firstQuery,
                        static_cast<uint32_t>(GPU_TIMER_POINTS_PER_SEGMENT))) {
                    allAvailable = false;
                    break;
                }
            }
            if (!allAvailable) {
                break;
            }
        }

        if (allAvailable) {
            const auto readMs = [&](const GpuTimerPass pass) {
                const size_t passIndex = static_cast<size_t>(pass);
                uint64_t elapsedNs = 0u;
                for (size_t segment = 0; segment < GPU_TIMER_MAX_SEGMENTS_PER_PASS; ++segment) {
                    if (m_gpuTimerSegmentStates[readIndex][passIndex][segment] !=
                        GpuTimerSegmentState::Issued) {
                        continue;
                    }
                    std::array<uint64_t, GPU_TIMER_POINTS_PER_SEGMENT> timestamps{};
                    const uint32_t firstQuery =
                        gpuTimerQueryIndex(readIndex, passIndex, segment, 0u);
                    if (!m_rhiDevice->getQueryResults(
                            m_gpuTimerQueryPool, firstQuery,
                            static_cast<uint32_t>(GPU_TIMER_POINTS_PER_SEGMENT),
                            timestamps.data())) {
                        std::abort();
                    }
                    const uint64_t start = timestamps[0];
                    const uint64_t end = timestamps[1];
                    if (end < start) {
                        std::abort();
                    }
                    elapsedNs += end - start;
                }
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
            m_gpuFrameStats.sequence = ++m_gpuFrameSequence;
            if (!m_gpuTimingHistory.record(m_gpuFrameStats)) {
                std::abort();
            }
            m_gpuTimerAllocatedSegmentCounts[readIndex].fill(0u);
            for (auto& pass : m_gpuTimerSegmentStates[readIndex]) {
                pass.fill(GpuTimerSegmentState::Unused);
            }
        }
    }

    if (m_shadowFrameIssued[readIndex]) {
        const ShadowFrameStats& pendingStats = m_shadowFrameSlots[readIndex];
        const int cascadeCount = std::min(static_cast<int>(SHADOW_TIMER_CASCADE_COUNT),
                                          std::max(0, pendingStats.cascadeCount));
        const uint32_t firstQuery = shadowQueryIndex(readIndex, 0u, 0u);
        const uint32_t queryCount = static_cast<uint32_t>(cascadeCount) *
                                    static_cast<uint32_t>(SHADOW_TIMER_POINT_COUNT);
        const bool allAvailable = queryCount > 0u &&
            m_rhiDevice->areQueryResultsAvailable(
                m_shadowTimestampQueryPool, firstQuery, queryCount);

        if (allAvailable) {
            std::array<uint64_t, SHADOW_TIMER_CASCADE_COUNT * SHADOW_TIMER_POINT_COUNT>
                timestamps{};
            if (!m_rhiDevice->getQueryResults(
                    m_shadowTimestampQueryPool, firstQuery, queryCount, timestamps.data())) {
                std::abort();
            }
            ShadowFrameStats stats = pendingStats;
            stats.supported = true;
            stats.valid = true;
            stats.gpuTotalMs = 0.0;
            for (int cascade = 0; cascade < cascadeCount; ++cascade) {
                const auto readTimestamp = [&](const ShadowTimestampPoint point) {
                    const size_t localIndex = static_cast<size_t>(cascade) *
                                                  SHADOW_TIMER_POINT_COUNT +
                                              static_cast<size_t>(point);
                    return timestamps[localIndex];
                };
                const uint64_t start = readTimestamp(ShadowTimestampPoint::Start);
                const uint64_t opaqueEnd = readTimestamp(ShadowTimestampPoint::OpaqueEnd);
                const uint64_t end = readTimestamp(ShadowTimestampPoint::End);
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
    for (const auto& pass : m_gpuTimerSegmentStates[readIndex]) {
        for (const GpuTimerSegmentState state : pass) {
            if (state == GpuTimerSegmentState::Recording) {
                std::abort();
            }
            if (state == GpuTimerSegmentState::Issued) {
                slotStillPending = true;
            }
        }
    }
    if (m_shadowFrameIssued[readIndex]) {
        slotStillPending = true;
    }
    m_gpuTimerCanIssueThisFrame = !slotStillPending;
    if (!m_gpuTimerCanIssueThisFrame) {
        return;
    }

    for (const auto& pass : m_gpuTimerSegmentStates[m_gpuTimerWriteIndex]) {
        for (const GpuTimerSegmentState state : pass) {
            if (state == GpuTimerSegmentState::Recording) {
                std::abort();
            }
        }
    }
    m_gpuTimerWriteIndex = (m_gpuTimerWriteIndex + 1) % GPU_TIMER_RING_SIZE;
    m_gpuTimerAllocatedSegmentCounts[m_gpuTimerWriteIndex].fill(0u);
    for (auto& pass : m_gpuTimerSegmentStates[m_gpuTimerWriteIndex]) {
        pass.fill(GpuTimerSegmentState::Unused);
    }
    for (auto& cascade : m_shadowTimestampIssued[m_gpuTimerWriteIndex]) {
        cascade.fill(false);
    }
    m_shadowFrameSlots[m_gpuTimerWriteIndex] = ShadowFrameStats{};
    m_shadowFrameIssued[m_gpuTimerWriteIndex] = false;
    m_shadowFrameActive = false;

    const uint32_t timerSlotFirstQuery = gpuTimerQueryIndex(
        m_gpuTimerWriteIndex, 0u, 0u, 0u);
    const uint32_t timerSlotQueryCount = static_cast<uint32_t>(
        GPU_TIMER_PASS_COUNT * GPU_TIMER_MAX_SEGMENTS_PER_PASS *
        GPU_TIMER_POINTS_PER_SEGMENT);
    commandList.resetQueryPool(
        m_gpuTimerQueryPool, timerSlotFirstQuery, timerSlotQueryCount);

    const uint32_t shadowSlotFirstQuery = shadowQueryIndex(
        m_gpuTimerWriteIndex, 0u, 0u);
    const uint32_t shadowSlotQueryCount = static_cast<uint32_t>(
        SHADOW_TIMER_CASCADE_COUNT * SHADOW_TIMER_POINT_COUNT);
    commandList.resetQueryPool(
        m_shadowTimestampQueryPool, shadowSlotFirstQuery, shadowSlotQueryCount);
}

GpuTimerSegmentToken RenderDebugService::beginGpuTimer(RhiCommandList& commandList,
                                                       const GpuTimerPass pass) {
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled || !m_gpuTimerCanIssueThisFrame) {
        return {};
    }

    const std::lock_guard<std::mutex> timerLock(m_gpuTimerMutex);
    const size_t passIndex = static_cast<size_t>(pass);
    if (passIndex >= GPU_TIMER_PASS_COUNT) {
        std::abort();
    }
    const size_t segmentIndex =
        m_gpuTimerAllocatedSegmentCounts[m_gpuTimerWriteIndex][passIndex];
    if (segmentIndex >= GPU_TIMER_MAX_SEGMENTS_PER_PASS) {
        std::abort();
    }
    m_gpuTimerAllocatedSegmentCounts[m_gpuTimerWriteIndex][passIndex] =
        static_cast<uint8_t>(segmentIndex + 1u);
    const uint32_t firstQuery = gpuTimerQueryIndex(
        m_gpuTimerWriteIndex, passIndex, segmentIndex, 0u);
    commandList.writeTimestamp(m_gpuTimerQueryPool, firstQuery);
    m_gpuTimerSegmentStates[m_gpuTimerWriteIndex][passIndex][segmentIndex] =
        GpuTimerSegmentState::Recording;
    return {
        pass,
        static_cast<uint8_t>(m_gpuTimerWriteIndex),
        static_cast<uint8_t>(segmentIndex),
        true
    };
}

void RenderDebugService::endGpuTimer(RhiCommandList& commandList,
                                     const GpuTimerSegmentToken token) {
    if (!token.valid) {
        return;
    }

    const std::lock_guard<std::mutex> timerLock(m_gpuTimerMutex);
    const size_t passIndex = static_cast<size_t>(token.pass);
    if (!m_gpuTimersInitialized || token.frameIndex != m_gpuTimerWriteIndex ||
        passIndex >= GPU_TIMER_PASS_COUNT ||
        token.segmentIndex >= GPU_TIMER_MAX_SEGMENTS_PER_PASS ||
        m_gpuTimerSegmentStates[token.frameIndex][passIndex][token.segmentIndex] !=
            GpuTimerSegmentState::Recording) {
        std::abort();
    }
    const uint32_t endQuery = gpuTimerQueryIndex(
        token.frameIndex, passIndex, token.segmentIndex, 1u);
    commandList.writeTimestamp(m_gpuTimerQueryPool, endQuery);
    m_gpuTimerSegmentStates[token.frameIndex][passIndex][token.segmentIndex] =
        GpuTimerSegmentState::Issued;
}

void RenderDebugService::cancelGpuTimer(const GpuTimerSegmentToken token) {
    if (!token.valid) {
        return;
    }

    const std::lock_guard<std::mutex> timerLock(m_gpuTimerMutex);
    const size_t passIndex = static_cast<size_t>(token.pass);
    if (!m_gpuTimersInitialized || token.frameIndex != m_gpuTimerWriteIndex ||
        passIndex >= GPU_TIMER_PASS_COUNT ||
        token.segmentIndex >= GPU_TIMER_MAX_SEGMENTS_PER_PASS ||
        m_gpuTimerSegmentStates[token.frameIndex][passIndex][token.segmentIndex] !=
            GpuTimerSegmentState::Recording) {
        std::abort();
    }
    m_gpuTimerSegmentStates[token.frameIndex][passIndex][token.segmentIndex] =
        GpuTimerSegmentState::Unused;
}

GpuTimerCheckpoint RenderDebugService::gpuTimerCheckpoint() const {
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled || !m_gpuTimerCanIssueThisFrame) {
        return {};
    }
    const std::lock_guard<std::mutex> timerLock(m_gpuTimerMutex);
    return {
        m_gpuTimerAllocatedSegmentCounts[m_gpuTimerWriteIndex],
        static_cast<uint8_t>(m_gpuTimerWriteIndex),
        true
    };
}

void RenderDebugService::cancelGpuTimersSince(const GpuTimerCheckpoint& checkpoint) {
    if (!checkpoint.valid) {
        return;
    }
    const std::lock_guard<std::mutex> timerLock(m_gpuTimerMutex);
    if (!m_gpuTimersInitialized || checkpoint.frameIndex != m_gpuTimerWriteIndex) {
        std::abort();
    }
    for (size_t passIndex = 0; passIndex < GPU_TIMER_PASS_COUNT; ++passIndex) {
        const size_t firstSegment = checkpoint.segmentCounts[passIndex];
        const size_t allocatedSegments =
            m_gpuTimerAllocatedSegmentCounts[m_gpuTimerWriteIndex][passIndex];
        if (firstSegment > allocatedSegments) {
            std::abort();
        }
        for (size_t segment = firstSegment; segment < allocatedSegments; ++segment) {
            GpuTimerSegmentState& state =
                m_gpuTimerSegmentStates[m_gpuTimerWriteIndex][passIndex][segment];
            state = GpuTimerSegmentState::Unused;
        }
        m_gpuTimerAllocatedSegmentCounts[m_gpuTimerWriteIndex][passIndex] =
            checkpoint.segmentCounts[passIndex];
    }
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

void RenderDebugService::markShadowTimestamp(RhiCommandList& commandList,
                                             const int cascadeIndex,
                                             const ShadowTimestampPoint point) {
    const std::lock_guard<std::mutex> timerLock(m_gpuTimerMutex);
    if (!m_shadowFrameActive || cascadeIndex < 0 ||
        cascadeIndex >= static_cast<int>(SHADOW_TIMER_CASCADE_COUNT)) {
        return;
    }
    const size_t pointIndex = static_cast<size_t>(point);
    if (pointIndex >= SHADOW_TIMER_POINT_COUNT) {
        return;
    }

    const uint32_t queryIndex = shadowQueryIndex(
        m_gpuTimerWriteIndex, static_cast<size_t>(cascadeIndex), pointIndex);
    commandList.writeTimestamp(m_shadowTimestampQueryPool, queryIndex);
    m_shadowTimestampIssued[m_gpuTimerWriteIndex][static_cast<size_t>(cascadeIndex)][pointIndex] = true;
}

void RenderDebugService::endShadowFrame() {
    if (!m_shadowFrameActive) {
        return;
    }

    const int cascadeCount = m_shadowFrameSlots[m_gpuTimerWriteIndex].cascadeCount;
    for (int cascade = 0; cascade < cascadeCount; ++cascade) {
        for (size_t point = 0; point < SHADOW_TIMER_POINT_COUNT; ++point) {
            if (!m_shadowTimestampIssued[m_gpuTimerWriteIndex]
                                        [static_cast<size_t>(cascade)][point]) {
                std::abort();
            }
        }
    }
    m_shadowFrameIssued[m_gpuTimerWriteIndex] = true;
    m_shadowFrameActive = false;
}

void RenderDebugService::cancelShadowFrame() {
    if (!m_shadowFrameActive) {
        return;
    }
    for (auto& cascade : m_shadowTimestampIssued[m_gpuTimerWriteIndex]) {
        cascade.fill(false);
    }
    m_shadowFrameSlots[m_gpuTimerWriteIndex] = ShadowFrameStats{};
    m_shadowFrameIssued[m_gpuTimerWriteIndex] = false;
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
