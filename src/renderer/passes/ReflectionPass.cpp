#include "ReflectionPass.h"
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
#include <algorithm>
#include <cstddef>
#include <optional>

namespace {
[[nodiscard]] bool sameTextureView(const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

[[nodiscard]] bool sameTextureViews(const std::array<RhiTextureViewHandle, 7>& lhs,
                                    const std::array<RhiTextureViewHandle, 7>& rhs) {
    for (size_t i = 0u; i < lhs.size(); ++i) {
        if (!sameTextureView(lhs[i], rhs[i])) {
            return false;
        }
    }
    return true;
}
} // namespace

void ReflectionPass::init(ResourceMgr& resourceMgr) {
    m_reflectionShader = resourceMgr.getShader("reflection_probe");
    m_reflectionFilterShader = resourceMgr.getShader("reflection_filter");
    m_noiseTexture = resourceMgr.getTexture2DHandle("shader_noise2d");
    m_rippleNormalTexture = resourceMgr.getTexture2DHandle("shader_ripple_normal");
    m_resourceMgr = &resourceMgr;
}

void ReflectionPass::shutdown() {
    destroyTemporalRhiResources();
    m_reflectionShader = nullptr;
    m_reflectionFilterShader = nullptr;
    m_noiseTexture = {};
    m_rippleNormalTexture = {};
    m_resourceMgr = nullptr;
}

void ReflectionPass::execute(const FrameContext& ctx, const RenderSettings& settings,
                              DeferredRenderTargets& targets) {
    if (m_reflectionShader == nullptr) return;

    renderReflection(ctx, settings, targets);

    if (settings.reflection.filterEnabled &&
        settings.debug.reflectionDebugMode == 0 &&
        m_reflectionFilterShader != nullptr) {
        renderFilter(ctx, settings.reflection, targets);
    }
    if (settings.reflection.temporalEnabled &&
        settings.debug.reflectionDebugMode == 0 &&
        ctx.hasPreviousFrame) {
        renderTemporal(ctx, settings.reflection, targets);
    }
}

void ReflectionPass::renderReflection(const FrameContext& ctx, const RenderSettings& settings,
                                       DeferredRenderTargets& targets) {
    if (m_reflectionShader == nullptr) return;
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureReflectionTextureView(*ctx.shared->rhiDevice)) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.reflectionTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "Reflection";
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

    m_reflectionShader->use();
    m_reflectionShader->setInt("uSceneLightingTex", 0);
    m_reflectionShader->setInt("uDepthTex", 1);
    m_reflectionShader->setInt("uNormalAoTex", 2);
    m_reflectionShader->setInt("uMaterialTex", 3);
    m_reflectionShader->setInt("uMaterialAuxTex", 4);
    m_reflectionShader->setInt("uSkyCaptureTex", 5);
    m_reflectionShader->setInt("uAtmosphereLut", 6);
    m_reflectionShader->setInt("uVoxelLightTex", 7);
    m_reflectionShader->setInt("uNoiseTex", 8);
    m_reflectionShader->setInt("uRippleNormalTex", 9);
    m_reflectionShader->setMat4("uViewProj",
        settings.taa.enabled ? ctx.camera.jitteredViewProj : ctx.camera.viewProj);
    m_reflectionShader->setMat4("uInvViewProj",
        settings.taa.enabled ? ctx.camera.jitteredInvViewProj : ctx.camera.invViewProj);
    m_reflectionShader->setVec3("uCameraPos", ctx.camera.position);
    m_reflectionShader->setVec3("uSunDirection", ctx.skyColors.sunDirection);
    m_reflectionShader->setVec3("uMoonDirection", ctx.skyColors.moonDirection);
    m_reflectionShader->setFloat("uSkyIntensity", ctx.skyIntensity);
    m_reflectionShader->setFloat("uMoonVisibility", ctx.skyColors.moonVisibility);
    m_reflectionShader->setFloat("uWeatherWetness", ctx.weather.wetness);
    m_reflectionShader->setFloat("uSurfaceWetness", ctx.weather.surfaceWetness);
    m_reflectionShader->setFloat("uSkyWetness", ctx.weather.skyWetness);
    m_reflectionShader->setFloat("uFogWetness", ctx.weather.fogWetness);
    m_reflectionShader->setFloat("uCloudWetness", ctx.weather.cloudWetness);
    m_reflectionShader->setFloat("uTime", ctx.shaderTime);
    m_reflectionShader->setInt("uReflectionDebugMode", settings.debug.reflectionDebugMode);
    m_reflectionShader->setInt("uRainWetSurfacesEnabled", settings.weather.rainLinesEnabled ? 1 : 0);
    m_reflectionShader->setInt("uRainSurfaceRipplesEnabled", settings.weather.surfaceRipplesEnabled ? 1 : 0);
    m_reflectionShader->setFloat("uNearPlane", ctx.camera.nearPlane);
    m_reflectionShader->setFloat("uFarPlane", ctx.camera.farPlane);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.sceneLightingTextureHandle()));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.normalAoTextureHandle()));
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.materialTextureHandle()));
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.materialAuxTextureHandle()));
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.skyCaptureTextureHandle()));
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_3D, renderer::rhi::gl::textureId(targets.atmosphereLutTextureHandle()));
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.voxelLightTextureHandle()));
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(m_noiseTexture));
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(m_rippleNormalTexture));
    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_reflectionShader);

    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, 0);
    for (int unit = 5; unit >= 0; --unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_3D, 0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}

