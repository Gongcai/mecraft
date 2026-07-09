#include "SsaoPass.h"
#include "../core/RenderScene.h"
#include "../targets/DeferredRenderTargets.h"
#include "../core/Shader.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../../resource/ResourceMgr.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstddef>
#include <optional>

namespace {
[[nodiscard]] bool sameTextureView(const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

template <size_t Count>
[[nodiscard]] bool sameTextureViews(const std::array<RhiTextureViewHandle, Count>& lhs,
                                    const std::array<RhiTextureViewHandle, Count>& rhs) {
    for (size_t i = 0u; i < lhs.size(); ++i) {
        if (!sameTextureView(lhs[i], rhs[i])) {
            return false;
        }
    }
    return true;
}
} // namespace

void SsaoPass::init(ResourceMgr& resourceMgr) {
    m_noiseTexture = resourceMgr.getTexture2DHandle("shader_noise2d");
}

void SsaoPass::shutdown() {
    destroyBaseRhiResources();
    destroyUpsampleRhiResources();
    destroyFilterRhiResources();
    destroyTemporalRhiResources();
    destroyNoiseTextureView();
    m_noiseTexture = {};
}

void SsaoPass::execute(const FrameContext& ctx, const RenderSettings& settings,
                       DeferredRenderTargets& targets) {
    if (!settings.ssao.enabled) return;

    renderSsaoBase(ctx, settings.ssao, targets);
    if (settings.ssao.filterEnabled) {
        renderSsaoFilter(ctx, targets);
    }
    renderSsaoUpsample(ctx, settings.ssao, targets);
    if (settings.ssao.temporalEnabled) {
        renderSsaoTemporal(ctx, settings.ssao, targets);
    }
}

void SsaoPass::renderSsaoBase(const FrameContext& ctx, const SsaoSettings& ssao,
                               DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsaoHalfResTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice)) {
        return;
    }

    // Render SSAO at half resolution for performance
    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.ssaoHalfResTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "SsaoHalfRes";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.halfWidth())),
        static_cast<uint32_t>(std::max(1, targets.halfHeight()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!ensureNoiseTextureView(rhiDevice)) {
        return;
    }
    const std::array<RhiTextureViewHandle, 3> views = {
        targets.depthTextureViewHandle(),
        targets.normalAoTextureViewHandle(),
        m_noiseTextureView
    };
    if (!ensureBaseRhiPipeline(rhiDevice) ||
        !ensureBaseBindGroup(rhiDevice, views)) {
        return;
    }

    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);

    // Half-res: invResolution refers to the half-res viewport for UV computation
    const int halfW = std::max(1, targets.width() / 2);
    const int halfH = std::max(1, targets.height() / 2);
    struct BasePushConstants {
        glm::mat4 projection;
        glm::mat4 invProjection;
        glm::vec4 params0;
        glm::ivec4 params1;
    };
    const glm::mat4& projection = ctx.camera.projection;
    const BasePushConstants pushConstants{
        projection,
        glm::inverse(projection),
        glm::vec4(1.0f / static_cast<float>(halfW),
                  1.0f / static_cast<float>(halfH),
                  ssao.radius,
                  ssao.strength),
        glm::ivec4(static_cast<int>(ctx.frameIndex % 64),
                   std::clamp(ssao.samples, 1, 64),
                   0,
                   0)
    };
    commandList.setGraphicsPipeline(m_basePipeline);
    commandList.setBindGroup(0u, m_baseBindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}

