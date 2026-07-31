#include "SkyIblPass.h"

#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"

#include <algorithm>
#include <array>
#include <optional>

namespace {
static_assert(renderer::contracts::kSkyIblGenerationCount == 2u);

struct SkyIblPushConstants {
    uint32_t face = 0u;
    float roughness = 0.0f;
    uint32_t sourceResolution = renderer::contracts::kSkyIblCubeExtent;
    uint32_t sampleCount = renderer::contracts::kSkyIblGgxSampleCount;
};

[[nodiscard]] bool sameView(const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}
} // namespace

void SkyIblPass::shutdown() {
    destroyResources();
}

bool SkyIblPass::prepareFrame(RhiDevice& rhiDevice, const RhiTextureViewHandle skyCaptureView,
                              const uint64_t frameIndex) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyResources();
    }
    m_rhiDevice = &rhiDevice;
    if (!skyCaptureView.isValid())
        return false;
    const bool resourcesReady = (m_generations[0].radianceTexture.isValid() || createResources(rhiDevice)) &&
                                (m_skyRadiancePipeline.isValid() || createPipelines(rhiDevice)) &&
                                (m_generations[0].radianceView.isValid() || createViews(rhiDevice));
    if (!resourcesReady)
        return false;
    bool bindGroupsReady = m_skyRadianceBindGroup.isValid() && sameView(m_boundSkyCaptureView, skyCaptureView);
    for (const GenerationResources& generation : m_generations)
        bindGroupsReady = bindGroupsReady && generation.prefilterBindGroup.isValid();
    if (!bindGroupsReady && !createBindGroups(rhiDevice, skyCaptureView))
        return false;

    m_requestedRevision = std::max(m_requestedRevision, renderer::contracts::skyIblRevisionForFrame(frameIndex));
    if (m_buildGeneration >= renderer::contracts::kSkyIblGenerationCount) {
        if (m_activeGeneration >= renderer::contracts::kSkyIblGenerationCount) {
            beginBuild(0u, m_requestedRevision, true);
        } else if (m_requestedRevision > m_committedRevision) {
            beginBuild(1u - m_activeGeneration, m_requestedRevision, false);
        }
    }
    m_consumerGeneration =
        m_activeGeneration < renderer::contracts::kSkyIblGenerationCount ? m_activeGeneration : m_buildGeneration;
    return m_consumerGeneration < renderer::contracts::kSkyIblGenerationCount;
}

bool SkyIblPass::importGraphResources(RenderGraph& graph, GraphResources& resources) const {
    if (m_rhiDevice == nullptr || !m_dfgLutTexture.isValid() || !m_dfgLutView.isValid() ||
        m_consumerGeneration >= renderer::contracts::kSkyIblGenerationCount) {
        return false;
    }
    const auto import = [&graph, this](const char* name, const RhiTextureHandle texture,
                                       const RhiTextureViewHandle view, const bool initialized,
                                       RgTextureHandle& output) {
        RhiTextureDesc desc;
        if (!m_rhiDevice->getTextureDesc(texture, desc))
            return false;
        output = graph.importTexture({name, texture, desc,
                                      initialized ? RhiResourceState::ShaderRead : RhiResourceState::Undefined,
                                      RhiResourceState::ShaderRead, view});
        return output.isValid();
    };
    static constexpr const char* kRadianceNames[] = {"SkyIbl.Radiance.Generation0", "SkyIbl.Radiance.Generation1"};
    static constexpr const char* kPrefilterNames[] = {"SkyIbl.SpecularPrefilter.Generation0",
                                                      "SkyIbl.SpecularPrefilter.Generation1"};
    for (uint32_t index = 0u; index < renderer::contracts::kSkyIblGenerationCount; ++index) {
        const GenerationResources& generation = m_generations[index];
        if (!generation.radianceTexture.isValid() || !generation.specularPrefilterTexture.isValid() ||
            !generation.radianceView.isValid() || !generation.specularPrefilterView.isValid() ||
            !import(kRadianceNames[index], generation.radianceTexture, generation.radianceView,
                    generation.radianceStateInitialized, resources.generations[index].radiance) ||
            !import(kPrefilterNames[index], generation.specularPrefilterTexture, generation.specularPrefilterView,
                    generation.prefilterStateInitialized, resources.generations[index].specularPrefilter)) {
            return false;
        }
    }
    resources.consumerSpecularPrefilter = resources.generations[m_consumerGeneration].specularPrefilter;
    return resources.consumerSpecularPrefilter.isValid() &&
           import("SkyIbl.DfgLut", m_dfgLutTexture, m_dfgLutView, m_dfgReady, resources.dfgLut);
}

