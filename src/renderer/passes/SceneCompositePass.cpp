#include "SceneCompositePass.h"
#include "../core/RenderScene.h"
#include "../targets/DeferredRenderTargets.h"
#include "../gi/VoxelGiClipmap.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cstddef>
#include <optional>

namespace {
[[nodiscard]] bool sameTextureHandle(const RhiTextureHandle lhs, const RhiTextureHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

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
    glm::vec4 voxelOriginSize;
    glm::vec4 voxel0;
    glm::vec4 voxel1;
    glm::vec4 voxel2;
    glm::ivec4 flags0;
    glm::ivec4 flags1;
};
static_assert(sizeof(SceneCompositeParams) == 256u);
} // namespace

void SceneCompositePass::init(ResourceMgr&) {}

void SceneCompositePass::shutdown() {
    destroyRhiResources();
    destroyVoxelGiTextureView();
}

RgPassHandle SceneCompositePass::addGraphPasses(
    RenderGraph& graph,
    const FrameContext& ctx,
    const RenderSettings& settings,
    DeferredRenderTargets& targets,
    const VoxelGiClipmap* voxelGiClipmap,
    const GraphResources& resources,
    const RgPassHandle dependency) {
    if (!dependency.isValid() || !resources.sceneLighting.isValid() ||
        !resources.reflection.isValid() || !resources.cloud.isValid() ||
        !resources.depth.isValid() || !resources.normalAo.isValid() ||
        !resources.material.isValid() || !resources.materialAux.isValid() ||
        !resources.skyCapture.isValid() || !resources.albedo.isValid() ||
        !resources.ssgi.isValid() || !resources.output.isValid() ||
        !resources.reactiveMask.isValid() ||
        !resources.transparencyMask.isValid()) {
        return {};
    }

    const bool voxelGiEnabled = settings.voxelGi.enabled &&
                                voxelGiClipmap != nullptr &&
                                voxelGiClipmap->textureHandle().isValid() &&
                                resources.voxelGi.isValid();
    if (settings.voxelGi.enabled && voxelGiClipmap != nullptr &&
        voxelGiClipmap->textureHandle().isValid() != resources.voxelGi.isValid()) {
        return {};
    }

    RenderGraphPassBuilder composite = graph.addPass(
        {"SceneComposite", RgPassType::Graphics, RhiQueueType::Graphics,
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
        .readTexture(resources.ssgi, RhiResourceState::ShaderRead);
    if (voxelGiEnabled) {
        composite.readTexture(resources.voxelGi, RhiResourceState::ShaderRead);
    }
    composite.writeTexture(resources.output, RhiResourceState::RenderTarget)
        .writeTexture(resources.reactiveMask, RhiResourceState::RenderTarget)
        .writeTexture(resources.transparencyMask, RhiResourceState::RenderTarget)
        .setExecute([this, frame = &ctx, frameTargets = &targets,
                     frameSettings = settings, voxelGiClipmap](RgPassContext& pass) {
            return recordGraphPass(pass.commandList(), *frame, frameSettings,
                                   *frameTargets, voxelGiClipmap);
        });
    return composite.handle();
}

bool SceneCompositePass::recordGraphPass(
    RhiCommandList& commandList,
    const FrameContext& ctx,
    const RenderSettings& settings,
    DeferredRenderTargets& targets,
    const VoxelGiClipmap* voxelGiClipmap) {
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
    const bool voxelGiEnabled = settings.voxelGi.enabled &&
                                voxelGiClipmap != nullptr &&
                                voxelGiClipmap->textureHandle().isValid();
    if (!ensureRhiPipelines(rhiDevice)) {
        return false;
    }
    if (voxelGiEnabled && !ensureVoxelGiTextureView(rhiDevice, *voxelGiClipmap)) {
        return false;
    }
    const std::array<RhiTextureViewHandle, 11> views = {
        targets.sceneLightingTextureViewHandle(),
        targets.reflectionTextureViewHandle(),
        targets.cloudTextureViewHandle(),
        targets.depthTextureViewHandle(),
        targets.normalAoTextureViewHandle(),
        targets.materialTextureViewHandle(),
        targets.materialAuxTextureViewHandle(),
        targets.skyCaptureTextureViewHandle(),
        targets.albedoTextureViewHandle(),
        targets.ssgiTextureViewHandle(),
        voxelGiEnabled ? m_voxelGiTextureView : RhiTextureViewHandle{}
    };
    if (!ensureBindGroup(rhiDevice, voxelGiEnabled, views)) {
        return false;
    }

    SceneCompositeParams params{};
    params.invViewProj = usesTemporalProjectionJitter(
        settings.upscale.type, settings.taa.enabled)
        ? ctx.camera.jitteredInvViewProj
        : ctx.camera.invViewProj;
    params.cameraPosSkyIntensity = glm::vec4(ctx.camera.position, ctx.skyIntensity);
    params.sunDirectionVisibility = glm::vec4(ctx.skyColors.sunDirection,
                                              ctx.skyColors.sunVisibility);
    params.moonDirectionVisibility = glm::vec4(ctx.skyColors.moonDirection,
                                               ctx.skyColors.moonVisibility);
    params.atmosphereComposite = glm::vec4(ctx.skyColors.moonPhaseAngle,
                                           ctx.weather.skyWetness,
                                           ctx.weather.wetness,
                                           settings.cloud.sceneCloudCompositeStrength);
    params.reflectionWater = glm::vec4(settings.reflection.sceneReflectionCompositeStrength,
                                       0.4f,
                                       0.14f,
                                       0.08f);
    params.status = glm::vec4(0.0f);
    if (voxelGiEnabled) {
        params.voxelOriginSize = glm::vec4(voxelGiClipmap->origin(), voxelGiClipmap->voxelSize());
        params.voxel0 = glm::vec4(static_cast<float>(voxelGiClipmap->resolution()),
                                  static_cast<float>(voxelGiClipmap->mipLevels()),
                                  settings.voxelGi.strength,
                                  settings.voxelGi.normalBias);
        params.voxel1 = glm::vec4(settings.voxelGi.sampleDistance,
                                  settings.voxelGi.traceDistance,
                                  settings.voxelGi.coneAperture,
                                  settings.voxelGi.occupancyScale);
        params.voxel2 = glm::vec4(settings.voxelGi.occlusionStrength,
                                  settings.voxelGi.receiverShadowBoost,
                                  0.0f,
                                  0.0f);
    }
    params.flags0 = glm::ivec4(settings.ssgi.enabled ? 1 : 0,
                               voxelGiEnabled ? 1 : 0,
                               voxelGiEnabled && settings.voxelGi.debugEnabled ? 1 : 0,
                               settings.voxelGi.coneSteps);
    params.flags1 = glm::ivec4(settings.debug.reflectionDebugMode,
                               ctx.eyeInWater ? 1 : 0,
                               0,
                               0);

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
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = colorAttachments;
    renderingInfo.colorAttachmentCount = 3u;

    commandList.bufferBarrier({m_uniformBuffer, RhiResourceState::UniformBuffer,
                               RhiResourceState::TransferDst});
    commandList.updateBuffer(m_uniformBuffer, 0u, &params, sizeof(params));
    commandList.bufferBarrier({m_uniformBuffer, RhiResourceState::TransferDst,
                               RhiResourceState::UniformBuffer});
    commandList.beginRendering(renderingInfo);
    commandList.setGraphicsPipeline(voxelGiEnabled ? m_voxelPipeline : m_basePipeline);
    commandList.setBindGroup(0u, voxelGiEnabled ? m_voxelBindGroup : m_baseBindGroup);
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    return true;
}

bool SceneCompositePass::ensureVoxelGiTextureView(
    RhiDevice& rhiDevice,
    const VoxelGiClipmap& voxelGiClipmap) {
    const RhiTextureHandle texture = voxelGiClipmap.textureHandle();
    if (m_voxelGiViewDevice != nullptr && m_voxelGiViewDevice != &rhiDevice) {
        destroyVoxelGiTextureView();
    }
    if (m_voxelGiTextureView.isValid() && sameTextureHandle(m_voxelGiViewTexture, texture)) {
        return true;
    }

    destroyVoxelGiTextureView();
    if (!texture.isValid() || voxelGiClipmap.resolution() <= 0 || voxelGiClipmap.mipLevels() <= 0) {
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = texture;
    viewDesc.viewType = RhiTextureViewType::Texture3D;
    viewDesc.format = RhiTextureFormat::Rgba16Float;
    viewDesc.baseMip = 0u;
    viewDesc.mipCount = static_cast<uint32_t>(voxelGiClipmap.mipLevels());
    viewDesc.baseLayer = 0u;
    viewDesc.layerCount = static_cast<uint32_t>(voxelGiClipmap.resolution());
    m_voxelGiTextureView = rhiDevice.createTextureView(viewDesc);
    if (!m_voxelGiTextureView.isValid()) {
        return false;
    }

    m_voxelGiViewTexture = texture;
    m_voxelGiViewDevice = &rhiDevice;
    return true;
}

void SceneCompositePass::destroyVoxelGiTextureView() {
    destroyVoxelBindGroup();
    if (m_voxelGiViewDevice != nullptr && m_voxelGiTextureView.isValid()) {
        m_voxelGiViewDevice->destroyTextureView(m_voxelGiTextureView);
    }
    m_voxelGiTextureView = {};
    m_voxelGiViewTexture = {};
    m_voxelGiViewDevice = nullptr;
}

bool SceneCompositePass::ensureRhiPipelines(RhiDevice& rhiDevice) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyRhiResources();
        destroyVoxelGiTextureView();
    }
    if (m_basePipeline.isValid() && m_voxelPipeline.isValid()) {
        return true;
    }
    m_rhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> baseFragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/scene_composite.frag");
    renderer::rhi::RhiShaderSourceOptions voxelOptions;
    voxelOptions.preprocessorDefinitions.push_back("MECRAFT_SCENE_VOXEL_GI");
    const std::optional<std::string> voxelFragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/scene_composite.frag", voxelOptions);
    if (!vertexSource.has_value() || !baseFragmentSource.has_value() ||
        !voxelFragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "SceneComposite.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_vertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "SceneComposite.Base.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = baseFragmentSource->c_str();
    fragmentDesc.sourceSize = baseFragmentSource->size();
    m_baseFragmentShader = rhiDevice.createShader(fragmentDesc);
    fragmentDesc.debugName = "SceneComposite.Voxel.Fragment";
    fragmentDesc.source = voxelFragmentSource->c_str();
    fragmentDesc.sourceSize = voxelFragmentSource->size();
    m_voxelFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_vertexShader.isValid() || !m_baseFragmentShader.isValid() ||
        !m_voxelFragmentShader.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiBufferDesc uniformBufferDesc;
    uniformBufferDesc.debugName = "SceneComposite.Params";
    uniformBufferDesc.size = sizeof(SceneCompositeParams);
    uniformBufferDesc.usage = rhiFlag(RhiBufferUsage::Uniform) |
                              rhiFlag(RhiBufferUsage::TransferDst);
    uniformBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    uniformBufferDesc.initialState = RhiResourceState::UniformBuffer;
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
    m_voxelSampler = createSampler(RhiFilter::Linear, RhiMipmapMode::Linear);
    if (!m_nearestSampler.isValid() || !m_linearSampler.isValid() ||
        !m_voxelSampler.isValid()) {
        destroyRhiResources();
        return false;
    }

    auto createBindGroupLayout = [&](const char* debugName, const bool includeVoxel) {
        RhiBindGroupLayoutDesc desc;
        desc.debugName = debugName;
        for (uint32_t binding = 0u; binding < 10u; ++binding) {
            desc.entries.push_back({
                binding,
                RhiBindingType::CombinedTextureSampler,
                rhiFlag(RhiShaderStage::Fragment),
                1u
            });
        }
        if (includeVoxel) {
            desc.entries.push_back({
                10u,
                RhiBindingType::CombinedTextureSampler,
                rhiFlag(RhiShaderStage::Fragment),
                1u
            });
        }
        desc.entries.push_back({
            11u,
            RhiBindingType::UniformBuffer,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
        return rhiDevice.createBindGroupLayout(desc);
    };
    m_baseBindGroupLayout = createBindGroupLayout("SceneComposite.Base.BindGroupLayout", false);
    m_voxelBindGroupLayout = createBindGroupLayout("SceneComposite.Voxel.BindGroupLayout", true);
    if (!m_baseBindGroupLayout.isValid() || !m_voxelBindGroupLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    auto createPipelineLayout = [&](const char* debugName,
                                    const RhiBindGroupLayoutHandle bindGroupLayout) {
        RhiPipelineLayoutDesc desc;
        desc.debugName = debugName;
        desc.bindGroupLayouts.push_back(bindGroupLayout);
        return rhiDevice.createPipelineLayout(desc);
    };
    m_basePipelineLayout = createPipelineLayout(
        "SceneComposite.Base.PipelineLayout",
        m_baseBindGroupLayout);
    m_voxelPipelineLayout = createPipelineLayout(
        "SceneComposite.Voxel.PipelineLayout",
        m_voxelBindGroupLayout);
    if (!m_basePipelineLayout.isValid() || !m_voxelPipelineLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    auto createPipeline = [&](const char* debugName,
                              const RhiShaderHandle fragmentShader,
                              const RhiPipelineLayoutHandle pipelineLayout) {
        RhiGraphicsPipelineDesc desc;
        desc.debugName = debugName;
        desc.vertexShader = m_vertexShader;
        desc.fragmentShader = fragmentShader;
        desc.layout = pipelineLayout;
        desc.topology = RhiPrimitiveTopology::TriangleList;
        desc.raster.cullMode = RhiCullMode::None;
        desc.depthStencil.depthTestEnabled = false;
        desc.depthStencil.depthWriteEnabled = false;
        desc.colorFormats = {
            RhiTextureFormat::Rgba16Float,
            RhiTextureFormat::R8Unorm,
            RhiTextureFormat::R8Unorm
        };
        desc.blend.attachments.resize(3u);
        return rhiDevice.createGraphicsPipeline(desc);
    };
    m_basePipeline = createPipeline(
        "SceneComposite.Base.Pipeline",
        m_baseFragmentShader,
        m_basePipelineLayout);
    m_voxelPipeline = createPipeline(
        "SceneComposite.Voxel.Pipeline",
        m_voxelFragmentShader,
        m_voxelPipelineLayout);
    if (!m_basePipeline.isValid() || !m_voxelPipeline.isValid()) {
        destroyRhiResources();
        return false;
    }

    return true;
}

bool SceneCompositePass::ensureBindGroup(
    RhiDevice& rhiDevice,
    const bool voxelGiEnabled,
    const std::array<RhiTextureViewHandle, 11>& views) {
    if (!ensureRhiPipelines(rhiDevice)) {
        return false;
    }
    const uint32_t textureCount = voxelGiEnabled ? 11u : 10u;
    for (uint32_t binding = 0u; binding < textureCount; ++binding) {
        if (!views[binding].isValid()) {
            return false;
        }
    }

    RhiBindGroupHandle& bindGroup = voxelGiEnabled ? m_voxelBindGroup : m_baseBindGroup;
    std::array<RhiTextureViewHandle, 11>& boundViews = voxelGiEnabled
        ? m_voxelBoundViews
        : m_baseBoundViews;
    if (bindGroup.isValid() && sameTextureViews(boundViews, views)) {
        return true;
    }
    if (bindGroup.isValid()) {
        rhiDevice.destroyBindGroup(bindGroup);
        bindGroup = {};
    }

    const RhiSamplerHandle samplers[11] = {
        m_linearSampler,
        m_linearSampler,
        m_linearSampler,
        m_nearestSampler,
        m_nearestSampler,
        m_nearestSampler,
        m_nearestSampler,
        m_linearSampler,
        m_nearestSampler,
        m_linearSampler,
        m_voxelSampler
    };
    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = voxelGiEnabled ? m_voxelBindGroupLayout : m_baseBindGroupLayout;
    for (uint32_t binding = 0u; binding < textureCount; ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler = samplers[binding];
        bindGroupDesc.entries.push_back(entry);
    }

    RhiBindGroupEntry uniformEntry;
    uniformEntry.binding = 11u;
    uniformEntry.resource.buffer.buffer = m_uniformBuffer;
    uniformEntry.resource.buffer.offset = 0u;
    uniformEntry.resource.buffer.range = sizeof(SceneCompositeParams);
    bindGroupDesc.entries.push_back(uniformEntry);

    bindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!bindGroup.isValid()) {
        boundViews = {};
        return false;
    }
    boundViews = views;
    return true;
}

void SceneCompositePass::destroyVoxelBindGroup() {
    if (m_rhiDevice != nullptr && m_voxelBindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(m_voxelBindGroup);
    }
    m_voxelBindGroup = {};
    m_voxelBoundViews = {};
}

void SceneCompositePass::destroyBindGroups() {
    if (m_rhiDevice != nullptr && m_baseBindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(m_baseBindGroup);
    }
    m_baseBindGroup = {};
    m_baseBoundViews = {};
    destroyVoxelBindGroup();
}

void SceneCompositePass::destroyRhiResources() {
    destroyBindGroups();
    if (m_rhiDevice != nullptr) {
        const RhiPipelineHandle pipelines[] = {m_basePipeline, m_voxelPipeline};
        for (const RhiPipelineHandle pipeline : pipelines) {
            if (pipeline.isValid()) {
                m_rhiDevice->destroyPipeline(pipeline);
            }
        }
        const RhiShaderHandle shaders[] = {
            m_vertexShader,
            m_baseFragmentShader,
            m_voxelFragmentShader
        };
        for (const RhiShaderHandle shader : shaders) {
            if (shader.isValid()) {
                m_rhiDevice->destroyShader(shader);
            }
        }
        const RhiPipelineLayoutHandle pipelineLayouts[] = {
            m_basePipelineLayout,
            m_voxelPipelineLayout
        };
        for (const RhiPipelineLayoutHandle layout : pipelineLayouts) {
            if (layout.isValid()) {
                m_rhiDevice->destroyPipelineLayout(layout);
            }
        }
        const RhiBindGroupLayoutHandle bindGroupLayouts[] = {
            m_baseBindGroupLayout,
            m_voxelBindGroupLayout
        };
        for (const RhiBindGroupLayoutHandle layout : bindGroupLayouts) {
            if (layout.isValid()) {
                m_rhiDevice->destroyBindGroupLayout(layout);
            }
        }
        if (m_uniformBuffer.isValid()) {
            m_rhiDevice->destroyBuffer(m_uniformBuffer);
        }
        const RhiSamplerHandle samplers[] = {
            m_nearestSampler,
            m_linearSampler,
            m_voxelSampler
        };
        for (const RhiSamplerHandle sampler : samplers) {
            if (sampler.isValid()) {
                m_rhiDevice->destroySampler(sampler);
            }
        }
    }

    m_uniformBuffer = {};
    m_nearestSampler = {};
    m_linearSampler = {};
    m_voxelSampler = {};
    m_baseBindGroupLayout = {};
    m_voxelBindGroupLayout = {};
    m_basePipelineLayout = {};
    m_voxelPipelineLayout = {};
    m_vertexShader = {};
    m_baseFragmentShader = {};
    m_voxelFragmentShader = {};
    m_basePipeline = {};
    m_voxelPipeline = {};
    m_rhiDevice = nullptr;
}