void SsaoPass::renderSsaoFilter(const FrameContext& ctx, DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsaoHalfResFilteredTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureSsaoHalfResTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice)) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.ssaoHalfResFilteredTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "SsaoHalfResFilter";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.halfWidth())),
        static_cast<uint32_t>(std::max(1, targets.halfHeight()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    const std::array<RhiTextureViewHandle, 3> views = {
        targets.ssaoHalfResTextureViewHandle(),
        targets.depthTextureViewHandle(),
        targets.normalAoTextureViewHandle()
    };
    if (!ensureFilterRhiPipeline(rhiDevice) ||
        !ensureFilterBindGroup(rhiDevice, views)) {
        return;
    }

    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);

    const int halfW = std::max(1, targets.width() / 2);
    const int halfH = std::max(1, targets.height() / 2);
    const glm::vec4 pushConstants(
        static_cast<float>(halfW),
        static_cast<float>(halfH),
        ctx.camera.nearPlane,
        0.0f);
    commandList.setGraphicsPipeline(m_filterPipeline);
    commandList.setBindGroup(0u, m_filterBindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}

void SsaoPass::renderSsaoUpsample(const FrameContext& ctx, const SsaoSettings& ssao, DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsaoFilteredTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice) ||
        !(ssao.filterEnabled
              ? targets.ensureSsaoHalfResFilteredTextureView(*ctx.shared->rhiDevice)
              : targets.ensureSsaoHalfResTextureView(*ctx.shared->rhiDevice))) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.ssaoFilteredTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "SsaoUpsample";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    const std::array<RhiTextureViewHandle, 2> views = {
        ssao.filterEnabled
            ? targets.ssaoHalfResFilteredTextureViewHandle()
            : targets.ssaoHalfResTextureViewHandle(),
        targets.depthTextureViewHandle()
    };
    if (!ensureUpsampleRhiPipeline(rhiDevice) ||
        !ensureUpsampleBindGroup(rhiDevice, views)) {
        return;
    }

    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);

    const int halfW = std::max(1, targets.width() / 2);
    const int halfH = std::max(1, targets.height() / 2);
    const glm::vec4 pushConstants(
        static_cast<float>(halfW),
        static_cast<float>(halfH),
        ctx.camera.nearPlane,
        0.0f);
    commandList.setGraphicsPipeline(m_upsamplePipeline);
    commandList.setBindGroup(0u, m_upsampleBindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}

void SsaoPass::renderSsaoTemporal(const FrameContext& ctx, const SsaoSettings& ssao, DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsaoTemporalTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureSsaoHistoryTextureViews(*ctx.shared->rhiDevice) ||
        !targets.ensureSsaoFilteredTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureVelocityTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice)) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.ssaoTemporalTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "SsaoTemporal";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    const std::array<RhiTextureViewHandle, 4> views = {
        targets.ssaoFilteredTextureViewHandle(),
        targets.ssaoHistoryTexturePrevViewHandle(),
        targets.velocityTextureViewHandle(),
        targets.depthTextureViewHandle()
    };
    if (!ensureTemporalRhiPipeline(rhiDevice) ||
        !ensureTemporalBindGroup(rhiDevice, views)) {
        return;
    }

    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);
    commandList.setGraphicsPipeline(m_temporalPipeline);
    commandList.setBindGroup(0u, m_temporalBindGroup);
    const glm::vec4 pushConstants(
        static_cast<float>(std::max(1, targets.width())),
        static_cast<float>(std::max(1, targets.height())),
        ssao.historyWeight,
        ctx.camera.nearPlane);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);

    // Copy temporal result to history[current] for next frame's reprojection
    targets.copySsaoTemporalToHistory(rhiDevice);
}

bool SsaoPass::ensureNoiseTextureView(RhiDevice& rhiDevice) {
    if (m_noiseViewDevice != nullptr && m_noiseViewDevice != &rhiDevice) {
        destroyNoiseTextureView();
    }
    if (m_noiseTextureView.isValid()) {
        return true;
    }
    if (!m_noiseTexture.isValid()) {
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = m_noiseTexture;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.format = RhiTextureFormat::Rgba8Unorm;
    viewDesc.baseMip = 0u;
    viewDesc.mipCount = 1u;
    viewDesc.baseLayer = 0u;
    viewDesc.layerCount = 1u;
    m_noiseTextureView = rhiDevice.createTextureView(viewDesc);
    if (!m_noiseTextureView.isValid()) {
        return false;
    }

    m_noiseViewDevice = &rhiDevice;
    return true;
}

void SsaoPass::destroyNoiseTextureView() {
    if (m_noiseViewDevice != nullptr && m_noiseTextureView.isValid()) {
        m_noiseViewDevice->destroyTextureView(m_noiseTextureView);
    }
    m_noiseTextureView = {};
    m_noiseViewDevice = nullptr;
}

bool SsaoPass::ensureBaseRhiPipeline(RhiDevice& rhiDevice) {
    if (m_baseRhiDevice != nullptr && m_baseRhiDevice != &rhiDevice) {
        destroyBaseRhiResources();
    }
    if (m_basePipeline.isValid()) {
        return true;
    }
    m_baseRhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/ssao.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "SsaoBase.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_baseVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "SsaoBase.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_baseFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_baseVertexShader.isValid() || !m_baseFragmentShader.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    RhiSamplerDesc nearestSamplerDesc;
    nearestSamplerDesc.minFilter = RhiFilter::Nearest;
    nearestSamplerDesc.magFilter = RhiFilter::Nearest;
    nearestSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    nearestSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_baseNearestSampler = rhiDevice.createSampler(nearestSamplerDesc);

    RhiSamplerDesc noiseSamplerDesc;
    noiseSamplerDesc.minFilter = RhiFilter::Linear;
    noiseSamplerDesc.magFilter = RhiFilter::Linear;
    noiseSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    noiseSamplerDesc.addressU = RhiAddressMode::Repeat;
    noiseSamplerDesc.addressV = RhiAddressMode::Repeat;
    noiseSamplerDesc.addressW = RhiAddressMode::Repeat;
    m_baseNoiseSampler = rhiDevice.createSampler(noiseSamplerDesc);
    if (!m_baseNearestSampler.isValid() || !m_baseNoiseSampler.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "SsaoBase.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 3u; ++binding) {
        bindGroupLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    m_baseBindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_baseBindGroupLayout.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "SsaoBase.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_baseBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::mat4) * 2u +
                                                                 sizeof(glm::vec4) +
                                                                 sizeof(glm::ivec4));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_basePipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_basePipelineLayout.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "SsaoBase.Pipeline";
    pipelineDesc.vertexShader = m_baseVertexShader;
    pipelineDesc.fragmentShader = m_baseFragmentShader;
    pipelineDesc.layout = m_basePipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::R8Unorm);
    pipelineDesc.blend.attachments.push_back({});
    m_basePipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_basePipeline.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    return true;
}

