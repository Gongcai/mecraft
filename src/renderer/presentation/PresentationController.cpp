#include "PresentationController.h"

#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiShaderSourceLoader.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace {

[[nodiscard]] bool isRenderableAcquireStatus(const RhiFrameStatus status) {
    return status == RhiFrameStatus::Success ||
           status == RhiFrameStatus::Suboptimal;
}

[[nodiscard]] bool sameTextureHandle(const RhiTextureHandle lhs,
                                     const RhiTextureHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

[[nodiscard]] bool sameTextureViewHandle(const RhiTextureViewHandle lhs,
                                         const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

class NativePresentationBackend final : public PresentationBackend {
public:
    explicit NativePresentationBackend(RhiDevice& rhiDevice)
        : m_rhiDevice(rhiDevice) {
    }

    [[nodiscard]] PresentationMode mode() const override {
        return PresentationMode::Native;
    }

    bool resize(const uint32_t width, const uint32_t height) override {
        return m_rhiDevice.resizeSwapchain(width, height);
    }

    [[nodiscard]] RhiFrameAcquireResult acquireFrame() override {
        return m_rhiDevice.acquireFrame();
    }

    PresentationBackendPresentResult presentFrame(
        const RhiPresentInfo& info) override {
        const RhiFrameStatus status = m_rhiDevice.presentFrame(info);
        const uint32_t displayedFrameCount = isRenderableAcquireStatus(status)
            ? 1u
            : 0u;
        return {status, displayedFrameCount, 0u};
    }

private:
    RhiDevice& m_rhiDevice;
};

[[nodiscard]] bool isNonFatalPresentationStatus(const RhiFrameStatus status) {
    return status == RhiFrameStatus::OutOfDate ||
           status == RhiFrameStatus::Minimized ||
           status == RhiFrameStatus::SurfaceLost;
}

} // namespace

std::unique_ptr<PresentationBackend> createNativePresentationBackend(
    RhiDevice& rhiDevice) {
    return std::make_unique<NativePresentationBackend>(rhiDevice);
}

PresentationController::PresentationController(
    PresentationBackend& backend)
    : m_backend(backend) {
    m_statistics.mode = m_backend.mode();
}

PresentationController::~PresentationController() {
    shutdownUiComposition();
}

bool PresentationController::initUiComposition(RhiDevice& rhiDevice) {
    if (m_uiDevice != nullptr && m_uiDevice != &rhiDevice) {
        return false;
    }
    if (m_uiDevice == nullptr) {
        m_uiDevice = &rhiDevice;
        m_uiColorFormat = rhiDevice.swapchainColorFormat();
        m_uiDepthFormat = rhiDevice.swapchainDepthStencilFormat();
    }
    if (m_uiColorFormat == RhiTextureFormat::Undefined ||
        m_uiDepthFormat == RhiTextureFormat::Undefined) {
        return false;
    }
    return createUiCompositionPipeline();
}

void PresentationController::shutdownUiComposition() {
    if (m_uiDevice != nullptr) {
        m_uiDevice->waitIdle();
    }
    destroyUiTargets();
    destroyUiCompositionPipeline();
    m_uiDevice = nullptr;
    m_uiColorFormat = RhiTextureFormat::Undefined;
    m_uiDepthFormat = RhiTextureFormat::Undefined;
    m_uiWidth = 0u;
    m_uiHeight = 0u;
    m_nextUiSlot = 0u;
    m_uiResourceGeneration = 0u;
}

std::optional<PresentationUiFrame> PresentationController::acquireUiFrame(
    const PresentationFrame& frame) {
    if (m_uiDevice == nullptr || !frame.shouldRender() || !m_frameOpen ||
        frame.realFrameNumber != m_openRealFrameNumber ||
        frame.acquired.frameIndex != m_openFrame.frameIndex ||
        frame.acquired.imageIndex != m_openFrame.imageIndex ||
        !ensureUiTargets(frame.acquired.width, frame.acquired.height) ||
        m_uiSlots.empty()) {
        return std::nullopt;
    }

    UiSlot& slot = m_uiSlots[m_nextUiSlot % m_uiSlots.size()];
    ++m_nextUiSlot;
    if (!waitForUiSlot(slot)) {
        return std::nullopt;
    }
    const uint32_t slotIndex = static_cast<uint32_t>(&slot - m_uiSlots.data());
    return PresentationUiFrame{
        slot.colorTexture,
        slot.colorView,
        m_uiColorFormat,
        m_uiWidth,
        m_uiHeight,
        slotIndex,
        m_uiResourceGeneration,
        frame.realFrameNumber,
        true
    };
}

std::optional<PresentationFrameResources> PresentationController::frameResources(
    const PresentationFrame& frame,
    const PresentationUiFrame& uiFrame) const {
    if (!validateUiFrame(uiFrame) || !frame.shouldRender() || !m_frameOpen ||
        frame.realFrameNumber != m_openRealFrameNumber ||
        frame.acquired.frameIndex != m_openFrame.frameIndex ||
        frame.acquired.imageIndex != m_openFrame.imageIndex ||
        !frame.acquired.colorTexture.isValid() ||
        !frame.acquired.colorView.isValid()) {
        return std::nullopt;
    }
    return PresentationFrameResources{
        frame.acquired.colorTexture,
        frame.acquired.colorView,
        uiFrame.colorTexture,
        uiFrame.colorView,
        m_uiColorFormat,
        frame.acquired.width,
        frame.acquired.height,
        frame.realFrameNumber,
        true
    };
}

bool PresentationController::beginUiRendering(
    RhiCommandList& commandList,
    const PresentationUiFrame& uiFrame) {
    if (!validateUiFrame(uiFrame)) {
        return false;
    }
    UiSlot& slot = m_uiSlots[uiFrame.slotIndex];
    if (slot.colorState != RhiResourceState::Undefined &&
        slot.colorState != RhiResourceState::ShaderRead) {
        return false;
    }

    commandList.textureBarrier({
        slot.colorTexture,
        slot.colorState,
        RhiResourceState::RenderTarget
    });
    commandList.textureBarrier({
        slot.depthTexture,
        slot.depthState,
        RhiResourceState::DepthWrite
    });

    RhiColorAttachment colorAttachment;
    colorAttachment.view = slot.colorView;
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 0.0f;

    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = slot.depthView;
    depthAttachment.depthLoadOp = RhiLoadOp::Clear;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;
    depthAttachment.clearDepth = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "Presentation.UiTarget";
    renderingInfo.renderArea = {0, 0, m_uiWidth, m_uiHeight};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    renderingInfo.depthStencilAttachment = &depthAttachment;
    commandList.beginRendering(renderingInfo);
    slot.colorState = RhiResourceState::RenderTarget;
    slot.depthState = RhiResourceState::DepthWrite;
    return true;
}

bool PresentationController::endUiRenderingAndComposite(
    RhiCommandList& commandList,
    const PresentationFrame& frame,
    const PresentationUiFrame& uiFrame) {
    const std::optional<PresentationFrameResources> resources =
        frameResources(frame, uiFrame);
    if (!resources.has_value()) {
        return false;
    }
    UiSlot& slot = m_uiSlots[uiFrame.slotIndex];
    if (slot.colorState != RhiResourceState::RenderTarget ||
        !m_uiCompositePipeline.isValid() ||
        uiFrame.slotIndex >= m_uiCompositeBindGroups.size() ||
        !m_uiCompositeBindGroups[uiFrame.slotIndex].isValid()) {
        return false;
    }

    commandList.endRendering();
    commandList.textureBarrier({
        slot.colorTexture,
        RhiResourceState::RenderTarget,
        RhiResourceState::ShaderRead
    });

    commandList.textureBarrier({
        resources->hudlessColorTexture,
        RhiResourceState::Present,
        RhiResourceState::RenderTarget
    });
    RhiColorAttachment swapchainAttachment;
    swapchainAttachment.view = resources->hudlessColorView;
    swapchainAttachment.loadOp = RhiLoadOp::Load;
    swapchainAttachment.storeOp = RhiStoreOp::Store;
    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "Presentation.UiComposite";
    renderingInfo.renderArea = {0, 0, resources->width, resources->height};
    renderingInfo.colorAttachments = &swapchainAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    commandList.beginRendering(renderingInfo);
    commandList.setViewport({
        0.0f,
        0.0f,
        static_cast<float>(resources->width),
        static_cast<float>(resources->height),
        0.0f,
        1.0f
    });
    commandList.setScissor({0, 0, resources->width, resources->height});
    commandList.setGraphicsPipeline(m_uiCompositePipeline);
    commandList.setBindGroup(0u, m_uiCompositeBindGroups[uiFrame.slotIndex]);
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    commandList.textureBarrier({
        resources->hudlessColorTexture,
        RhiResourceState::RenderTarget,
        RhiResourceState::Present
    });
    slot.colorState = RhiResourceState::ShaderRead;
    return true;
}

bool PresentationController::submitUiFrame(
    RhiCommandList& commandList,
    const PresentationUiFrame& uiFrame) {
    if (!validateUiFrame(uiFrame) || !commandList.end()) {
        return false;
    }
    RhiCommandList* commandLists[] = {&commandList};
    RhiSubmissionToken completionToken;
    if (!m_uiDevice->submit({
            "Presentation.UiSubmit",
            commandLists,
            1u,
            RhiQueueType::Graphics,
            nullptr,
            0u
        }, &completionToken)) {
        return false;
    }
    m_uiSlots[uiFrame.slotIndex].completionToken = completionToken;
    return true;
}

bool PresentationController::validateUiFrame(
    const PresentationUiFrame& uiFrame) const {
    return m_uiDevice != nullptr &&
           uiFrame.resourceGeneration == m_uiResourceGeneration &&
           uiFrame.realFrameNumber == m_openRealFrameNumber &&
           uiFrame.slotIndex < m_uiSlots.size() &&
           uiFrame.colorFormat == m_uiColorFormat &&
           uiFrame.width == m_uiWidth &&
           uiFrame.height == m_uiHeight &&
           uiFrame.premultipliedAlpha &&
           sameTextureHandle(uiFrame.colorTexture,
                             m_uiSlots[uiFrame.slotIndex].colorTexture) &&
           sameTextureViewHandle(uiFrame.colorView,
                                 m_uiSlots[uiFrame.slotIndex].colorView);
}

bool PresentationController::ensureUiTargets(const uint32_t width,
                                              const uint32_t height) {
    if (m_uiDevice == nullptr || width == 0u || height == 0u ||
        !m_uiCompositePipeline.isValid()) {
        return false;
    }
    const uint32_t slotCount = std::max(2u, m_uiDevice->capabilities().swapchainImageCount);
    if (!m_uiSlots.empty() &&
        (m_uiWidth != width || m_uiHeight != height ||
         m_uiSlots.size() != slotCount)) {
        destroyUiTargets();
    }
    if (!m_uiSlots.empty()) {
        return true;
    }
    if (m_uiResourceGeneration == std::numeric_limits<uint64_t>::max()) {
        return false;
    }
    m_uiWidth = width;
    m_uiHeight = height;
    m_nextUiSlot = 0u;
    ++m_uiResourceGeneration;
    m_uiSlots.resize(slotCount);
    m_uiCompositeBindGroups.resize(slotCount);

    for (uint32_t index = 0u; index < slotCount; ++index) {
        UiSlot& slot = m_uiSlots[index];
        RhiTextureDesc colorDesc;
        colorDesc.debugName = "Presentation.UiColor";
        colorDesc.format = m_uiColorFormat;
        colorDesc.width = width;
        colorDesc.height = height;
        colorDesc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                          rhiFlag(RhiTextureUsage::ColorAttachment);
        slot.colorTexture = m_uiDevice->createTexture(colorDesc, nullptr);

        RhiTextureDesc depthDesc;
        depthDesc.debugName = "Presentation.UiDepth";
        depthDesc.format = m_uiDepthFormat;
        depthDesc.width = width;
        depthDesc.height = height;
        depthDesc.usage = rhiFlag(RhiTextureUsage::DepthStencilAttachment);
        slot.depthTexture = m_uiDevice->createTexture(depthDesc, nullptr);
        if (!slot.colorTexture.isValid() || !slot.depthTexture.isValid()) {
            destroyUiTargets();
            return false;
        }

        RhiTextureViewDesc colorViewDesc;
        colorViewDesc.texture = slot.colorTexture;
        colorViewDesc.viewType = RhiTextureViewType::Texture2D;
        colorViewDesc.format = m_uiColorFormat;
        slot.colorView = m_uiDevice->createTextureView(colorViewDesc);

        RhiTextureViewDesc depthViewDesc;
        depthViewDesc.texture = slot.depthTexture;
        depthViewDesc.viewType = RhiTextureViewType::Texture2D;
        depthViewDesc.format = m_uiDepthFormat;
        slot.depthView = m_uiDevice->createTextureView(depthViewDesc);
        if (!slot.colorView.isValid() || !slot.depthView.isValid()) {
            destroyUiTargets();
            return false;
        }

        RhiBindGroupDesc bindGroupDesc;
        bindGroupDesc.layout = m_uiCompositeBindGroupLayout;
        RhiBindGroupEntry entry;
        entry.binding = 0u;
        entry.resource.combinedTextureSampler.textureView = slot.colorView;
        entry.resource.combinedTextureSampler.sampler = m_uiSampler;
        bindGroupDesc.entries.push_back(entry);
        m_uiCompositeBindGroups[index] = m_uiDevice->createBindGroup(bindGroupDesc);
        if (!m_uiCompositeBindGroups[index].isValid()) {
            destroyUiTargets();
            return false;
        }
    }
    return true;
}

bool PresentationController::createUiCompositionPipeline() {
    if (m_uiDevice == nullptr) {
        return false;
    }
    if (m_uiCompositePipeline.isValid()) {
        return true;
    }
    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/presentation_ui_composite_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/presentation_ui_composite_rhi.frag");
    if (!vertexSource || !fragmentSource) {
        return false;
    }

    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "Presentation.UiComposite.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str();
    shaderDesc.sourceSize = vertexSource->size();
    m_uiCompositeVertexShader = m_uiDevice->createShader(shaderDesc);
    shaderDesc.debugName = "Presentation.UiComposite.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = fragmentSource->c_str();
    shaderDesc.sourceSize = fragmentSource->size();
    m_uiCompositeFragmentShader = m_uiDevice->createShader(shaderDesc);

    RhiSamplerDesc samplerDesc;
    samplerDesc.minFilter = RhiFilter::Linear;
    samplerDesc.magFilter = RhiFilter::Linear;
    samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    samplerDesc.addressU = RhiAddressMode::ClampToEdge;
    samplerDesc.addressV = RhiAddressMode::ClampToEdge;
    samplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_uiSampler = m_uiDevice->createSampler(samplerDesc);

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "Presentation.UiComposite.BindGroupLayout";
    bindGroupLayoutDesc.entries.push_back({
        0u,
        RhiBindingType::CombinedTextureSampler,
        rhiFlag(RhiShaderStage::Fragment),
        1u
    });
    m_uiCompositeBindGroupLayout =
        m_uiDevice->createBindGroupLayout(bindGroupLayoutDesc);

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "Presentation.UiComposite.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_uiCompositeBindGroupLayout);
    m_uiCompositePipelineLayout =
        m_uiDevice->createPipelineLayout(pipelineLayoutDesc);

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "Presentation.UiComposite.Pipeline";
    pipelineDesc.vertexShader = m_uiCompositeVertexShader;
    pipelineDesc.fragmentShader = m_uiCompositeFragmentShader;
    pipelineDesc.layout = m_uiCompositePipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(m_uiColorFormat);
    RhiBlendAttachmentState blend;
    blend.blendEnabled = true;
    blend.srcColor = RhiBlendFactor::One;
    blend.dstColor = RhiBlendFactor::OneMinusSrcAlpha;
    blend.srcAlpha = RhiBlendFactor::One;
    blend.dstAlpha = RhiBlendFactor::OneMinusSrcAlpha;
    pipelineDesc.blend.attachments.push_back(blend);
    m_uiCompositePipeline = m_uiDevice->createGraphicsPipeline(pipelineDesc);
    if (!m_uiCompositeVertexShader.isValid() ||
        !m_uiCompositeFragmentShader.isValid() ||
        !m_uiSampler.isValid() ||
        !m_uiCompositeBindGroupLayout.isValid() ||
        !m_uiCompositePipelineLayout.isValid() ||
        !m_uiCompositePipeline.isValid()) {
        destroyUiCompositionPipeline();
        return false;
    }
    return true;
}

void PresentationController::destroyUiTargets() {
    if (m_uiDevice == nullptr) {
        m_uiSlots.clear();
        m_uiCompositeBindGroups.clear();
        return;
    }
    for (RhiBindGroupHandle& bindGroup : m_uiCompositeBindGroups) {
        if (bindGroup.isValid()) {
            m_uiDevice->destroyBindGroup(bindGroup);
        }
        bindGroup = {};
    }
    for (UiSlot& slot : m_uiSlots) {
        if (slot.colorView.isValid()) m_uiDevice->destroyTextureView(slot.colorView);
        if (slot.depthView.isValid()) m_uiDevice->destroyTextureView(slot.depthView);
        if (slot.colorTexture.isValid()) m_uiDevice->destroyTexture(slot.colorTexture);
        if (slot.depthTexture.isValid()) m_uiDevice->destroyTexture(slot.depthTexture);
        slot = {};
    }
    m_uiSlots.clear();
    m_uiCompositeBindGroups.clear();
}

void PresentationController::destroyUiCompositionPipeline() {
    if (m_uiDevice == nullptr) {
        m_uiCompositePipeline = {};
        m_uiCompositePipelineLayout = {};
        m_uiCompositeBindGroupLayout = {};
        m_uiSampler = {};
        m_uiCompositeFragmentShader = {};
        m_uiCompositeVertexShader = {};
        return;
    }
    if (m_uiCompositePipeline.isValid()) m_uiDevice->destroyPipeline(m_uiCompositePipeline);
    if (m_uiCompositePipelineLayout.isValid()) m_uiDevice->destroyPipelineLayout(m_uiCompositePipelineLayout);
    if (m_uiCompositeBindGroupLayout.isValid()) m_uiDevice->destroyBindGroupLayout(m_uiCompositeBindGroupLayout);
    if (m_uiSampler.isValid()) m_uiDevice->destroySampler(m_uiSampler);
    if (m_uiCompositeFragmentShader.isValid()) m_uiDevice->destroyShader(m_uiCompositeFragmentShader);
    if (m_uiCompositeVertexShader.isValid()) m_uiDevice->destroyShader(m_uiCompositeVertexShader);
    m_uiCompositePipeline = {};
    m_uiCompositePipelineLayout = {};
    m_uiCompositeBindGroupLayout = {};
    m_uiSampler = {};
    m_uiCompositeFragmentShader = {};
    m_uiCompositeVertexShader = {};
}

bool PresentationController::waitForUiSlot(UiSlot& slot) {
    if (!slot.completionToken.isValid()) {
        return true;
    }
    bool complete = false;
    if (!m_uiDevice->isSubmissionComplete(slot.completionToken, complete)) {
        return false;
    }
    if (!complete && !m_uiDevice->waitForSubmission(slot.completionToken)) {
        return false;
    }
    slot.completionToken = {};
    return true;
}

PresentationFrame PresentationController::beginFrame(const int width,
                                                       const int height) {
    if (m_frameOpen) {
        return failBegin(PresentationFailure::FrameAlreadyOpen);
    }
    if (width <= 0 || height <= 0) {
        ++m_statistics.skippedFrames;
        m_statistics.lastAcquireStatus = RhiFrameStatus::Minimized;
        m_statistics.lastFailure = PresentationFailure::None;
        return {
            PresentationResult::Skipped,
            PresentationFailure::None,
            RhiFrameStatus::Minimized,
            {},
            0u
        };
    }

    const uint32_t requestedWidth = static_cast<uint32_t>(width);
    const uint32_t requestedHeight = static_cast<uint32_t>(height);
    if (!m_extentValid || requestedWidth != m_width || requestedHeight != m_height) {
        if (!m_backend.resize(requestedWidth, requestedHeight)) {
            return failBegin(PresentationFailure::ResizeRejected);
        }
        m_width = requestedWidth;
        m_height = requestedHeight;
        m_extentValid = true;
        ++m_statistics.resizeOperations;
    }

    ++m_statistics.acquireAttempts;
    RhiFrameAcquireResult acquired = m_backend.acquireFrame();
    m_statistics.lastAcquireStatus = acquired.status;
    if (isRenderableAcquireStatus(acquired.status)) {
        m_frameOpen = true;
        m_openFrame = acquired;
        m_openRealFrameNumber = ++m_statistics.realFramesAcquired;
        m_statistics.lastFailure = PresentationFailure::None;
        return {
            PresentationResult::Ready,
            PresentationFailure::None,
            acquired.status,
            acquired,
            m_openRealFrameNumber
        };
    }
    if (isNonFatalPresentationStatus(acquired.status)) {
        invalidateExtentForStatus(acquired.status);
        ++m_statistics.skippedFrames;
        m_statistics.lastFailure = PresentationFailure::None;
        return {
            PresentationResult::Skipped,
            PresentationFailure::None,
            acquired.status,
            acquired,
            0u
        };
    }
    return failBegin(PresentationFailure::AcquireRejected, acquired.status);
}

PresentationCompleteResult PresentationController::presentFrame(
    const PresentationFrame& frame) {
    if (!m_frameOpen) {
        return failPresent(PresentationFailure::FrameNotOpen);
    }
    if (!frame.shouldRender() ||
        frame.realFrameNumber != m_openRealFrameNumber ||
        frame.acquired.frameIndex != m_openFrame.frameIndex ||
        frame.acquired.imageIndex != m_openFrame.imageIndex) {
        return failPresent(PresentationFailure::FrameIdentityMismatch);
    }

    const PresentationBackendPresentResult backendResult = m_backend.presentFrame({
        m_openFrame.frameIndex,
        m_openFrame.imageIndex
    });
    const RhiFrameStatus status = backendResult.status;
    m_statistics.lastPresentStatus = status;
    m_frameOpen = false;
    m_openFrame = {};
    m_openRealFrameNumber = 0u;

    if (isRenderableAcquireStatus(status)) {
        if (backendResult.displayedFrameCount == 0u ||
            backendResult.generatedFrameCount >= backendResult.displayedFrameCount) {
            return failPresent(
                PresentationFailure::BackendFrameCountInvalid,
                status);
        }
        ++m_statistics.realFramesPresented;
        m_statistics.generatedFramesPresented += backendResult.generatedFrameCount;
        m_statistics.displayedFrames += backendResult.displayedFrameCount;
        m_statistics.lastFailure = PresentationFailure::None;
        return {
            PresentationResult::Presented,
            PresentationFailure::None,
            status
        };
    }
    if (isNonFatalPresentationStatus(status)) {
        invalidateExtentForStatus(status);
        ++m_statistics.skippedFrames;
        m_statistics.lastFailure = PresentationFailure::None;
        return {
            PresentationResult::Skipped,
            PresentationFailure::None,
            status
        };
    }
    return failPresent(PresentationFailure::PresentRejected, status);
}

PresentationFrame PresentationController::failBegin(
    const PresentationFailure failure,
    const RhiFrameStatus status) {
    ++m_statistics.failedOperations;
    m_statistics.lastFailure = failure;
    return {
        failure == PresentationFailure::FrameAlreadyOpen
            ? PresentationResult::ContractViolation
            : PresentationResult::Failed,
        failure,
        status,
        {},
        0u
    };
}

PresentationCompleteResult PresentationController::failPresent(
    const PresentationFailure failure,
    const RhiFrameStatus status) {
    ++m_statistics.failedOperations;
    m_statistics.lastFailure = failure;
    return {
        failure == PresentationFailure::FrameNotOpen ||
        failure == PresentationFailure::FrameIdentityMismatch
            ? PresentationResult::ContractViolation
            : PresentationResult::Failed,
        failure,
        status
    };
}

void PresentationController::invalidateExtentForStatus(
    const RhiFrameStatus status) {
    if (status == RhiFrameStatus::OutOfDate ||
        status == RhiFrameStatus::SurfaceLost) {
        m_extentValid = false;
    }
}

const char* presentationFailureMessage(const PresentationFailure failure) {
    switch (failure) {
    case PresentationFailure::None:
        return "no presentation failure";
    case PresentationFailure::ResizeRejected:
        return "presentation backend rejected the requested framebuffer extent";
    case PresentationFailure::AcquireRejected:
        return "presentation backend failed to acquire a real frame";
    case PresentationFailure::PresentRejected:
        return "presentation backend failed to present the acquired real frame";
    case PresentationFailure::BackendFrameCountInvalid:
        return "presentation backend reported invalid displayed-frame counters";
    case PresentationFailure::FrameAlreadyOpen:
        return "presentation controller already owns an acquired real frame";
    case PresentationFailure::FrameNotOpen:
        return "presentation controller has no acquired real frame to present";
    case PresentationFailure::FrameIdentityMismatch:
        return "presented frame identity does not match the acquired real frame";
    }
    std::abort();
}
