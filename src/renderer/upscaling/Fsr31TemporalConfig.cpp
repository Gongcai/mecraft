#include "renderer/upscaling/Fsr31TemporalConfig.h"

#include <FidelityFX/host/ffx_fsr3upscaler.h>

#include <limits>
#include <optional>

namespace {

[[nodiscard]] std::optional<FfxFsr3UpscalerQualityMode> toFsrQualityMode(
    const TemporalUpscaleQuality quality) {
    switch (quality) {
        case TemporalUpscaleQuality::Quality:
            return FFX_FSR3UPSCALER_QUALITY_MODE_QUALITY;
        case TemporalUpscaleQuality::Balanced:
            return FFX_FSR3UPSCALER_QUALITY_MODE_BALANCED;
        case TemporalUpscaleQuality::Performance:
            return FFX_FSR3UPSCALER_QUALITY_MODE_PERFORMANCE;
        case TemporalUpscaleQuality::UltraPerformance:
            return FFX_FSR3UPSCALER_QUALITY_MODE_ULTRA_PERFORMANCE;
        case TemporalUpscaleQuality::Native:
            return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] bool fitsSdkDimension(const uint32_t value) {
    return value <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
}

} // namespace

Fsr31RenderExtentResult queryFsr31RenderExtent(
    const TemporalUpscaleQuality quality,
    const TemporalExtent outputExtent) {
    if (!outputExtent.isValid()) {
        return {Fsr31TemporalConfigStatus::InvalidOutputExtent, {}, 0};
    }
    const auto qualityMode = toFsrQualityMode(quality);
    if (!qualityMode.has_value()) {
        return {Fsr31TemporalConfigStatus::InvalidQuality, {}, 0};
    }

    TemporalExtent renderExtent;
    const FfxErrorCode error = ffxFsr3UpscalerGetRenderResolutionFromQualityMode(
        &renderExtent.width,
        &renderExtent.height,
        outputExtent.width,
        outputExtent.height,
        *qualityMode);
    if (error != FFX_OK) {
        return {Fsr31TemporalConfigStatus::SdkError, {}, error};
    }
    if (!renderExtent.isValid()) {
        return {Fsr31TemporalConfigStatus::InvalidRenderExtent, {}, 0};
    }
    return {Fsr31TemporalConfigStatus::Success, renderExtent, 0};
}

Fsr31JitterResult queryFsr31Jitter(
    const uint64_t frameIndex,
    const TemporalExtent renderExtent,
    const TemporalExtent outputExtent) {
    if (!renderExtent.isValid()) {
        Fsr31JitterResult result;
        result.status = Fsr31TemporalConfigStatus::InvalidRenderExtent;
        return result;
    }
    if (!outputExtent.isValid()) {
        Fsr31JitterResult result;
        result.status = Fsr31TemporalConfigStatus::InvalidOutputExtent;
        return result;
    }
    if (!fitsSdkDimension(renderExtent.width) ||
        !fitsSdkDimension(outputExtent.width)) {
        Fsr31JitterResult result;
        result.status = Fsr31TemporalConfigStatus::DimensionOutOfRange;
        return result;
    }

    const int32_t phaseCount = ffxFsr3UpscalerGetJitterPhaseCount(
        static_cast<int32_t>(renderExtent.width),
        static_cast<int32_t>(outputExtent.width));
    if (phaseCount <= 0) {
        Fsr31JitterResult result;
        result.status = Fsr31TemporalConfigStatus::InvalidJitterPhaseCount;
        return result;
    }
    const int32_t phaseIndex = static_cast<int32_t>(
        frameIndex % static_cast<uint64_t>(phaseCount));
    TemporalJitter jitter;
    const FfxErrorCode error = ffxFsr3UpscalerGetJitterOffset(
        &jitter.pixels.x, &jitter.pixels.y, phaseIndex, phaseCount);
    if (error != FFX_OK) {
        Fsr31JitterResult result;
        result.status = Fsr31TemporalConfigStatus::SdkError;
        result.phaseCount = phaseCount;
        result.phaseIndex = phaseIndex;
        result.sdkError = error;
        return result;
    }
    jitter.projectionOffset = {
        jitter.pixels.x / static_cast<float>(renderExtent.width),
        -jitter.pixels.y / static_cast<float>(renderExtent.height)
    };
    Fsr31JitterResult result;
    result.status = Fsr31TemporalConfigStatus::Success;
    result.jitter = jitter;
    result.phaseCount = phaseCount;
    result.phaseIndex = phaseIndex;
    return result;
}