bool SsaoPass::ensureBaseBindGroup(RhiDevice& rhiDevice,
                                   const std::array<RhiTextureViewHandle, 3>& views) {
    if (!ensureBaseRhiPipeline(rhiDevice)) {
        return false;
    }
    for (const RhiTextureViewHandle view : views) {
        if (!view.isValid()) {
            return false;
        }
    }
    if (m_baseBindGroup.isValid() && sameTextureViews(m_baseBoundViews, views)) {
        return true;
    }

    destroyBaseBindGroup();

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_baseBindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler =
            binding == 2u ? m_baseNoiseSampler : m_baseNearestSampler;
        bindGroupDesc.entries.push_back(entry);
    }

    m_baseBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_baseBindGroup.isValid()) {
        m_baseBoundViews = {};
        return false;
    }

    m_baseBoundViews = views;
    return true;
}

void SsaoPass::destroyBaseBindGroup() {
    if (m_baseRhiDevice != nullptr && m_baseBindGroup.isValid()) {
        m_baseRhiDevice->destroyBindGroup(m_baseBindGroup);
    }
    m_baseBindGroup = {};
    m_baseBoundViews = {};
}

void SsaoPass::destroyBaseRhiResources() {
    destroyBaseBindGroup();
    if (m_baseRhiDevice != nullptr) {
        if (m_basePipeline.isValid()) {
            m_baseRhiDevice->destroyPipeline(m_basePipeline);
        }
        if (m_baseVertexShader.isValid()) {
            m_baseRhiDevice->destroyShader(m_baseVertexShader);
        }
        if (m_baseFragmentShader.isValid()) {
            m_baseRhiDevice->destroyShader(m_baseFragmentShader);
        }
        if (m_basePipelineLayout.isValid()) {
            m_baseRhiDevice->destroyPipelineLayout(m_basePipelineLayout);
        }
        if (m_baseBindGroupLayout.isValid()) {
            m_baseRhiDevice->destroyBindGroupLayout(m_baseBindGroupLayout);
        }
        if (m_baseNearestSampler.isValid()) {
            m_baseRhiDevice->destroySampler(m_baseNearestSampler);
        }
        if (m_baseNoiseSampler.isValid()) {
            m_baseRhiDevice->destroySampler(m_baseNoiseSampler);
        }
    }

    m_basePipeline = {};
    m_baseVertexShader = {};
    m_baseFragmentShader = {};
    m_basePipelineLayout = {};
    m_baseBindGroupLayout = {};
    m_baseNearestSampler = {};
    m_baseNoiseSampler = {};
    m_baseRhiDevice = nullptr;
}