RgPassHandle SkyIblPass::addGraphPasses(RenderGraph& graph, const RgTextureHandle skyCapture,
                                        const GraphResources& resources, const RgPassHandle dependency) {
    if (!skyCapture.isValid() || !resources.consumerSpecularPrefilter.isValid() || !resources.dfgLut.isValid() ||
        !dependency.isValid())
        return {};

    RgPassHandle tail = dependency;
    if (m_buildGeneration < renderer::contracts::kSkyIblGenerationCount) {
        const uint32_t generation = m_buildGeneration;
        const GraphGeneration& buildResources = resources.generations[generation];
        if (!buildResources.radiance.isValid() || !buildResources.specularPrefilter.isValid()) {
            return {};
        }
        if (m_buildNeedsRadiance) {
            RenderGraphPassBuilder radiance =
                graph.addPass({"SkyIbl.Radiance", RgPassType::Graphics, RhiQueueType::Graphics, true});
            radiance.dependsOn(tail)
                .readTexture(skyCapture, RhiResourceState::ShaderRead)
                .writeTexture(buildResources.radiance, RhiResourceState::RenderTarget)
                .setExecute([this, generation](RgPassContext& pass) {
                    return recordSkyRadiance(pass.commandList(), generation);
                });
            m_radianceScheduled = true;
            tail = radiance.handle();
        }

        const uint32_t remaining = renderer::contracts::kSkyIblPrefilterWorkItemCount - m_nextPrefilterWorkItem;
        const uint32_t workItemCount = m_bootstrapBuild ? remaining : 1u;
        RenderGraphPassBuilder prefilter =
            graph.addPass({"SkyIbl.GgxPrefilter", RgPassType::Graphics, RhiQueueType::Graphics, true});
        prefilter.dependsOn(tail)
            .readTexture(buildResources.radiance, RhiResourceState::ShaderRead)
            .writeTexture(buildResources.specularPrefilter, RhiResourceState::RenderTarget)
            .setExecute([this, generation, first = m_nextPrefilterWorkItem, workItemCount](RgPassContext& pass) {
                return recordSpecularPrefilter(pass.commandList(), generation, first, workItemCount);
            });
        m_scheduledWorkItemCount = workItemCount;
        m_prefilterScheduled = true;
        tail = prefilter.handle();
    }
    if (!m_dfgReady && !m_dfgScheduled) {
        RenderGraphPassBuilder dfg =
            graph.addPass({"SkyIbl.DfgLut", RgPassType::Graphics, RhiQueueType::Graphics, true});
        dfg.dependsOn(tail)
            .writeTexture(resources.dfgLut, RhiResourceState::RenderTarget)
            .setExecute([this](RgPassContext& pass) { return recordDfgLut(pass.commandList()); });
        m_dfgScheduled = true;
        tail = dfg.handle();
    }
    return tail;
}

void SkyIblPass::finishGraphExecution(const bool succeeded) {
    if (succeeded) {
        if (m_radianceScheduled && m_buildGeneration < renderer::contracts::kSkyIblGenerationCount) {
            m_generations[m_buildGeneration].radianceStateInitialized = true;
            m_buildNeedsRadiance = false;
        }
        if (m_prefilterScheduled && m_buildGeneration < renderer::contracts::kSkyIblGenerationCount) {
            GenerationResources& generation = m_generations[m_buildGeneration];
            generation.prefilterStateInitialized = true;
            m_nextPrefilterWorkItem += m_scheduledWorkItemCount;
            if (m_nextPrefilterWorkItem == renderer::contracts::kSkyIblPrefilterWorkItemCount) {
                generation.complete = true;
                generation.revision = m_buildRevision;
                m_activeGeneration = m_buildGeneration;
                m_consumerGeneration = m_activeGeneration;
                m_committedRevision = m_buildRevision;
                m_buildGeneration = renderer::contracts::kSkyIblGenerationCount;
                m_buildRevision = 0u;
                m_nextPrefilterWorkItem = 0u;
                m_bootstrapBuild = false;
            }
        }
        if (m_dfgScheduled)
            m_dfgReady = true;
    }
    m_radianceScheduled = false;
    m_prefilterScheduled = false;
    m_scheduledWorkItemCount = 0u;
    m_dfgScheduled = false;
}

