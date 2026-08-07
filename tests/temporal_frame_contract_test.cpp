#include "app/AppSettings.h"
#include "renderer/contracts/SceneIdentityContract.h"
#include "renderer/contracts/TemporalFrameContract.h"
#include "renderer/contracts/TemporalReprojectionContract.h"
#include "renderer/core/RenderSettings.h"
#include "renderer/passes/TemporalUpscalePass.h"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <type_traits>

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
    frame.extents = makeTemporalFrameExtents({1600u, 900u}, {1280u, 720u}, {640u, 360u}, {1920u, 1080u});
    frame.jitter.pixels = {0.25f, -0.5f};
    frame.jitter.projectionOffset = {frame.jitter.pixels.x / static_cast<float>(frame.extents.renderExtent.width),
                                     -frame.jitter.pixels.y / static_cast<float>(frame.extents.renderExtent.height)};
    frame.motionVectorScale = {static_cast<float>(frame.extents.renderExtent.width),
                               static_cast<float>(frame.extents.renderExtent.height)};
    frame.frameDeltaMilliseconds = 16.0f;
    frame.preExposure = 1.0f;
    frame.previousPreExposure = 1.0f;
    frame.cameraNear = 0.1f;
    frame.cameraFar = 500.0f;
    frame.verticalFovRadians = 1.22173048f;
    frame.resetReasons = temporalResetReasonBit(TemporalResetReason::None);
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
    return requireTrue(settings.type == TemporalUpscalerType::Native, "temporal upscaler must default to native") &&
           requireTrue(settings.quality == TemporalUpscaleQuality::Native,
                       "native upscaling must use native quality") &&
           requireTrue(!settings.dynamicResolutionEnabled, "dynamic resolution must require explicit enablement") &&
           requireTrue(!settings.debugVisualizationEnabled, "temporal debug visualization must default to disabled");
}

bool testRtgiNrdDependencyNormalization() {
    RenderSettings settings;
    settings.rtgi.enabled = true;
    settings.nrd.enabled = true;

    nlohmann::json encoded = app::serializeRenderSettings(settings);
    encoded["rtgi"]["enabled"] = false;

    RenderSettings decoded;
    std::string error;
    if (!requireTrue(app::deserializeRenderSettings(encoded, decoded, error),
                     "legacy RTGI/NRD settings must remain loadable")) {
        return false;
    }
    if (!requireTrue(!decoded.rtgi.enabled && !decoded.nrd.enabled, "loading disabled RTGI must also disable NRD")) {
        return false;
    }

    settings.rtgi.enabled = false;
    settings.nrd.enabled = true;
    return requireTrue(normalizeRenderSettingsDependencies(settings) && !settings.nrd.enabled,
                       "runtime settings normalization must disable NRD with RTGI");
}

bool testTemporalReconstructionSelection() {
    return requireTrue(usesTemporalProjectionJitter(TemporalUpscalerType::Native, true),
                       "native TAA must jitter scene rasterization") &&
           requireTrue(!usesTemporalProjectionJitter(TemporalUpscalerType::Native, false),
                       "native rendering without TAA must not jitter scene rasterization") &&
           requireTrue(usesTemporalProjectionJitter(TemporalUpscalerType::Fsr31, false),
                       "FSR 3.1 must jitter scene rasterization independently of native TAA") &&
           requireTrue(usesTemporalProjectionJitter(TemporalUpscalerType::Dlss, false),
                       "DLSS must jitter scene rasterization independently of native TAA") &&
           requireTrue(usesNativeTaaResolve(TemporalUpscalerType::Native, true),
                       "native TAA must execute only in Native reconstruction mode") &&
           requireTrue(!usesNativeTaaResolve(TemporalUpscalerType::Fsr31, true),
                       "FSR 3.1 must exclude the native TAA resolve");
}

bool testNrdValidationDebugViewSelection() {
    return requireTrue(isNrdValidationView(100), "NRD validation view must consume the optional validation output") &&
           requireTrue(!isNrdValidationView(101),
                       "NRD reprojection coverage view must not consume the optional validation output");
}

