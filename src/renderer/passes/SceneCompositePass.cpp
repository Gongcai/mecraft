#include "SceneCompositePass.h"
#include "../core/RenderScene.h"
#include "../targets/DeferredRenderTargets.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"

#include <glm/glm.hpp>
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
    for (size_t index = 0u; index < lhs.size(); ++index) {
        if (!sameTextureView(lhs[index], rhs[index])) {
            return false;
        }
    }
    return true;
}

struct alignas(16) SceneCompositeParams {
    glm::mat4 invViewProj;
    glm::vec4 cameraPosSkyIntensity;
    glm::vec4 sunDirectionVisibility;
    glm::vec4 moonDirectionVisibility;
    glm::vec4 atmosphereComposite;
    glm::vec4 reflectionWater;
    glm::vec4 status;
    glm::ivec4 flags0;
    glm::ivec4 flags1;
};
static_assert(sizeof(SceneCompositeParams) == 192u);
} // namespace

void SceneCompositePass::init(GameResources&) {}

void SceneCompositePass::shutdown() {
    destroyRhiResources();
}

RgPassHandle SceneCompositePass::addGraphPasses(RenderGraph& graph, const FrameContext& ctx,
                                                const RenderSettings& settings, DeferredRenderTargets& targets,
                                                const GraphResources& resources, const RgPassHandle dependency) {
    if (!dependency.isValid() || !resources.sceneLighting.isValid() || !resources.reflection.isValid() ||
        !resources.cloud.isValid() || !resources.depth.isValid() || !resources.normalAo.isValid() ||
        !resources.material.isValid() || !resources.materialAux.isValid() || !resources.skyCapture.isValid() ||
        !resources.albedo.isValid() || !resources.ssgi.isValid() || !resources.output.isValid() ||
        !resources.reactiveMask.isValid() || !resources.transparencyMask.isValid()) {
        return {};
    }

    RenderGraphPassBuilder composite = graph.addPass({"SceneComposite", RgPassType::Graphics, RhiQueueType::Graphics,
                                                      /*threadSafeRecord=*/true});
    composite.dependsOn(dependency)
        .readTexture(resources.sceneLighting, RhiResourceState::ShaderRead)
        .readTexture(resources.reflection, RhiResourceState::ShaderRead)
        .readTexture(resources.cloud, RhiResourceState::ShaderRead)
        .readTexture(resources.depth, RhiResourceState::DepthRead)
        .readTexture(resources.normalAo, RhiResourceState::ShaderRead)
        .readTexture(resources.material, RhiResourceState::ShaderRead)
        .readTexture(resources.materialAux, RhiResourceState::ShaderRead)
        .readTexture(resources.skyCapture, RhiResourceState::ShaderRead)
        .readTexture(resources.albedo, RhiResourceState::ShaderRead)
        .readTexture(resources.ssgi, RhiResourceState::ShaderRead)
        .writeTexture(resources.output, RhiResourceState::RenderTarget)
        .writeTexture(resources.reactiveMask, RhiResourceState::RenderTarget)
        .writeTexture(resources.transparencyMask, RhiResourceState::RenderTarget)
        .setExecute([this, frame = &ctx, frameTargets = &targets, frameSettings = settings](RgPassContext& pass) {
            return recordGraphPass(pass.commandList(), *frame, frameSettings, *frameTargets);
        });
    return composite.handle();
}

