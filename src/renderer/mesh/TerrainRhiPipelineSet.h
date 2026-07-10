#ifndef MECRAFT_TERRAIN_RHI_PIPELINE_SET_H
#define MECRAFT_TERRAIN_RHI_PIPELINE_SET_H

#include "TerrainRenderer.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiTypes.h"

#include <array>

class ResourceMgr;
class RhiCommandList;
class RhiDevice;

class TerrainRhiPipelineSet {
public:
    TerrainRhiPipelineSet();
    ~TerrainRhiPipelineSet();

    void init(RhiDevice& rhiDevice);
    void shutdown();

    bool prepareGBuffer(RhiCommandList& commandList,
                        ResourceMgr& resourceMgr,
                        const TerrainFrameData& frame,
                        const TerrainRenderSettings& settings);

    [[nodiscard]] RhiBindGroupLayoutHandle metadataLayout() const { return m_metadataLayout; }
    [[nodiscard]] RhiPipelineHandle gbufferOpaquePipeline() const { return m_gbufferOpaquePipeline; }
    [[nodiscard]] RhiPipelineHandle gbufferCutoutPipeline() const { return m_gbufferCutoutPipeline; }
    [[nodiscard]] RhiBindGroupHandle gbufferOpaqueBindGroup() const { return m_gbufferBindGroups[0]; }
    [[nodiscard]] RhiBindGroupHandle gbufferCutoutBindGroup() const { return m_gbufferBindGroups[1]; }

private:
    static constexpr size_t kGBufferTextureSlotCount = 7u;

    bool ensureGBufferPipeline(ResourceMgr& resourceMgr);
    bool ensureGBufferTextureViews(ResourceMgr& resourceMgr);
    bool ensureGBufferBindGroups();
    bool ensureTextureView(size_t slot,
                           RhiTextureHandle texture,
                           RhiTextureViewType viewType);
    void destroyGBufferBindGroups();
    void destroyGBufferTextureViews();
    void destroyGBufferResources();

    RhiDevice* m_rhiDevice = nullptr;
    bool m_hasNormalMaps = false;
    bool m_hasSpecularMaps = false;
    float m_samplerAnisotropy = 1.0f;

    RhiBindGroupLayoutHandle m_metadataLayout;
    RhiBindGroupLayoutHandle m_gbufferMaterialLayout;
    RhiPipelineLayoutHandle m_gbufferPipelineLayout;
    RhiShaderHandle m_gbufferVertexShader;
    RhiShaderHandle m_gbufferFragmentShader;
    RhiPipelineHandle m_gbufferOpaquePipeline;
    RhiPipelineHandle m_gbufferCutoutPipeline;
    std::array<RhiBufferHandle, 2> m_gbufferParamsBuffers{};
    std::array<RhiBindGroupHandle, 2> m_gbufferBindGroups{};
    RhiSamplerHandle m_blockSampler;
    RhiSamplerHandle m_linearClampSampler;
    RhiSamplerHandle m_linearRepeatSampler;
    std::array<RhiTextureHandle, kGBufferTextureSlotCount> m_gbufferViewTextures{};
    std::array<RhiTextureViewHandle, kGBufferTextureSlotCount> m_gbufferTextureViews{};
    std::array<RhiTextureViewHandle, kGBufferTextureSlotCount> m_gbufferBoundViews{};
};

#endif // MECRAFT_TERRAIN_RHI_PIPELINE_SET_H