bool testTemporalUpscalerInputChangeSelection() {
    const DebugSettings baseline;
    DebugSettings changed = baseline;
    changed.viewMode = 98;
    if (!requireTrue(changesTemporalUpscalerInput(baseline, changed),
                     "deferred debug views must invalidate temporal upscaler history")) {
        return false;
    }

    changed = baseline;
    changed.deferredLightDebugMode = 1;
    if (!requireTrue(changesTemporalUpscalerInput(baseline, changed),
                     "lighting debug output must invalidate temporal upscaler history")) {
        return false;
    }

    changed = baseline;
    changed.reflectionDebugMode = 1;
    if (!requireTrue(changesTemporalUpscalerInput(baseline, changed),
                     "reflection debug output must invalidate temporal upscaler history")) {
        return false;
    }

    changed = baseline;
    changed.postprocessDebugMode = 1;
    return requireTrue(!changesTemporalUpscalerInput(baseline, changed),
                       "post-upscale debug output must preserve temporal upscaler history");
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

glm::vec2 applySkyHomography(const renderer::contracts::TemporalSkyReprojectionHomography& homography,
                             const glm::vec2 clipPosition) {
    const glm::vec3 source(clipPosition, 1.0f);
    const glm::vec3 projected(glm::dot(glm::vec3(homography.row0), source),
                              glm::dot(glm::vec3(homography.row1), source),
                              glm::dot(glm::vec3(homography.row2), source));
    return glm::vec2(projected) / projected.z;
}

glm::mat4 applyProjectionJitter(glm::mat4 projection, const glm::vec2 jitterOffset) {
    for (int column = 0; column < 4; ++column) {
        projection[column][0] += jitterOffset.x * projection[column][3];
        projection[column][1] += jitterOffset.y * projection[column][3];
    }
    return projection;
}

bool testSkyVelocityReprojection() {
    const glm::mat4 projection = glm::perspective(glm::radians(70.0f), 16.0f / 9.0f, 0.1f, 500.0f);
    const glm::vec3 forward = glm::normalize(glm::vec3(0.12f, -0.04f, -1.0f));
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::mat4 previousView =
        glm::lookAt(glm::vec3(-24.0f, 71.0f, 18.0f), glm::vec3(-24.0f, 71.0f, 18.0f) + forward, up);
    const glm::mat4 translatedView =
        glm::lookAt(glm::vec3(43.0f, 64.0f, -31.0f), glm::vec3(43.0f, 64.0f, -31.0f) + forward, up);
    const glm::vec2 jitterOffset(0.00031f, -0.00047f);
    const glm::mat4 translationOnly = renderer::contracts::makeTemporalSkyClipToPrevClip(
        projection, translatedView, projection, previousView, jitterOffset, true);
    const auto translationHomography = renderer::contracts::makeTemporalSkyReprojectionHomography(translationOnly);
    constexpr std::array<glm::vec2, 4u> kClipSamples{glm::vec2(-0.8f, -0.6f), glm::vec2(0.7f, -0.4f),
                                                     glm::vec2(-0.3f, 0.75f), glm::vec2(0.2f, 0.1f)};
    for (const glm::vec2 sample : kClipSamples) {
        const glm::vec2 reprojected = applySkyHomography(translationHomography, sample);
        if (!requireTrue(glm::length(reprojected - sample) < 1.0e-5f,
                         "camera translation must not produce velocity on infinite sky pixels")) {
            return false;
        }
    }

    const glm::vec3 previousForward = glm::normalize(glm::vec3(-0.16f, 0.06f, -1.0f));
    const glm::vec3 currentForward = glm::normalize(glm::vec3(0.19f, -0.03f, -1.0f));
    const glm::mat4 rotatedPreviousView =
        glm::lookAt(glm::vec3(-8.0f, 67.0f, 11.0f), glm::vec3(-8.0f, 67.0f, 11.0f) + previousForward, up);
    const glm::mat4 rotatedCurrentView =
        glm::lookAt(glm::vec3(35.0f, 59.0f, -27.0f), glm::vec3(35.0f, 59.0f, -27.0f) + currentForward, up);
    const glm::mat4 rotationReprojection = renderer::contracts::makeTemporalSkyClipToPrevClip(
        projection, rotatedCurrentView, projection, rotatedPreviousView, jitterOffset, true);
    const auto rotationHomography = renderer::contracts::makeTemporalSkyReprojectionHomography(rotationReprojection);
    const glm::vec3 worldDirection = glm::normalize(glm::vec3(0.02f, 0.01f, -1.0f));
    const glm::mat4 jitteredProjection = applyProjectionJitter(projection, jitterOffset);
    const glm::vec4 currentClip = jitteredProjection * rotatedCurrentView * glm::vec4(worldDirection, 0.0f);
    const glm::vec4 expectedPreviousClip = jitteredProjection * rotatedPreviousView * glm::vec4(worldDirection, 0.0f);
    const glm::vec2 currentNdc = glm::vec2(currentClip) / currentClip.w;
    const glm::vec2 expectedPreviousNdc = glm::vec2(expectedPreviousClip) / expectedPreviousClip.w;
    const glm::vec2 reprojectedPreviousNdc = applySkyHomography(rotationHomography, currentNdc);
    return requireTrue(glm::length(reprojectedPreviousNdc - expectedPreviousNdc) < 1.0e-5f,
                       "sky reprojection must preserve rotational camera motion and current jitter");
}

bool testTemporalExtents() {
    TemporalFrameExtents extents =
        makeTemporalFrameExtents({1920u, 1080u}, {1280u, 720u}, {640u, 360u}, {2560u, 1440u});
    if (!requireTrue(extents.isValid(), "separate resource, render, signal, and output extents must validate") ||
        !requireTrue(extents.renderRect == TemporalRect{0u, 0u, 1280u, 720u},
                     "render rect must match the active render extent") ||
        !requireTrue(extents.signalRect == TemporalRect{0u, 0u, 640u, 360u},
                     "signal rect must match the active signal extent")) {
        return false;
    }

    extents.renderRect = {64u, 32u, 1280u, 720u};
    extents.signalRect = {16u, 8u, 640u, 360u};
    if (!requireTrue(extents.isValid(), "non-zero active-rect origins inside the resource must validate")) {
        return false;
    }

    extents.renderRect = {1000u, 500u, 1280u, 720u};
    return requireTrue(!extents.isValid(), "active rectangles extending beyond the resource must be rejected");
}

bool testTemporalReset() {
    const TemporalFrameExtents previous =
        makeTemporalFrameExtents({1920u, 1080u}, {1280u, 720u}, {640u, 360u}, {2560u, 1440u});
    const TemporalResetReasons noExplicitReasons = temporalResetReasonBit(TemporalResetReason::None);

    const TemporalResetReasons firstFrame =
        evaluateTemporalResetReasons(false, previous, previous, noExplicitReasons, {});
    if (!requireTrue(requiresTemporalReset(firstFrame) &&
                         hasTemporalResetReason(firstFrame, TemporalResetReason::FirstFrame),
                     "missing previous frame must report FirstFrame")) {
        return false;
    }

    const TemporalResetReasons methodChange = evaluateTemporalResetReasons(
        false, previous, previous, temporalResetReasonBit(TemporalResetReason::Method), {});
    if (!requireTrue(hasTemporalResetReason(methodChange, TemporalResetReason::Method) &&
                         !hasTemporalResetReason(methodChange, TemporalResetReason::FirstFrame),
                     "known invalidation causes must not be relabeled as FirstFrame")) {
        return false;
    }

    const TemporalResetReasons stable = evaluateTemporalResetReasons(true, previous, previous, noExplicitReasons, {});
    if (!requireTrue(!requiresTemporalReset(stable), "stable extents without explicit causes must preserve history")) {
        return false;
    }

    TemporalFrameExtents changed = previous;
    changed.resourceExtent = {2048u, 1152u};
    TemporalResetReasons reasons = evaluateTemporalResetReasons(true, previous, changed, noExplicitReasons, {});
    if (!requireTrue(hasTemporalResetReason(reasons, TemporalResetReason::ResourceExtent),
                     "resource allocation changes must report ResourceExtent")) {
        return false;
    }

    changed = previous;
    changed.outputExtent = {3840u, 2160u};
    reasons = evaluateTemporalResetReasons(true, previous, changed, noExplicitReasons, {});
    if (!requireTrue(hasTemporalResetReason(reasons, TemporalResetReason::ResourceExtent),
                     "output allocation changes must report ResourceExtent")) {
        return false;
    }

    changed = previous;
    changed.renderExtent = {960u, 540u};
    changed.renderRect = {32u, 24u, 960u, 540u};
    reasons = evaluateTemporalResetReasons(true, previous, changed, noExplicitReasons, {});
    if (!requireTrue(hasTemporalResetReason(reasons, TemporalResetReason::ActiveRect),
                     "unaccepted render active-rect changes must report ActiveRect")) {
        return false;
    }
    reasons = evaluateTemporalResetReasons(true, previous, changed, noExplicitReasons, {true, false});
    if (!requireTrue(!requiresTemporalReset(reasons),
                     "consumers may preserve history across accepted render-rect changes")) {
        return false;
    }

    changed = previous;
    changed.signalExtent = {480u, 270u};
    changed.signalRect = {8u, 4u, 480u, 270u};
    reasons = evaluateTemporalResetReasons(true, previous, changed, noExplicitReasons, {false, true});
    if (!requireTrue(!requiresTemporalReset(reasons),
                     "consumers may preserve history across accepted signal-rect changes")) {
        return false;
    }

    const TemporalResetReasons explicitReasons = TemporalResetReason::CameraCut | TemporalResetReason::AssetRevision;
    reasons = evaluateTemporalResetReasons(true, previous, previous, explicitReasons, {true, true});
    if (!requireTrue(hasTemporalResetReason(reasons, TemporalResetReason::CameraCut) &&
                         hasTemporalResetReason(reasons, TemporalResetReason::AssetRevision),
                     "explicit reset causes must remain independently observable")) {
        return false;
    }

    TemporalResetReasons describedReasons = temporalResetReasonBit(TemporalResetReason::None);
    for (const TemporalResetReasonDescriptor& descriptor : temporalResetReasonDescriptors()) {
        if (!requireTrue(descriptor.reason != TemporalResetReason::None && descriptor.stableId != nullptr &&
                             descriptor.stableId[0] != '\0' &&
                             !hasTemporalResetReason(describedReasons, descriptor.reason),
                         "reset descriptors must contain unique non-zero reasons and stable IDs")) {
            return false;
        }
        describedReasons |= temporalResetReasonBit(descriptor.reason);
    }
    if (!requireTrue(describedReasons == ((1u << 15u) - 1u),
                     "reset descriptors must cover every declared temporal reset bit")) {
        return false;
    }

    // Owner-scoped filtering keeps scene-domain histories alive when only the
    // denoiser/upscaler signal chain changes.
    const TemporalResetReasons denoiserOnly = temporalResetReasonBit(TemporalResetReason::DenoiserMethod);
    const TemporalResetReasons upscalerInputOnly = temporalResetReasonBit(TemporalResetReason::UpscalerInput);
    const TemporalResetReasons preExposureOnly = temporalResetReasonBit(TemporalResetReason::PreExposure);
    const TemporalResetReasons assetRevisionOnly = temporalResetReasonBit(TemporalResetReason::AssetRevision);
    const TemporalResetReasons cameraCut = temporalResetReasonBit(TemporalResetReason::CameraCut);
    if (!requireTrue(ownerRequiresTemporalReset(TemporalHistoryOwner::NrdDiffuse, denoiserOnly) &&
                         !ownerRequiresTemporalReset(TemporalHistoryOwner::ScreenSpace, denoiserOnly) &&
                         !ownerRequiresTemporalReset(TemporalHistoryOwner::Clouds, denoiserOnly) &&
                         !ownerRequiresTemporalReset(TemporalHistoryOwner::Volumetrics, denoiserOnly) &&
                         ownerRequiresTemporalReset(TemporalHistoryOwner::Upscaler, denoiserOnly),
                     "a denoiser method change must reset NRD and its temporal upscaler consumer")) {
        return false;
    }
    if (!requireTrue(!ownerRequiresTemporalReset(TemporalHistoryOwner::NrdDiffuse, upscalerInputOnly) &&
                         !ownerRequiresTemporalReset(TemporalHistoryOwner::ScreenSpace, upscalerInputOnly) &&
                         !ownerRequiresTemporalReset(TemporalHistoryOwner::Clouds, upscalerInputOnly) &&
                         !ownerRequiresTemporalReset(TemporalHistoryOwner::Volumetrics, upscalerInputOnly) &&
                         ownerRequiresTemporalReset(TemporalHistoryOwner::Upscaler, upscalerInputOnly),
                     "an upscaler input change must only reset the temporal upscaler")) {
        return false;
    }
    if (!requireTrue(!ownerRequiresTemporalReset(TemporalHistoryOwner::NrdDiffuse, preExposureOnly) &&
                         ownerRequiresTemporalReset(TemporalHistoryOwner::ScreenSpace, preExposureOnly) &&
                         ownerRequiresTemporalReset(TemporalHistoryOwner::Upscaler, preExposureOnly),
                     "a pre-exposure step must reset scene-domain owners but never NRD")) {
        return false;
    }
    if (!requireTrue(ownerRequiresTemporalReset(TemporalHistoryOwner::NrdDiffuse, assetRevisionOnly) &&
                         ownerRequiresTemporalReset(TemporalHistoryOwner::ScreenSpace, assetRevisionOnly) &&
                         ownerRequiresTemporalReset(TemporalHistoryOwner::Upscaler, assetRevisionOnly),
                     "an asset edit must reset NRD and scene-domain histories")) {
        return false;
    }
    return requireTrue(ownerRequiresTemporalReset(TemporalHistoryOwner::NrdDiffuse, cameraCut) &&
                           ownerRequiresTemporalReset(TemporalHistoryOwner::ScreenSpace, cameraCut) &&
                           ownerRequiresTemporalReset(TemporalHistoryOwner::Clouds, cameraCut),
                       "world and camera causes must reset every history owner");
}

bool testStableSceneIdentity() {
    using renderer::contracts::allocateStableSceneId;
    using renderer::contracts::kVoxelMaterialIdCapacity;
    using renderer::contracts::StableGeometryId;
    using renderer::contracts::StableGeometryIdTag;
    using renderer::contracts::StableMaterialId;
    using renderer::contracts::stableMaterialIdForVoxelLayer;
    using renderer::contracts::StableMaterialIdTag;
    using renderer::contracts::StableObjectId;
    using renderer::contracts::StableObjectIdTag;

    static_assert(!std::is_same_v<StableObjectId, StableMaterialId>);
    static_assert(!std::is_same_v<StableObjectId, StableGeometryId>);
    static_assert(!std::is_convertible_v<StableObjectId, StableMaterialId>);

    const StableObjectId invalidObject;
    const StableObjectId object{42u};
    const StableObjectId sameObject{42u};
    const StableObjectId differentObject{43u};
    const StableMaterialId material{42u};
    const StableGeometryId geometry{7u};
    const auto firstObject = allocateStableSceneId<StableObjectIdTag>();
    const auto secondObject = allocateStableSceneId<StableObjectIdTag>();
    const auto firstMaterial = allocateStableSceneId<StableMaterialIdTag>();
    const auto secondMaterial = allocateStableSceneId<StableMaterialIdTag>();
    const auto firstGeometry = allocateStableSceneId<StableGeometryIdTag>();
    const auto secondGeometry = allocateStableSceneId<StableGeometryIdTag>();
    constexpr uint32_t kAnimatedBaseLayer = 37u;
    constexpr uint32_t kAnimationFrameLayer = kAnimatedBaseLayer + 5u;
    const auto animatedMaterial = stableMaterialIdForVoxelLayer(kAnimatedBaseLayer);
    const auto lastVoxelMaterial = stableMaterialIdForVoxelLayer(kVoxelMaterialIdCapacity - 1u);
    const auto outOfRangeVoxelMaterial = stableMaterialIdForVoxelLayer(kVoxelMaterialIdCapacity);

    return requireTrue(!invalidObject.isValid(), "zero must remain the explicit invalid stable object ID") &&
           requireTrue(object.isValid() && material.isValid() && geometry.isValid(),
                       "non-zero stable scene IDs must be valid") &&
           requireTrue(object == sameObject && object != differentObject,
                       "stable IDs must compare by their preserved numeric identity") &&
           requireTrue(object.value == material.value,
                       "different ID domains may reuse numeric values without type aliasing") &&
           requireTrue(firstObject.has_value() && secondObject.has_value() && firstMaterial.has_value() &&
                           secondMaterial.has_value() && firstGeometry.has_value() && secondGeometry.has_value(),
                       "stable scene ID allocation must return valid non-zero values") &&
           requireTrue(secondObject->value == firstObject->value + 1u &&
                           secondMaterial->value == firstMaterial->value + 1u &&
                           secondGeometry->value == firstGeometry->value + 1u,
                       "stable scene IDs must increase monotonically without reuse") &&
           requireTrue(firstObject->value == firstGeometry->value &&
                           firstMaterial->value == kVoxelMaterialIdCapacity + 1u,
                       "strong identity domains must own independent allocation sequences") &&
           requireTrue(animatedMaterial.has_value() && animatedMaterial->value == kAnimatedBaseLayer + 1u &&
                           animatedMaterial->value != kAnimationFrameLayer + 1u,
                       "voxel material identity must derive from the base layer only") &&
           requireTrue(lastVoxelMaterial.has_value() && lastVoxelMaterial->value == kVoxelMaterialIdCapacity &&
                           !outOfRangeVoxelMaterial.has_value(),
                       "voxel material IDs must exactly cover the reserved layer range");
}

bool testFrameValidation() {
    TemporalFrameInput frame = completeFrame();
    if (!requireTrue(!validateTemporalFrame(frame).has_value(), "complete temporal frame must validate")) {
        return false;
    }

    frame.extents.resourceExtent = {};
    if (!requireTrue(validateTemporalFrame(frame) == TemporalFrameValidationError::InvalidResourceExtent,
                     "zero resource extent must be rejected")) {
        return false;
    }

    frame = completeFrame();
    frame.extents.renderExtent = {};
    if (!requireTrue(validateTemporalFrame(frame) == TemporalFrameValidationError::InvalidRenderExtent,
                     "zero render extent must be rejected")) {
        return false;
    }

    frame = completeFrame();
    frame.extents.signalRect = {1500u, 800u, 640u, 360u};
    if (!requireTrue(validateTemporalFrame(frame) == TemporalFrameValidationError::InvalidSignalRect,
                     "signal rectangles outside the resource must be rejected")) {
        return false;
    }

    frame = completeFrame();
    frame.motionVectorScale.x = 0.0f;
    if (!requireTrue(validateTemporalFrame(frame) == TemporalFrameValidationError::InvalidMotionVectorScale,
                     "motion-vector scale must match the render extent")) {
        return false;
    }

    frame = completeFrame();
    frame.previousPreExposure = std::numeric_limits<float>::infinity();
    if (!requireTrue(validateTemporalFrame(frame) == TemporalFrameValidationError::InvalidPreExposure,
                     "current and previous pre-exposure must remain finite and positive")) {
        return false;
    }

    frame = completeFrame();
    frame.clipToPrevClip[0][0] = std::numeric_limits<float>::quiet_NaN();
    if (!requireTrue(validateTemporalFrame(frame) == TemporalFrameValidationError::InvalidCameraMatrices,
                     "non-finite camera matrices must be rejected")) {
        return false;
    }

    frame = completeFrame();
    frame.textures.hdrColorView = {};
    if (!requireTrue(validateTemporalFrame(frame) == TemporalFrameValidationError::MissingHdrColorView,
                     "missing HDR color view must be rejected explicitly")) {
        return false;
    }

    frame = completeFrame();
    frame.textures.depthView = {};
    if (!requireTrue(validateTemporalFrame(frame) == TemporalFrameValidationError::MissingDepthView,
                     "missing depth view must be rejected explicitly")) {
        return false;
    }

    frame = completeFrame();
    frame.textures.velocityView = {};
    if (!requireTrue(validateTemporalFrame(frame) == TemporalFrameValidationError::MissingVelocityView,
                     "missing velocity view must be rejected explicitly")) {
        return false;
    }

    frame = completeFrame();
    frame.textures.exposureView = {};
    if (!requireTrue(validateTemporalFrame(frame) == TemporalFrameValidationError::MissingExposureView,
                     "missing exposure view must be rejected explicitly")) {
        return false;
    }

    frame = completeFrame();
    frame.textures.reactiveMask = {};
    if (!requireTrue(validateTemporalFrame(frame) == TemporalFrameValidationError::MissingReactiveMask,
                     "missing reactive mask must be rejected explicitly")) {
        return false;
    }

    frame = completeFrame();
    frame.textures.reactiveMaskView = {};
    if (!requireTrue(validateTemporalFrame(frame) == TemporalFrameValidationError::MissingReactiveMaskView,
                     "missing reactive-mask view must be rejected explicitly")) {
        return false;
    }

    frame = completeFrame();
    frame.textures.transparencyMaskView = {};
    if (!requireTrue(validateTemporalFrame(frame) == TemporalFrameValidationError::MissingTransparencyMaskView,
                     "missing transparency-mask view must be rejected explicitly")) {
        return false;
    }

    frame = completeFrame();
    frame.textures.outputHdrColorView = {};
    return requireTrue(validateTemporalFrame(frame) == TemporalFrameValidationError::MissingOutputHdrColorView,
                       "missing HDR output view must be rejected explicitly");
}

bool testTemporalUpscaleDispatch() {
    TemporalUpscalePass pass;
    UpscaleSettings settings;
    TemporalFrameInput frame = completeFrame();
    frame.extents.outputExtent = frame.extents.renderExtent;

    const TemporalUpscaleResult nativeResult = pass.execute(settings, frame);
    if (!requireTrue(nativeResult.succeeded(), "native temporal reconstruction must accept matching extents") ||
        !requireTrue(nativeResult.outputHdrColor.index == frame.textures.hdrColor.index,
                     "native temporal reconstruction must preserve the HDR scene input") ||
        !requireTrue(nativeResult.outputHdrColorView.index == frame.textures.hdrColorView.index,
                     "native temporal reconstruction must preserve the HDR scene view") ||
        !requireTrue(nativeResult.outputExtent == frame.extents.outputExtent,
                     "native temporal reconstruction must preserve the output extent")) {
        return false;
    }

    frame.extents.outputExtent = {1920u, 1080u};
    if (!requireTrue(pass.execute(settings, frame).status == TemporalUpscaleStatus::NativeExtentMismatch,
                     "native temporal reconstruction must reject mismatched extents")) {
        return false;
    }

    frame.extents.outputExtent = frame.extents.renderExtent;
    settings.type = TemporalUpscalerType::Fsr31;
    if (!requireTrue(pass.execute(settings, frame).status == TemporalUpscaleStatus::Fsr31Unavailable,
                     "FSR 3.1 must report unavailable before SDK initialization")) {
        return false;
    }

    settings.type = TemporalUpscalerType::Dlss;
    if (!requireTrue(pass.execute(settings, frame).status == TemporalUpscaleStatus::DlssUnavailable,
                     "DLSS must report unavailable before Streamline initialization")) {
        return false;
    }

    frame.textures.exposure = {};
    settings.type = TemporalUpscalerType::Native;
    return requireTrue(pass.execute(settings, frame).status == TemporalUpscaleStatus::InvalidFrame,
                       "temporal dispatch must reject incomplete frame resources");
}

} // namespace

int main() {
    if (!testSettingsDefaults())
        return 1;
    if (!testRtgiNrdDependencyNormalization())
        return 1;
    if (!testTemporalReconstructionSelection())
        return 1;
    if (!testNrdValidationDebugViewSelection())
        return 1;
    if (!testTemporalUpscalerInputChangeSelection())
        return 1;
    if (!testMotionVectorConvention())
        return 1;
    if (!testSkyVelocityReprojection())
        return 1;
    if (!testTemporalExtents())
        return 1;
    if (!testTemporalReset())
        return 1;
    if (!testStableSceneIdentity())
        return 1;
    if (!testFrameValidation())
        return 1;
    if (!testTemporalUpscaleDispatch())
        return 1;
    return 0;
}
