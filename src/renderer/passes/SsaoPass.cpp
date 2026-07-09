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
    m_ssaoFilterShader = resourceMgr.getShader("ssao_filter");
    m_ssaoUpsampleShader = resourceMgr.getShader("ssao_upsample");
    m_noiseTexture = resourceMgr.getTexture2DHandle("shader_noise2d");
}

void SsaoPass::shutdown() {
    destroyTemporalRhiResources();
    m_ssaoShader = nullptr;
    m_ssaoFilterShader = nullptr;
    m_ssaoUpsampleShader = nullptr;
    m_noiseTexture = {};
}

void SsaoPass::execute(const FrameContext& ctx, const RenderSettings& settings,
                       DeferredRenderTargets& targets) {
    if (!settings.ssao.enabled) return;

    renderSsaoBase(ctx, settings.ssao, targets);
    if (settings.ssao.filterEnabled && m_ssaoFilterShader != nullptr) {
        renderSsaoFilter(ctx, targets);
    }
    if (m_ssaoUpsampleShader != nullptr) {
        renderSsaoUpsample(ctx, settings.ssao, targets);
    }
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
    if (m_ssaoFilterShader == nullptr) return;
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsaoHalfResFilteredTextureView(*ctx.shared->rhiDevice)) {
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
    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    const int halfW = std::max(1, targets.width() / 2);
    const int halfH = std::max(1, targets.height() / 2);

    m_ssaoFilterShader->use();
    m_ssaoFilterShader->setInt("uSsaoTex", 0);
    m_ssaoFilterShader->setInt("uDepthTex", 1);
    m_ssaoFilterShader->setInt("uNormalAoTex", 2);
    m_ssaoFilterShader->setVec2("uScreenSize",
        glm::vec2(static_cast<float>(halfW), static_cast<float>(halfH)));
    m_ssaoFilterShader->setFloat("uNear", ctx.camera.nearPlane);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.ssaoHalfResTextureHandle()));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.normalAoTextureHandle()));
    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_ssaoFilterShader);

    for (int i = 2; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}

void SsaoPass::renderSsaoUpsample(const FrameContext& ctx, const SsaoSettings& ssao, DeferredRenderTargets& targets) {
    if (m_ssaoUpsampleShader == nullptr) return;
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsaoFilteredTextureView(*ctx.shared->rhiDevice)) {
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
    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    const int halfW = std::max(1, targets.width() / 2);
    const int halfH = std::max(1, targets.height() / 2);

    m_ssaoUpsampleShader->use();
    m_ssaoUpsampleShader->setInt("uSsaoHalfResTex", 0);
    m_ssaoUpsampleShader->setInt("uDepthTex", 1);
    m_ssaoUpsampleShader->setVec2("uHalfResSize",
        glm::vec2(static_cast<float>(halfW), static_cast<float>(halfH)));
    m_ssaoUpsampleShader->setFloat("uNear", ctx.camera.nearPlane);

    // Read from filtered half-res if filter is enabled, otherwise raw half-res
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ssao.filterEnabled && m_ssaoFilterShader != nullptr
        ? renderer::rhi::gl::textureId(targets.ssaoHalfResFilteredTextureHandle())
        : renderer::rhi::gl::textureId(targets.ssaoHalfResTextureHandle()));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));
    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_ssaoUpsampleShader);

    for (int i = 1; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
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
