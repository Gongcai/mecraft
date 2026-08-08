#include "RenderDebugService.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace {

double gpuTimerPassMilliseconds(const GpuFrameStats& stats, const GpuTimerPass pass) {
    switch (pass) {
    case GpuTimerPass::GBuffer: return stats.gbufferMs;
    case GpuTimerPass::Shadow: return stats.shadowMs;
    case GpuTimerPass::Ssao: return stats.ssaoMs;
    case GpuTimerPass::Ssgi: return stats.ssgiMs;
    case GpuTimerPass::Rtgi: return stats.rtgiMs;
    case GpuTimerPass::Nrd: return stats.nrdMs;
    case GpuTimerPass::RtgiTrace: return stats.rtgiTraceMs;
    case GpuTimerPass::RtgiSignalPack: return stats.rtgiSignalPackMs;
    case GpuTimerPass::NrdGuidePrep: return stats.nrdGuidePrepMs;
    case GpuTimerPass::NrdDispatch: return stats.nrdDispatchMs;
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

bool gpuTimerPassIncludedInTrackedTotal(const GpuTimerPass pass) {
    return pass != GpuTimerPass::RtgiTrace && pass != GpuTimerPass::RtgiSignalPack &&
           pass != GpuTimerPass::NrdGuidePrep && pass != GpuTimerPass::NrdDispatch;
}

GpuTimingPercentiles calculatePercentiles(const std::array<double, GpuTimingHistory::kCapacity>& samples,
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
    return {nearestRank(50u), nearestRank(95u), nearestRank(99u)};
}

double renderGraphCpuTimingMilliseconds(const RenderGraphFrameStats& stats, const RenderGraphCpuTimingStage stage) {
    switch (stage) {
    case RenderGraphCpuTimingStage::Build: return stats.cpuBuildMs;
    case RenderGraphCpuTimingStage::Compile: return stats.cpuCompileMs;
    case RenderGraphCpuTimingStage::Execute: return stats.cpuExecuteMs;
    case RenderGraphCpuTimingStage::Record: return stats.cpuRecordMs;
    case RenderGraphCpuTimingStage::Submit: return stats.cpuSubmitMs;
    case RenderGraphCpuTimingStage::ShadowPrep: return stats.cpuShadowPrepMs;
    case RenderGraphCpuTimingStage::Context: return stats.cpuContextMs;
    case RenderGraphCpuTimingStage::TerrainPrep: return stats.cpuTerrainPrepMs;
    case RenderGraphCpuTimingStage::Count: break;
    }
    std::abort();
}

double renderGraphGpuTimingMilliseconds(const RenderGraphFrameStats& stats, const RenderGraphGpuTimingMetric metric) {
    switch (metric) {
    case RenderGraphGpuTimingMetric::Total: return stats.gpuTotalMs;
    case RenderGraphGpuTimingMetric::Span: return stats.gpuSpanMs;
    case RenderGraphGpuTimingMetric::Idle: return stats.gpuIdleMs;
    case RenderGraphGpuTimingMetric::Overlap: return std::max(0.0, stats.gpuTotalMs - stats.gpuSpanMs);
    case RenderGraphGpuTimingMetric::Count: break;
    }
    std::abort();
}

} // namespace

const char* gpuTimerPassName(const GpuTimerPass pass) {
    switch (pass) {
    case GpuTimerPass::GBuffer: return "GBuffer";
    case GpuTimerPass::Shadow: return "Shadow";
    case GpuTimerPass::Ssao: return "SSAO";
    case GpuTimerPass::Ssgi: return "SSGI";
    case GpuTimerPass::Rtgi: return "RTGI";
    case GpuTimerPass::Nrd: return "NRD";
    case GpuTimerPass::Lighting: return "Lighting";
    case GpuTimerPass::Transparent: return "Transparent";
    case GpuTimerPass::Volumetric: return "Volumetric";
    case GpuTimerPass::Reflection: return "Reflection";
    case GpuTimerPass::Cloud: return "Cloud";
    case GpuTimerPass::Water: return "Water";
    case GpuTimerPass::Post: return "Post";
    case GpuTimerPass::RtgiTrace: return "RTGI.Trace";
    case GpuTimerPass::RtgiSignalPack: return "RTGI.SignalPack";
    case GpuTimerPass::NrdGuidePrep: return "NRD.GuidePrep";
    case GpuTimerPass::NrdDispatch: return "NRD.Dispatch";
    case GpuTimerPass::Count: break;
    }
    std::abort();
}

const char* renderGraphCpuTimingStageName(const RenderGraphCpuTimingStage stage) {
    switch (stage) {
    case RenderGraphCpuTimingStage::Build: return "Build";
    case RenderGraphCpuTimingStage::Compile: return "Compile";
    case RenderGraphCpuTimingStage::Execute: return "Execute";
    case RenderGraphCpuTimingStage::Record: return "Record";
    case RenderGraphCpuTimingStage::Submit: return "Submit";
    case RenderGraphCpuTimingStage::ShadowPrep: return "ShadowPrep";
    case RenderGraphCpuTimingStage::Context: return "Context";
    case RenderGraphCpuTimingStage::TerrainPrep: return "TerrainPrep";
    case RenderGraphCpuTimingStage::Count: break;
    }
    std::abort();
}

const char* renderGraphGpuTimingMetricName(const RenderGraphGpuTimingMetric metric) {
    switch (metric) {
    case RenderGraphGpuTimingMetric::Total: return "Total";
    case RenderGraphGpuTimingMetric::Span: return "Span";
    case RenderGraphGpuTimingMetric::Idle: return "Idle";
    case RenderGraphGpuTimingMetric::Overlap: return "Overlap";
    case RenderGraphGpuTimingMetric::Count: break;
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
    if (!stats.supported || !stats.valid || stats.sequence == 0u || stats.sequence <= m_lastSequence) {
        return false;
    }

    std::array<double, static_cast<size_t>(GpuTimerPass::Count)> passValues{};
    double totalMs = 0.0;
    for (size_t passIndex = 0u; passIndex < static_cast<size_t>(GpuTimerPass::Count); ++passIndex) {
        const auto pass = static_cast<GpuTimerPass>(passIndex);
        const double milliseconds = gpuTimerPassMilliseconds(stats, pass);
        if (!std::isfinite(milliseconds) || milliseconds < 0.0) {
            return false;
        }
        passValues[passIndex] = milliseconds;
        if (gpuTimerPassIncludedInTrackedTotal(pass)) {
            totalMs += milliseconds;
        }
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
    for (size_t passIndex = 0u; passIndex < static_cast<size_t>(GpuTimerPass::Count); ++passIndex) {
        GpuTimerPassWindowStats& passStats = stats.passes[passIndex];
        passStats.pass = static_cast<GpuTimerPass>(passIndex);
        passStats.gpuMs = calculatePercentiles(m_passSamples[passIndex], m_sampleCount);
    }
    return stats;
}

void RenderGraphTimingHistory::reset() {
    for (auto& samples : m_cpuSamples) {
        samples.fill(0.0);
    }
    for (auto& samples : m_gpuSamples) {
        samples.fill(0.0);
    }
    m_completeGpuFrameSamples.fill(0.0);
    m_cpuNextSample = 0u;
    m_cpuSampleCount = 0u;
    m_gpuNextSample = 0u;
    m_gpuSampleCount = 0u;
    m_observedGpuSampleCount = 0u;
    m_lastGpuExecution = 0u;
    m_completeGpuFrameNextSample = 0u;
    m_completeGpuFrameSampleCount = 0u;
    m_observedCompleteGpuFrameSampleCount = 0u;
    m_lastCompleteGpuFrameSequence = 0u;
    m_latest = {};
}

bool RenderGraphTimingHistory::record(const RenderGraphFrameStats& stats) {
    if (!stats.valid) {
        return false;
    }

    std::array<double, static_cast<size_t>(RenderGraphCpuTimingStage::Count)> cpuValues{};
    for (size_t index = 0u; index < static_cast<size_t>(RenderGraphCpuTimingStage::Count); ++index) {
        const double milliseconds = renderGraphCpuTimingMilliseconds(
            stats, static_cast<RenderGraphCpuTimingStage>(index));
        if (!std::isfinite(milliseconds) || milliseconds < 0.0) {
            return false;
        }
        cpuValues[index] = milliseconds;
    }

    const bool gpuCandidate = stats.execution != 0u && !stats.passes.empty();
    const bool acceptGpu = gpuCandidate && stats.execution > m_lastGpuExecution;
    std::array<double, static_cast<size_t>(RenderGraphGpuTimingMetric::Count)> gpuValues{};
    if (acceptGpu) {
        for (size_t index = 0u; index < static_cast<size_t>(RenderGraphGpuTimingMetric::Count); ++index) {
            const double milliseconds = renderGraphGpuTimingMilliseconds(
                stats, static_cast<RenderGraphGpuTimingMetric>(index));
            if (!std::isfinite(milliseconds) || milliseconds < 0.0) {
                return false;
            }
            gpuValues[index] = milliseconds;
        }
    }

    const bool completeGpuFrameCandidate = stats.completeGpuFrame.supported && stats.completeGpuFrame.valid &&
                                           stats.completeGpuFrame.sequence != 0u;
    const bool acceptCompleteGpuFrame = completeGpuFrameCandidate &&
                                       stats.completeGpuFrame.sequence > m_lastCompleteGpuFrameSequence;
    if (completeGpuFrameCandidate &&
        (!std::isfinite(stats.completeGpuFrame.spanMs) || stats.completeGpuFrame.spanMs < 0.0)) {
        return false;
    }

    for (size_t index = 0u; index < cpuValues.size(); ++index) {
        m_cpuSamples[index][m_cpuNextSample] = cpuValues[index];
    }
    m_cpuNextSample = (m_cpuNextSample + 1u) % kCapacity;
    m_cpuSampleCount = std::min(m_cpuSampleCount + 1u, kCapacity);

    m_latest.valid = true;
    m_latest.execution = stats.execution;
    m_latest.passCount = stats.passCount;
    m_latest.batchCount = stats.batchCount;
    m_latest.submitCount = stats.submitCount;
    m_latest.workerRecordedBatches = stats.workerRecordedBatches;
    m_latest.gpuValid = gpuCandidate;
    m_latest.completeGpuFrameValid = completeGpuFrameCandidate;
    m_latest.completeGpuFrameSequence = stats.completeGpuFrame.sequence;
    m_latest.completeGpuFrameSpanMs = stats.completeGpuFrame.spanMs;

    if (acceptGpu) {
        for (size_t index = 0u; index < gpuValues.size(); ++index) {
            m_gpuSamples[index][m_gpuNextSample] = gpuValues[index];
        }
        m_gpuNextSample = (m_gpuNextSample + 1u) % kCapacity;
        m_gpuSampleCount = std::min(m_gpuSampleCount + 1u, kCapacity);
        ++m_observedGpuSampleCount;
        m_lastGpuExecution = stats.execution;
    }
    if (acceptCompleteGpuFrame) {
        m_completeGpuFrameSamples[m_completeGpuFrameNextSample] = stats.completeGpuFrame.spanMs;
        m_completeGpuFrameNextSample = (m_completeGpuFrameNextSample + 1u) % kCapacity;
        m_completeGpuFrameSampleCount = std::min(m_completeGpuFrameSampleCount + 1u, kCapacity);
        ++m_observedCompleteGpuFrameSampleCount;
        m_lastCompleteGpuFrameSequence = stats.completeGpuFrame.sequence;
    }
    return true;
}

RenderGraphTimingWindowStats RenderGraphTimingHistory::snapshot() const {
    RenderGraphTimingWindowStats stats;
    stats.cpuValid = m_cpuSampleCount > 0u;
    stats.gpuValid = m_gpuSampleCount > 0u;
    stats.capacity = kCapacity;
    stats.cpuSampleCount = m_cpuSampleCount;
    stats.gpuSampleCount = m_gpuSampleCount;
    stats.observedGpuSampleCount = m_observedGpuSampleCount;
    stats.completeGpuFrameValid = m_completeGpuFrameSampleCount > 0u;
    stats.completeGpuFrameSampleCount = m_completeGpuFrameSampleCount;
    stats.observedCompleteGpuFrameSampleCount = m_observedCompleteGpuFrameSampleCount;
    stats.completeGpuFrameSpanMs = calculatePercentiles(m_completeGpuFrameSamples, m_completeGpuFrameSampleCount);
    for (size_t index = 0u; index < static_cast<size_t>(RenderGraphCpuTimingStage::Count); ++index) {
        stats.cpu[index] = calculatePercentiles(m_cpuSamples[index], m_cpuSampleCount);
    }
    for (size_t index = 0u; index < static_cast<size_t>(RenderGraphGpuTimingMetric::Count); ++index) {
        stats.gpu[index] = calculatePercentiles(m_gpuSamples[index], m_gpuSampleCount);
    }
    stats.latest = m_latest;
    return stats;
}

uint32_t RenderDebugService::gpuTimerQueryIndex(const size_t frameIndex, const size_t passIndex,
                                                const size_t segmentIndex, const size_t pointIndex) {
    return static_cast<uint32_t>(
        (((frameIndex * GPU_TIMER_PASS_COUNT + passIndex) * GPU_TIMER_MAX_SEGMENTS_PER_PASS + segmentIndex) *
         GPU_TIMER_POINTS_PER_SEGMENT) +
        pointIndex);
}

uint32_t RenderDebugService::gpuFrameSpanQueryIndex(const size_t frameIndex, const GpuFrameSpanPoint point) {
    return static_cast<uint32_t>(frameIndex * GPU_FRAME_SPAN_POINT_COUNT + static_cast<size_t>(point));
}

uint32_t RenderDebugService::shadowQueryIndex(const size_t frameIndex, const size_t cascadeIndex,
                                              const size_t pointIndex) {
    return static_cast<uint32_t>((frameIndex * SHADOW_TIMER_CASCADE_COUNT + cascadeIndex) * SHADOW_TIMER_POINT_COUNT +
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

    RhiQueryPoolDesc gpuFrameSpanPoolDesc;
    gpuFrameSpanPoolDesc.debugName = "RenderDebug.FrameSpanTimestamps";
    gpuFrameSpanPoolDesc.queryCount = GPU_FRAME_SPAN_QUERY_COUNT;
    m_gpuFrameSpanQueryPool = rhiDevice.createQueryPool(gpuFrameSpanPoolDesc);
    if (!m_gpuFrameSpanQueryPool.isValid()) {
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
    m_gpuFrameSpanStates.fill(GpuFrameSpanState::Unused);
    for (auto& frame : m_shadowTimestampIssued) {
        for (auto& cascade : frame) {
            cascade.fill(false);
        }
    }
    m_shadowFrameIssued.fill(false);
    m_gpuFrameStats = {};
    m_gpuFrameStats.supported = true;
    m_gpuFrameSequence = 0u;
    m_gpuFrameSpanStats = {};
    m_gpuFrameSpanStats.supported = true;
    m_gpuFrameSpanSequence = 0u;
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
    if (m_rhiDevice != nullptr && m_gpuFrameSpanQueryPool.isValid()) {
        m_rhiDevice->destroyQueryPool(m_gpuFrameSpanQueryPool);
    }
    m_gpuFrameSpanQueryPool = {};
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
    m_gpuFrameSpanStates.fill(GpuFrameSpanState::Unused);
    for (auto& frame : m_shadowTimestampIssued) {
        for (auto& cascade : frame) {
            cascade.fill(false);
        }
    }
    m_shadowFrameIssued.fill(false);
    m_gpuTimersInitialized = false;
    m_gpuFrameStats = {};
    m_gpuFrameSequence = 0u;
    m_gpuFrameSpanStats = {};
    m_gpuFrameSpanSequence = 0u;
    m_gpuTimingHistory.reset();
    m_shadowFrameStats.valid = false;
    m_shadowFrameActive = false;
}

void RenderDebugService::beginFrame(RhiCommandList& commandList) {
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled) {
        m_gpuFrameStats.supported = m_gpuTimersInitialized && m_gpuFrameStats.supported;
        m_gpuFrameStats.valid = false;
        m_gpuFrameSpanStats.supported = m_gpuTimersInitialized && m_gpuFrameSpanStats.supported;
        m_gpuFrameSpanStats.valid = false;
        m_gpuFrameSpanStats.sequence = 0u;
        m_gpuFrameSpanStats.spanMs = 0.0;
        m_shadowFrameStats.supported = m_gpuFrameStats.supported;
        m_shadowFrameStats.valid = false;
        m_gpuTimerCanIssueThisFrame = false;
        return;
    }

    m_shadowFrameStats.supported = true;
    m_gpuFrameSpanStats.supported = true;
    m_gpuFrameSpanStats.valid = false;
    m_gpuFrameSpanStats.sequence = 0u;
    m_gpuFrameSpanStats.spanMs = 0.0;

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
                if (m_gpuTimerSegmentStates[readIndex][pass][segment] != GpuTimerSegmentState::Issued) {
                    continue;
                }
                const uint32_t firstQuery = gpuTimerQueryIndex(readIndex, pass, segment, 0u);
                if (!m_rhiDevice->areQueryResultsAvailable(m_gpuTimerQueryPool, firstQuery,
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
                    if (m_gpuTimerSegmentStates[readIndex][passIndex][segment] != GpuTimerSegmentState::Issued) {
                        continue;
                    }
                    std::array<uint64_t, GPU_TIMER_POINTS_PER_SEGMENT> timestamps{};
                    const uint32_t firstQuery = gpuTimerQueryIndex(readIndex, passIndex, segment, 0u);
                    if (!m_rhiDevice->getQueryResults(m_gpuTimerQueryPool, firstQuery,
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
            m_gpuFrameStats.rtgiTraceMs = readMs(GpuTimerPass::RtgiTrace);
            m_gpuFrameStats.rtgiSignalPackMs = readMs(GpuTimerPass::RtgiSignalPack);
            m_gpuFrameStats.nrdGuidePrepMs = readMs(GpuTimerPass::NrdGuidePrep);
            m_gpuFrameStats.nrdDispatchMs = readMs(GpuTimerPass::NrdDispatch);
            m_gpuFrameStats.rtgiMs = m_gpuFrameStats.rtgiTraceMs + m_gpuFrameStats.rtgiSignalPackMs;
            m_gpuFrameStats.nrdMs = m_gpuFrameStats.nrdGuidePrepMs + m_gpuFrameStats.nrdDispatchMs;
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
        const int cascadeCount =
            std::min(static_cast<int>(SHADOW_TIMER_CASCADE_COUNT), std::max(0, pendingStats.cascadeCount));
        const uint32_t firstQuery = shadowQueryIndex(readIndex, 0u, 0u);
        const uint32_t queryCount =
            static_cast<uint32_t>(cascadeCount) * static_cast<uint32_t>(SHADOW_TIMER_POINT_COUNT);
        const bool allAvailable = queryCount > 0u && m_rhiDevice->areQueryResultsAvailable(m_shadowTimestampQueryPool,
                                                                                           firstQuery, queryCount);

        if (allAvailable) {
            std::array<uint64_t, SHADOW_TIMER_CASCADE_COUNT * SHADOW_TIMER_POINT_COUNT> timestamps{};
            if (!m_rhiDevice->getQueryResults(m_shadowTimestampQueryPool, firstQuery, queryCount, timestamps.data())) {
                std::abort();
            }
            ShadowFrameStats stats = pendingStats;
            stats.supported = true;
            stats.valid = true;
            stats.gpuTotalMs = 0.0;
            for (int cascade = 0; cascade < cascadeCount; ++cascade) {
                const auto readTimestamp = [&](const ShadowTimestampPoint point) {
                    const size_t localIndex =
                        static_cast<size_t>(cascade) * SHADOW_TIMER_POINT_COUNT + static_cast<size_t>(point);
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

    const GpuFrameSpanState spanState = m_gpuFrameSpanStates[readIndex];
    if (spanState == GpuFrameSpanState::Issued) {
        const uint32_t firstQuery = gpuFrameSpanQueryIndex(readIndex, GpuFrameSpanPoint::Start);
        if (m_rhiDevice->areQueryResultsAvailable(m_gpuFrameSpanQueryPool, firstQuery,
                                                   static_cast<uint32_t>(GPU_FRAME_SPAN_POINT_COUNT))) {
            std::array<uint64_t, GPU_FRAME_SPAN_POINT_COUNT> timestamps{};
            if (!m_rhiDevice->getQueryResults(m_gpuFrameSpanQueryPool, firstQuery,
                                              static_cast<uint32_t>(GPU_FRAME_SPAN_POINT_COUNT), timestamps.data())) {
                std::abort();
            }
            const uint64_t start = timestamps[static_cast<size_t>(GpuFrameSpanPoint::Start)];
            const uint64_t end = timestamps[static_cast<size_t>(GpuFrameSpanPoint::End)];
            if (end < start) {
                std::abort();
            }
            m_gpuFrameSpanStats.valid = true;
            m_gpuFrameSpanStats.sequence = ++m_gpuFrameSpanSequence;
            m_gpuFrameSpanStats.spanMs = static_cast<double>(end - start) / 1000000.0;
            m_gpuFrameSpanStates[readIndex] = GpuFrameSpanState::Unused;
        }
    } else if (spanState == GpuFrameSpanState::Started) {
        // A frame that reached the ring without a terminal marker is incomplete.
        m_gpuFrameSpanStates[readIndex] = GpuFrameSpanState::Unused;
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
    if (m_gpuFrameSpanStates[readIndex] == GpuFrameSpanState::Issued) {
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
    if (m_gpuFrameSpanStates[m_gpuTimerWriteIndex] == GpuFrameSpanState::Started) {
        std::abort();
    }
    m_gpuFrameSpanStates[m_gpuTimerWriteIndex] = GpuFrameSpanState::Unused;

    const uint32_t timerSlotFirstQuery = gpuTimerQueryIndex(m_gpuTimerWriteIndex, 0u, 0u, 0u);
    const uint32_t timerSlotQueryCount =
        static_cast<uint32_t>(GPU_TIMER_PASS_COUNT * GPU_TIMER_MAX_SEGMENTS_PER_PASS * GPU_TIMER_POINTS_PER_SEGMENT);
    commandList.resetQueryPool(m_gpuTimerQueryPool, timerSlotFirstQuery, timerSlotQueryCount);

    const uint32_t shadowSlotFirstQuery = shadowQueryIndex(m_gpuTimerWriteIndex, 0u, 0u);
    const uint32_t shadowSlotQueryCount = static_cast<uint32_t>(SHADOW_TIMER_CASCADE_COUNT * SHADOW_TIMER_POINT_COUNT);
    commandList.resetQueryPool(m_shadowTimestampQueryPool, shadowSlotFirstQuery, shadowSlotQueryCount);

    const uint32_t spanSlotFirstQuery = gpuFrameSpanQueryIndex(m_gpuTimerWriteIndex, GpuFrameSpanPoint::Start);
    commandList.resetQueryPool(m_gpuFrameSpanQueryPool, spanSlotFirstQuery,
                               static_cast<uint32_t>(GPU_FRAME_SPAN_POINT_COUNT));
    commandList.writeTimestamp(m_gpuFrameSpanQueryPool, spanSlotFirstQuery);
    m_gpuFrameSpanStates[m_gpuTimerWriteIndex] = GpuFrameSpanState::Started;
}

void RenderDebugService::endGpuFrameSpan(RhiCommandList& commandList) {
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled || !m_gpuTimerCanIssueThisFrame) {
        return;
    }

    const std::lock_guard<std::mutex> timerLock(m_gpuTimerMutex);
    if (m_gpuFrameSpanStates[m_gpuTimerWriteIndex] != GpuFrameSpanState::Started) {
        std::abort();
    }
    const uint32_t endQuery = gpuFrameSpanQueryIndex(m_gpuTimerWriteIndex, GpuFrameSpanPoint::End);
    commandList.writeTimestamp(m_gpuFrameSpanQueryPool, endQuery);
    m_gpuFrameSpanStates[m_gpuTimerWriteIndex] = GpuFrameSpanState::Issued;
}

void RenderDebugService::cancelGpuFrameSpan() {
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled || !m_gpuTimerCanIssueThisFrame) {
        return;
    }

    const std::lock_guard<std::mutex> timerLock(m_gpuTimerMutex);
    GpuFrameSpanState& state = m_gpuFrameSpanStates[m_gpuTimerWriteIndex];
    if (state == GpuFrameSpanState::Started || state == GpuFrameSpanState::Issued) {
        state = GpuFrameSpanState::Unused;
    }
}

GpuTimerSegmentToken RenderDebugService::beginGpuTimer(RhiCommandList& commandList, const GpuTimerPass pass) {
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled || !m_gpuTimerCanIssueThisFrame) {
        return {};
    }

    const std::lock_guard<std::mutex> timerLock(m_gpuTimerMutex);
    const size_t passIndex = static_cast<size_t>(pass);
    if (passIndex >= GPU_TIMER_PASS_COUNT) {
        std::abort();
    }
    const size_t segmentIndex = m_gpuTimerAllocatedSegmentCounts[m_gpuTimerWriteIndex][passIndex];
    if (segmentIndex >= GPU_TIMER_MAX_SEGMENTS_PER_PASS) {
        std::abort();
    }
    m_gpuTimerAllocatedSegmentCounts[m_gpuTimerWriteIndex][passIndex] = static_cast<uint8_t>(segmentIndex + 1u);
    const uint32_t firstQuery = gpuTimerQueryIndex(m_gpuTimerWriteIndex, passIndex, segmentIndex, 0u);
    commandList.writeTimestamp(m_gpuTimerQueryPool, firstQuery);
    m_gpuTimerSegmentStates[m_gpuTimerWriteIndex][passIndex][segmentIndex] = GpuTimerSegmentState::Recording;
    return {pass, static_cast<uint8_t>(m_gpuTimerWriteIndex), static_cast<uint8_t>(segmentIndex), true};
}

void RenderDebugService::endGpuTimer(RhiCommandList& commandList, const GpuTimerSegmentToken token) {
    if (!token.valid) {
        return;
    }

    const std::lock_guard<std::mutex> timerLock(m_gpuTimerMutex);
    const size_t passIndex = static_cast<size_t>(token.pass);
    if (!m_gpuTimersInitialized || token.frameIndex != m_gpuTimerWriteIndex || passIndex >= GPU_TIMER_PASS_COUNT ||
        token.segmentIndex >= GPU_TIMER_MAX_SEGMENTS_PER_PASS ||
        m_gpuTimerSegmentStates[token.frameIndex][passIndex][token.segmentIndex] != GpuTimerSegmentState::Recording) {
        std::abort();
    }
    const uint32_t endQuery = gpuTimerQueryIndex(token.frameIndex, passIndex, token.segmentIndex, 1u);
    commandList.writeTimestamp(m_gpuTimerQueryPool, endQuery);
    m_gpuTimerSegmentStates[token.frameIndex][passIndex][token.segmentIndex] = GpuTimerSegmentState::Issued;
}

void RenderDebugService::cancelGpuTimer(const GpuTimerSegmentToken token) {
    if (!token.valid) {
        return;
    }

    const std::lock_guard<std::mutex> timerLock(m_gpuTimerMutex);
    const size_t passIndex = static_cast<size_t>(token.pass);
    if (!m_gpuTimersInitialized || token.frameIndex != m_gpuTimerWriteIndex || passIndex >= GPU_TIMER_PASS_COUNT ||
        token.segmentIndex >= GPU_TIMER_MAX_SEGMENTS_PER_PASS ||
        m_gpuTimerSegmentStates[token.frameIndex][passIndex][token.segmentIndex] != GpuTimerSegmentState::Recording) {
        std::abort();
    }
    m_gpuTimerSegmentStates[token.frameIndex][passIndex][token.segmentIndex] = GpuTimerSegmentState::Unused;
}

GpuTimerCheckpoint RenderDebugService::gpuTimerCheckpoint() const {
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled || !m_gpuTimerCanIssueThisFrame) {
        return {};
    }
    const std::lock_guard<std::mutex> timerLock(m_gpuTimerMutex);
    return {m_gpuTimerAllocatedSegmentCounts[m_gpuTimerWriteIndex], static_cast<uint8_t>(m_gpuTimerWriteIndex), true};
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
        const size_t allocatedSegments = m_gpuTimerAllocatedSegmentCounts[m_gpuTimerWriteIndex][passIndex];
        if (firstSegment > allocatedSegments) {
            std::abort();
        }
        for (size_t segment = firstSegment; segment < allocatedSegments; ++segment) {
            GpuTimerSegmentState& state = m_gpuTimerSegmentStates[m_gpuTimerWriteIndex][passIndex][segment];
            state = GpuTimerSegmentState::Unused;
        }
        m_gpuTimerAllocatedSegmentCounts[m_gpuTimerWriteIndex][passIndex] = checkpoint.segmentCounts[passIndex];
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

void RenderDebugService::recordShadowCascadeStats(const int cascadeIndex, const ShadowCascadeStats& stats) {
    if (!m_shadowFrameActive || cascadeIndex < 0 || cascadeIndex >= static_cast<int>(SHADOW_TIMER_CASCADE_COUNT)) {
        return;
    }

    ShadowFrameStats& frame = m_shadowFrameSlots[m_gpuTimerWriteIndex];
    frame.cascades[static_cast<size_t>(cascadeIndex)] = stats;
}

void RenderDebugService::recordShadowFrameTotals(const int submitted, const int culled, const float maxCasterDistance) {
    if (!m_shadowFrameActive) {
        return;
    }

    ShadowFrameStats& frame = m_shadowFrameSlots[m_gpuTimerWriteIndex];
    frame.submitted = submitted;
    frame.culled = culled;
    frame.maxCasterDistance = maxCasterDistance;
}

void RenderDebugService::markShadowTimestamp(RhiCommandList& commandList, const int cascadeIndex,
                                             const ShadowTimestampPoint point) {
    const std::lock_guard<std::mutex> timerLock(m_gpuTimerMutex);
    if (!m_shadowFrameActive || cascadeIndex < 0 || cascadeIndex >= static_cast<int>(SHADOW_TIMER_CASCADE_COUNT)) {
        return;
    }
    const size_t pointIndex = static_cast<size_t>(point);
    if (pointIndex >= SHADOW_TIMER_POINT_COUNT) {
        return;
    }

    const uint32_t queryIndex = shadowQueryIndex(m_gpuTimerWriteIndex, static_cast<size_t>(cascadeIndex), pointIndex);
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
            if (!m_shadowTimestampIssued[m_gpuTimerWriteIndex][static_cast<size_t>(cascade)][point]) {
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
