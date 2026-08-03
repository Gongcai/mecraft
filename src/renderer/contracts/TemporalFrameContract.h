#ifndef MECRAFT_TEMPORAL_FRAME_CONTRACT_H
#define MECRAFT_TEMPORAL_FRAME_CONTRACT_H

#include "renderer/rhi/RhiHandles.h"

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

/// Two-dimensional pixel extent shared by temporal reconstruction stages.
struct TemporalExtent {
    uint32_t width = 0u;
    uint32_t height = 0u;

    /// Determine whether both pixel dimensions contain at least one pixel.
    /// @return True when width and height are both non-zero.
    [[nodiscard]] constexpr bool isValid() const { return width > 0u && height > 0u; }
};

/// Compare two pixel extents component by component.
/// @param lhs Left extent.
/// @param rhs Right extent.
/// @return True when both width and height are equal.
[[nodiscard]] constexpr bool operator==(const TemporalExtent lhs, const TemporalExtent rhs) {
    return lhs.width == rhs.width && lhs.height == rhs.height;
}

/// Compare two pixel extents for inequality.
/// @param lhs Left extent.
/// @param rhs Right extent.
/// @return True when either width or height differs.
[[nodiscard]] constexpr bool operator!=(const TemporalExtent lhs, const TemporalExtent rhs) {
    return !(lhs == rhs);
}

/// Active pixel rectangle inside a temporal resource.
struct TemporalRect {
    uint32_t x = 0u;
    uint32_t y = 0u;
    uint32_t width = 0u;
    uint32_t height = 0u;

    /// Check that the rectangle is non-empty and completely contained by a resource.
    /// @param resourceExtent Allocated resource dimensions containing the rectangle.
    /// @return True when every pixel addressed by the rectangle exists in the resource.
    [[nodiscard]] constexpr bool isValidWithin(const TemporalExtent resourceExtent) const {
        return width > 0u && height > 0u && x <= resourceExtent.width && y <= resourceExtent.height &&
               width <= resourceExtent.width - x && height <= resourceExtent.height - y;
    }
};

/// Compare two active rectangles component by component.
/// @param lhs Left rectangle.
/// @param rhs Right rectangle.
/// @return True when origin and dimensions are equal.
[[nodiscard]] constexpr bool operator==(const TemporalRect lhs, const TemporalRect rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width && lhs.height == rhs.height;
}

/// Compare two active rectangles for inequality.
/// @param lhs Left rectangle.
/// @param rhs Right rectangle.
/// @return True when origin or dimensions differ.
[[nodiscard]] constexpr bool operator!=(const TemporalRect lhs, const TemporalRect rhs) {
    return !(lhs == rhs);
}

/// Complete resource and active-area dimensions shared by temporal consumers.
struct TemporalFrameExtents {
    /// Allocated dimensions of render-domain temporal resources.
    TemporalExtent resourceExtent;
    /// Active scene rasterization dimensions for the current frame.
    TemporalExtent renderExtent;
    /// Active dimensions of RTGI, reflection, or denoising signals.
    TemporalExtent signalExtent;
    /// Presentation dimensions after temporal reconstruction.
    TemporalExtent outputExtent;
    /// Active rasterization region inside resourceExtent.
    TemporalRect renderRect;
    /// Active signal region inside resourceExtent.
    TemporalRect signalRect;

    /// Validate relationships between allocated resources, active dimensions, and rectangles.
    /// @return True when every extent is valid and both active rectangles match their domains.
    [[nodiscard]] constexpr bool isValid() const {
        return resourceExtent.isValid() && renderExtent.isValid() && signalExtent.isValid() && outputExtent.isValid() &&
               renderRect.isValidWithin(resourceExtent) && signalRect.isValidWithin(resourceExtent) &&
               renderRect.width == renderExtent.width && renderRect.height == renderExtent.height &&
               signalRect.width == signalExtent.width && signalRect.height == signalExtent.height;
    }
};

