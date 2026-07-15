#include "renderer/upscaling/DlssVulkanContext.h"

#include "renderer/rhi/vulkan/VkRhiDevice.h"
#include "renderer/rhi/vulkan/VkRhiInterop.h"
#include "renderer/upscaling/StreamlineRuntime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace {

[[nodiscard]] std::optional<StreamlineDlssMode> toDlssMode(
    const TemporalUpscaleQuality quality) {
    switch (quality) {
        case TemporalUpscaleQuality::Quality:
            return StreamlineDlssMode::Quality;
        case TemporalUpscaleQuality::Balanced:
            return StreamlineDlssMode::Balanced;
        case TemporalUpscaleQuality::Performance:
            return StreamlineDlssMode::Performance;
        case TemporalUpscaleQuality::UltraPerformance:
            return StreamlineDlssMode::UltraPerformance;
        case TemporalUpscaleQuality::Native:
            return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] float halton(uint32_t index, const uint32_t base) {
    float value = 0.0f;
    float fraction = 1.0f;
    while (index > 0u) {
        fraction /= static_cast<float>(base);
        value += fraction * static_cast<float>(index % base);
        index /= base;
    }
    return value;
}

[[nodiscard]] bool hasSingleSubresource(const VkRhiTextureInteropInfo& info) {
    return info.imageType == VK_IMAGE_TYPE_2D &&
           info.viewType == VK_IMAGE_VIEW_TYPE_2D &&
           info.mipCount == 1u && info.layerCount == 1u;
}

[[nodiscard]] bool hasUsage(
    const VkRhiTextureInteropInfo& info,
    const VkImageUsageFlags usage) {
    return (info.usage & usage) == usage;
}

[[nodiscard]] bool validResource(
    const VkRhiTextureInteropInfo& info,
    const VkFormat format,
    const TemporalExtent extent,
    const VkImageUsageFlags usage) {
    return info.image != VK_NULL_HANDLE && info.view != VK_NULL_HANDLE &&
           info.format == format && info.extent.width == extent.width &&
           info.extent.height == extent.height && info.extent.depth == 1u &&
           hasSingleSubresource(info) && hasUsage(info, usage);
}

[[nodiscard]] StreamlineDlssResource toDlssResource(
    const VkRhiTextureInteropInfo& info,
    const RhiResourceState state) {
    StreamlineDlssResource resource;
    resource.image = info.image;
    resource.view = info.view;
    resource.layout = VkRhiInterop::resourceLayout(state);
    resource.format = info.format;
    resource.extent = info.extent;
    resource.usage = info.usage;
    resource.aspectMask = info.aspectMask;
    resource.mipLevels = info.mipLevels;
    resource.arrayLayers = info.arrayLayers;
    resource.baseMip = info.baseMip;
    resource.mipCount = info.mipCount;
    resource.baseLayer = info.baseLayer;
    resource.layerCount = info.layerCount;
    return resource;
}

[[nodiscard]] std::array<float, 16u> toRowMajorArray(const glm::mat4& matrix) {
    std::array<float, 16u> result{};
    for (uint32_t row = 0u; row < 4u; ++row) {
        for (uint32_t column = 0u; column < 4u; ++column) {
            result[row * 4u + column] = matrix[column][row];
        }
    }
    return result;
}

} // namespace

DlssRenderExtentResult queryDlssRenderExtent(
    const TemporalUpscaleQuality quality,
    const TemporalExtent outputExtent) {
    const auto mode = toDlssMode(quality);
    if (!mode.has_value()) {
        return {DlssVulkanStatus::InvalidQuality, {}};
    }
    if (!outputExtent.isValid()) {
        return {DlssVulkanStatus::InvalidExtent, {}};
    }
    StreamlineDlssOptions options;
    options.mode = *mode;
    options.outputWidth = outputExtent.width;
    options.outputHeight = outputExtent.height;
    StreamlineDlssOptimalSettings settings;
    if (!StreamlineRuntime::instance().queryDlssOptimalSettings(
            options, settings)) {
        return {DlssVulkanStatus::RuntimeUnavailable, {}};
    }
    const TemporalExtent renderExtent{settings.renderWidth, settings.renderHeight};
    if (!renderExtent.isValid()) {
        return {DlssVulkanStatus::InvalidExtent, {}};
    }
    return {DlssVulkanStatus::Success, renderExtent};
}