bool SkyIblPass::createResources(RhiDevice& rhiDevice) {
    const auto create = [&rhiDevice](const char* name, const uint32_t width, const uint32_t height,
                                     const uint32_t layers, const uint32_t mipLevels,
                                     const RhiTextureDimension dimension, const RhiTextureFormat format,
                                     const RhiMemoryCategory category) {
        RhiTextureDesc desc;
        desc.debugName = name;
        desc.dimension = dimension;
        desc.format = format;
        desc.width = width;
        desc.height = height;
        desc.depthOrLayers = layers;
        desc.mipLevels = mipLevels;
        desc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment);
        desc.memoryCategory = category;
        desc.queueSharing = RhiTextureQueueSharing::GraphicsComputeConcurrent;
        return rhiDevice.createTexture(desc, nullptr);
    };
    static constexpr const char* kRadianceNames[] = {"SkyIbl.Radiance.Generation0", "SkyIbl.Radiance.Generation1"};
    static constexpr const char* kPrefilterNames[] = {"SkyIbl.SpecularPrefilter.Generation0",
                                                      "SkyIbl.SpecularPrefilter.Generation1"};
    for (uint32_t index = 0u; index < renderer::contracts::kSkyIblGenerationCount; ++index) {
        GenerationResources& generation = m_generations[index];
        generation.radianceTexture =
            create(kRadianceNames[index], renderer::contracts::kSkyIblCubeExtent,
                   renderer::contracts::kSkyIblCubeExtent, renderer::contracts::kSkyIblCubeFaceCount, 1u,
                   RhiTextureDimension::Cube, RhiTextureFormat::Rgba16Float, RhiMemoryCategory::Texture);
        generation.specularPrefilterTexture = create(
            kPrefilterNames[index], renderer::contracts::kSkyIblCubeExtent, renderer::contracts::kSkyIblCubeExtent,
            renderer::contracts::kSkyIblCubeFaceCount, renderer::contracts::kSkyIblCubeMipCount,
            RhiTextureDimension::Cube, RhiTextureFormat::Rgba16Float, RhiMemoryCategory::Texture);
    }
    m_dfgLutTexture =
        create("SkyIbl.DfgLut", renderer::contracts::kSkyIblDfgExtent, renderer::contracts::kSkyIblDfgExtent, 1u, 1u,
               RhiTextureDimension::Texture2D, RhiTextureFormat::Rg16Float, RhiMemoryCategory::Texture);
    bool valid = m_dfgLutTexture.isValid();
    for (const GenerationResources& generation : m_generations)
        valid = valid && generation.radianceTexture.isValid() && generation.specularPrefilterTexture.isValid();
    if (!valid) {
        destroyResources();
        return false;
    }
    return true;
}

