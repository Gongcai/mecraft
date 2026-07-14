#ifndef MECRAFT_TEMPORAL_UPSCALE_PASS_H
#define MECRAFT_TEMPORAL_UPSCALE_PASS_H

#include "renderer/contracts/TemporalFrameContract.h"
#include "renderer/core/RenderSettings.h"

#include <optional>

enum class TemporalUpscaleStatus {
    Success,
    InvalidFrame,
    NativeExtentMismatch,
    Fsr31Unavailable,
    DlssUnavailable
};

struct TemporalUpscaleResult {
    TemporalUpscaleStatus status = TemporalUpscaleStatus::InvalidFrame;
    std::optional<TemporalFrameValidationError> validationError;
    RhiTextureHandle outputHdrColor;
    TemporalExtent outputExtent;

    [[nodiscard]] bool succeeded() const {
        return status == TemporalUpscaleStatus::Success;
    }
};

/// Dispatches the selected HDR temporal reconstruction implementation.
class TemporalUpscalePass {
public:
    /// Execute one temporal reconstruction frame.
    /// @param type Explicitly selected temporal reconstruction technology.
    /// @param frame Fully populated backend-independent frame contract.
    /// @return Status and HDR output consumed by display post-processing.
    [[nodiscard]] TemporalUpscaleResult execute(
        TemporalUpscalerType type,
        const TemporalFrameInput& frame) const;

    [[nodiscard]] static const char* statusText(TemporalUpscaleStatus status);
};

#endif // MECRAFT_TEMPORAL_UPSCALE_PASS_H
