#include "renderer/contracts/TemporalFrameContract.h"
#include "renderer/core/RenderSettings.h"

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
    frame.textures.depth = textureHandle(2u);
    frame.textures.velocity = textureHandle(3u);
    frame.textures.exposure = textureHandle(4u);
    frame.textures.reactiveMask = textureHandle(5u);
    frame.textures.transparencyMask = textureHandle(6u);
    frame.textures.outputHdrColor = textureHandle(7u);
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
    frame.textures.reactiveMask = {};
    return requireTrue(validateTemporalFrame(frame) ==
                           TemporalFrameValidationError::MissingReactiveMask,
                       "missing reactive mask must be rejected explicitly");
}

} // namespace

int main() {
    if (!testSettingsDefaults()) return 1;
    if (!testMotionVectorConvention()) return 1;
    if (!testTemporalReset()) return 1;
    if (!testFrameValidation()) return 1;
    return 0;
}