/// Create a temporal extent contract whose render and signal rectangles begin at the origin.
/// @param resourceExtent Allocated dimensions shared by temporal work resources.
/// @param renderExtent Active scene rasterization dimensions for this frame.
/// @param signalExtent Active dimensions of the temporal signal consumed this frame.
/// @param outputExtent Presentation dimensions after reconstruction.
/// @return A complete extent contract with origin-aligned active rectangles.
[[nodiscard]] constexpr TemporalFrameExtents makeTemporalFrameExtents(const TemporalExtent resourceExtent,
                                                                      const TemporalExtent renderExtent,
                                                                      const TemporalExtent signalExtent,
                                                                      const TemporalExtent outputExtent) {
    return {resourceExtent,
            renderExtent,
            signalExtent,
            outputExtent,
            {0u, 0u, renderExtent.width, renderExtent.height},
            {0u, 0u, signalExtent.width, signalExtent.height}};
}

/// Stable causes that invalidate temporal history or require temporal resource replacement.
enum class TemporalResetReason : uint32_t {
    None = 0u,
    FirstFrame = 1u << 0u,
    CameraCut = 1u << 1u,
    Teleport = 1u << 2u,
    WorldReload = 1u << 3u,
    WorldOriginShift = 1u << 4u,
    ResourceExtent = 1u << 5u,
    PipelineMode = 1u << 6u,
    Method = 1u << 7u,
    Projection = 1u << 8u,
    FrameDiscontinuity = 1u << 9u,
    AssetRevision = 1u << 10u,
    ActiveRect = 1u << 11u
};

using TemporalResetReasons = uint32_t;

/// Convert one reset reason into its bitmask representation.
/// @param reason Reset cause whose bit is requested.
/// @return Bitmask containing only the selected cause, or zero for None.
[[nodiscard]] constexpr TemporalResetReasons temporalResetReasonBit(const TemporalResetReason reason) {
    return static_cast<TemporalResetReasons>(reason);
}

/// Combine two reset-reason enum values.
/// @param lhs Left reset cause.
/// @param rhs Right reset cause.
/// @return Bitmask containing both causes.
[[nodiscard]] constexpr TemporalResetReasons operator|(const TemporalResetReason lhs, const TemporalResetReason rhs) {
    return temporalResetReasonBit(lhs) | temporalResetReasonBit(rhs);
}

/// Add one reset-reason enum value to an existing bitmask.
/// @param lhs Existing reset-reason bitmask.
/// @param rhs Reset cause to add.
/// @return Bitmask containing the previous and added causes.
[[nodiscard]] constexpr TemporalResetReasons operator|(const TemporalResetReasons lhs, const TemporalResetReason rhs) {
    return lhs | temporalResetReasonBit(rhs);
}

/// Test whether a reset-reason bitmask contains one cause.
/// @param reasons Reset-reason bitmask to inspect.
/// @param reason Cause whose presence is queried.
/// @return True when the selected cause is present.
[[nodiscard]] constexpr bool hasTemporalResetReason(const TemporalResetReasons reasons,
                                                    const TemporalResetReason reason) {
    return (reasons & temporalResetReasonBit(reason)) != 0u;
}

/// Determine whether any cause requires temporal history invalidation.
/// @param reasons Reset-reason bitmask to inspect.
/// @return True when at least one reset cause is present.
[[nodiscard]] constexpr bool requiresTemporalReset(const TemporalResetReasons reasons) {
    return reasons != temporalResetReasonBit(TemporalResetReason::None);
}

/// Declares which active-area changes a temporal history implementation can preserve.
struct TemporalHistoryPolicy {
    /// True when render active-area changes preserve compatible history coordinates.
    bool acceptsRenderRectChange = false;
    /// True when signal active-area changes preserve compatible history coordinates.
    bool acceptsSignalRectChange = false;
};

/// Machine-readable metadata for one reset-reason bit.
struct TemporalResetReasonDescriptor {
    /// Reset cause represented by this descriptor.
    TemporalResetReason reason = TemporalResetReason::None;
    /// Stable machine-readable identifier used by diagnostics and tests.
    const char* stableId = nullptr;
};

/// Fixed metadata table for every observable non-zero reset cause.
inline constexpr std::array<TemporalResetReasonDescriptor, 12u> kTemporalResetReasonDescriptors{
    {{TemporalResetReason::FirstFrame, "first_frame"},
     {TemporalResetReason::CameraCut, "camera_cut"},
     {TemporalResetReason::Teleport, "teleport"},
     {TemporalResetReason::WorldReload, "world_reload"},
     {TemporalResetReason::WorldOriginShift, "world_origin_shift"},
     {TemporalResetReason::ResourceExtent, "resource_extent"},
     {TemporalResetReason::PipelineMode, "pipeline_mode"},
     {TemporalResetReason::Method, "method"},
     {TemporalResetReason::Projection, "projection"},
     {TemporalResetReason::FrameDiscontinuity, "frame_discontinuity"},
     {TemporalResetReason::AssetRevision, "asset_revision"},
     {TemporalResetReason::ActiveRect, "active_rect"}}};

