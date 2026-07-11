#ifndef MECRAFT_FALLING_BLOCK_RENDERER_H
#define MECRAFT_FALLING_BLOCK_RENDERER_H

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "../core/Shader.h"
#include "../mesh/BlockMeshBuilder.h"
#include "../../world/block/Block.h"
#include "../../world/block/BlockStateRegistry.h"

class Camera;
class IWorldView;
class ResourceMgr;
class RhiCommandList;
class RhiDevice;
class Window;

namespace ecs { class GameplayRegistry; }

/// Renders falling-block entities (FallingBlockTag) as full-cube block meshes
/// translated to the entity's interpolated render position.
///
/// Geometry is shared with DropRenderer via renderer::buildBlockCubeMesh.
/// Uses an explicit RHI GBuffer pipeline and the transitional shadow-depth path.
/// Reuses DropEntityIdComponent as the per-object velocity key.
class FallingBlockRenderer {
public:
    [[nodiscard]] bool init(ResourceMgr& resourceMgr);
    void shutdown();
    void prepareFrame(const IWorldView& worldView, const ecs::GameplayRegistry& registry);

    // GBuffer path: renders falling blocks into the deferred GBuffer.
    // Caller must have already bound the GBuffer FBO with terrain+entity depth
    // and attached the per-object velocity target.
    void renderToGBuffer(RhiCommandList& commandList, const glm::mat4& jitteredViewProj,
                         const glm::mat4& previousViewProj,
                         float animationTime);

    // Shadow path: renders falling blocks into the CSM shadow map.
    // Caller must have already bound the shadow FBO layer.
    void renderToShadowMap(const glm::mat4& shadowViewProj,
                           const glm::mat4& shadowView, const glm::mat4& shadowProjection,
                           float animationTime, float shaderTime);

private:
    struct RenderInstance {
        BlockStateId stateId{};
        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 previousModel = glm::mat4(1.0f);
        glm::vec2 light = glm::vec2(1.0f, 0.0f);
    };

    const renderer::BlockCubeMesh* getOrCreateMesh(BlockStateId stateId);
    [[nodiscard]] bool createGBufferRhiResources();
    void destroyGBufferRhiResources();

    ResourceMgr* m_resourceMgr = nullptr;
    RhiDevice* m_rhiDevice = nullptr;
    Shader* m_shadowShader = nullptr;    // shadow_depth (block path, uUseModel=1)
    std::unordered_map<BlockStateId, renderer::BlockCubeMesh> m_meshes;
    // Per-object velocity: previous-frame model matrix per entity (by drop ID).
    std::unordered_map<std::size_t, glm::mat4> m_previousModelMatrices;
    std::vector<RenderInstance> m_renderInstances;
    RhiTextureViewHandle m_textureArrayView;
    RhiTextureViewHandle m_grassColormapView;
    RhiTextureViewHandle m_foliageColormapView;
    RhiSamplerHandle m_sampler;
    RhiShaderHandle m_gbufferVertexShader;
    RhiShaderHandle m_gbufferFragmentShader;
    RhiBindGroupLayoutHandle m_gbufferBindGroupLayout;
    RhiPipelineLayoutHandle m_gbufferPipelineLayout;
    RhiPipelineHandle m_gbufferPipeline;
    RhiBindGroupHandle m_gbufferBindGroup;
};

#endif // MECRAFT_FALLING_BLOCK_RENDERER_H