bool SkyIblPass::createViews(RhiDevice& rhiDevice) {
    auto createCubeView = [&rhiDevice](const RhiTextureHandle texture, const uint32_t mip, const uint32_t face) {
        RhiTextureViewDesc desc;
        desc.texture = texture;
        desc.viewType = RhiTextureViewType::Texture2D;
        desc.format = RhiTextureFormat::Rgba16Float;
        desc.baseMip = mip;
        desc.mipCount = 1u;
        desc.baseLayer = face;
        desc.layerCount = 1u;
        return rhiDevice.createTextureView(desc);
    };
    for (GenerationResources& generation : m_generations) {
        RhiTextureViewDesc cubeDesc;
        cubeDesc.texture = generation.radianceTexture;
        cubeDesc.viewType = RhiTextureViewType::Cube;
        cubeDesc.format = RhiTextureFormat::Rgba16Float;
        cubeDesc.baseMip = 0u;
        cubeDesc.mipCount = 1u;
        cubeDesc.baseLayer = 0u;
        cubeDesc.layerCount = renderer::contracts::kSkyIblCubeFaceCount;
        generation.radianceView = rhiDevice.createTextureView(cubeDesc);
        cubeDesc.texture = generation.specularPrefilterTexture;
        cubeDesc.mipCount = renderer::contracts::kSkyIblCubeMipCount;
        generation.specularPrefilterView = rhiDevice.createTextureView(cubeDesc);
    }
    RhiTextureViewDesc lutDesc;
    lutDesc.texture = m_dfgLutTexture;
    lutDesc.viewType = RhiTextureViewType::Texture2D;
    lutDesc.format = RhiTextureFormat::Rg16Float;
    lutDesc.baseMip = 0u;
    lutDesc.mipCount = 1u;
    lutDesc.baseLayer = 0u;
    lutDesc.layerCount = 1u;
    m_dfgLutView = rhiDevice.createTextureView(lutDesc);
    for (GenerationResources& generation : m_generations) {
        for (uint32_t face = 0u; face < renderer::contracts::kSkyIblCubeFaceCount; ++face) {
            generation.radianceFaceViews[face] = createCubeView(generation.radianceTexture, 0u, face);
            for (uint32_t mip = 0u; mip < renderer::contracts::kSkyIblCubeMipCount; ++mip) {
                generation.specularFaceMipViews[mip][face] =
                    createCubeView(generation.specularPrefilterTexture, mip, face);
            }
        }
    }
    for (const GenerationResources& generation : m_generations) {
        if (!generation.radianceView.isValid() || !generation.specularPrefilterView.isValid()) {
            destroyResources();
            return false;
        }
        for (const auto view : generation.radianceFaceViews) {
            if (!view.isValid()) {
                destroyResources();
                return false;
            }
        }
        for (const auto& mipViews : generation.specularFaceMipViews)
            for (const auto view : mipViews) {
                if (!view.isValid()) {
                    destroyResources();
                    return false;
                }
            }
    }
    if (!m_dfgLutView.isValid()) {
        destroyResources();
        return false;
    }
    return true;
}