/// Return the fixed table of observable temporal reset causes.
/// @return Compile-time table containing every non-zero reason and its stable identifier.
[[nodiscard]] constexpr const std::array<TemporalResetReasonDescriptor, 12u>& temporalResetReasonDescriptors() {
    return kTemporalResetReasonDescriptors;
}

/// Evaluate all reset causes visible to the shared frame contract.
/// @param hasPreviousFrame Whether previous-frame matrices and resources are valid.
/// @param previousExtents Previous frame resource and active-area dimensions.
/// @param currentExtents Current frame resource and active-area dimensions.
/// @param explicitReasons Event-driven reset causes supplied by scene and asset systems.
/// @param policy Active-area changes accepted by the temporal history consumer.
/// @return Bitmask containing every reason that invalidates this consumer's history.
[[nodiscard]] constexpr TemporalResetReasons evaluateTemporalResetReasons(const bool hasPreviousFrame,
                                                                          const TemporalFrameExtents& previousExtents,
                                                                          const TemporalFrameExtents& currentExtents,
                                                                          TemporalResetReasons explicitReasons,
                                                                          const TemporalHistoryPolicy policy) {
    if (!hasPreviousFrame && !requiresTemporalReset(explicitReasons)) {
        explicitReasons = explicitReasons | TemporalResetReason::FirstFrame;
    }
    if (hasPreviousFrame && (previousExtents.resourceExtent != currentExtents.resourceExtent ||
                             previousExtents.outputExtent != currentExtents.outputExtent)) {
        explicitReasons = explicitReasons | TemporalResetReason::ResourceExtent;
    }
    if (hasPreviousFrame && !policy.acceptsRenderRectChange &&
        previousExtents.renderRect != currentExtents.renderRect) {
        explicitReasons = explicitReasons | TemporalResetReason::ActiveRect;
    }
    if (hasPreviousFrame && !policy.acceptsSignalRectChange &&
        previousExtents.signalRect != currentExtents.signalRect) {
        explicitReasons = explicitReasons | TemporalResetReason::ActiveRect;
    }
    return explicitReasons;
}

/// Describes the single motion-vector convention produced by Mecraft.
struct TemporalMotionVectorConvention {
    /// Motion vectors store currentTextureUv - previousTextureUv.
    static constexpr bool currentMinusPrevious = true;
    /// Motion vectors are normalized in native texture UV coordinates.
    static constexpr bool normalizedTextureCoordinates = true;
    /// Native texture UV Y increases from the top edge toward the bottom edge.
    static constexpr bool positiveYDown = true;
    /// RG16F storage may contain rejection sentinels outside the visible UV range.
    static constexpr float minimumStoredValue = -2.0f;
    static constexpr float maximumStoredValue = 2.0f;
};

/// Frame-scoped jitter expressed in both SDK-facing pixels and projection space.
struct TemporalJitter {
    /// Pixel offset in the native texture domain, where positive Y points down.
    glm::vec2 pixels = glm::vec2(0.0f);
    /// Projection offset, where positive Y follows the bottom-left clip UV domain.
    glm::vec2 projectionOffset = glm::vec2(0.0f);
};

/// RHI resources consumed and produced by one temporal reconstruction dispatch.
struct TemporalFrameTextures {
    RhiTextureHandle hdrColor;
    RhiTextureViewHandle hdrColorView;
    RhiTextureHandle depth;
    RhiTextureViewHandle depthView;
    RhiTextureHandle velocity;
    RhiTextureViewHandle velocityView;
    RhiTextureHandle exposure;
    RhiTextureViewHandle exposureView;
    RhiTextureHandle reactiveMask;
    RhiTextureViewHandle reactiveMaskView;
    RhiTextureHandle transparencyMask;
    RhiTextureViewHandle transparencyMaskView;
    RhiTextureHandle outputHdrColor;
    RhiTextureViewHandle outputHdrColorView;
};

