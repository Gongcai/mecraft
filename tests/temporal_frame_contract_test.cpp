#include "renderer/contracts/TemporalFrameContract.h"
#include "renderer/core/RenderSettings.h"
#include "renderer/passes/TemporalUpscalePass.h"

#include <iostream>

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

RhiTextureHandle textureHandle(const uint32_t index) {
    return {index, 1u};
}

RhiTextureViewHandle textureViewHandle(const uint32_t index) {
    return {index, 1u};
}

TemporalFrameInput completeFrame() {
    TemporalFrameInput frame;
    frame.renderExtent = {1280u, 720u};
    frame.outputExtent = {1920u, 1080u};
    frame.jitter.pixels = {0.25f, -0.5f};
    frame.jitter.projectionOffset = {
        frame.jitter.pixels.x / static_cast<float>(frame.renderExtent.width),
        -frame.jitter.pixels.y / static_cast<float>(frame.renderExtent.height)
    };
    frame.motionVectorScale = {
        static_cast<float>(frame.renderExtent.width),
        static_cast<float>(frame.renderExtent.height)
    };
    frame.frameDeltaMilliseconds = 16.0f;
    frame.preExposure = 1.0f;
    frame.cameraNear = 0.1f;
    frame.cameraFar = 500.0f;
    frame.verticalFovRadians = 1.22173048f;
    frame.reset = false;
    frame.textures.hdrColor = textureHandle(1u);
    frame.textures.hdrColorView = textureViewHandle(1u);
    frame.textures.depth = textureHandle(2u);
    frame.textures.depthView = textureViewHandle(2u);
    frame.textures.velocity = textureHandle(3u);
    frame.textures.velocityView = textureViewHandle(3u);
    frame.textures.exposure = textureHandle(4u);
    frame.textures.exposureView = textureViewHandle(4u);
    frame.textures.reactiveMask = textureHandle(5u);
    frame.textures.reactiveMaskView = textureViewHandle(5u);
    frame.textures.transparencyMask = textureHandle(6u);
    frame.textures.transparencyMaskView = textureViewHandle(6u);
    frame.textures.outputHdrColor = textureHandle(7u);
    frame.textures.outputHdrColorView = textureViewHandle(7u);
    return frame;
}

bool testSettingsDefaults() {
    const UpscaleSettings settings;
    return requireTrue(settings.type == TemporalUpscalerType::Native,
                       "temporal upscaler must default to native") &&
           requireTrue(settings.quality == TemporalUpscaleQuality::Native,
                       "native upscaling must use native quality") &&
           requireTrue(!settings.dynamicResolutionEnabled,
                       "dynamic resolution must require explicit enablement") &&
           requireTrue(!settings.debugVisualizationEnabled,
                       "temporal debug visualization must default to disabled");
}

bool testTemporalReconstructionSelection() {
    return requireTrue(
               usesTemporalProjectionJitter(TemporalUpscalerType::Native, true),
               "native TAA must jitter scene rasterization") &&
           requireTrue(
               !usesTemporalProjectionJitter(TemporalUpscalerType::Native, false),
               "native rendering without TAA must not jitter scene rasterization") &&
           requireTrue(
               usesTemporalProjectionJitter(TemporalUpscalerType::Fsr31, false),
               "FSR 3.1 must jitter scene rasterization independently of native TAA") &&
           requireTrue(
               usesNativeTaaResolve(TemporalUpscalerType::Native, true),
               "native TAA must execute only in Native reconstruction mode") &&
           requireTrue(
               !usesNativeTaaResolve(TemporalUpscalerType::Fsr31, true),
               "FSR 3.1 must exclude the native TAA resolve");
}

bool testMotionVectorConvention() {
    return requireTrue(TemporalMotionVectorConvention::currentMinusPrevious,
                       "motion vectors must store current minus previous UV") &&
           requireTrue(TemporalMotionVectorConvention::normalizedTextureCoordinates,
                       "motion vectors must use normalized texture coordinates") &&
           requireTrue(TemporalMotionVectorConvention::positiveYDown,
                       "temporal SDK motion vectors must use top-left texture UV") &&
           requireTrue(TemporalMotionVectorConvention::minimumStoredValue == -2.0f &&
                           TemporalMotionVectorConvention::maximumStoredValue == 2.0f,
                       "motion-vector storage range must match RG16F resolve clamping");
}

bool testTemporalReset() {
    const TemporalExtent renderExtent{1280u, 720u};
    const TemporalExtent outputExtent{1920u, 1080u};
    return requireTrue(requiresTemporalReset(
                           false, renderExtent, outputExtent, renderExtent, outputExtent),
                       "missing previous frame must request temporal reset") &&
           requireTrue(!requiresTemporalReset(
                           true, renderExtent, outputExtent, renderExtent, outputExtent),
                       "stable extents must preserve temporal history") &&
           requireTrue(requiresTemporalReset(
                           true, renderExtent, outputExtent, {960u, 540u}, outputExtent),
                       "render extent change must request temporal reset") &&
           requireTrue(requiresTemporalReset(
                           true, renderExtent, outputExtent, renderExtent, {2560u, 1440u}),
                       "output extent change must request temporal reset");
}

