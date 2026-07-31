#include "SkyIblPass.h"

#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"

#include <algorithm>
#include <array>
#include <optional>

namespace {
struct SkyIblPushConstants {
  uint32_t face = 0u;
  float roughness = 0.0f;
  uint32_t sourceResolution = renderer::contracts::kSkyIblCubeExtent;
  uint32_t sampleCount = renderer::contracts::kSkyIblGgxSampleCount;
};

[[nodiscard]] bool sameView(const RhiTextureViewHandle lhs,
                            const RhiTextureViewHandle rhs) {
  return lhs.index == rhs.index && lhs.generation == rhs.generation;
}
} // namespace

void SkyIblPass::shutdown() { destroyResources(); }

bool SkyIblPass::prepareFrame(RhiDevice &rhiDevice,
                              const RhiTextureViewHandle skyCaptureView) {
  if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
    destroyResources();
  }
  m_rhiDevice = &rhiDevice;
  if (!skyCaptureView.isValid())
    return false;
  const bool resourcesReady =
      (m_skyRadianceTexture.isValid() || createResources(rhiDevice)) &&
      (m_skyRadiancePipeline.isValid() || createPipelines(rhiDevice)) &&
      (m_skyRadianceView.isValid() || createViews(rhiDevice));
  if (!resourcesReady)
    return false;
  return (m_skyRadianceBindGroup.isValid() &&
          sameView(m_boundSkyCaptureView, skyCaptureView)) ||
         createBindGroups(rhiDevice, skyCaptureView);
}

bool SkyIblPass::importGraphResources(RenderGraph &graph,
                                      GraphResources &resources) const {
  if (m_rhiDevice == nullptr || !m_skyRadianceTexture.isValid() ||
      !m_specularPrefilterTexture.isValid() || !m_dfgLutTexture.isValid() ||
      !m_skyRadianceView.isValid() || !m_specularPrefilterView.isValid() ||
      !m_dfgLutView.isValid()) {
    return false;
  }
  const auto import = [&graph,
                       this](const char *name, const RhiTextureHandle texture,
                             const RhiTextureViewHandle view,
                             const bool initialized, RgTextureHandle &output) {
    RhiTextureDesc desc;
    if (!m_rhiDevice->getTextureDesc(texture, desc))
      return false;
    output = graph.importTexture({name, texture, desc,
                                  initialized ? RhiResourceState::ShaderRead
                                              : RhiResourceState::Undefined,
                                  RhiResourceState::ShaderRead, view});
    return output.isValid();
  };
  return import("SkyIbl.Radiance", m_skyRadianceTexture, m_skyRadianceView,
                m_productsInitialized, resources.skyRadiance) &&
         import("SkyIbl.SpecularPrefilter", m_specularPrefilterTexture,
                m_specularPrefilterView, m_productsInitialized,
                resources.specularPrefilter) &&
         import("SkyIbl.DfgLut", m_dfgLutTexture, m_dfgLutView, m_dfgReady,
                resources.dfgLut);
}

RgPassHandle SkyIblPass::addGraphPasses(RenderGraph &graph,
                                        const RgTextureHandle skyCapture,
                                        const GraphResources &resources,
                                        const RgPassHandle dependency) {
  if (!skyCapture.isValid() || !resources.skyRadiance.isValid() ||
      !resources.specularPrefilter.isValid() || !resources.dfgLut.isValid() ||
      !dependency.isValid())
    return {};

  RenderGraphPassBuilder radiance = graph.addPass(
      {"SkyIbl.Radiance", RgPassType::Graphics, RhiQueueType::Graphics, true});
  radiance.dependsOn(dependency)
      .readTexture(skyCapture, RhiResourceState::ShaderRead)
      .writeTexture(resources.skyRadiance, RhiResourceState::RenderTarget)
      .setExecute([this](RgPassContext &pass) {
        return recordSkyRadiance(pass.commandList());
      });

  RenderGraphPassBuilder prefilter =
      graph.addPass({"SkyIbl.GgxPrefilter", RgPassType::Graphics,
                     RhiQueueType::Graphics, true});
  prefilter.dependsOn(radiance.handle())
      .readTexture(resources.skyRadiance, RhiResourceState::ShaderRead)
      .writeTexture(resources.specularPrefilter, RhiResourceState::RenderTarget)
      .setExecute([this](RgPassContext &pass) {
        return recordSpecularPrefilter(pass.commandList());
      });

  RgPassHandle tail = prefilter.handle();
  if (!m_dfgReady && !m_dfgScheduled) {
    RenderGraphPassBuilder dfg = graph.addPass(
        {"SkyIbl.DfgLut", RgPassType::Graphics, RhiQueueType::Graphics, true});
    dfg.dependsOn(tail)
        .writeTexture(resources.dfgLut, RhiResourceState::RenderTarget)
        .setExecute([this](RgPassContext &pass) {
          return recordDfgLut(pass.commandList());
        });
    m_dfgScheduled = true;
    tail = dfg.handle();
  }
  return tail;
}