bool SceneCompositePass::recordGraphPass(RhiCommandList& commandList, const FrameContext& ctx,
                                         const RenderSettings& settings, DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSceneCompositeTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureSceneLightingTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureReflectionTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureCloudTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice) ||
        !targets.ensureSkyCaptureTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureSsgiTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureReactiveMaskTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureTransparencyMaskTextureView(*ctx.shared->rhiDevice)) {
        return false;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!ensureRhiPipelines(rhiDevice)) {
        return false;
    }
    const std::array<RhiTextureViewHandle, 10> views = {
        targets.sceneLightingTextureViewHandle(), targets.reflectionTextureViewHandle(),
        targets.cloudTextureViewHandle(),         targets.depthTextureViewHandle(),
        targets.normalAoTextureViewHandle(),      targets.materialTextureViewHandle(),
        targets.materialAuxTextureViewHandle(),   targets.skyCaptureTextureViewHandle(),
        targets.albedoTextureViewHandle(),        targets.ssgiTextureViewHandle()};
    if (!ensureBindGroup(rhiDevice, views)) {
        return false;
    }

    SceneCompositeParams params{};
    params.invViewProj = usesTemporalProjectionJitter(settings.upscale.type, settings.taa.enabled)
                             ? ctx.camera.jitteredInvViewProj
                             : ctx.camera.invViewProj;
    params.cameraPosSkyIntensity = glm::vec4(ctx.camera.position, ctx.skyIntensity);
    params.sunDirectionVisibility = glm::vec4(ctx.skyColors.sunDirection, ctx.skyColors.sunVisibility);
    params.moonDirectionVisibility = glm::vec4(ctx.skyColors.moonDirection, ctx.skyColors.moonVisibility);
    params.atmosphereComposite = glm::vec4(ctx.skyColors.moonPhaseAngle, ctx.weather.skyWetness, ctx.weather.wetness,
                                           settings.cloud.sceneCloudCompositeStrength);
    params.reflectionWater = glm::vec4(settings.reflection.sceneReflectionCompositeStrength, 0.4f, 0.14f, 0.08f);
    params.status = glm::vec4(0.0f, 0.0f, ctx.preExposure, 0.0f);
    params.flags0 = glm::ivec4(settings.ssgi.enabled ? 1 : 0, 0, 0, 0);
    params.flags1 = glm::ivec4(settings.debug.reflectionDebugMode, ctx.eyeInWater ? 1 : 0, 0, 0);

    RhiColorAttachment colorAttachments[3];
    colorAttachments[0].view = targets.sceneCompositeTextureViewHandle();
    colorAttachments[0].loadOp = RhiLoadOp::Clear;
    colorAttachments[0].storeOp = RhiStoreOp::Store;
    colorAttachments[0].clearColor[0] = 0.0f;
    colorAttachments[0].clearColor[1] = 0.0f;
    colorAttachments[0].clearColor[2] = 0.0f;
    colorAttachments[0].clearColor[3] = 1.0f;
    for (size_t index = 1u; index < 3u; ++index) {
        colorAttachments[index].loadOp = RhiLoadOp::Clear;
        colorAttachments[index].storeOp = RhiStoreOp::Store;
        colorAttachments[index].clearColor[0] = 0.0f;
        colorAttachments[index].clearColor[1] = 0.0f;
        colorAttachments[index].clearColor[2] = 0.0f;
        colorAttachments[index].clearColor[3] = 0.0f;
    }
    colorAttachments[1].view = targets.reactiveMaskTextureViewHandle();
    colorAttachments[2].view = targets.transparencyMaskTextureViewHandle();

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "SceneComposite";
    renderingInfo.renderArea = {0, 0, static_cast<uint32_t>(std::max(1, targets.width())),
                                static_cast<uint32_t>(std::max(1, targets.height()))};
    renderingInfo.colorAttachments = colorAttachments;
    renderingInfo.colorAttachmentCount = 3u;

    commandList.bufferBarrier({m_uniformBuffer, RhiResourceState::UniformBuffer, RhiResourceState::TransferDst});
    commandList.updateBuffer(m_uniformBuffer, 0u, &params, sizeof(params));
    commandList.bufferBarrier({m_uniformBuffer, RhiResourceState::TransferDst, RhiResourceState::UniformBuffer});
    commandList.beginRendering(renderingInfo);
    commandList.setGraphicsPipeline(m_pipeline);
    commandList.setBindGroup(0u, m_bindGroup);
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    return true;
}

bool SceneCompositePass::ensureRhiPipelines(RhiDevice& rhiDevice) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyRhiResources();
    }
    if (m_pipeline.isValid()) {
        return true;
    }
    m_rhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/scene_composite.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "SceneComposite.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_vertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "SceneComposite.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_fragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_vertexShader.isValid() || !m_fragmentShader.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiBufferDesc uniformBufferDesc;
    uniformBufferDesc.debugName = "SceneComposite.Params";
    uniformBufferDesc.size = sizeof(SceneCompositeParams);
    uniformBufferDesc.usage = rhiFlag(RhiBufferUsage::Uniform) | rhiFlag(RhiBufferUsage::TransferDst);
    uniformBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    uniformBufferDesc.initialState = RhiResourceState::UniformBuffer;
    uniformBufferDesc.memoryCategory = RhiMemoryCategory::Uniform;
    m_uniformBuffer = rhiDevice.createBuffer(uniformBufferDesc, nullptr, 0u);
    if (!m_uniformBuffer.isValid()) {
        destroyRhiResources();
        return false;
    }

    auto createSampler = [&](const RhiFilter filter, const RhiMipmapMode mipmapMode) {
        RhiSamplerDesc samplerDesc;
        samplerDesc.minFilter = filter;
        samplerDesc.magFilter = filter;
        samplerDesc.mipmapMode = mipmapMode;
        samplerDesc.addressU = RhiAddressMode::ClampToEdge;
        samplerDesc.addressV = RhiAddressMode::ClampToEdge;
        samplerDesc.addressW = RhiAddressMode::ClampToEdge;
        return rhiDevice.createSampler(samplerDesc);
    };
    m_nearestSampler = createSampler(RhiFilter::Nearest, RhiMipmapMode::Nearest);
    m_linearSampler = createSampler(RhiFilter::Linear, RhiMipmapMode::Nearest);
    if (!m_nearestSampler.isValid() || !m_linearSampler.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "SceneComposite.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 10u; ++binding) {
        bindGroupLayoutDesc.entries.push_back(
            {binding, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u});
    }
    bindGroupLayoutDesc.entries.push_back({10u, RhiBindingType::UniformBuffer, rhiFlag(RhiShaderStage::Fragment), 1u});
    m_bindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_bindGroupLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "SceneComposite.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout);
    m_pipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_pipelineLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "SceneComposite.Pipeline";
    pipelineDesc.vertexShader = m_vertexShader;
    pipelineDesc.fragmentShader = m_fragmentShader;
    pipelineDesc.layout = m_pipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats = {RhiTextureFormat::Rgba16Float, RhiTextureFormat::R8Unorm, RhiTextureFormat::R8Unorm};
    pipelineDesc.blend.attachments.resize(3u);
    m_pipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_pipeline.isValid()) {
        destroyRhiResources();
        return false;
    }

    return true;
}