bool SkyIblPass::createPipelines(RhiDevice& rhiDevice) {
    const auto load = [](const char* path) {
        return renderer::rhi::loadShaderSource(path);
    };
    const auto vertex = load("assets/shaders/fullscreen_triangle_rhi.vert");
    const auto radiance = load("assets/shaders/sky_ibl_radiance.frag");
    const auto prefilter = load("assets/shaders/sky_ibl_prefilter.frag");
    const auto dfg = load("assets/shaders/sky_ibl_dfg.frag");
    if (!vertex || !radiance || !prefilter || !dfg) {
        destroyResources();
        return false;
    }
    auto shader = [&rhiDevice](const char* name, const RhiShaderStage stage, const std::string& source) {
        RhiShaderDesc desc;
        desc.debugName = name;
        desc.stage = stage;
        desc.source = source.c_str();
        desc.sourceSize = source.size();
        return rhiDevice.createShader(desc);
    };
    m_fullscreenVertexShader = shader("SkyIbl.Vertex", RhiShaderStage::Vertex, *vertex);
    m_skyRadianceFragmentShader = shader("SkyIbl.Radiance.Fragment", RhiShaderStage::Fragment, *radiance);
    m_prefilterFragmentShader = shader("SkyIbl.Prefilter.Fragment", RhiShaderStage::Fragment, *prefilter);
    m_dfgFragmentShader = shader("SkyIbl.Dfg.Fragment", RhiShaderStage::Fragment, *dfg);
    if (!m_fullscreenVertexShader.isValid() || !m_skyRadianceFragmentShader.isValid() ||
        !m_prefilterFragmentShader.isValid() || !m_dfgFragmentShader.isValid()) {
        destroyResources();
        return false;
    }
    RhiBindGroupLayoutDesc layoutDesc;
    layoutDesc.debugName = "SkyIbl.TextureLayout";
    layoutDesc.entries.push_back({0u, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u});
    m_skyRadianceBindGroupLayout = rhiDevice.createBindGroupLayout(layoutDesc);
    m_prefilterBindGroupLayout = rhiDevice.createBindGroupLayout(layoutDesc);
    if (!m_skyRadianceBindGroupLayout.isValid() || !m_prefilterBindGroupLayout.isValid()) {
        destroyResources();
        return false;
    }
    RhiPipelineLayoutDesc pipelineLayout;
    pipelineLayout.debugName = "SkyIbl.RadianceLayout";
    pipelineLayout.bindGroupLayouts.push_back(m_skyRadianceBindGroupLayout);
    pipelineLayout.pushConstantBytes = sizeof(SkyIblPushConstants);
    pipelineLayout.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_skyRadiancePipelineLayout = rhiDevice.createPipelineLayout(pipelineLayout);
    pipelineLayout.debugName = "SkyIbl.PrefilterLayout";
    pipelineLayout.bindGroupLayouts.clear();
    pipelineLayout.bindGroupLayouts.push_back(m_prefilterBindGroupLayout);
    m_prefilterPipelineLayout = rhiDevice.createPipelineLayout(pipelineLayout);
    pipelineLayout.debugName = "SkyIbl.DfgLayout";
    pipelineLayout.bindGroupLayouts.clear();
    pipelineLayout.pushConstantBytes = 0u;
    pipelineLayout.pushConstantStages = 0u;
    m_dfgPipelineLayout = rhiDevice.createPipelineLayout(pipelineLayout);
    const auto graphics = [this, &rhiDevice](const char* name, const RhiShaderHandle fragment,
                                             const RhiPipelineLayoutHandle layout, const RhiTextureFormat colorFormat) {
        RhiGraphicsPipelineDesc desc;
        desc.debugName = name;
        desc.vertexShader = m_fullscreenVertexShader;
        desc.fragmentShader = fragment;
        desc.layout = layout;
        desc.raster.cullMode = RhiCullMode::None;
        desc.depthStencil.depthTestEnabled = false;
        desc.depthStencil.depthWriteEnabled = false;
        desc.colorFormats.push_back(colorFormat);
        desc.blend.attachments.push_back({});
        return rhiDevice.createGraphicsPipeline(desc);
    };
    m_skyRadiancePipeline = graphics("SkyIbl.Radiance.Pipeline", m_skyRadianceFragmentShader,
                                     m_skyRadiancePipelineLayout, RhiTextureFormat::Rgba16Float);
    m_prefilterPipeline = graphics("SkyIbl.Prefilter.Pipeline", m_prefilterFragmentShader, m_prefilterPipelineLayout,
                                   RhiTextureFormat::Rgba16Float);
    m_dfgPipeline =
        graphics("SkyIbl.Dfg.Pipeline", m_dfgFragmentShader, m_dfgPipelineLayout, RhiTextureFormat::Rg16Float);
    if (!m_skyRadiancePipeline.isValid() || !m_prefilterPipeline.isValid() || !m_dfgPipeline.isValid()) {
        destroyResources();
        return false;
    }
    return true;
}

bool SkyIblPass::createBindGroups(RhiDevice& rhiDevice, const RhiTextureViewHandle skyCaptureView) {
    if (!m_linearClampSampler.isValid()) {
        RhiSamplerDesc samplerDesc;
        samplerDesc.minFilter = RhiFilter::Linear;
        samplerDesc.magFilter = RhiFilter::Linear;
        samplerDesc.mipmapMode = RhiMipmapMode::Linear;
        samplerDesc.addressU = RhiAddressMode::ClampToEdge;
        samplerDesc.addressV = RhiAddressMode::ClampToEdge;
        samplerDesc.addressW = RhiAddressMode::ClampToEdge;
        m_linearClampSampler = rhiDevice.createSampler(samplerDesc);
        if (!m_linearClampSampler.isValid()) {
            destroyResources();
            return false;
        }
    }
    if (m_skyRadianceBindGroup.isValid()) {
        rhiDevice.destroyBindGroup(m_skyRadianceBindGroup);
        m_skyRadianceBindGroup = {};
    }
    for (GenerationResources& generation : m_generations)
        if (generation.prefilterBindGroup.isValid()) {
            rhiDevice.destroyBindGroup(generation.prefilterBindGroup);
            generation.prefilterBindGroup = {};
        }
    const auto create = [&rhiDevice, this](const RhiBindGroupLayoutHandle layout, const RhiTextureViewHandle view) {
        RhiBindGroupDesc desc;
        desc.layout = layout;
        RhiBindGroupEntry entry;
        entry.binding = 0u;
        entry.resource.combinedTextureSampler = {view, m_linearClampSampler};
        desc.entries.push_back(entry);
        return rhiDevice.createBindGroup(desc);
    };
    m_skyRadianceBindGroup = create(m_skyRadianceBindGroupLayout, skyCaptureView);
    bool valid = m_skyRadianceBindGroup.isValid();
    for (GenerationResources& generation : m_generations) {
        generation.prefilterBindGroup = create(m_prefilterBindGroupLayout, generation.radianceView);
        valid = valid && generation.prefilterBindGroup.isValid();
    }
    if (!valid) {
        destroyResources();
        return false;
    }
    m_boundSkyCaptureView = skyCaptureView;
    return true;
}