DlssJitterResult queryDlssJitter(
    const uint64_t frameIndex,
    const TemporalExtent renderExtent,
    const TemporalExtent outputExtent) {
    if (!renderExtent.isValid() || !outputExtent.isValid()) {
        return {};
    }
    const float scale = static_cast<float>(outputExtent.width) /
                        static_cast<float>(renderExtent.width);
    const uint32_t phaseCount = std::max(
        8u, static_cast<uint32_t>(std::ceil(8.0f * scale * scale)));
    const uint32_t phaseIndex = static_cast<uint32_t>(
        frameIndex % static_cast<uint64_t>(phaseCount));
    TemporalJitter jitter;
    jitter.pixels = {
        halton(phaseIndex + 1u, 2u) - 0.5f,
        halton(phaseIndex + 1u, 3u) - 0.5f
    };
    jitter.projectionOffset = {
        2.0f * jitter.pixels.x / static_cast<float>(renderExtent.width),
        -2.0f * jitter.pixels.y / static_cast<float>(renderExtent.height)
    };
    return {DlssVulkanStatus::Success, jitter, phaseCount, phaseIndex};
}

DlssVulkanContext::~DlssVulkanContext() {
    static_cast<void>(shutdown());
}

bool DlssVulkanContext::initialize(
    const TemporalUpscaleQuality quality,
    const TemporalExtent renderExtent,
    const TemporalExtent outputExtent) {
    if (m_initialized || !renderExtent.isValid() || !outputExtent.isValid()) {
        return false;
    }
    const auto mode = toDlssMode(quality);
    if (!mode.has_value()) {
        return false;
    }
    const DlssRenderExtentResult expected = queryDlssRenderExtent(
        quality, outputExtent);
    if (!expected.succeeded() || expected.extent != renderExtent) {
        return false;
    }
    StreamlineDlssOptions options;
    options.mode = *mode;
    options.outputWidth = outputExtent.width;
    options.outputHeight = outputExtent.height;
    if (!StreamlineRuntime::instance().configureDlss(kViewport, options)) {
        return false;
    }
    m_quality = quality;
    m_renderExtent = renderExtent;
    m_outputExtent = outputExtent;
    m_initialized = true;
    return true;
}

bool DlssVulkanContext::shutdown() {
    if (!m_initialized) {
        return true;
    }
    if (!StreamlineRuntime::instance().releaseDlssResources(kViewport)) {
        return false;
    }
    m_quality = TemporalUpscaleQuality::Native;
    m_renderExtent = {};
    m_outputExtent = {};
    m_initialized = false;
    return true;
}