void SkyIblPass::finishGraphExecution(const bool succeeded) {
  if (succeeded) {
    m_productsInitialized = true;
    if (m_dfgScheduled)
      m_dfgReady = true;
  }
  m_dfgScheduled = false;
}

bool SkyIblPass::createResources(RhiDevice &rhiDevice) {
  const auto create = [&rhiDevice](const char *name, const uint32_t width,
                                   const uint32_t height, const uint32_t layers,
                                   const uint32_t mipLevels,
                                   const RhiTextureDimension dimension,
                                   const RhiTextureFormat format,
                                   const RhiMemoryCategory category) {
    RhiTextureDesc desc;
    desc.debugName = name;
    desc.dimension = dimension;
    desc.format = format;
    desc.width = width;
    desc.height = height;
    desc.depthOrLayers = layers;
    desc.mipLevels = mipLevels;
    desc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                 rhiFlag(RhiTextureUsage::ColorAttachment);
    desc.memoryCategory = category;
    desc.queueSharing = RhiTextureQueueSharing::GraphicsComputeConcurrent;
    return rhiDevice.createTexture(desc, nullptr);
  };
  m_skyRadianceTexture = create(
      "SkyIbl.Radiance", renderer::contracts::kSkyIblCubeExtent,
      renderer::contracts::kSkyIblCubeExtent, 6u, 1u, RhiTextureDimension::Cube,
      RhiTextureFormat::Rgba16Float, RhiMemoryCategory::Texture);
  m_specularPrefilterTexture = create(
      "SkyIbl.SpecularPrefilter", renderer::contracts::kSkyIblCubeExtent,
      renderer::contracts::kSkyIblCubeExtent, 6u,
      renderer::contracts::kSkyIblCubeMipCount, RhiTextureDimension::Cube,
      RhiTextureFormat::Rgba16Float, RhiMemoryCategory::Texture);
  m_dfgLutTexture =
      create("SkyIbl.DfgLut", renderer::contracts::kSkyIblDfgExtent,
             renderer::contracts::kSkyIblDfgExtent, 1u, 1u,
             RhiTextureDimension::Texture2D, RhiTextureFormat::Rg16Float,
             RhiMemoryCategory::Texture);
  if (!m_skyRadianceTexture.isValid() ||
      !m_specularPrefilterTexture.isValid() || !m_dfgLutTexture.isValid()) {
    destroyResources();
    return false;
  }
  return true;
}