void ReflectionPass::renderFilter(const FrameContext& ctx, const ReflectionSettings& reflection,
                                   DeferredRenderTargets& targets) {
    if (m_reflectionFilterShader == nullptr) return;

    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureReflectionTextureView(*ctx.shared->rhiDevice)) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.reflectionTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "ReflectionFilter";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    targets.copyReflectionToTemporalScratch(rhiDevice);
    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    m_reflectionFilterShader->use();
    m_reflectionFilterShader->setInt("uReflectionTex", 0);
    m_reflectionFilterShader->setInt("uDepthTex", 1);
    m_reflectionFilterShader->setInt("uNormalAoTex", 2);
    m_reflectionFilterShader->setInt("uMaterialTex", 3);
    m_reflectionFilterShader->setInt("uMaterialAuxTex", 4);
    m_reflectionFilterShader->setVec2("uScreenSize",
        glm::vec2(static_cast<float>(std::max(1, targets.width())),
                   static_cast<float>(std::max(1, targets.height()))));
    m_reflectionFilterShader->setFloat("uFilterStrength", reflection.filterStrength);
    m_reflectionFilterShader->setFloat("uSurfaceWetness", ctx.weather.surfaceWetness);
    m_reflectionFilterShader->setMat4("uInvViewProj", ctx.camera.invViewProj);
    m_reflectionFilterShader->setVec3("uCameraPos", ctx.camera.position);
    m_reflectionFilterShader->setFloat("uNearPlane", ctx.camera.nearPlane);
    m_reflectionFilterShader->setFloat("uFarPlane", ctx.camera.farPlane);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.reflectionTemporalScratchTextureHandle()));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.normalAoTextureHandle()));
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.materialTextureHandle()));
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.materialAuxTextureHandle()));
    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_reflectionFilterShader);

    for (int i = 4; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}

void ReflectionPass::renderTemporal(const FrameContext& ctx, const ReflectionSettings& reflection,
                                     DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureReflectionTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureReflectionTemporalScratchTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureHistoryReflectionTextureViews(*ctx.shared->rhiDevice) ||
        !targets.ensureVelocityTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice)) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.reflectionTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "ReflectionTemporal";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    targets.copyReflectionToTemporalScratch(rhiDevice);
    const std::array<RhiTextureViewHandle, 7> views = {
        targets.reflectionTemporalScratchTextureViewHandle(),
        targets.historyReflectionTexturePrevViewHandle(),
        targets.velocityTextureViewHandle(),
        targets.depthTextureViewHandle(),
        targets.normalAoTextureViewHandle(),
        targets.materialTextureViewHandle(),
        targets.materialAuxTextureViewHandle()
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
        reflection.historyWeight,
        ctx.camera.nearPlane);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}

bool ReflectionPass::ensureTemporalRhiPipeline(RhiDevice& rhiDevice) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyTemporalRhiResources();
    }
    if (m_temporalPipeline.isValid()) {
        return true;
    }
    m_rhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/reflection_temporal.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "ReflectionTemporal.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_temporalVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "ReflectionTemporal.Fragment";
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
    bindGroupLayoutDesc.debugName = "ReflectionTemporal.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 7u; ++binding) {
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
    pipelineLayoutDesc.debugName = "ReflectionTemporal.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_temporalBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::vec4));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_temporalPipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_temporalPipelineLayout.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "ReflectionTemporal.Pipeline";
    pipelineDesc.vertexShader = m_temporalVertexShader;
    pipelineDesc.fragmentShader = m_temporalFragmentShader;
    pipelineDesc.layout = m_temporalPipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
    pipelineDesc.blend.attachments.push_back({});
    m_temporalPipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_temporalPipeline.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    return true;
}

bool ReflectionPass::ensureTemporalBindGroup(RhiDevice& rhiDevice,
                                             const std::array<RhiTextureViewHandle, 7>& views) {
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

void ReflectionPass::destroyTemporalBindGroup() {
    if (m_rhiDevice != nullptr && m_temporalBindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(m_temporalBindGroup);
    }
    m_temporalBindGroup = {};
    m_temporalBoundViews = {};
}

void ReflectionPass::destroyTemporalRhiResources() {
    destroyTemporalBindGroup();
    if (m_rhiDevice != nullptr) {
        if (m_temporalPipeline.isValid()) {
            m_rhiDevice->destroyPipeline(m_temporalPipeline);
        }
        if (m_temporalVertexShader.isValid()) {
            m_rhiDevice->destroyShader(m_temporalVertexShader);
        }
        if (m_temporalFragmentShader.isValid()) {
            m_rhiDevice->destroyShader(m_temporalFragmentShader);
        }
        if (m_temporalPipelineLayout.isValid()) {
            m_rhiDevice->destroyPipelineLayout(m_temporalPipelineLayout);
        }
        if (m_temporalBindGroupLayout.isValid()) {
            m_rhiDevice->destroyBindGroupLayout(m_temporalBindGroupLayout);
        }
        if (m_temporalNearestSampler.isValid()) {
            m_rhiDevice->destroySampler(m_temporalNearestSampler);
        }
        if (m_temporalLinearSampler.isValid()) {
            m_rhiDevice->destroySampler(m_temporalLinearSampler);
        }
    }

    m_temporalPipeline = {};
    m_temporalVertexShader = {};
    m_temporalFragmentShader = {};
    m_temporalPipelineLayout = {};
    m_temporalBindGroupLayout = {};
    m_temporalNearestSampler = {};
    m_temporalLinearSampler = {};
    m_rhiDevice = nullptr;
}
