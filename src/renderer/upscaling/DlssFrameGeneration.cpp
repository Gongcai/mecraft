#include "renderer/upscaling/DlssFrameGeneration.h"

#include "renderer/presentation/PresentationController.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/vulkan/VkRhiDevice.h"
#include "renderer/rhi/vulkan/VkRhiInterop.h"
#include "renderer/upscaling/StreamlineRuntime.h"

#include <array>
#include <iostream>
#include <optional>

namespace {

constexpr uint32_t kStreamlineViewport = 0u;

[[nodiscard]] std::array<float, 16u> toRowMajorArray(const glm::mat4& matrix) {
    std::array<float, 16u> result{};
    for (uint32_t row = 0u; row < 4u; ++row) {
        for (uint32_t column = 0u; column < 4u; ++column) {
            result[row * 4u + column] = matrix[column][row];
        }
    }
    return result;
}

[[nodiscard]] StreamlineDlssFrameConstants frameConstants(const TemporalFrameInput& frame) {
    StreamlineDlssFrameConstants constants;
    constants.cameraViewToClip = toRowMajorArray(frame.cameraViewToClip);
    constants.clipToCameraView = toRowMajorArray(frame.clipToCameraView);
    constants.clipToPrevClip = toRowMajorArray(frame.clipToPrevClip);
    constants.prevClipToClip = toRowMajorArray(frame.prevClipToClip);
    constants.jitterOffset = {frame.jitter.pixels.x, frame.jitter.pixels.y};
    constants.motionVectorScale = {-1.0f, -1.0f};
    constants.cameraPosition = {frame.cameraPosition.x, frame.cameraPosition.y, frame.cameraPosition.z};
    constants.cameraUp = {frame.cameraUp.x, frame.cameraUp.y, frame.cameraUp.z};
    constants.cameraRight = {frame.cameraRight.x, frame.cameraRight.y, frame.cameraRight.z};
    constants.cameraForward = {frame.cameraForward.x, frame.cameraForward.y, frame.cameraForward.z};
    constants.cameraNear = frame.cameraNear;
    constants.cameraFar = frame.cameraFar;
    constants.verticalFovRadians = frame.verticalFovRadians;
    constants.cameraAspectRatio = frame.cameraAspectRatio;
    constants.depthInverted = frame.depthInverted;
    constants.reset = requiresTemporalReset(frame.resetReasons);
    return constants;
}

[[nodiscard]] StreamlineDlssResource toStreamlineResource(const VkRhiTextureInteropInfo& info,
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

[[nodiscard]] bool validTexture(const VkRhiTextureInteropInfo& info, const VkFormat format, const TemporalExtent extent,
                                const VkImageUsageFlags requiredUsage) {
    return info.image != VK_NULL_HANDLE && info.view != VK_NULL_HANDLE && info.format == format &&
           info.extent.width == extent.width && info.extent.height == extent.height && info.extent.depth == 1u &&
           info.imageType == VK_IMAGE_TYPE_2D && info.viewType == VK_IMAGE_VIEW_TYPE_2D && info.mipCount == 1u &&
           info.layerCount == 1u && (info.usage & requiredUsage) == requiredUsage;
}

[[nodiscard]] bool sameOptions(const StreamlineDlssFrameGenerationOptions& lhs,
                               const StreamlineDlssFrameGenerationOptions& rhs) {
    return lhs.enabled == rhs.enabled && lhs.renderWidth == rhs.renderWidth && lhs.renderHeight == rhs.renderHeight &&
           lhs.outputWidth == rhs.outputWidth && lhs.outputHeight == rhs.outputHeight &&
           lhs.backBufferCount == rhs.backBufferCount && lhs.colorFormat == rhs.colorFormat &&
           lhs.depthFormat == rhs.depthFormat && lhs.motionVectorFormat == rhs.motionVectorFormat &&
           lhs.hudlessFormat == rhs.hudlessFormat && lhs.uiFormat == rhs.uiFormat;
}

class DlssFrameGenerationPresentationBackend final : public PresentationBackend {
public:
    explicit DlssFrameGenerationPresentationBackend(VkRhiDevice& device) : m_device(device) {}

    ~DlssFrameGenerationPresentationBackend() override {
        if (m_enabled && !setFrameGenerationEnabled(false)) {
            std::cerr << StreamlineRuntime::instance().lastError() << '\n';
        }
    }

    [[nodiscard]] PresentationMode mode() const override {
        return m_enabled ? PresentationMode::DlssFrameGeneration : PresentationMode::Native;
    }

    bool resize(const uint32_t width, const uint32_t height) override {
        if (m_renderingGameFrames || !m_device.resizeSwapchain(width, height)) {
            return false;
        }
        m_configured = false;
        m_options = {};
        return true;
    }

    [[nodiscard]] RhiFrameAcquireResult acquireFrame() override { return m_device.acquireFrame(); }

    PresentationBackendPresentResult presentFrame(const RhiPresentInfo& info) override {
        StreamlineRuntime& streamline = StreamlineRuntime::instance();
        if (m_enabled && !m_renderingGameFrames &&
            !streamline.clearDlssFrameGenerationResources(info.trackingFrameIndex, kStreamlineViewport)) {
            std::cerr << streamline.lastError() << '\n';
            if (!m_device.cancelFrame(info)) {
                std::cerr << "DLSS Frame Generation failed to release the acquired frame\n";
            }
            return {};
        }

        const RhiFrameStatus status = m_device.presentFrame(info);
        if (status != RhiFrameStatus::Success && status != RhiFrameStatus::Suboptimal) {
            return {status, 0u, 0u};
        }
        if (!m_enabled) {
            return {status, 1u, 0u};
        }

        StreamlineDlssFrameGenerationState state;
        if (!streamline.queryDlssFrameGenerationState(kStreamlineViewport, state) || state.status != 0u) {
            std::cerr << streamline.lastError() << '\n';
            return {RhiFrameStatus::Error, 0u, 0u};
        }
        if (m_renderingGameFrames) {
            if (state.inputsProcessingCompletionFence == nullptr || state.inputsProcessingCompletionValue == 0u ||
                !VkRhiInterop::queueExternalTimelineWait(m_device, state.inputsProcessingCompletionFence,
                                                         state.inputsProcessingCompletionValue)) {
                return {RhiFrameStatus::Error, 0u, 0u};
            }
        }
        const uint32_t displayedFrames = m_renderingGameFrames ? state.framesPresented : 1u;
        if (displayedFrames == 0u || displayedFrames > 2u) {
            return {RhiFrameStatus::Error, 0u, 0u};
        }
        return {status, displayedFrames, displayedFrames - 1u};
    }

    bool cancelFrame(const RhiPresentInfo& info) override {
        StreamlineRuntime& streamline = StreamlineRuntime::instance();
        const bool resourcesCleared =
            !m_enabled || streamline.clearDlssFrameGenerationResources(info.trackingFrameIndex, kStreamlineViewport);
        if (!resourcesCleared) {
            std::cerr << streamline.lastError() << '\n';
        }
        return m_device.cancelFrame(info);
    }

    [[nodiscard]] bool vsyncEnabled() const override { return m_device.vsyncEnabled(); }

    [[nodiscard]] bool supportsVsyncControl() const override {
        return !m_enabled && m_device.capabilities().vsyncControl;
    }

    bool setVsyncEnabled(const bool enabled) override { return !m_enabled && m_device.setVsyncEnabled(enabled); }

    [[nodiscard]] bool supportsFrameGeneration() const override {
        const StreamlineRuntime& streamline = StreamlineRuntime::instance();
        return streamline.dlssFrameGenerationSupported() && streamline.reflexLowLatencyAvailable() &&
               (!m_device.vsyncEnabled() || m_device.capabilities().vsyncControl);
    }

    [[nodiscard]] bool frameGenerationEnabled() const override { return m_enabled; }

    [[nodiscard]] bool frameGenerationActive() const override { return m_enabled && m_renderingGameFrames; }

    bool setFrameGenerationEnabled(const bool enabled) override {
        if (enabled == m_enabled) {
            return true;
        }
        StreamlineRuntime& streamline = StreamlineRuntime::instance();
        if (enabled) {
            if (!supportsFrameGeneration()) {
                return false;
            }
            m_vsyncBeforeFrameGeneration = m_device.vsyncEnabled();
            if (m_vsyncBeforeFrameGeneration && !m_device.setVsyncEnabled(false)) {
                return false;
            }
            if (!VkRhiInterop::recreateFrameGenerationSwapchain(m_device, true)) {
                std::cerr << streamline.lastError() << '\n';
                return false;
            }
            m_enabled = true;
            m_renderingGameFrames = false;
            m_configured = false;
            return true;
        }

        if (m_configured) {
            StreamlineDlssFrameGenerationOptions disabledOptions = m_options;
            disabledOptions.enabled = false;
            if (!streamline.configureDlssFrameGeneration(kStreamlineViewport, disabledOptions)) {
                std::cerr << streamline.lastError() << '\n';
                return false;
            }
            m_renderingGameFrames = false;
            if (!streamline.releaseDlssFrameGenerationResources(kStreamlineViewport)) {
                std::cerr << streamline.lastError() << '\n';
                return false;
            }
        }
        if (!VkRhiInterop::recreateFrameGenerationSwapchain(m_device, false)) {
            std::cerr << streamline.lastError() << '\n';
            return false;
        }
        m_enabled = false;
        m_renderingGameFrames = false;
        m_configured = false;
        m_options = {};
        if (m_device.vsyncEnabled() != m_vsyncBeforeFrameGeneration &&
            !m_device.setVsyncEnabled(m_vsyncBeforeFrameGeneration)) {
            return false;
        }
        return true;
    }

    bool setRenderingGameFrames(const bool renderingGameFrames) override {
        if (!m_enabled) {
            m_renderingGameFrames = false;
            return true;
        }
        if (renderingGameFrames == m_renderingGameFrames) {
            return true;
        }
        if (m_configured) {
            StreamlineDlssFrameGenerationOptions options = m_options;
            options.enabled = renderingGameFrames;
            if (!StreamlineRuntime::instance().configureDlssFrameGeneration(kStreamlineViewport, options)) {
                return false;
            }
            m_options = options;
        }
        m_renderingGameFrames = renderingGameFrames;
        return true;
    }

    [[nodiscard]] bool requiresFrameGenerationInputs() const override { return m_enabled && m_renderingGameFrames; }

    bool prepareFrameGeneration(const PresentationFrameResources& resources,
                                const TemporalFrameInput& temporalFrame) override {
        if (!m_enabled || !m_renderingGameFrames || !resources.uiPremultipliedAlpha ||
            !temporalFrame.renderingGameFrames) {
            return false;
        }
        const auto depth =
            VkRhiInterop::textureInfo(m_device, temporalFrame.textures.depth, temporalFrame.textures.depthView);
        const auto motionVectors =
            VkRhiInterop::textureInfo(m_device, temporalFrame.textures.velocity, temporalFrame.textures.velocityView);
        const auto hudlessColor =
            VkRhiInterop::textureInfo(m_device, resources.hudlessColorTexture, resources.hudlessColorView);
        const auto uiColor = VkRhiInterop::textureInfo(m_device, resources.uiColorTexture, resources.uiColorView);
        const TemporalExtent outputExtent{resources.width, resources.height};
        if (!depth.has_value() || !motionVectors.has_value() || !hudlessColor.has_value() || !uiColor.has_value() ||
            !validTexture(*depth, VK_FORMAT_D32_SFLOAT, temporalFrame.extents.resourceExtent,
                          VK_IMAGE_USAGE_SAMPLED_BIT) ||
            !validTexture(*motionVectors, VK_FORMAT_R16G16_SFLOAT, temporalFrame.extents.resourceExtent,
                          VK_IMAGE_USAGE_SAMPLED_BIT) ||
            !validTexture(*hudlessColor, VK_FORMAT_B8G8R8A8_UNORM, outputExtent, VK_IMAGE_USAGE_SAMPLED_BIT) ||
            !validTexture(*uiColor, VK_FORMAT_B8G8R8A8_UNORM, outputExtent, VK_IMAGE_USAGE_SAMPLED_BIT)) {
            return false;
        }

        StreamlineDlssFrameGenerationOptions options;
        options.enabled = m_renderingGameFrames;
        options.renderWidth = temporalFrame.extents.renderRect.width;
        options.renderHeight = temporalFrame.extents.renderRect.height;
        options.outputWidth = resources.width;
        options.outputHeight = resources.height;
        options.backBufferCount = m_device.capabilities().swapchainImageCount;
        options.colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
        options.depthFormat = depth->format;
        options.motionVectorFormat = motionVectors->format;
        options.hudlessFormat = hudlessColor->format;
        options.uiFormat = uiColor->format;
        StreamlineRuntime& streamline = StreamlineRuntime::instance();
        if (!m_configured || !sameOptions(options, m_options)) {
            if (!streamline.configureDlssFrameGeneration(kStreamlineViewport, options)) {
                return false;
            }
            m_options = options;
            m_configured = true;
        }

        StreamlineDlssFrameGenerationDispatchInfo dispatch;
        dispatch.frameIndex = temporalFrame.frameIndex;
        dispatch.viewport = kStreamlineViewport;
        dispatch.constants = frameConstants(temporalFrame);
        dispatch.depth = toStreamlineResource(*depth, RhiResourceState::ShaderRead);
        dispatch.motionVectors = toStreamlineResource(*motionVectors, RhiResourceState::ShaderRead);
        dispatch.hudlessColor = toStreamlineResource(*hudlessColor, RhiResourceState::ShaderRead);
        dispatch.uiColorAndAlpha = toStreamlineResource(*uiColor, RhiResourceState::ShaderRead);
        return streamline.tagDlssFrameGenerationResources(dispatch);
    }

private:
    VkRhiDevice& m_device;
    StreamlineDlssFrameGenerationOptions m_options;
    bool m_vsyncBeforeFrameGeneration = true;
    bool m_enabled = false;
    bool m_renderingGameFrames = false;
    bool m_configured = false;
};

} // namespace

std::unique_ptr<PresentationBackend> createDlssFrameGenerationPresentationBackend(RhiDevice& rhiDevice) {
    if (rhiDevice.backend() != RhiBackend::Vulkan) {
        return nullptr;
    }
    return std::make_unique<DlssFrameGenerationPresentationBackend>(static_cast<VkRhiDevice&>(rhiDevice));
}
