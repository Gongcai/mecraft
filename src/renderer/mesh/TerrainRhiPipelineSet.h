#ifndef MECRAFT_TERRAIN_RHI_PIPELINE_SET_H
#define MECRAFT_TERRAIN_RHI_PIPELINE_SET_H

#include "TerrainRenderer.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiTypes.h"

#include <array>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class ResourceMgr;
class RhiCommandList;
class RhiDevice;

struct TerrainShadowFrameData {
    glm::mat4 modelView = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);
    glm::vec3 lightDirection = glm::vec3(0.0f, -1.0f, 0.0f);
    float animationTime = 0.0f;
    float shaderTime = 0.0f;
    int passMode = 0;
};

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
    bool prepareShadow(RhiCommandList& commandList,
                       ResourceMgr& resourceMgr,
                       const TerrainShadowFrameData& frame);

    [[nodiscard]] RhiBindGroupLayoutHandle metadataLayout() const { return m_metadataLayout; }
    [[nodiscard]] RhiPipelineHandle gbufferOpaquePipeline() const { return m_gbufferOpaquePipeline; }
    [[nodiscard]] RhiPipelineHandle gbufferCutoutPipeline() const { return m_gbufferCutoutPipeline; }
    [[nodiscard]] RhiBindGroupHandle gbufferOpaqueBindGroup() const { return m_gbufferBindGroups[0]; }
    [[nodiscard]] RhiBindGroupHandle gbufferCutoutBindGroup() const { return m_gbufferBindGroups[1]; }
    [[nodiscard]] RhiBindGroupLayoutHandle shadowMetadataLayout() const { return m_shadowMetadataLayout; }
    [[nodiscard]] RhiPipelineHandle shadowOpaquePipeline() const { return m_shadowOpaquePipeline; }
    [[nodiscard]] RhiPipelineHandle shadowCutoutPipeline() const { return m_shadowCutoutPipeline; }
    [[nodiscard]] RhiPipelineHandle shadowTransparentPipeline() const { return m_shadowTransparentPipeline; }
    [[nodiscard]] RhiBindGroupHandle shadowBindGroup() const { return m_shadowBindGroup; }

private:
    static constexpr size_t kGBufferTextureSlotCount = 7u;
    static constexpr size_t kShadowTextureSlotCount = 4u;

    bool ensureGBufferPipeline(ResourceMgr& resourceMgr);
    bool ensureGBufferTextureViews(ResourceMgr& resourceMgr);
    bool ensureGBufferBindGroups();
    bool ensureTextureView(size_t slot,
                           RhiTextureHandle texture,
                           RhiTextureViewType viewType);
    void destroyGBufferBindGroups();
    void destroyGBufferTextureViews();
    void destroyGBufferResources();
    bool ensureShadowPipeline(ResourceMgr& resourceMgr);
    bool ensureShadowTextureViews(ResourceMgr& resourceMgr);
    bool ensureShadowBindGroup();
    bool ensureShadowTextureView(size_t slot,
                                 RhiTextureHandle texture,
                                 RhiTextureViewType viewType);
    void destroyShadowBindGroup();
    void destroyShadowTextureViews();
    void destroyShadowResources();

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

    float m_shadowSamplerAnisotropy = 1.0f;
    RhiBindGroupLayoutHandle m_shadowMetadataLayout;
    RhiBindGroupLayoutHandle m_shadowMaterialLayout;
    RhiPipelineLayoutHandle m_shadowPipelineLayout;
    RhiShaderHandle m_shadowVertexShader;
    RhiShaderHandle m_shadowFragmentShader;
    RhiPipelineHandle m_shadowOpaquePipeline;
    RhiPipelineHandle m_shadowCutoutPipeline;
    RhiPipelineHandle m_shadowTransparentPipeline;
    RhiBufferHandle m_shadowParamsBuffer;
    RhiBindGroupHandle m_shadowBindGroup;
    RhiSamplerHandle m_shadowBlockSampler;
    RhiSamplerHandle m_shadowLinearClampSampler;
    RhiSamplerHandle m_shadowLinearRepeatSampler;
    std::array<RhiTextureHandle, kShadowTextureSlotCount> m_shadowViewTextures{};
    std::array<RhiTextureViewHandle, kShadowTextureSlotCount> m_shadowTextureViews{};
    std::array<RhiTextureViewHandle, kShadowTextureSlotCount> m_shadowBoundViews{};
};

#endif // MECRAFT_TERRAIN_RHI_PIPELINE_SET_H