bool testFrameValidation() {
    TemporalFrameInput frame = completeFrame();
    if (!requireTrue(!validateTemporalFrame(frame).has_value(),
                     "complete temporal frame must validate")) {
        return false;
    }

    frame.renderExtent = {};
    if (!requireTrue(validateTemporalFrame(frame) ==
                         TemporalFrameValidationError::InvalidRenderExtent,
                     "zero render extent must be rejected")) {
        return false;
    }

    frame = completeFrame();
    frame.textures.hdrColorView = {};
    if (!requireTrue(validateTemporalFrame(frame) ==
                         TemporalFrameValidationError::MissingHdrColorView,
                     "missing HDR color view must be rejected explicitly")) {
        return false;
    }

    frame = completeFrame();
    frame.textures.depthView = {};
    if (!requireTrue(validateTemporalFrame(frame) ==
                         TemporalFrameValidationError::MissingDepthView,
                     "missing depth view must be rejected explicitly")) {
        return false;
    }

    frame = completeFrame();
    frame.textures.velocityView = {};
    if (!requireTrue(validateTemporalFrame(frame) ==
                         TemporalFrameValidationError::MissingVelocityView,
                     "missing velocity view must be rejected explicitly")) {
        return false;
    }

    frame = completeFrame();
    frame.textures.exposureView = {};
    if (!requireTrue(validateTemporalFrame(frame) ==
                         TemporalFrameValidationError::MissingExposureView,
                     "missing exposure view must be rejected explicitly")) {
        return false;
    }

    frame = completeFrame();
    frame.textures.reactiveMask = {};
    if (!requireTrue(validateTemporalFrame(frame) ==
                         TemporalFrameValidationError::MissingReactiveMask,
                     "missing reactive mask must be rejected explicitly")) {
        return false;
    }

    frame = completeFrame();
    frame.textures.reactiveMaskView = {};
    if (!requireTrue(validateTemporalFrame(frame) ==
                         TemporalFrameValidationError::MissingReactiveMaskView,
                     "missing reactive-mask view must be rejected explicitly")) {
        return false;
    }

    frame = completeFrame();
    frame.textures.transparencyMaskView = {};
    if (!requireTrue(validateTemporalFrame(frame) ==
                         TemporalFrameValidationError::MissingTransparencyMaskView,
                     "missing transparency-mask view must be rejected explicitly")) {
        return false;
    }

    frame = completeFrame();
    frame.textures.outputHdrColorView = {};
    return requireTrue(validateTemporalFrame(frame) ==
                           TemporalFrameValidationError::MissingOutputHdrColorView,
                       "missing HDR output view must be rejected explicitly");
}

bool testTemporalUpscaleDispatch() {
    const TemporalUpscalePass pass;
    TemporalFrameInput frame = completeFrame();
    frame.outputExtent = frame.renderExtent;

    const TemporalUpscaleResult nativeResult = pass.execute(
        TemporalUpscalerType::Native,
        frame);
    if (!requireTrue(nativeResult.succeeded(),
                     "native temporal reconstruction must accept matching extents") ||
        !requireTrue(nativeResult.outputHdrColor.index == frame.textures.hdrColor.index,
                     "native temporal reconstruction must preserve the HDR scene input") ||
        !requireTrue(nativeResult.outputHdrColorView.index ==
                         frame.textures.hdrColorView.index,
                     "native temporal reconstruction must preserve the HDR scene view") ||
        !requireTrue(nativeResult.outputExtent == frame.outputExtent,
                     "native temporal reconstruction must preserve the output extent")) {
        return false;
    }

    frame.outputExtent = {1920u, 1080u};
    if (!requireTrue(pass.execute(TemporalUpscalerType::Native, frame).status ==
                         TemporalUpscaleStatus::NativeExtentMismatch,
                     "native temporal reconstruction must reject mismatched extents")) {
        return false;
    }

    frame.outputExtent = frame.renderExtent;
    if (!requireTrue(pass.execute(TemporalUpscalerType::Fsr31, frame).status ==
                         TemporalUpscaleStatus::Fsr31Unavailable,
                     "FSR 3.1 must report unavailable before SDK initialization")) {
        return false;
    }

    frame.textures.exposure = {};
    return requireTrue(pass.execute(TemporalUpscalerType::Native, frame).status ==
                           TemporalUpscaleStatus::InvalidFrame,
                       "temporal dispatch must reject incomplete frame resources");
}

} // namespace

int main() {
    if (!testSettingsDefaults()) return 1;
    if (!testTemporalReconstructionSelection()) return 1;
    if (!testMotionVectorConvention()) return 1;
    if (!testTemporalReset()) return 1;
    if (!testFrameValidation()) return 1;
    if (!testTemporalUpscaleDispatch()) return 1;
    return 0;
}