bool SkyIblPass::createViews(RhiDevice &rhiDevice) {
  auto createCubeView = [&rhiDevice](const RhiTextureHandle texture,
                                     const uint32_t mip, const uint32_t face) {
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
  RhiTextureViewDesc cubeDesc;
  cubeDesc.texture = m_skyRadianceTexture;
  cubeDesc.viewType = RhiTextureViewType::Cube;
  cubeDesc.format = RhiTextureFormat::Rgba16Float;
  cubeDesc.baseMip = 0u;
  cubeDesc.mipCount = 1u;
  cubeDesc.baseLayer = 0u;
  cubeDesc.layerCount = 6u;
  m_skyRadianceView = rhiDevice.createTextureView(cubeDesc);
  cubeDesc.texture = m_specularPrefilterTexture;
  cubeDesc.mipCount = renderer::contracts::kSkyIblCubeMipCount;
  m_specularPrefilterView = rhiDevice.createTextureView(cubeDesc);
  RhiTextureViewDesc lutDesc;
  lutDesc.texture = m_dfgLutTexture;
  lutDesc.viewType = RhiTextureViewType::Texture2D;
  lutDesc.format = RhiTextureFormat::Rg16Float;
  lutDesc.baseMip = 0u;
  lutDesc.mipCount = 1u;
  lutDesc.baseLayer = 0u;
  lutDesc.layerCount = 1u;
  m_dfgLutView = rhiDevice.createTextureView(lutDesc);
  for (uint32_t face = 0u; face < 6u; ++face) {
    m_skyRadianceFaceViews[face] =
        createCubeView(m_skyRadianceTexture, 0u, face);
    for (uint32_t mip = 0u; mip < renderer::contracts::kSkyIblCubeMipCount;
         ++mip) {
      m_specularFaceMipViews[mip][face] =
          createCubeView(m_specularPrefilterTexture, mip, face);
    }
  }
  for (const auto view : m_skyRadianceFaceViews) {
    if (!view.isValid()) {
      destroyResources();
      return false;
    }
  }
  for (const auto &mipViews : m_specularFaceMipViews)
    for (const auto view : mipViews) {
      if (!view.isValid()) {
        destroyResources();
        return false;
      }
    }
  if (!m_skyRadianceView.isValid() || !m_specularPrefilterView.isValid() ||
      !m_dfgLutView.isValid()) {
    destroyResources();
    return false;
  }
  return true;
}

bool SkyIblPass::createPipelines(RhiDevice &rhiDevice) {
  const auto load = [](const char *path) {
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
  auto shader = [&rhiDevice](const char *name, const RhiShaderStage stage,
                             const std::string &source) {
    RhiShaderDesc desc;
    desc.debugName = name;
    desc.stage = stage;
    desc.source = source.c_str();
    desc.sourceSize = source.size();
    return rhiDevice.createShader(desc);
  };
  m_fullscreenVertexShader =
      shader("SkyIbl.Vertex", RhiShaderStage::Vertex, *vertex);
  m_skyRadianceFragmentShader =
      shader("SkyIbl.Radiance.Fragment", RhiShaderStage::Fragment, *radiance);
  m_prefilterFragmentShader =
      shader("SkyIbl.Prefilter.Fragment", RhiShaderStage::Fragment, *prefilter);
  m_dfgFragmentShader =
      shader("SkyIbl.Dfg.Fragment", RhiShaderStage::Fragment, *dfg);
  if (!m_fullscreenVertexShader.isValid() ||
      !m_skyRadianceFragmentShader.isValid() ||
      !m_prefilterFragmentShader.isValid() || !m_dfgFragmentShader.isValid()) {
    destroyResources();
    return false;
  }
  RhiBindGroupLayoutDesc layoutDesc;
  layoutDesc.debugName = "SkyIbl.TextureLayout";
  layoutDesc.entries.push_back({0u, RhiBindingType::CombinedTextureSampler,
                                rhiFlag(RhiShaderStage::Fragment), 1u});
  m_skyRadianceBindGroupLayout = rhiDevice.createBindGroupLayout(layoutDesc);
  m_prefilterBindGroupLayout = rhiDevice.createBindGroupLayout(layoutDesc);
  if (!m_skyRadianceBindGroupLayout.isValid() ||
      !m_prefilterBindGroupLayout.isValid()) {
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
  const auto graphics = [this, &rhiDevice](const char *name,
                                           const RhiShaderHandle fragment,
                                           const RhiPipelineLayoutHandle layout,
                                           const RhiTextureFormat colorFormat) {
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
  m_skyRadiancePipeline =
      graphics("SkyIbl.Radiance.Pipeline", m_skyRadianceFragmentShader,
               m_skyRadiancePipelineLayout, RhiTextureFormat::Rgba16Float);
  m_prefilterPipeline =
      graphics("SkyIbl.Prefilter.Pipeline", m_prefilterFragmentShader,
               m_prefilterPipelineLayout, RhiTextureFormat::Rgba16Float);
  m_dfgPipeline = graphics("SkyIbl.Dfg.Pipeline", m_dfgFragmentShader,
                           m_dfgPipelineLayout, RhiTextureFormat::Rg16Float);
  if (!m_skyRadiancePipeline.isValid() || !m_prefilterPipeline.isValid() ||
      !m_dfgPipeline.isValid()) {
    destroyResources();
    return false;
  }
  return true;
}

bool SkyIblPass::createBindGroups(RhiDevice &rhiDevice,
                                  const RhiTextureViewHandle skyCaptureView) {
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
  if (m_prefilterBindGroup.isValid()) {
    rhiDevice.destroyBindGroup(m_prefilterBindGroup);
    m_prefilterBindGroup = {};
  }
  const auto create = [&rhiDevice, this](const RhiBindGroupLayoutHandle layout,
                                         const RhiTextureViewHandle view) {
    RhiBindGroupDesc desc;
    desc.layout = layout;
    RhiBindGroupEntry entry;
    entry.binding = 0u;
    entry.resource.combinedTextureSampler = {view, m_linearClampSampler};
    desc.entries.push_back(entry);
    return rhiDevice.createBindGroup(desc);
  };
  m_skyRadianceBindGroup = create(m_skyRadianceBindGroupLayout, skyCaptureView);
  m_prefilterBindGroup = create(m_prefilterBindGroupLayout, m_skyRadianceView);
  if (!m_skyRadianceBindGroup.isValid() || !m_prefilterBindGroup.isValid()) {
    destroyResources();
    return false;
  }
  m_boundSkyCaptureView = skyCaptureView;
  return true;
}

bool SkyIblPass::recordSkyRadiance(RhiCommandList &commandList) const {
  for (uint32_t face = 0u; face < 6u; ++face) {
    RhiColorAttachment attachment{m_skyRadianceFaceViews[face],
                                  RhiLoadOp::Clear, RhiStoreOp::Store};
    RhiRenderingInfo rendering{"SkyIbl.Radiance.Face",
                               {0, 0, renderer::contracts::kSkyIblCubeExtent,
                                renderer::contracts::kSkyIblCubeExtent},
                               &attachment,
                               1u,
                               nullptr};
    commandList.beginRendering(rendering);
    commandList.setViewport(
        {0.0f, 0.0f, static_cast<float>(renderer::contracts::kSkyIblCubeExtent),
         static_cast<float>(renderer::contracts::kSkyIblCubeExtent), 0.0f,
         1.0f});
    commandList.setScissor(rendering.renderArea);
    commandList.setGraphicsPipeline(m_skyRadiancePipeline);
    commandList.setBindGroup(0u, m_skyRadianceBindGroup);
    SkyIblPushConstants constants;
    constants.face = face;
    commandList.pushConstants(&constants, sizeof(constants),
                              rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
  }
  return true;
}

bool SkyIblPass::recordSpecularPrefilter(RhiCommandList &commandList) const {
  for (uint32_t mip = 0u; mip < renderer::contracts::kSkyIblCubeMipCount;
       ++mip) {
    const uint32_t extent =
        std::max(1u, renderer::contracts::kSkyIblCubeExtent >> mip);
    for (uint32_t face = 0u; face < 6u; ++face) {
      RhiColorAttachment attachment{m_specularFaceMipViews[mip][face],
                                    RhiLoadOp::Clear, RhiStoreOp::Store};
      RhiRenderingInfo rendering{"SkyIbl.Prefilter.Face",
                                 {0, 0, extent, extent},
                                 &attachment,
                                 1u,
                                 nullptr};
      commandList.beginRendering(rendering);
      commandList.setViewport({0.0f, 0.0f, static_cast<float>(extent),
                               static_cast<float>(extent), 0.0f, 1.0f});
      commandList.setScissor(rendering.renderArea);
      commandList.setGraphicsPipeline(m_prefilterPipeline);
      commandList.setBindGroup(0u, m_prefilterBindGroup);
      SkyIblPushConstants constants;
      constants.face = face;
      constants.roughness = renderer::contracts::skyIblRoughnessForMip(mip);
      commandList.pushConstants(&constants, sizeof(constants),
                                rhiFlag(RhiShaderStage::Fragment));
      commandList.draw(3u, 1u, 0u, 0u);
      commandList.endRendering();
    }
  }
  return true;
}

bool SkyIblPass::recordDfgLut(RhiCommandList &commandList) const {
  RhiColorAttachment attachment{m_dfgLutView, RhiLoadOp::Clear,
                                RhiStoreOp::Store};
  RhiRenderingInfo rendering{"SkyIbl.DfgLut",
                             {0, 0, renderer::contracts::kSkyIblDfgExtent,
                              renderer::contracts::kSkyIblDfgExtent},
                             &attachment,
                             1u,
                             nullptr};
  commandList.beginRendering(rendering);
  commandList.setViewport(
      {0.0f, 0.0f, static_cast<float>(renderer::contracts::kSkyIblDfgExtent),
       static_cast<float>(renderer::contracts::kSkyIblDfgExtent), 0.0f, 1.0f});
  commandList.setScissor(rendering.renderArea);
  commandList.setGraphicsPipeline(m_dfgPipeline);
  commandList.draw(3u, 1u, 0u, 0u);
  commandList.endRendering();
  return true;
}

void SkyIblPass::destroyResources() {
  if (m_rhiDevice != nullptr) {
    if (m_skyRadianceBindGroup.isValid())
      m_rhiDevice->destroyBindGroup(m_skyRadianceBindGroup);
    if (m_prefilterBindGroup.isValid())
      m_rhiDevice->destroyBindGroup(m_prefilterBindGroup);
    const RhiPipelineHandle pipelines[] = {m_skyRadiancePipeline,
                                           m_prefilterPipeline, m_dfgPipeline};
    for (const auto pipeline : pipelines)
      if (pipeline.isValid())
        m_rhiDevice->destroyPipeline(pipeline);
    const RhiPipelineLayoutHandle layouts[] = {m_skyRadiancePipelineLayout,
                                               m_prefilterPipelineLayout,
                                               m_dfgPipelineLayout};
    for (const auto layout : layouts)
      if (layout.isValid())
        m_rhiDevice->destroyPipelineLayout(layout);
    const RhiBindGroupLayoutHandle groupLayouts[] = {
        m_skyRadianceBindGroupLayout, m_prefilterBindGroupLayout};
    for (const auto layout : groupLayouts)
      if (layout.isValid())
        m_rhiDevice->destroyBindGroupLayout(layout);
    if (m_linearClampSampler.isValid())
      m_rhiDevice->destroySampler(m_linearClampSampler);
    const RhiShaderHandle shaders[] = {
        m_fullscreenVertexShader, m_skyRadianceFragmentShader,
        m_prefilterFragmentShader, m_dfgFragmentShader};
    for (const auto shader : shaders)
      if (shader.isValid())
        m_rhiDevice->destroyShader(shader);
    const RhiTextureViewHandle views[] = {
        m_skyRadianceView, m_specularPrefilterView, m_dfgLutView};
    for (const auto view : views)
      if (view.isValid())
        m_rhiDevice->destroyTextureView(view);
    for (const auto view : m_skyRadianceFaceViews)
      if (view.isValid())
        m_rhiDevice->destroyTextureView(view);
    for (const auto &mipViews : m_specularFaceMipViews)
      for (const auto view : mipViews)
        if (view.isValid())
          m_rhiDevice->destroyTextureView(view);
    const RhiTextureHandle textures[] = {
        m_skyRadianceTexture, m_specularPrefilterTexture, m_dfgLutTexture};
    for (const auto texture : textures)
      if (texture.isValid())
        m_rhiDevice->destroyTexture(texture);
  }
  *this = SkyIblPass{};
}
