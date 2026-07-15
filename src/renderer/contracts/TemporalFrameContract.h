#ifndef MECRAFT_TEMPORAL_FRAME_CONTRACT_H
#define MECRAFT_TEMPORAL_FRAME_CONTRACT_H

#include "renderer/rhi/RhiHandles.h"

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <optional>

/// Two-dimensional pixel extent shared by temporal reconstruction stages.
struct TemporalExtent {
    uint32_t width = 0u;
    uint32_t height = 0u;

    [[nodiscard]] constexpr bool isValid() const {
        return width > 0u && height > 0u;
    }
};

[[nodiscard]] constexpr bool operator==(const TemporalExtent lhs, const TemporalExtent rhs) {
    return lhs.width == rhs.width && lhs.height == rhs.height;
}

[[nodiscard]] constexpr bool operator!=(const TemporalExtent lhs, const TemporalExtent rhs) {
    return !(lhs == rhs);
}

/// Determine whether temporal history can be reused for the current frame extents.
/// @param hasPreviousFrame Whether previous-frame matrices and resources are valid.
/// @param previousRenderExtent Previous scene rendering extent.
/// @param previousOutputExtent Previous presentation extent.
/// @param renderExtent Current scene rendering extent.
/// @param outputExtent Current presentation extent.
/// @return True when history must be cleared before temporal sampling.
[[nodiscard]] constexpr bool requiresTemporalReset(
    const bool hasPreviousFrame,
    const TemporalExtent previousRenderExtent,
    const TemporalExtent previousOutputExtent,
    const TemporalExtent renderExtent,
    const TemporalExtent outputExtent) {
    return !hasPreviousFrame || previousRenderExtent != renderExtent ||
           previousOutputExtent != outputExtent;
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
    TemporalExtent renderExtent;
    TemporalExtent outputExtent;
    TemporalJitter jitter;
    glm::vec2 motionVectorScale = glm::vec2(0.0f);
    float frameDeltaMilliseconds = 0.0f;
    float preExposure = 1.0f;
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
    bool reset = true;
    TemporalFrameTextures textures;
};

enum class TemporalFrameValidationError {
    InvalidRenderExtent,
    InvalidOutputExtent,
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
[[nodiscard]] inline std::optional<TemporalFrameValidationError> validateTemporalFrame(
    const TemporalFrameInput& frame) {
    if (!frame.renderExtent.isValid()) return TemporalFrameValidationError::InvalidRenderExtent;
    if (!frame.outputExtent.isValid()) return TemporalFrameValidationError::InvalidOutputExtent;
    if (!std::isfinite(frame.jitter.pixels.x) ||
        !std::isfinite(frame.jitter.pixels.y) ||
        !std::isfinite(frame.jitter.projectionOffset.x) ||
        !std::isfinite(frame.jitter.projectionOffset.y)) {
        return TemporalFrameValidationError::InvalidJitter;
    }
    if (!std::isfinite(frame.motionVectorScale.x) ||
        !std::isfinite(frame.motionVectorScale.y) ||
        frame.motionVectorScale.x != static_cast<float>(frame.renderExtent.width) ||
        frame.motionVectorScale.y != static_cast<float>(frame.renderExtent.height)) {
        return TemporalFrameValidationError::InvalidMotionVectorScale;
    }
    if (!std::isfinite(frame.frameDeltaMilliseconds) || frame.frameDeltaMilliseconds < 0.0f) {
        return TemporalFrameValidationError::InvalidFrameDelta;
    }
    if (!std::isfinite(frame.preExposure) || frame.preExposure <= 0.0f) {
        return TemporalFrameValidationError::InvalidPreExposure;
    }
    if (!std::isfinite(frame.cameraNear) || !std::isfinite(frame.cameraFar) ||
        frame.cameraNear <= 0.0f || frame.cameraFar <= frame.cameraNear) {
        return TemporalFrameValidationError::InvalidCameraRange;
    }
    if (!std::isfinite(frame.verticalFovRadians) || frame.verticalFovRadians <= 0.0f) {
        return TemporalFrameValidationError::InvalidVerticalFov;
    }
    if (!std::isfinite(frame.cameraAspectRatio) || frame.cameraAspectRatio <= 0.0f) {
        return TemporalFrameValidationError::InvalidCameraAspectRatio;
    }
    const glm::mat4 matrices[] = {
        frame.cameraViewToClip,
        frame.clipToCameraView,
        frame.clipToPrevClip,
        frame.prevClipToClip
    };
    for (const glm::mat4& matrix : matrices) {
        for (uint32_t column = 0u; column < 4u; ++column) {
            for (uint32_t row = 0u; row < 4u; ++row) {
                if (!std::isfinite(matrix[column][row])) {
                    return TemporalFrameValidationError::InvalidCameraMatrices;
                }
            }
        }
    }
    const glm::vec3 vectors[] = {
        frame.cameraPosition,
        frame.cameraUp,
        frame.cameraRight,
        frame.cameraForward
    };
    for (const glm::vec3& vector : vectors) {
        if (!std::isfinite(vector.x) || !std::isfinite(vector.y) ||
            !std::isfinite(vector.z)) {
            return TemporalFrameValidationError::InvalidCameraVectors;
        }
    }
    if (glm::dot(frame.cameraUp, frame.cameraUp) <= 0.0f ||
        glm::dot(frame.cameraRight, frame.cameraRight) <= 0.0f ||
        glm::dot(frame.cameraForward, frame.cameraForward) <= 0.0f) {
        return TemporalFrameValidationError::InvalidCameraVectors;
    }
    if (!frame.textures.hdrColor.isValid()) return TemporalFrameValidationError::MissingHdrColor;
    if (!frame.textures.hdrColorView.isValid()) {
        return TemporalFrameValidationError::MissingHdrColorView;
    }
    if (!frame.textures.depth.isValid()) return TemporalFrameValidationError::MissingDepth;
    if (!frame.textures.depthView.isValid()) return TemporalFrameValidationError::MissingDepthView;
    if (!frame.textures.velocity.isValid()) return TemporalFrameValidationError::MissingVelocity;
    if (!frame.textures.velocityView.isValid()) {
        return TemporalFrameValidationError::MissingVelocityView;
    }
    if (!frame.textures.exposure.isValid()) return TemporalFrameValidationError::MissingExposure;
    if (!frame.textures.exposureView.isValid()) {
        return TemporalFrameValidationError::MissingExposureView;
    }
    if (!frame.textures.reactiveMask.isValid()) return TemporalFrameValidationError::MissingReactiveMask;
    if (!frame.textures.reactiveMaskView.isValid()) {
        return TemporalFrameValidationError::MissingReactiveMaskView;
    }
    if (!frame.textures.transparencyMask.isValid()) return TemporalFrameValidationError::MissingTransparencyMask;
    if (!frame.textures.transparencyMaskView.isValid()) {
        return TemporalFrameValidationError::MissingTransparencyMaskView;
    }
    if (!frame.textures.outputHdrColor.isValid()) return TemporalFrameValidationError::MissingOutputHdrColor;
    if (!frame.textures.outputHdrColorView.isValid()) {
        return TemporalFrameValidationError::MissingOutputHdrColorView;
    }
    return std::nullopt;
}

#endif // MECRAFT_TEMPORAL_FRAME_CONTRACT_H
