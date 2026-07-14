#include "TemporalUpscalePass.h"

TemporalUpscaleResult TemporalUpscalePass::execute(
    const TemporalUpscalerType type,
    const TemporalFrameInput& frame) const {
    const std::optional<TemporalFrameValidationError> validationError =
        validateTemporalFrame(frame);
    if (validationError.has_value()) {
        return {TemporalUpscaleStatus::InvalidFrame, validationError, {}, {}, {}};
    }

    switch (type) {
        case TemporalUpscalerType::Native:
            if (frame.renderExtent != frame.outputExtent) {
                return {TemporalUpscaleStatus::NativeExtentMismatch, std::nullopt, {}, {}, {}};
            }
            return {
                TemporalUpscaleStatus::Success,
                std::nullopt,
                frame.textures.hdrColor,
                frame.textures.hdrColorView,
                frame.outputExtent
            };
        case TemporalUpscalerType::Fsr31:
            return {TemporalUpscaleStatus::Fsr31Unavailable, std::nullopt, {}, {}, {}};
        case TemporalUpscalerType::Dlss:
            return {TemporalUpscaleStatus::DlssUnavailable, std::nullopt, {}, {}, {}};
    }
    return {TemporalUpscaleStatus::InvalidFrame, std::nullopt, {}, {}, {}};
}

const char* TemporalUpscalePass::statusText(const TemporalUpscaleStatus status) {
    switch (status) {
        case TemporalUpscaleStatus::Success:
            return "success";
        case TemporalUpscaleStatus::InvalidFrame:
            return "temporal frame contract is invalid";
        case TemporalUpscaleStatus::NativeExtentMismatch:
            return "native temporal reconstruction requires matching render and output extents";
        case TemporalUpscaleStatus::Fsr31Unavailable:
            return "FSR 3.1 temporal reconstruction is not initialized";
        case TemporalUpscaleStatus::DlssUnavailable:
            return "DLSS temporal reconstruction is not initialized";
    }
    return "unknown temporal reconstruction status";
}
