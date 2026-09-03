#ifndef MECRAFT_PARTICLESYSTEM_H
#define MECRAFT_PARTICLESYSTEM_H

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include "renderer/contracts/GameplayRenderSnapshot.h"
#include "renderer/rhi/RhiHandles.h"

struct GameResources;
class RhiCommandList;
class RhiDevice;
struct TextureArray;

class ParticleSystem {
public:
    [[nodiscard]] bool init(GameResources& resources, RhiDevice& rhiDevice);
    void shutdown();

    void prepareFrame(const glm::mat4& view, const std::vector<renderer::contracts::ParticleRenderData>& particles,
                      RhiCommandList& commandList);

    /// Reports whether the latest prepareFrame produced billboard vertices.
    /// Used to skip scene writeback copies when no particle was rendered.
    [[nodiscard]] bool hasPreparedVertices() const { return m_preparedVertexCount > 0u; }
    void render(RhiCommandList& commandList, const glm::mat4& viewProj);
    void renderToSceneResolved(RhiCommandList& commandList, RhiTextureHandle voxelLightTexture,
                               RhiTextureHandle depthTexture, const glm::mat4& viewProj, const glm::vec2& screenSize,
                               float preExposure);

private:
    // Build billboard vertices from the immutable particle snapshot. Returns particle count.
    int buildVertices(const glm::mat4& view, const std::vector<renderer::contracts::ParticleRenderData>& particles,
                      std::vector<float>& vertices);
    [[nodiscard]] bool createRhiResources();
    [[nodiscard]] bool ensureDeferredBindGroup(RhiTextureHandle voxelLightTexture, RhiTextureHandle depthTexture);
    void destroyDeferredBindGroup();
    void destroyRhiResources();

    RhiDevice* m_rhiDevice = nullptr;
    const TextureArray* m_texArray = nullptr;
    RhiTextureHandle m_boundVoxelLightTexture;
    RhiTextureHandle m_boundDepthTexture;
    RhiBufferHandle m_rhiVertexBuffer;
    RhiTextureViewHandle m_textureArrayView;
    RhiTextureViewHandle m_voxelLightView;
    RhiTextureViewHandle m_depthView;
    RhiSamplerHandle m_linearSampler;
    RhiSamplerHandle m_nearestSampler;
    RhiShaderHandle m_vertexShader;
    RhiShaderHandle m_forwardFragmentShader;
    RhiShaderHandle m_deferredFragmentShader;
    RhiBindGroupLayoutHandle m_forwardBindGroupLayout;
    RhiBindGroupLayoutHandle m_deferredBindGroupLayout;
    RhiPipelineLayoutHandle m_forwardPipelineLayout;
    RhiPipelineLayoutHandle m_deferredPipelineLayout;
    RhiPipelineHandle m_forwardPipeline;
    RhiPipelineHandle m_deferredPipeline;
    RhiBindGroupHandle m_forwardBindGroup;
    RhiBindGroupHandle m_deferredBindGroup;
    uint32_t m_preparedVertexCount = 0;
    std::vector<float> m_vertexBuffer;

    static constexpr int MAX_PARTICLES = 1000;
    static constexpr uint64_t VERTEX_BUFFER_BYTES = MAX_PARTICLES * 6u * 8u * sizeof(float);
};

#endif // MECRAFT_PARTICLESYSTEM_H