/// Backend-independent input contract shared by native, FSR 3.1, and DLSS paths.
struct TemporalFrameInput {
    uint64_t frameIndex = 0u;
    TemporalFrameExtents extents;
    TemporalResetReasons resetReasons = temporalResetReasonBit(TemporalResetReason::FirstFrame);
    TemporalJitter jitter;
    glm::vec2 motionVectorScale = glm::vec2(0.0f);
    float frameDeltaMilliseconds = 0.0f;
    float preExposure = 1.0f;
    float previousPreExposure = 1.0f;
    float cameraNear = 0.0f;
    float cameraFar = 0.0f;
    float verticalFovRadians = 0.0f;
    float cameraAspectRatio = 1.0f;
    glm::mat4 cameraViewToClip = glm::mat4(1.0f);
    glm::mat4 clipToCameraView = glm::mat4(1.0f);
    glm::mat4 clipToPrevClip = glm::mat4(1.0f);
    glm::mat4 prevClipToClip = glm::mat4(1.0f);
    glm::vec3 cameraPosition = glm::vec3(0.0f);
    glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 cameraRight = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 cameraForward = glm::vec3(0.0f, 0.0f, -1.0f);
    bool depthInverted = false;
    bool renderingGameFrames = true;
    TemporalFrameTextures textures;
};

enum class TemporalFrameValidationError {
    InvalidResourceExtent,
    InvalidRenderExtent,
    InvalidSignalExtent,
    InvalidOutputExtent,
    InvalidRenderRect,
    InvalidSignalRect,
    InvalidJitter,
    InvalidMotionVectorScale,
    InvalidFrameDelta,
    InvalidPreExposure,
    InvalidCameraRange,
    InvalidVerticalFov,
    InvalidCameraAspectRatio,
    InvalidCameraMatrices,
    InvalidCameraVectors,
    MissingHdrColor,
    MissingHdrColorView,
    MissingDepth,
    MissingDepthView,
    MissingVelocity,
    MissingVelocityView,
    MissingExposure,
    MissingExposureView,
    MissingReactiveMask,
    MissingReactiveMaskView,
    MissingTransparencyMask,
    MissingTransparencyMaskView,
    MissingOutputHdrColor,
    MissingOutputHdrColorView
};