bool SsaoPass::ensureFilterRhiPipeline(RhiDevice& rhiDevice) {
    if (m_filterRhiDevice != nullptr && m_filterRhiDevice != &rhiDevice) {
        destroyFilterRhiResources();
    }
    if (m_filterPipeline.isValid()) {
        return true;
    }
    m_filterRhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/ssao_filter.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "SsaoFilter.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_filterVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "SsaoFilter.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_filterFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_filterVertexShader.isValid() || !m_filterFragmentShader.isValid()) {
        destroyFilterRhiResources();
        return false;
    }

    RhiSamplerDesc samplerDesc;
    samplerDesc.minFilter = RhiFilter::Nearest;
    samplerDesc.magFilter = RhiFilter::Nearest;
    samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    samplerDesc.addressU = RhiAddressMode::ClampToEdge;
    samplerDesc.addressV = RhiAddressMode::ClampToEdge;
    samplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_filterSampler = rhiDevice.createSampler(samplerDesc);
    if (!m_filterSampler.isValid()) {
        destroyFilterRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "SsaoFilter.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 3u; ++binding) {
        bindGroupLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    m_filterBindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_filterBindGroupLayout.isValid()) {
        destroyFilterRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "SsaoFilter.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_filterBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::vec4));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_filterPipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_filterPipelineLayout.isValid()) {
        destroyFilterRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "SsaoFilter.Pipeline";
    pipelineDesc.vertexShader = m_filterVertexShader;
    pipelineDesc.fragmentShader = m_filterFragmentShader;
    pipelineDesc.layout = m_filterPipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::R8Unorm);
    pipelineDesc.blend.attachments.push_back({});
    m_filterPipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_filterPipeline.isValid()) {
        destroyFilterRhiResources();
        return false;
    }

    return true;
}

bool SsaoPass::ensureFilterBindGroup(RhiDevice& rhiDevice,
                                     const std::array<RhiTextureViewHandle, 3>& views) {
    if (!ensureFilterRhiPipeline(rhiDevice)) {
        return false;
    }
    for (const RhiTextureViewHandle view : views) {
        if (!view.isValid()) {
            return false;
        }
    }
    if (m_filterBindGroup.isValid() && sameTextureViews(m_filterBoundViews, views)) {
        return true;
    }

    destroyFilterBindGroup();

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_filterBindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler = m_filterSampler;
        bindGroupDesc.entries.push_back(entry);
    }

    m_filterBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_filterBindGroup.isValid()) {
        m_filterBoundViews = {};
        return false;
    }

    m_filterBoundViews = views;
    return true;
}

void SsaoPass::destroyFilterBindGroup() {
    if (m_filterRhiDevice != nullptr && m_filterBindGroup.isValid()) {
        m_filterRhiDevice->destroyBindGroup(m_filterBindGroup);
    }
    m_filterBindGroup = {};
    m_filterBoundViews = {};
}

void SsaoPass::destroyFilterRhiResources() {
    destroyFilterBindGroup();
    if (m_filterRhiDevice != nullptr) {
        if (m_filterPipeline.isValid()) {
            m_filterRhiDevice->destroyPipeline(m_filterPipeline);
        }
        if (m_filterVertexShader.isValid()) {
            m_filterRhiDevice->destroyShader(m_filterVertexShader);
        }
        if (m_filterFragmentShader.isValid()) {
            m_filterRhiDevice->destroyShader(m_filterFragmentShader);
        }
        if (m_filterPipelineLayout.isValid()) {
            m_filterRhiDevice->destroyPipelineLayout(m_filterPipelineLayout);
        }
        if (m_filterBindGroupLayout.isValid()) {
            m_filterRhiDevice->destroyBindGroupLayout(m_filterBindGroupLayout);
        }
        if (m_filterSampler.isValid()) {
            m_filterRhiDevice->destroySampler(m_filterSampler);
        }
    }

    m_filterPipeline = {};
    m_filterVertexShader = {};
    m_filterFragmentShader = {};
    m_filterPipelineLayout = {};
    m_filterBindGroupLayout = {};
    m_filterSampler = {};
    m_filterRhiDevice = nullptr;
}

