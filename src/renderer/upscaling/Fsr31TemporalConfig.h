#ifndef MECRAFT_FSR31_TEMPORAL_CONFIG_H
#define MECRAFT_FSR31_TEMPORAL_CONFIG_H

#include "renderer/contracts/TemporalFrameContract.h"
#include "renderer/core/RenderSettings.h"

#include <cstdint>

enum class Fsr31TemporalConfigStatus {
    Success,
    InvalidOutputExtent,
    InvalidRenderExtent,
    InvalidQuality,
    DimensionOutOfRange,
    InvalidJitterPhaseCount,
    SdkError
};

struct Fsr31RenderExtentResult {
    Fsr31TemporalConfigStatus status = Fsr31TemporalConfigStatus::InvalidOutputExtent;
    TemporalExtent extent;
    int32_t sdkError = 0;

    [[nodiscard]] bool succeeded() const { return status == Fsr31TemporalConfigStatus::Success; }
};

struct Fsr31JitterResult {
    Fsr31TemporalConfigStatus status = Fsr31TemporalConfigStatus::InvalidRenderExtent;
    TemporalJitter jitter;
    int32_t phaseCount = 0;
    int32_t phaseIndex = 0;
    int32_t sdkError = 0;

    [[nodiscard]] bool succeeded() const { return status == Fsr31TemporalConfigStatus::Success; }
};

/// Query the official FSR 3.1 render resolution for one output extent.
/// @param quality Project-level quality setting mapped to the SDK quality mode.
/// @param outputExtent Display-resolution target requested from the upscaler.
/// @return Explicit status and the SDK-selected render extent on success.
[[nodiscard]] Fsr31RenderExtentResult queryFsr31RenderExtent(TemporalUpscaleQuality quality,
                                                             TemporalExtent outputExtent);

/// Query one official FSR 3.1 Halton jitter sample.
/// @param frameIndex Monotonically increasing real-frame index.
/// @param renderExtent Scene rendering extent used to compute the phase count.
/// @param outputExtent Upscaled output extent used to compute the phase count.
/// @return Explicit status, pixel/projection jitter, and sequence metadata.
[[nodiscard]] Fsr31JitterResult queryFsr31Jitter(uint64_t frameIndex, TemporalExtent renderExtent,
                                                 TemporalExtent outputExtent);

/// Convert Mecraft current-minus-previous UV velocity into the FSR convention.
/// @param motionVectorScale Positive pixel scale from the temporal frame contract.
/// @return Negative pixel scale that converts stored velocity to previous-minus-current.
[[nodiscard]] glm::vec2 fsr31MotionVectorScale(glm::vec2 motionVectorScale);

#endif // MECRAFT_FSR31_TEMPORAL_CONFIG_H