bool SceneCompositePass::ensureBindGroup(RhiDevice& rhiDevice, const std::array<RhiTextureViewHandle, 10>& views) {
    if (!ensureRhiPipelines(rhiDevice)) {
        return false;
    }
    for (uint32_t binding = 0u; binding < views.size(); ++binding) {
        if (!views[binding].isValid()) {
            return false;
        }
    }

    if (m_bindGroup.isValid() && sameTextureViews(m_boundViews, views)) {
        return true;
    }
    if (m_bindGroup.isValid()) {
        rhiDevice.destroyBindGroup(m_bindGroup);
        m_bindGroup = {};
    }

    const RhiSamplerHandle samplers[10] = {m_linearSampler,  m_linearSampler,  m_linearSampler,  m_nearestSampler,
                                           m_nearestSampler, m_nearestSampler, m_nearestSampler, m_linearSampler,
                                           m_nearestSampler, m_linearSampler};
    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_bindGroupLayout;
    for (uint32_t binding = 0u; binding < views.size(); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler = samplers[binding];
        bindGroupDesc.entries.push_back(entry);
    }

    RhiBindGroupEntry uniformEntry;
    uniformEntry.binding = 10u;
    uniformEntry.resource.buffer.buffer = m_uniformBuffer;
    uniformEntry.resource.buffer.offset = 0u;
    uniformEntry.resource.buffer.range = sizeof(SceneCompositeParams);
    bindGroupDesc.entries.push_back(uniformEntry);

    m_bindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_bindGroup.isValid()) {
        m_boundViews = {};
        return false;
    }
    m_boundViews = views;
    return true;
}

void SceneCompositePass::destroyBindGroups() {
    if (m_rhiDevice != nullptr && m_bindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(m_bindGroup);
    }
    m_bindGroup = {};
    m_boundViews = {};
}

void SceneCompositePass::destroyRhiResources() {
    destroyBindGroups();
    if (m_rhiDevice != nullptr) {
        if (m_pipeline.isValid()) {
            m_rhiDevice->destroyPipeline(m_pipeline);
        }
        const RhiShaderHandle shaders[] = {m_vertexShader, m_fragmentShader};
        for (const RhiShaderHandle shader : shaders) {
            if (shader.isValid()) {
                m_rhiDevice->destroyShader(shader);
            }
        }
        if (m_pipelineLayout.isValid()) {
            m_rhiDevice->destroyPipelineLayout(m_pipelineLayout);
        }
        if (m_bindGroupLayout.isValid()) {
            m_rhiDevice->destroyBindGroupLayout(m_bindGroupLayout);
        }
        if (m_uniformBuffer.isValid()) {
            m_rhiDevice->destroyBuffer(m_uniformBuffer);
        }
        const RhiSamplerHandle samplers[] = {m_nearestSampler, m_linearSampler};
        for (const RhiSamplerHandle sampler : samplers) {
            if (sampler.isValid()) {
                m_rhiDevice->destroySampler(sampler);
            }
        }
    }

    m_uniformBuffer = {};
    m_nearestSampler = {};
    m_linearSampler = {};
    m_bindGroupLayout = {};
    m_pipelineLayout = {};
    m_vertexShader = {};
    m_fragmentShader = {};
    m_pipeline = {};
    m_rhiDevice = nullptr;
}