bool SsaoPass::ensureUpsampleRhiPipeline(RhiDevice& rhiDevice) {
    if (m_upsampleRhiDevice != nullptr && m_upsampleRhiDevice != &rhiDevice) {
        destroyUpsampleRhiResources();
    }
    if (m_upsamplePipeline.isValid()) {
        return true;
    }
    m_upsampleRhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/ssao_upsample.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "SsaoUpsample.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_upsampleVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "SsaoUpsample.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_upsampleFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_upsampleVertexShader.isValid() || !m_upsampleFragmentShader.isValid()) {
        destroyUpsampleRhiResources();
        return false;
    }

    RhiSamplerDesc nearestSamplerDesc;
    nearestSamplerDesc.minFilter = RhiFilter::Nearest;
    nearestSamplerDesc.magFilter = RhiFilter::Nearest;
    nearestSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    nearestSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_upsampleNearestSampler = rhiDevice.createSampler(nearestSamplerDesc);

    RhiSamplerDesc linearSamplerDesc;
    linearSamplerDesc.minFilter = RhiFilter::Linear;
    linearSamplerDesc.magFilter = RhiFilter::Linear;
    linearSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    linearSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    linearSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    linearSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_upsampleLinearSampler = rhiDevice.createSampler(linearSamplerDesc);
    if (!m_upsampleNearestSampler.isValid() || !m_upsampleLinearSampler.isValid()) {
        destroyUpsampleRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "SsaoUpsample.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 2u; ++binding) {
        bindGroupLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    m_upsampleBindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_upsampleBindGroupLayout.isValid()) {
        destroyUpsampleRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "SsaoUpsample.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_upsampleBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::vec4));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_upsamplePipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_upsamplePipelineLayout.isValid()) {
        destroyUpsampleRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "SsaoUpsample.Pipeline";
    pipelineDesc.vertexShader = m_upsampleVertexShader;
    pipelineDesc.fragmentShader = m_upsampleFragmentShader;
    pipelineDesc.layout = m_upsamplePipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::R8Unorm);
    pipelineDesc.blend.attachments.push_back({});
    m_upsamplePipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_upsamplePipeline.isValid()) {
        destroyUpsampleRhiResources();
        return false;
    }

    return true;
}

bool SsaoPass::ensureUpsampleBindGroup(RhiDevice& rhiDevice,
                                       const std::array<RhiTextureViewHandle, 2>& views) {
    if (!ensureUpsampleRhiPipeline(rhiDevice)) {
        return false;
    }
    for (const RhiTextureViewHandle view : views) {
        if (!view.isValid()) {
            return false;
        }
    }
    if (m_upsampleBindGroup.isValid() && sameTextureViews(m_upsampleBoundViews, views)) {
        return true;
    }

    destroyUpsampleBindGroup();

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_upsampleBindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler =
            binding == 0u ? m_upsampleNearestSampler : m_upsampleLinearSampler;
        bindGroupDesc.entries.push_back(entry);
    }

    m_upsampleBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_upsampleBindGroup.isValid()) {
        m_upsampleBoundViews = {};
        return false;
    }

    m_upsampleBoundViews = views;
    return true;
}

void SsaoPass::destroyUpsampleBindGroup() {
    if (m_upsampleRhiDevice != nullptr && m_upsampleBindGroup.isValid()) {
        m_upsampleRhiDevice->destroyBindGroup(m_upsampleBindGroup);
    }
    m_upsampleBindGroup = {};
    m_upsampleBoundViews = {};
}

void SsaoPass::destroyUpsampleRhiResources() {
    destroyUpsampleBindGroup();
    if (m_upsampleRhiDevice != nullptr) {
        if (m_upsamplePipeline.isValid()) {
            m_upsampleRhiDevice->destroyPipeline(m_upsamplePipeline);
        }
        if (m_upsampleVertexShader.isValid()) {
            m_upsampleRhiDevice->destroyShader(m_upsampleVertexShader);
        }
        if (m_upsampleFragmentShader.isValid()) {
            m_upsampleRhiDevice->destroyShader(m_upsampleFragmentShader);
        }
        if (m_upsamplePipelineLayout.isValid()) {
            m_upsampleRhiDevice->destroyPipelineLayout(m_upsamplePipelineLayout);
        }
        if (m_upsampleBindGroupLayout.isValid()) {
            m_upsampleRhiDevice->destroyBindGroupLayout(m_upsampleBindGroupLayout);
        }
        if (m_upsampleNearestSampler.isValid()) {
            m_upsampleRhiDevice->destroySampler(m_upsampleNearestSampler);
        }
        if (m_upsampleLinearSampler.isValid()) {
            m_upsampleRhiDevice->destroySampler(m_upsampleLinearSampler);
        }
    }

    m_upsamplePipeline = {};
    m_upsampleVertexShader = {};
    m_upsampleFragmentShader = {};
    m_upsamplePipelineLayout = {};
    m_upsampleBindGroupLayout = {};
    m_upsampleNearestSampler = {};
    m_upsampleLinearSampler = {};
    m_upsampleRhiDevice = nullptr;
}

