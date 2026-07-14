#include <FidelityFX/host/ffx_fsr3upscaler.h>

#include "renderer/upscaling/Fsr31TemporalConfig.h"

#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

bool requireTrue(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

bool testVersion() {
    const FfxVersionNumber expected = FFX_SDK_MAKE_VERSION(3u, 1u, 4u);
    return requireTrue(ffxFsr3UpscalerGetEffectVersion() == expected,
                       "FidelityFX FSR upscaler version must be 3.1.4");
}

bool testRenderExtentQueries() {
    uint32_t width = 0u;
    uint32_t height = 0u;
    if (!requireTrue(ffxFsr3UpscalerGetRenderResolutionFromQualityMode(
                         &width, &height, 1920u, 1080u,
                         FFX_FSR3UPSCALER_QUALITY_MODE_QUALITY) == FFX_OK &&
                         width == 1280u && height == 720u,
                     "FSR Quality extent query must return 1280x720 for 1080p")) {
        return false;
    }
    if (!requireTrue(ffxFsr3UpscalerGetRenderResolutionFromQualityMode(
                         &width, &height, 1920u, 1080u,
                         FFX_FSR3UPSCALER_QUALITY_MODE_BALANCED) == FFX_OK &&
                         width == 1129u && height == 635u,
                     "FSR Balanced extent query must use the SDK ratio")) {
        return false;
    }
    if (!requireTrue(ffxFsr3UpscalerGetRenderResolutionFromQualityMode(
                         &width, &height, 1920u, 1080u,
                         FFX_FSR3UPSCALER_QUALITY_MODE_PERFORMANCE) == FFX_OK &&
                         width == 960u && height == 540u,
                     "FSR Performance extent query must return 960x540 for 1080p")) {
        return false;
    }
    return requireTrue(ffxFsr3UpscalerGetRenderResolutionFromQualityMode(
                           &width, &height, 1920u, 1080u,
                           static_cast<FfxFsr3UpscalerQualityMode>(99)) ==
                           static_cast<FfxErrorCode>(FFX_ERROR_INVALID_ENUM),
                       "FSR extent query must reject invalid quality modes");
}

bool testProjectRenderExtentQueries() {
    const Fsr31RenderExtentResult quality = queryFsr31RenderExtent(
        TemporalUpscaleQuality::Quality, {1920u, 1080u});
    const Fsr31RenderExtentResult balanced = queryFsr31RenderExtent(
        TemporalUpscaleQuality::Balanced, {1920u, 1080u});
    const Fsr31RenderExtentResult invalid = queryFsr31RenderExtent(
        TemporalUpscaleQuality::Native, {1920u, 1080u});
    return requireTrue(quality.succeeded() &&
                           quality.extent == TemporalExtent{1280u, 720u},
                       "project FSR Quality query must return 1280x720") &&
           requireTrue(balanced.succeeded() &&
                           balanced.extent == TemporalExtent{1129u, 635u},
                       "project FSR Balanced query must preserve the SDK result") &&
           requireTrue(invalid.status == Fsr31TemporalConfigStatus::InvalidQuality,
                       "project FSR query must reject Native quality");
}

bool testJitterQueries() {
    const int32_t phaseCount = ffxFsr3UpscalerGetJitterPhaseCount(1280, 1920);
    float jitterX = 0.0f;
    float jitterY = 0.0f;
    if (!requireTrue(phaseCount == 18,
                     "FSR Quality jitter phase count must be 18") ||
        !requireTrue(ffxFsr3UpscalerGetJitterOffset(
                         &jitterX, &jitterY, 0, phaseCount) == FFX_OK,
                     "FSR jitter query must accept a valid phase")) {
        return false;
    }
    return requireTrue(std::abs(jitterX) < 0.000001f &&
                           std::abs(jitterY + 1.0f / 6.0f) < 0.000001f,
                       "FSR jitter query must return the official Halton sequence");
}

bool testProjectJitterQueries() {
    const Fsr31JitterResult first = queryFsr31Jitter(
        0u, {1280u, 720u}, {1920u, 1080u});
    const Fsr31JitterResult wrapped = queryFsr31Jitter(
        18u, {1280u, 720u}, {1920u, 1080u});
    return requireTrue(first.succeeded() && first.phaseCount == 18 &&
                           first.phaseIndex == 0,
                       "project FSR jitter must expose the official phase metadata") &&
           requireTrue(std::abs(first.jitter.pixels.x) < 0.000001f &&
                           std::abs(first.jitter.pixels.y + 1.0f / 6.0f) < 0.000001f &&
                           std::abs(first.jitter.projectionOffset.x) < 0.000001f &&
                           std::abs(first.jitter.projectionOffset.y -
                                    1.0f / (6.0f * 720.0f)) < 0.000001f,
                       "project FSR jitter must preserve pixel and projection conventions") &&
           requireTrue(wrapped.succeeded() && wrapped.phaseIndex == 0 &&
                           wrapped.jitter.pixels == first.jitter.pixels,
                       "project FSR jitter must wrap at the official phase count");
}

} // namespace

int main() {
    if (!testVersion()) return 1;
    if (!testRenderExtentQueries()) return 1;
    if (!testProjectRenderExtentQueries()) return 1;
    if (!testJitterQueries()) return 1;
    if (!testProjectJitterQueries()) return 1;
    return 0;
}