/// Validate that a temporal frame is complete before it reaches a native SDK bridge.
/// @param frame Frame contract populated by the renderer.
/// @return Empty when valid, otherwise the first violated contract requirement.
[[nodiscard]] inline std::optional<TemporalFrameValidationError>
validateTemporalFrame(const TemporalFrameInput& frame) {
    if (!frame.extents.resourceExtent.isValid()) {
        return TemporalFrameValidationError::InvalidResourceExtent;
    }
    if (!frame.extents.renderExtent.isValid()) {
        return TemporalFrameValidationError::InvalidRenderExtent;
    }
    if (!frame.extents.signalExtent.isValid()) {
        return TemporalFrameValidationError::InvalidSignalExtent;
    }
    if (!frame.extents.outputExtent.isValid()) {
        return TemporalFrameValidationError::InvalidOutputExtent;
    }
    if (!frame.extents.renderRect.isValidWithin(frame.extents.resourceExtent) ||
        frame.extents.renderRect.width != frame.extents.renderExtent.width ||
        frame.extents.renderRect.height != frame.extents.renderExtent.height) {
        return TemporalFrameValidationError::InvalidRenderRect;
    }
    if (!frame.extents.signalRect.isValidWithin(frame.extents.resourceExtent) ||
        frame.extents.signalRect.width != frame.extents.signalExtent.width ||
        frame.extents.signalRect.height != frame.extents.signalExtent.height) {
        return TemporalFrameValidationError::InvalidSignalRect;
    }
    if (!std::isfinite(frame.jitter.pixels.x) || !std::isfinite(frame.jitter.pixels.y) ||
        !std::isfinite(frame.jitter.projectionOffset.x) || !std::isfinite(frame.jitter.projectionOffset.y)) {
        return TemporalFrameValidationError::InvalidJitter;
    }
    if (!std::isfinite(frame.motionVectorScale.x) || !std::isfinite(frame.motionVectorScale.y) ||
        frame.motionVectorScale.x != static_cast<float>(frame.extents.renderExtent.width) ||
        frame.motionVectorScale.y != static_cast<float>(frame.extents.renderExtent.height)) {
        return TemporalFrameValidationError::InvalidMotionVectorScale;
    }
    if (!std::isfinite(frame.frameDeltaMilliseconds) || frame.frameDeltaMilliseconds < 0.0f) {
        return TemporalFrameValidationError::InvalidFrameDelta;
    }
    if (!std::isfinite(frame.preExposure) || frame.preExposure <= 0.0f || !std::isfinite(frame.previousPreExposure) ||
        frame.previousPreExposure <= 0.0f) {
        return TemporalFrameValidationError::InvalidPreExposure;
    }
    if (!std::isfinite(frame.cameraNear) || !std::isfinite(frame.cameraFar) || frame.cameraNear <= 0.0f ||
        frame.cameraFar <= frame.cameraNear) {
        return TemporalFrameValidationError::InvalidCameraRange;
    }
    if (!std::isfinite(frame.verticalFovRadians) || frame.verticalFovRadians <= 0.0f) {
        return TemporalFrameValidationError::InvalidVerticalFov;
    }
    if (!std::isfinite(frame.cameraAspectRatio) || frame.cameraAspectRatio <= 0.0f) {
        return TemporalFrameValidationError::InvalidCameraAspectRatio;
    }
    const glm::mat4 matrices[] = {frame.cameraViewToClip, frame.clipToCameraView, frame.clipToPrevClip,
                                  frame.prevClipToClip};
    for (const glm::mat4& matrix : matrices) {
        for (uint32_t column = 0u; column < 4u; ++column) {
            for (uint32_t row = 0u; row < 4u; ++row) {
                if (!std::isfinite(matrix[column][row])) {
                    return TemporalFrameValidationError::InvalidCameraMatrices;
                }
            }
        }
    }
    const glm::vec3 vectors[] = {frame.cameraPosition, frame.cameraUp, frame.cameraRight, frame.cameraForward};
    for (const glm::vec3& vector : vectors) {
        if (!std::isfinite(vector.x) || !std::isfinite(vector.y) || !std::isfinite(vector.z)) {
            return TemporalFrameValidationError::InvalidCameraVectors;
        }
    }
    if (glm::dot(frame.cameraUp, frame.cameraUp) <= 0.0f || glm::dot(frame.cameraRight, frame.cameraRight) <= 0.0f ||
        glm::dot(frame.cameraForward, frame.cameraForward) <= 0.0f) {
        return TemporalFrameValidationError::InvalidCameraVectors;
    }
    if (!frame.textures.hdrColor.isValid())
        return TemporalFrameValidationError::MissingHdrColor;
    if (!frame.textures.hdrColorView.isValid()) {
        return TemporalFrameValidationError::MissingHdrColorView;
    }
    if (!frame.textures.depth.isValid())
        return TemporalFrameValidationError::MissingDepth;
    if (!frame.textures.depthView.isValid())
        return TemporalFrameValidationError::MissingDepthView;
    if (!frame.textures.velocity.isValid())
        return TemporalFrameValidationError::MissingVelocity;
    if (!frame.textures.velocityView.isValid()) {
        return TemporalFrameValidationError::MissingVelocityView;
    }
    if (!frame.textures.exposure.isValid())
        return TemporalFrameValidationError::MissingExposure;
    if (!frame.textures.exposureView.isValid()) {
        return TemporalFrameValidationError::MissingExposureView;
    }
    if (!frame.textures.reactiveMask.isValid())
        return TemporalFrameValidationError::MissingReactiveMask;
    if (!frame.textures.reactiveMaskView.isValid()) {
        return TemporalFrameValidationError::MissingReactiveMaskView;
    }
    if (!frame.textures.transparencyMask.isValid())
        return TemporalFrameValidationError::MissingTransparencyMask;
    if (!frame.textures.transparencyMaskView.isValid()) {
        return TemporalFrameValidationError::MissingTransparencyMaskView;
    }
    if (!frame.textures.outputHdrColor.isValid())
        return TemporalFrameValidationError::MissingOutputHdrColor;
    if (!frame.textures.outputHdrColorView.isValid()) {
        return TemporalFrameValidationError::MissingOutputHdrColorView;
    }
    return std::nullopt;
}

#endif // MECRAFT_TEMPORAL_FRAME_CONTRACT_H
