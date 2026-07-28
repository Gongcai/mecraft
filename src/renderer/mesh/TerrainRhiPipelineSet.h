#ifndef MECRAFT_TERRAIN_RHI_PIPELINE_SET_H
#define MECRAFT_TERRAIN_RHI_PIPELINE_SET_H

#include "TerrainRenderer.h"
#include "renderer/contracts/ClusteredLightingContract.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiTypes.h"

#include <array>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class ResourceMgr;
class DeferredRenderTargets;
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

struct TerrainWaterFrameData {
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 viewProj = glm::mat4(1.0f);
    glm::mat4 invViewProj = glm::mat4(1.0f);
    glm::vec3 cameraPos = glm::vec3(0.0f);
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    float animationTime = 0.0f;
    float shaderTime = 0.0f;
    glm::vec3 sunDirection = glm::vec3(0.0f);
    glm::vec3 moonDirection = glm::vec3(0.0f);
    glm::vec3 sunLightColor = glm::vec3(0.0f);
    glm::vec3 moonLightColor = glm::vec3(0.0f);
    glm::vec3 skyAmbientColor = glm::vec3(0.0f);
    float skyIntensity = 1.0f;
    float moonVisibility = 0.0f;
    float moonPhaseFlux = 0.0f;
    float weatherWetness = 0.0f;
    float skyWetness = 0.0f;
    float fogWetness = 0.0f;
    float cloudWetness = 0.0f;
    float surfaceWetness = 0.0f;
    uint64_t frameIndex = 0u;
    bool skyCaptureEnabled = false;
    bool compositeInputsEnabled = false;
    bool depthSofteningEnabled = false;
    bool volumetricFogActive = false;
    bool freezeBias = false;
    bool rainSurfaceRipplesEnabled = false;
    bool eyeInWater = false;
};

class TerrainRhiPipelineSet {
public:
    TerrainRhiPipelineSet();
    ~TerrainRhiPipelineSet();

    void init(RhiDevice& rhiDevice);
    void shutdown();

    [[nodiscard]] bool configureClusteredLighting(
        RhiBindGroupLayoutHandle bindGroupLayout,
        RhiBindGroupHandle bindGroup,
        const renderer::contracts::ClusterGrid& grid);

    bool prepareGBuffer(RhiCommandList& commandList,
                        ResourceMgr& resourceMgr,
                        const TerrainFrameData& frame,
                        const TerrainRenderSettings& settings);
    bool prepareShadow(RhiCommandList& commandList,
                       ResourceMgr& resourceMgr,
                       const TerrainShadowFrameData& frame);
    bool prepareTransparent(RhiCommandList& commandList,
                            ResourceMgr& resourceMgr,
                            DeferredRenderTargets& targets,
                            const TerrainFrameData& frame,
                            const TerrainRenderSettings& settings,
                            int heldBlockLightValue,
                            bool volumetricFogShadersReady);
    bool prepareWater(RhiCommandList& commandList,
                      ResourceMgr& resourceMgr,
                      DeferredRenderTargets& targets,
                      const TerrainWaterFrameData& frame);
    bool prepareForward(RhiCommandList& commandList,
                        ResourceMgr& resourceMgr,
                        const TerrainFrameData& frame);

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
    [[nodiscard]] RhiBindGroupLayoutHandle transparentMetadataLayout() const { return m_transparentMetadataLayout; }
    [[nodiscard]] RhiPipelineHandle transparentPipeline() const { return m_transparentPipeline; }
    [[nodiscard]] RhiBindGroupHandle transparentBindGroup() const { return m_transparentBindGroup; }
    [[nodiscard]] RhiBindGroupHandle transparentClusterBindGroup() const {
        return m_transparentClusterBindGroup;
    }
    [[nodiscard]] RhiBindGroupLayoutHandle waterMetadataLayout() const { return m_waterMetadataLayout; }
    [[nodiscard]] RhiPipelineHandle waterPipeline() const { return m_waterPipeline; }
    [[nodiscard]] RhiBindGroupHandle waterBindGroup() const { return m_waterBindGroup; }
    [[nodiscard]] RhiBindGroupLayoutHandle forwardMetadataLayout() const { return m_forwardMetadataLayout; }
    [[nodiscard]] RhiPipelineHandle forwardOpaquePipeline() const { return m_forwardOpaquePipeline; }
    [[nodiscard]] RhiPipelineHandle forwardCutoutPipeline() const { return m_forwardCutoutPipeline; }
    [[nodiscard]] RhiPipelineHandle forwardTransparentPipeline() const { return m_forwardTransparentPipeline; }
    [[nodiscard]] RhiBindGroupHandle forwardOpaqueBindGroup() const { return m_forwardBindGroups[0]; }
    [[nodiscard]] RhiBindGroupHandle forwardCutoutBindGroup() const { return m_forwardBindGroups[0]; }
    [[nodiscard]] RhiBindGroupHandle forwardTransparentBindGroup() const { return m_forwardBindGroups[1]; }

private:
    static constexpr size_t kGBufferTextureSlotCount = 7u;
    static constexpr size_t kShadowTextureSlotCount = 4u;
    static constexpr size_t kTransparentTextureSlotCount = 10u;
    static constexpr size_t kWaterTextureSlotCount = 9u;
    static constexpr size_t kForwardTextureSlotCount = 5u;

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
    bool ensureTransparentPipeline(ResourceMgr& resourceMgr);
    bool ensureTransparentTextureViews(ResourceMgr& resourceMgr,
                                       DeferredRenderTargets& targets);
    bool ensureTransparentBindGroup();
    bool ensureTransparentTextureView(size_t slot,
                                      RhiTextureHandle texture,
                                      RhiTextureViewType viewType,
                                      RhiTextureFormat format);
    void destroyTransparentBindGroup();
    void destroyTransparentTextureViews();
    void destroyTransparentResources();
    bool ensureWaterPipeline(ResourceMgr& resourceMgr);
    bool ensureWaterTextureViews(ResourceMgr& resourceMgr,
                                 DeferredRenderTargets& targets);
    bool ensureWaterBindGroup();
    bool ensureWaterTextureView(size_t slot,
                                RhiTextureHandle texture,
                                RhiTextureViewType viewType,
                                RhiTextureFormat format);
    void destroyWaterBindGroup();
    void destroyWaterTextureViews();
    void destroyWaterResources();
    bool ensureForwardPipeline(ResourceMgr& resourceMgr);
    bool ensureForwardTextureViews(ResourceMgr& resourceMgr);
    bool ensureForwardBindGroups();
    bool ensureForwardTextureView(size_t slot,
                                  RhiTextureHandle texture,
                                  RhiTextureViewType viewType);
    void destroyForwardBindGroups();
    void destroyForwardTextureViews();
    void destroyForwardResources();

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
    RhiShaderHandle m_shadowDepthFragmentShader;
    RhiShaderHandle m_shadowColorFragmentShader;
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

