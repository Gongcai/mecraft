#include "TerrainRhiPipelineSet.h"

#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiShaderSourceLoader.h"
#include "resource/ResourceMgr.h"
#include "resource/TextureAtlas.h"

#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <string>

namespace {
constexpr std::array<uint32_t, 7> kGBufferTextureBindings = {
    0u, 3u, 4u, 9u, 10u, 11u, 12u
};

template <typename Handle>
bool sameHandle(const Handle lhs, const Handle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

struct alignas(16) TerrainGBufferParams {
    glm::mat4 viewProj = glm::mat4(1.0f);
    glm::vec4 cameraAnimation = glm::vec4(0.0f);
    glm::vec4 surfaceParams = glm::vec4(0.0f);
    glm::ivec4 materialFlags = glm::ivec4(0);
    glm::ivec4 weatherFlags = glm::ivec4(0);
};

static_assert(sizeof(TerrainGBufferParams) == 128u,
              "Terrain GBuffer parameters must match the std140 shader block");
} // namespace

TerrainRhiPipelineSet::TerrainRhiPipelineSet() = default;

TerrainRhiPipelineSet::~TerrainRhiPipelineSet() {
    shutdown();
}

void TerrainRhiPipelineSet::init(RhiDevice& rhiDevice) {
    if (m_rhiDevice != &rhiDevice) {
        shutdown();
        m_rhiDevice = &rhiDevice;
    }
}

void TerrainRhiPipelineSet::shutdown() {
    destroyGBufferResources();
    m_rhiDevice = nullptr;
}

bool TerrainRhiPipelineSet::prepareGBuffer(RhiCommandList& commandList,
                                           ResourceMgr& resourceMgr,
                                           const TerrainFrameData& frame,
                                           const TerrainRenderSettings& settings) {
    if (m_rhiDevice == nullptr || !ensureGBufferPipeline(resourceMgr) ||
        !ensureGBufferTextureViews(resourceMgr) || !ensureGBufferBindGroups()) {
        return false;
    }

    const bool normalMapsEnabled = m_hasNormalMaps && settings.blockMaterialMapsEnabled &&
                                   settings.blockNormalMapsEnabled;
    const bool specularMapsEnabled = m_hasSpecularMaps && settings.blockMaterialMapsEnabled &&
                                     settings.blockSpecularMapsEnabled;
    const bool parallaxEnabled = normalMapsEnabled && settings.blockParallaxMapsEnabled &&
                                 settings.blockParallaxDepth > 0.0f;

    TerrainGBufferParams baseParams;
    baseParams.viewProj = frame.viewProj;
    baseParams.cameraAnimation = glm::vec4(frame.cameraPos, frame.animationTime);
    baseParams.surfaceParams = glm::vec4(
        frame.shaderTime,
        frame.surfaceWetness,
        parallaxEnabled ? settings.blockParallaxDepth : 0.0f,
        0.0f);
    baseParams.materialFlags = glm::ivec4(
        0,
        normalMapsEnabled ? 1 : 0,
        specularMapsEnabled ? 1 : 0,
        parallaxEnabled ? 1 : 0);
    baseParams.weatherFlags = glm::ivec4(
        settings.rainWetSurfacesEnabled ? 1 : 0,
        settings.rainSurfaceRipplesEnabled ? 1 : 0,
        0,
        0);

    TerrainGBufferParams cutoutParams = baseParams;
    cutoutParams.materialFlags.x = 1;
    commandList.updateBuffer(m_gbufferParamsBuffers[0], 0u, &baseParams, sizeof(baseParams));
    commandList.updateBuffer(m_gbufferParamsBuffers[1], 0u, &cutoutParams, sizeof(cutoutParams));
    return true;
}

bool TerrainRhiPipelineSet::ensureGBufferPipeline(ResourceMgr& resourceMgr) {
    const bool hasNormalMaps = resourceMgr.hasBlockNormalMaps();
    const bool hasSpecularMaps = resourceMgr.hasBlockSpecularMaps();
    const float anisotropy = std::max(1.0f, resourceMgr.getAtlasAnisotropy());
    if (m_gbufferOpaquePipeline.isValid() && m_gbufferCutoutPipeline.isValid() &&
        hasNormalMaps == m_hasNormalMaps && hasSpecularMaps == m_hasSpecularMaps &&
        anisotropy == m_samplerAnisotropy) {
        return true;
    }

    destroyGBufferResources();
    if (m_rhiDevice == nullptr) {
        return false;
    }
    m_hasNormalMaps = hasNormalMaps;
    m_hasSpecularMaps = hasSpecularMaps;
    m_samplerAnisotropy = anisotropy;

    renderer::rhi::RhiShaderSourceOptions sourceOptions;
    sourceOptions.preprocessorDefinitions.push_back("RHI_TERRAIN_MDI");
    if (m_hasNormalMaps) {
        sourceOptions.preprocessorDefinitions.push_back("RHI_TERRAIN_NORMAL_MAPS");
    }
    if (m_hasSpecularMaps) {
        sourceOptions.preprocessorDefinitions.push_back("RHI_TERRAIN_SPECULAR_MAPS");
    }
    const std::optional<std::string> vertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/chunk_gbuffer.vert",
        sourceOptions);
    const std::optional<std::string> fragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/chunk_gbuffer.frag",
        sourceOptions);
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        destroyGBufferResources();
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "Terrain.GBuffer.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_gbufferVertexShader = m_rhiDevice->createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "Terrain.GBuffer.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_gbufferFragmentShader = m_rhiDevice->createShader(fragmentDesc);
    if (!m_gbufferVertexShader.isValid() || !m_gbufferFragmentShader.isValid()) {
        destroyGBufferResources();
        return false;
    }

    RhiBufferDesc paramsDesc;
    paramsDesc.debugName = "Terrain.GBuffer.Params";
    paramsDesc.size = sizeof(TerrainGBufferParams);
    paramsDesc.usage = rhiFlag(RhiBufferUsage::Uniform) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    paramsDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    for (RhiBufferHandle& buffer : m_gbufferParamsBuffers) {
        buffer = m_rhiDevice->createBuffer(paramsDesc, nullptr, 0u);
    }
    if (!m_gbufferParamsBuffers[0].isValid() || !m_gbufferParamsBuffers[1].isValid()) {
        destroyGBufferResources();
        return false;
    }

    RhiSamplerDesc blockSamplerDesc;
    blockSamplerDesc.minFilter = RhiFilter::Nearest;
    blockSamplerDesc.magFilter = RhiFilter::Nearest;
    blockSamplerDesc.mipmapMode = RhiMipmapMode::Linear;
    blockSamplerDesc.addressU = RhiAddressMode::Repeat;
    blockSamplerDesc.addressV = RhiAddressMode::Repeat;
    blockSamplerDesc.addressW = RhiAddressMode::Repeat;
    blockSamplerDesc.maxAnisotropy = m_samplerAnisotropy;
    m_blockSampler = m_rhiDevice->createSampler(blockSamplerDesc);

    RhiSamplerDesc linearClampDesc;
    linearClampDesc.minFilter = RhiFilter::Linear;
    linearClampDesc.magFilter = RhiFilter::Linear;
    linearClampDesc.mipmapMode = RhiMipmapMode::Nearest;
    m_linearClampSampler = m_rhiDevice->createSampler(linearClampDesc);

    RhiSamplerDesc linearRepeatDesc = linearClampDesc;
    linearRepeatDesc.addressU = RhiAddressMode::Repeat;
    linearRepeatDesc.addressV = RhiAddressMode::Repeat;
    linearRepeatDesc.addressW = RhiAddressMode::Repeat;
    m_linearRepeatSampler = m_rhiDevice->createSampler(linearRepeatDesc);
    if (!m_blockSampler.isValid() || !m_linearClampSampler.isValid() ||
        !m_linearRepeatSampler.isValid()) {
        destroyGBufferResources();
        return false;
    }

    RhiBindGroupLayoutDesc metadataLayoutDesc;
    metadataLayoutDesc.debugName = "Terrain.MetadataLayout";
    metadataLayoutDesc.entries.push_back({
        0u,
        RhiBindingType::StorageBuffer,
        rhiFlag(RhiShaderStage::Vertex),
        1u
    });
    m_metadataLayout = m_rhiDevice->createBindGroupLayout(metadataLayoutDesc);

    RhiBindGroupLayoutDesc materialLayoutDesc;
    materialLayoutDesc.debugName = "Terrain.GBufferMaterialLayout";
    for (size_t slot = 0u; slot < 5u; ++slot) {
        materialLayoutDesc.entries.push_back({
            kGBufferTextureBindings[slot],
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    if (m_hasNormalMaps) {
        materialLayoutDesc.entries.push_back({
            11u,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    if (m_hasSpecularMaps) {
        materialLayoutDesc.entries.push_back({
            12u,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    materialLayoutDesc.entries.push_back({
        13u,
        RhiBindingType::UniformBuffer,
        rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment),
        1u
    });
    m_gbufferMaterialLayout = m_rhiDevice->createBindGroupLayout(materialLayoutDesc);
    if (!m_metadataLayout.isValid() || !m_gbufferMaterialLayout.isValid()) {
        destroyGBufferResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "Terrain.GBufferPipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_metadataLayout);
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_gbufferMaterialLayout);
    m_gbufferPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    if (!m_gbufferPipelineLayout.isValid()) {
        destroyGBufferResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = m_gbufferVertexShader;
    pipelineDesc.fragmentShader = m_gbufferFragmentShader;
    pipelineDesc.layout = m_gbufferPipelineLayout;
    pipelineDesc.vertexInput.bindings.push_back({
        0u,
        static_cast<uint32_t>(sizeof(PackedBlockVertex)),
        RhiVertexInputRate::Vertex
    });
    for (uint32_t attribute = 0u; attribute < 4u; ++attribute) {
        pipelineDesc.vertexInput.attributes.push_back({
            11u + attribute,
            0u,
            RhiVertexFormat::Uint,
            attribute * static_cast<uint32_t>(sizeof(uint32_t))
        });
    }
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::Less;
    pipelineDesc.colorFormats = {
        RhiTextureFormat::Rgba8Unorm,
        RhiTextureFormat::Rgba16Float,
        RhiTextureFormat::Rg8Unorm,
        RhiTextureFormat::Rgba8Unorm,
        RhiTextureFormat::Rgba8Unorm
    };
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    pipelineDesc.debugName = "Terrain.GBuffer.OpaquePipeline";
    pipelineDesc.raster.cullMode = RhiCullMode::Back;
    m_gbufferOpaquePipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    pipelineDesc.debugName = "Terrain.GBuffer.CutoutPipeline";
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    m_gbufferCutoutPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    if (!m_gbufferOpaquePipeline.isValid() || !m_gbufferCutoutPipeline.isValid()) {
        destroyGBufferResources();
        return false;
    }
    return true;
}

bool TerrainRhiPipelineSet::ensureGBufferTextureViews(ResourceMgr& resourceMgr) {
    const TextureArray& albedo = resourceMgr.getTextureArray();
    if (!ensureTextureView(0u, albedo.texture, RhiTextureViewType::Texture2DArray) ||
        !ensureTextureView(1u, resourceMgr.getGrassColormap(), RhiTextureViewType::Texture2D) ||
        !ensureTextureView(2u, resourceMgr.getFoliageColormap(), RhiTextureViewType::Texture2D) ||
        !ensureTextureView(3u, resourceMgr.getTexture2DHandle("shader_noise2d"), RhiTextureViewType::Texture2D) ||
        !ensureTextureView(4u, resourceMgr.getTexture2DHandle("shader_ripple_normal"), RhiTextureViewType::Texture2D)) {
        return false;
    }
    if (m_hasNormalMaps &&
        !ensureTextureView(5u,
                           resourceMgr.getBlockNormalTextureArray().texture,
                           RhiTextureViewType::Texture2DArray)) {
        return false;
    }
    if (m_hasSpecularMaps &&
        !ensureTextureView(6u,
                           resourceMgr.getBlockSpecularTextureArray().texture,
                           RhiTextureViewType::Texture2DArray)) {
        return false;
    }
    return true;
}

bool TerrainRhiPipelineSet::ensureGBufferBindGroups() {
    bool viewsChanged = false;
    for (size_t slot = 0u; slot < 5u; ++slot) {
        viewsChanged = viewsChanged || !sameHandle(m_gbufferBoundViews[slot], m_gbufferTextureViews[slot]);
    }
    if (m_hasNormalMaps) {
        viewsChanged = viewsChanged || !sameHandle(m_gbufferBoundViews[5], m_gbufferTextureViews[5]);
    }
    if (m_hasSpecularMaps) {
        viewsChanged = viewsChanged || !sameHandle(m_gbufferBoundViews[6], m_gbufferTextureViews[6]);
    }
    if (m_gbufferBindGroups[0].isValid() && m_gbufferBindGroups[1].isValid() && !viewsChanged) {
        return true;
    }

    destroyGBufferBindGroups();
    for (size_t pass = 0u; pass < m_gbufferBindGroups.size(); ++pass) {
        RhiBindGroupDesc desc;
        desc.layout = m_gbufferMaterialLayout;
        for (size_t slot = 0u; slot < 5u; ++slot) {
            RhiBindGroupEntry entry;
            entry.binding = kGBufferTextureBindings[slot];
            entry.resource.combinedTextureSampler.textureView = m_gbufferTextureViews[slot];
            entry.resource.combinedTextureSampler.sampler = slot == 0u
                ? m_blockSampler
                : (slot <= 2u ? m_linearClampSampler : m_linearRepeatSampler);
            desc.entries.push_back(entry);
        }
        if (m_hasNormalMaps) {
            RhiBindGroupEntry entry;
            entry.binding = 11u;
            entry.resource.combinedTextureSampler.textureView = m_gbufferTextureViews[5];
            entry.resource.combinedTextureSampler.sampler = m_blockSampler;
            desc.entries.push_back(entry);
        }
        if (m_hasSpecularMaps) {
            RhiBindGroupEntry entry;
            entry.binding = 12u;
            entry.resource.combinedTextureSampler.textureView = m_gbufferTextureViews[6];
            entry.resource.combinedTextureSampler.sampler = m_blockSampler;
            desc.entries.push_back(entry);
        }
        RhiBindGroupEntry paramsEntry;
        paramsEntry.binding = 13u;
        paramsEntry.resource.buffer.buffer = m_gbufferParamsBuffers[pass];
        paramsEntry.resource.buffer.range = sizeof(TerrainGBufferParams);
        desc.entries.push_back(paramsEntry);
        m_gbufferBindGroups[pass] = m_rhiDevice->createBindGroup(desc);
    }
    if (!m_gbufferBindGroups[0].isValid() || !m_gbufferBindGroups[1].isValid()) {
        destroyGBufferBindGroups();
        return false;
    }
    m_gbufferBoundViews = m_gbufferTextureViews;
    return true;
}

bool TerrainRhiPipelineSet::ensureTextureView(const size_t slot,
                                              const RhiTextureHandle texture,
                                              const RhiTextureViewType viewType) {
    if (m_rhiDevice == nullptr || slot >= m_gbufferTextureViews.size() || !texture.isValid()) {
        return false;
    }
    if (sameHandle(m_gbufferViewTextures[slot], texture) && m_gbufferTextureViews[slot].isValid()) {
        return true;
    }
    if (m_gbufferTextureViews[slot].isValid()) {
        m_rhiDevice->destroyTextureView(m_gbufferTextureViews[slot]);
    }

    RhiTextureViewDesc desc;
    desc.texture = texture;
    desc.viewType = viewType;
    desc.format = RhiTextureFormat::Rgba8Unorm;
    desc.mipCount = kRhiRemainingMipLevels;
    desc.layerCount = kRhiRemainingArrayLayers;
    m_gbufferTextureViews[slot] = m_rhiDevice->createTextureView(desc);
    if (!m_gbufferTextureViews[slot].isValid()) {
        std::cerr << "TerrainRhiPipelineSet: failed to create GBuffer texture view"
                  << " slot=" << slot
                  << " texture=" << texture.index << ':' << texture.generation
                  << " viewType=" << static_cast<uint32_t>(viewType) << '\n';
        m_gbufferViewTextures[slot] = {};
        return false;
    }
    m_gbufferViewTextures[slot] = texture;
    return true;
}

void TerrainRhiPipelineSet::destroyGBufferBindGroups() {
    if (m_rhiDevice != nullptr) {
        for (RhiBindGroupHandle& bindGroup : m_gbufferBindGroups) {
            if (bindGroup.isValid()) {
                m_rhiDevice->destroyBindGroup(bindGroup);
            }
            bindGroup = {};
        }
    }
    m_gbufferBoundViews = {};
}

void TerrainRhiPipelineSet::destroyGBufferTextureViews() {
    if (m_rhiDevice != nullptr) {
        for (RhiTextureViewHandle& view : m_gbufferTextureViews) {
            if (view.isValid()) {
                m_rhiDevice->destroyTextureView(view);
            }
            view = {};
        }
    }
    m_gbufferViewTextures = {};
}

void TerrainRhiPipelineSet::destroyGBufferResources() {
    destroyGBufferBindGroups();
    destroyGBufferTextureViews();
    if (m_rhiDevice != nullptr) {
        if (m_gbufferOpaquePipeline.isValid()) m_rhiDevice->destroyPipeline(m_gbufferOpaquePipeline);
        if (m_gbufferCutoutPipeline.isValid()) m_rhiDevice->destroyPipeline(m_gbufferCutoutPipeline);
        if (m_gbufferPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_gbufferPipelineLayout);
        if (m_gbufferMaterialLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_gbufferMaterialLayout);
        if (m_metadataLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_metadataLayout);
        if (m_gbufferFragmentShader.isValid()) m_rhiDevice->destroyShader(m_gbufferFragmentShader);
        if (m_gbufferVertexShader.isValid()) m_rhiDevice->destroyShader(m_gbufferVertexShader);
        for (RhiBufferHandle& buffer : m_gbufferParamsBuffers) {
            if (buffer.isValid()) m_rhiDevice->destroyBuffer(buffer);
            buffer = {};
        }
        if (m_blockSampler.isValid()) m_rhiDevice->destroySampler(m_blockSampler);
        if (m_linearClampSampler.isValid()) m_rhiDevice->destroySampler(m_linearClampSampler);
        if (m_linearRepeatSampler.isValid()) m_rhiDevice->destroySampler(m_linearRepeatSampler);
    }
    m_gbufferOpaquePipeline = {};
    m_gbufferCutoutPipeline = {};
    m_gbufferPipelineLayout = {};
    m_gbufferMaterialLayout = {};
    m_metadataLayout = {};
    m_gbufferFragmentShader = {};
    m_gbufferVertexShader = {};
    m_blockSampler = {};
    m_linearClampSampler = {};
    m_linearRepeatSampler = {};
    m_hasNormalMaps = false;
    m_hasSpecularMaps = false;
    m_samplerAnisotropy = 1.0f;
}
