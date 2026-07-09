#include "SsaoPass.h"
#include "../core/RenderScene.h"
#include "../targets/DeferredRenderTargets.h"
#include "../core/Shader.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"
#include "../../resource/ResourceMgr.h"

#include <glad/glad.h>

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
    m_ssaoShader = resourceMgr.getShader("ssao");
    m_noiseTexture = resourceMgr.getTexture2DHandle("shader_noise2d");
}

void SsaoPass::shutdown() {
    destroyUpsampleRhiResources();
    destroyFilterRhiResources();
    destroyTemporalRhiResources();
    m_ssaoShader = nullptr;
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
    if (m_ssaoShader == nullptr) return;
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsaoHalfResTextureView(*ctx.shared->rhiDevice)) {
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
    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    m_ssaoShader->use();
    m_ssaoShader->setInt("uDepthTex", 0);
    m_ssaoShader->setInt("uNormalAoTex", 1);
    m_ssaoShader->setInt("uNoiseTex", 2);
    const glm::mat4& proj = ctx.camera.projection;
    m_ssaoShader->setMat4("uProjection", proj);
    m_ssaoShader->setMat4("uInvProjection", glm::inverse(proj));
    m_ssaoShader->setFloat("uRadius", ssao.radius);
    m_ssaoShader->setFloat("uStrength", ssao.strength);
    // Half-res: invResolution refers to the half-res viewport for UV computation
    const int halfW = std::max(1, targets.width() / 2);
    const int halfH = std::max(1, targets.height() / 2);
    m_ssaoShader->setVec2("uInvResolution", glm::vec2(1.0f / halfW, 1.0f / halfH));
    m_ssaoShader->setInt("uFrameIndex", static_cast<int>(ctx.frameIndex % 64));
    m_ssaoShader->setInt("uSamples", std::clamp(ssao.samples, 1, 64));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.normalAoTextureHandle()));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(m_noiseTexture));
    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_ssaoShader);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
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
