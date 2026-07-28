#include "renderer/debug/RenderDebugService.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[gpu_timing_history_test] FAIL: " << message << '\n';
        return false;
    }
    return true;
}

bool requireNear(const double actual,
                 const double expected,
                 const char* message) {
    if (std::abs(actual - expected) > 1.0e-9) {
        std::cerr << "[gpu_timing_history_test] FAIL: " << message
                  << " actual=" << actual
                  << " expected=" << expected << '\n';
        return false;
    }
    return true;
}

bool testStableStageNames() {
    const char* expected[] = {
        "GBuffer",
        "Shadow",
        "SSAO",
        "SSGI",
        "Lighting",
        "Transparent",
        "Volumetric",
        "Reflection",
        "Cloud",
        "Water",
        "Post"
    };
    for (size_t index = 0u;
         index < static_cast<size_t>(GpuTimerPass::Count);
         ++index) {
        if (!requireTrue(
                std::string(gpuTimerPassName(
                    static_cast<GpuTimerPass>(index))) == expected[index],
                "GPU stage names must remain stable")) {
            return false;
        }
    }
    return true;
}

bool testFixedWindowPercentiles() {
    GpuTimingHistory history;
    const GpuTimingWindowStats empty = history.snapshot();
    if (!requireTrue(!empty.valid && empty.sampleCount == 0u &&
                         empty.capacity == GpuTimingHistory::kCapacity,
                     "empty history must expose its fixed capacity")) {
        return false;
    }

    GpuFrameStats invalid;
    invalid.supported = true;
    invalid.sequence = 1u;
    if (!requireTrue(!history.record(invalid),
                     "incomplete GPU frames must be rejected")) {
        return false;
    }

    GpuFrameStats frame;
    frame.supported = true;
    frame.valid = true;
    for (uint64_t sequence = 1u; sequence <= 1002u; ++sequence) {
        frame.sequence = sequence;
        frame.gbufferMs = static_cast<double>(sequence);
        frame.shadowMs = static_cast<double>(sequence) * 2.0;
        if (!requireTrue(history.record(frame),
                         "unique completed GPU frames must be recorded")) {
            return false;
        }
    }
    if (!requireTrue(!history.record(frame),
                     "duplicate GPU frame sequences must be rejected")) {
        return false;
    }
    frame.sequence = 1001u;
    if (!requireTrue(!history.record(frame),
                     "stale GPU frame sequences must be rejected")) {
        return false;
    }
    frame.sequence = 1003u;
    frame.gbufferMs = -1.0;
    if (!requireTrue(!history.record(frame),
                     "negative GPU durations must be rejected")) {
        return false;
    }

    const GpuTimingWindowStats stats = history.snapshot();
    if (!requireTrue(stats.valid && stats.sampleCount == 1000u &&
                         stats.observedSampleCount == 1002u,
                     "history must retain the latest fixed-size window")) {
        return false;
    }

    const auto& gbuffer =
        stats.passes[static_cast<size_t>(GpuTimerPass::GBuffer)].gpuMs;
    const auto& shadow =
        stats.passes[static_cast<size_t>(GpuTimerPass::Shadow)].gpuMs;
    return requireNear(gbuffer.p50Ms, 502.0,
                       "GBuffer p50 must use nearest-rank selection") &&
           requireNear(gbuffer.p95Ms, 952.0,
                       "GBuffer p95 must use nearest-rank selection") &&
           requireNear(gbuffer.p99Ms, 992.0,
                       "GBuffer p99 must use nearest-rank selection") &&
           requireNear(shadow.p50Ms, 1004.0,
                       "Shadow p50 must preserve stage values") &&
           requireNear(shadow.p95Ms, 1904.0,
                       "Shadow p95 must preserve stage values") &&
           requireNear(shadow.p99Ms, 1984.0,
                       "Shadow p99 must preserve stage values") &&
           requireNear(stats.totalTrackedGpuMs.p50Ms, 1506.0,
                       "tracked total p50 must be computed per frame") &&
           requireNear(stats.totalTrackedGpuMs.p95Ms, 2856.0,
                       "tracked total p95 must be computed per frame") &&
           requireNear(stats.totalTrackedGpuMs.p99Ms, 2976.0,
                       "tracked total p99 must be computed per frame");
}

} // namespace

int main() {
    if (!testStableStageNames() || !testFixedWindowPercentiles()) {
        return 1;
    }
    std::cout << "[gpu_timing_history_test] PASS\n";
    return 0;
}