bool SsaoPass::ensureTemporalRhiPipeline(RhiDevice& rhiDevice) {
    if (m_temporalRhiDevice != nullptr && m_temporalRhiDevice != &rhiDevice) {
        destroyTemporalRhiResources();
    }
    if (m_temporalPipeline.isValid()) {
        return true;
    }
    m_temporalRhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/ssao_temporal.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "SsaoTemporal.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_temporalVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "SsaoTemporal.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_temporalFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_temporalVertexShader.isValid() || !m_temporalFragmentShader.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiSamplerDesc nearestSamplerDesc;
    nearestSamplerDesc.minFilter = RhiFilter::Nearest;
    nearestSamplerDesc.magFilter = RhiFilter::Nearest;
    nearestSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    nearestSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_temporalNearestSampler = rhiDevice.createSampler(nearestSamplerDesc);

    RhiSamplerDesc linearSamplerDesc;
    linearSamplerDesc.minFilter = RhiFilter::Linear;
    linearSamplerDesc.magFilter = RhiFilter::Linear;
    linearSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    linearSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    linearSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    linearSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_temporalLinearSampler = rhiDevice.createSampler(linearSamplerDesc);
    if (!m_temporalNearestSampler.isValid() || !m_temporalLinearSampler.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "SsaoTemporal.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 4u; ++binding) {
        bindGroupLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    m_temporalBindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_temporalBindGroupLayout.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "SsaoTemporal.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_temporalBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::vec4));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_temporalPipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_temporalPipelineLayout.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "SsaoTemporal.Pipeline";
    pipelineDesc.vertexShader = m_temporalVertexShader;
    pipelineDesc.fragmentShader = m_temporalFragmentShader;
    pipelineDesc.layout = m_temporalPipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::R8Unorm);
    pipelineDesc.blend.attachments.push_back({});
    m_temporalPipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_temporalPipeline.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    return true;
}

bool SsaoPass::ensureTemporalBindGroup(RhiDevice& rhiDevice,
                                       const std::array<RhiTextureViewHandle, 4>& views) {
    if (!ensureTemporalRhiPipeline(rhiDevice)) {
        return false;
    }
    for (const RhiTextureViewHandle view : views) {
        if (!view.isValid()) {
            return false;
        }
    }
    if (m_temporalBindGroup.isValid() && sameTextureViews(m_temporalBoundViews, views)) {
        return true;
    }

    destroyTemporalBindGroup();

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_temporalBindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler =
            binding == 1u ? m_temporalLinearSampler : m_temporalNearestSampler;
        bindGroupDesc.entries.push_back(entry);
    }

    m_temporalBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_temporalBindGroup.isValid()) {
        m_temporalBoundViews = {};
        return false;
    }

    m_temporalBoundViews = views;
    return true;
}

void SsaoPass::destroyTemporalBindGroup() {
    if (m_temporalRhiDevice != nullptr && m_temporalBindGroup.isValid()) {
        m_temporalRhiDevice->destroyBindGroup(m_temporalBindGroup);
    }
    m_temporalBindGroup = {};
    m_temporalBoundViews = {};
}

void SsaoPass::destroyTemporalRhiResources() {
    destroyTemporalBindGroup();
    if (m_temporalRhiDevice != nullptr) {
        if (m_temporalPipeline.isValid()) {
            m_temporalRhiDevice->destroyPipeline(m_temporalPipeline);
        }
        if (m_temporalVertexShader.isValid()) {
            m_temporalRhiDevice->destroyShader(m_temporalVertexShader);
        }
        if (m_temporalFragmentShader.isValid()) {
            m_temporalRhiDevice->destroyShader(m_temporalFragmentShader);
        }
        if (m_temporalPipelineLayout.isValid()) {
            m_temporalRhiDevice->destroyPipelineLayout(m_temporalPipelineLayout);
        }
        if (m_temporalBindGroupLayout.isValid()) {
            m_temporalRhiDevice->destroyBindGroupLayout(m_temporalBindGroupLayout);
        }
        if (m_temporalNearestSampler.isValid()) {
            m_temporalRhiDevice->destroySampler(m_temporalNearestSampler);
        }
        if (m_temporalLinearSampler.isValid()) {
            m_temporalRhiDevice->destroySampler(m_temporalLinearSampler);
        }
    }

    m_temporalPipeline = {};
    m_temporalVertexShader = {};
    m_temporalFragmentShader = {};
    m_temporalPipelineLayout = {};
    m_temporalBindGroupLayout = {};
    m_temporalNearestSampler = {};
    m_temporalLinearSampler = {};
    m_temporalRhiDevice = nullptr;
}