    float m_transparentSamplerAnisotropy = 1.0f;
    RhiBindGroupLayoutHandle m_transparentMetadataLayout;
    RhiBindGroupLayoutHandle m_transparentMaterialLayout;
    RhiBindGroupLayoutHandle m_transparentClusterLayout;
    RhiPipelineLayoutHandle m_transparentPipelineLayout;
    RhiShaderHandle m_transparentVertexShader;
    RhiShaderHandle m_transparentFragmentShader;
    RhiPipelineHandle m_transparentPipeline;
    RhiBufferHandle m_transparentParamsBuffer;
    RhiBindGroupHandle m_transparentBindGroup;
    RhiBindGroupHandle m_transparentClusterBindGroup;
    renderer::contracts::ClusterGrid m_transparentClusterGrid;
    RhiSamplerHandle m_transparentBlockSampler;
    RhiSamplerHandle m_transparentLinearClampSampler;
    RhiSamplerHandle m_transparentLinearRepeatSampler;
    RhiSamplerHandle m_transparentNearestClampSampler;
    std::array<RhiTextureHandle, kTransparentTextureSlotCount> m_transparentViewTextures{};
    std::array<RhiTextureViewHandle, kTransparentTextureSlotCount> m_transparentTextureViews{};
    std::array<RhiTextureViewHandle, kTransparentTextureSlotCount> m_transparentBoundViews{};

    float m_waterSamplerAnisotropy = 1.0f;
    RhiBindGroupLayoutHandle m_waterMetadataLayout;
    RhiBindGroupLayoutHandle m_waterMaterialLayout;
    RhiPipelineLayoutHandle m_waterPipelineLayout;
    RhiShaderHandle m_waterVertexShader;
    RhiShaderHandle m_waterFragmentShader;
    RhiPipelineHandle m_waterPipeline;
    RhiBufferHandle m_waterParamsBuffer;
    RhiBindGroupHandle m_waterBindGroup;
    RhiSamplerHandle m_waterBlockSampler;
    RhiSamplerHandle m_waterLinearClampSampler;
    RhiSamplerHandle m_waterLinearRepeatSampler;
    RhiSamplerHandle m_waterNearestClampSampler;
    std::array<RhiTextureHandle, kWaterTextureSlotCount> m_waterViewTextures{};
    std::array<RhiTextureViewHandle, kWaterTextureSlotCount> m_waterTextureViews{};
    std::array<RhiTextureViewHandle, kWaterTextureSlotCount> m_waterBoundViews{};

    float m_forwardSamplerAnisotropy = 1.0f;
    RhiBindGroupLayoutHandle m_forwardMetadataLayout;
    RhiBindGroupLayoutHandle m_forwardMaterialLayout;
    RhiPipelineLayoutHandle m_forwardPipelineLayout;
    RhiShaderHandle m_forwardVertexShader;
    RhiShaderHandle m_forwardFragmentShader;
    RhiPipelineHandle m_forwardOpaquePipeline;
    RhiPipelineHandle m_forwardCutoutPipeline;
    RhiPipelineHandle m_forwardTransparentPipeline;
    std::array<RhiBufferHandle, 2> m_forwardParamsBuffers{};
    std::array<RhiBindGroupHandle, 2> m_forwardBindGroups{};
    RhiSamplerHandle m_forwardBlockSampler;
    RhiSamplerHandle m_forwardLinearClampSampler;
    std::array<RhiTextureHandle, kForwardTextureSlotCount> m_forwardViewTextures{};
    std::array<RhiTextureViewHandle, kForwardTextureSlotCount> m_forwardTextureViews{};
    std::array<RhiTextureViewHandle, kForwardTextureSlotCount> m_forwardBoundViews{};
};

#endif // MECRAFT_TERRAIN_RHI_PIPELINE_SET_H