DlssVulkanDispatchResult DlssVulkanContext::dispatch(
    const VkRhiDevice& device,
    const RhiCommandList& commandList,
    const TemporalFrameInput& frame) {
    if (!m_initialized || frame.renderExtent != m_renderExtent ||
        frame.outputExtent != m_outputExtent) {
        return {DlssVulkanStatus::RuntimeUnavailable};
    }
    const auto inputColor = VkRhiInterop::textureInfo(
        device, frame.textures.hdrColor, frame.textures.hdrColorView);
    const auto outputColor = VkRhiInterop::textureInfo(
        device, frame.textures.outputHdrColor, frame.textures.outputHdrColorView);
    const auto depth = VkRhiInterop::textureInfo(
        device, frame.textures.depth, frame.textures.depthView);
    const auto motionVectors = VkRhiInterop::textureInfo(
        device, frame.textures.velocity, frame.textures.velocityView);
    const auto exposure = VkRhiInterop::textureInfo(
        device, frame.textures.exposure, frame.textures.exposureView);
    const auto reactiveMask = VkRhiInterop::textureInfo(
        device, frame.textures.reactiveMask, frame.textures.reactiveMaskView);
    const auto transparencyMask = VkRhiInterop::textureInfo(
        device, frame.textures.transparencyMask,
        frame.textures.transparencyMaskView);
    if (!inputColor.has_value() || !outputColor.has_value() ||
        !depth.has_value() || !motionVectors.has_value() ||
        !exposure.has_value() || !reactiveMask.has_value() ||
        !transparencyMask.has_value() ||
        !validResource(*inputColor, VK_FORMAT_R16G16B16A16_SFLOAT,
                       frame.renderExtent, VK_IMAGE_USAGE_SAMPLED_BIT) ||
        !validResource(*outputColor, VK_FORMAT_R16G16B16A16_SFLOAT,
                       frame.outputExtent, VK_IMAGE_USAGE_STORAGE_BIT) ||
        !validResource(*depth, VK_FORMAT_D32_SFLOAT,
                       frame.renderExtent, VK_IMAGE_USAGE_SAMPLED_BIT) ||
        !validResource(*motionVectors, VK_FORMAT_R16G16_SFLOAT,
                       frame.renderExtent, VK_IMAGE_USAGE_SAMPLED_BIT) ||
        !validResource(*exposure, VK_FORMAT_R16G16B16A16_SFLOAT,
                       {1u, 1u}, VK_IMAGE_USAGE_SAMPLED_BIT) ||
        !validResource(*reactiveMask, VK_FORMAT_R8_UNORM,
                       frame.renderExtent, VK_IMAGE_USAGE_SAMPLED_BIT) ||
        !validResource(*transparencyMask, VK_FORMAT_R8_UNORM,
                       frame.renderExtent, VK_IMAGE_USAGE_SAMPLED_BIT)) {
        return {DlssVulkanStatus::InvalidResources};
    }
    const auto commandBuffer = VkRhiInterop::commandBuffer(device, commandList);
    if (!commandBuffer.has_value()) {
        return {DlssVulkanStatus::MissingCommandBuffer};
    }

    StreamlineDlssDispatchInfo dispatch;
    dispatch.frameIndex = frame.frameIndex;
    dispatch.viewport = kViewport;
    dispatch.commandBuffer = *commandBuffer;
    dispatch.constants.cameraViewToClip = toRowMajorArray(frame.cameraViewToClip);
    dispatch.constants.clipToCameraView = toRowMajorArray(frame.clipToCameraView);
    dispatch.constants.clipToPrevClip = toRowMajorArray(frame.clipToPrevClip);
    dispatch.constants.prevClipToClip = toRowMajorArray(frame.prevClipToClip);
    dispatch.constants.jitterOffset = {frame.jitter.pixels.x, frame.jitter.pixels.y};
    dispatch.constants.motionVectorScale = {-1.0f, -1.0f};
    dispatch.constants.cameraPosition = {
        frame.cameraPosition.x, frame.cameraPosition.y, frame.cameraPosition.z};
    dispatch.constants.cameraUp = {
        frame.cameraUp.x, frame.cameraUp.y, frame.cameraUp.z};
    dispatch.constants.cameraRight = {
        frame.cameraRight.x, frame.cameraRight.y, frame.cameraRight.z};
    dispatch.constants.cameraForward = {
        frame.cameraForward.x, frame.cameraForward.y, frame.cameraForward.z};
    dispatch.constants.cameraNear = frame.cameraNear;
    dispatch.constants.cameraFar = frame.cameraFar;
    dispatch.constants.verticalFovRadians = frame.verticalFovRadians;
    dispatch.constants.cameraAspectRatio = frame.cameraAspectRatio;
    dispatch.constants.depthInverted = frame.depthInverted;
    dispatch.constants.reset = frame.reset;
    dispatch.inputColor = toDlssResource(*inputColor, RhiResourceState::ShaderRead);
    dispatch.outputColor = toDlssResource(*outputColor, RhiResourceState::ShaderWrite);
    dispatch.depth = toDlssResource(*depth, RhiResourceState::ShaderRead);
    dispatch.motionVectors = toDlssResource(
        *motionVectors, RhiResourceState::ShaderRead);
    dispatch.exposure = toDlssResource(*exposure, RhiResourceState::ShaderRead);
    dispatch.reactiveMask = toDlssResource(
        *reactiveMask, RhiResourceState::ShaderRead);
    dispatch.transparencyMask = toDlssResource(
        *transparencyMask, RhiResourceState::ShaderRead);
    if (!StreamlineRuntime::instance().evaluateDlss(dispatch)) {
        return {DlssVulkanStatus::SdkError};
    }
    return {DlssVulkanStatus::Success};
}