bool SkyIblPass::recordSkyRadiance(RhiCommandList& commandList, const uint32_t generation) const {
    if (generation >= renderer::contracts::kSkyIblGenerationCount)
        return false;
    const GenerationResources& resources = m_generations[generation];
    for (uint32_t face = 0u; face < renderer::contracts::kSkyIblCubeFaceCount; ++face) {
        RhiColorAttachment attachment{resources.radianceFaceViews[face], RhiLoadOp::Clear, RhiStoreOp::Store};
        RhiRenderingInfo rendering{
            "SkyIbl.Radiance.Face",
            {0, 0, renderer::contracts::kSkyIblCubeExtent, renderer::contracts::kSkyIblCubeExtent},
            &attachment,
            1u,
            nullptr};
        commandList.beginRendering(rendering);
        commandList.setViewport({0.0f, 0.0f, static_cast<float>(renderer::contracts::kSkyIblCubeExtent),
                                 static_cast<float>(renderer::contracts::kSkyIblCubeExtent), 0.0f, 1.0f});
        commandList.setScissor(rendering.renderArea);
        commandList.setGraphicsPipeline(m_skyRadiancePipeline);
        commandList.setBindGroup(0u, m_skyRadianceBindGroup);
        SkyIblPushConstants constants;
        constants.face = face;
        commandList.pushConstants(&constants, sizeof(constants), rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(3u, 1u, 0u, 0u);
        commandList.endRendering();
    }
    return true;
}

bool SkyIblPass::recordSpecularPrefilter(RhiCommandList& commandList, const uint32_t generation,
                                         const uint32_t firstWorkItem, const uint32_t workItemCount) const {
    if (generation >= renderer::contracts::kSkyIblGenerationCount || workItemCount == 0u ||
        firstWorkItem + workItemCount > renderer::contracts::kSkyIblPrefilterWorkItemCount) {
        return false;
    }
    const GenerationResources& resources = m_generations[generation];
    for (uint32_t offset = 0u; offset < workItemCount; ++offset) {
        const uint32_t workItem = firstWorkItem + offset;
        const uint32_t mip = renderer::contracts::skyIblMipForWorkItem(workItem);
        const uint32_t face = renderer::contracts::skyIblFaceForWorkItem(workItem);
        const uint32_t extent = std::max(1u, renderer::contracts::kSkyIblCubeExtent >> mip);
        RhiColorAttachment attachment{resources.specularFaceMipViews[mip][face], RhiLoadOp::Clear, RhiStoreOp::Store};
        RhiRenderingInfo rendering{"SkyIbl.Prefilter.Face", {0, 0, extent, extent}, &attachment, 1u, nullptr};
        commandList.beginRendering(rendering);
        commandList.setViewport({0.0f, 0.0f, static_cast<float>(extent), static_cast<float>(extent), 0.0f, 1.0f});
        commandList.setScissor(rendering.renderArea);
        commandList.setGraphicsPipeline(m_prefilterPipeline);
        commandList.setBindGroup(0u, resources.prefilterBindGroup);
        SkyIblPushConstants constants;
        constants.face = face;
        constants.roughness = renderer::contracts::skyIblRoughnessForMip(mip);
        commandList.pushConstants(&constants, sizeof(constants), rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(3u, 1u, 0u, 0u);
        commandList.endRendering();
    }
    return true;
}

bool SkyIblPass::recordDfgLut(RhiCommandList& commandList) const {
    RhiColorAttachment attachment{m_dfgLutView, RhiLoadOp::Clear, RhiStoreOp::Store};
    RhiRenderingInfo rendering{"SkyIbl.DfgLut",
                               {0, 0, renderer::contracts::kSkyIblDfgExtent, renderer::contracts::kSkyIblDfgExtent},
                               &attachment,
                               1u,
                               nullptr};
    commandList.beginRendering(rendering);
    commandList.setViewport({0.0f, 0.0f, static_cast<float>(renderer::contracts::kSkyIblDfgExtent),
                             static_cast<float>(renderer::contracts::kSkyIblDfgExtent), 0.0f, 1.0f});
    commandList.setScissor(rendering.renderArea);
    commandList.setGraphicsPipeline(m_dfgPipeline);
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    return true;
}

void SkyIblPass::beginBuild(const uint32_t generation, const uint64_t revision, const bool bootstrap) {
    m_buildGeneration = generation;
    m_buildRevision = revision;
    m_nextPrefilterWorkItem = 0u;
    m_buildNeedsRadiance = true;
    m_bootstrapBuild = bootstrap;
    GenerationResources& resources = m_generations[generation];
    resources.complete = false;
    resources.revision = 0u;
}

void SkyIblPass::destroyResources() {
    if (m_rhiDevice != nullptr) {
        if (m_skyRadianceBindGroup.isValid())
            m_rhiDevice->destroyBindGroup(m_skyRadianceBindGroup);
        for (const GenerationResources& generation : m_generations)
            if (generation.prefilterBindGroup.isValid())
                m_rhiDevice->destroyBindGroup(generation.prefilterBindGroup);
        const RhiPipelineHandle pipelines[] = {m_skyRadiancePipeline, m_prefilterPipeline, m_dfgPipeline};
        for (const auto pipeline : pipelines)
            if (pipeline.isValid())
                m_rhiDevice->destroyPipeline(pipeline);
        const RhiPipelineLayoutHandle layouts[] = {m_skyRadiancePipelineLayout, m_prefilterPipelineLayout,
                                                   m_dfgPipelineLayout};
        for (const auto layout : layouts)
            if (layout.isValid())
                m_rhiDevice->destroyPipelineLayout(layout);
        const RhiBindGroupLayoutHandle groupLayouts[] = {m_skyRadianceBindGroupLayout, m_prefilterBindGroupLayout};
        for (const auto layout : groupLayouts)
            if (layout.isValid())
                m_rhiDevice->destroyBindGroupLayout(layout);
        if (m_linearClampSampler.isValid())
            m_rhiDevice->destroySampler(m_linearClampSampler);
        const RhiShaderHandle shaders[] = {m_fullscreenVertexShader, m_skyRadianceFragmentShader,
                                           m_prefilterFragmentShader, m_dfgFragmentShader};
        for (const auto shader : shaders)
            if (shader.isValid())
                m_rhiDevice->destroyShader(shader);
        if (m_dfgLutView.isValid())
            m_rhiDevice->destroyTextureView(m_dfgLutView);
        for (const GenerationResources& generation : m_generations) {
            const RhiTextureViewHandle views[] = {generation.radianceView, generation.specularPrefilterView};
            for (const auto view : views)
                if (view.isValid())
                    m_rhiDevice->destroyTextureView(view);
            for (const auto view : generation.radianceFaceViews)
                if (view.isValid())
                    m_rhiDevice->destroyTextureView(view);
            for (const auto& mipViews : generation.specularFaceMipViews)
                for (const auto view : mipViews)
                    if (view.isValid())
                        m_rhiDevice->destroyTextureView(view);
        }
        if (m_dfgLutTexture.isValid())
            m_rhiDevice->destroyTexture(m_dfgLutTexture);
        for (const GenerationResources& generation : m_generations) {
            if (generation.radianceTexture.isValid())
                m_rhiDevice->destroyTexture(generation.radianceTexture);
            if (generation.specularPrefilterTexture.isValid())
                m_rhiDevice->destroyTexture(generation.specularPrefilterTexture);
        }
    }
    *this = SkyIblPass{};
}
