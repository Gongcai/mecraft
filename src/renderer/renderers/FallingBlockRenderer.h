#ifndef MECRAFT_FALLING_BLOCK_RENDERER_H
#define MECRAFT_FALLING_BLOCK_RENDERER_H

#include <cstdint>
#include <unordered_map>

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "../core/Shader.h"
#include "../mesh/BlockMeshBuilder.h"
#include "../../world/block/Block.h"
#include "../../world/block/BlockStateRegistry.h"

class Camera;
class IWorldView;
class ResourceMgr;
class Window;

namespace ecs { class GameplayRegistry; }

/// Renders falling-block entities (FallingBlockTag) as full-cube block meshes
/// translated to the entity's interpolated render position.
///
/// Geometry is shared with DropRenderer via renderer::buildBlockCubeMesh.
/// Lighting/shadow shaders are reused (drop_gbuffer / shadow_depth) — no new
/// shaders required. Reuses DropEntityIdComponent as the per-object velocity key.
class FallingBlockRenderer {
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown();

    // GBuffer path: renders falling blocks into the deferred GBuffer.
    // Caller must have already bound the GBuffer FBO with terrain+entity depth
    // and attached the per-object velocity target.
    void renderToGBuffer(const IWorldView& worldView, const ecs::GameplayRegistry& registry,
                         const glm::mat4& jitteredViewProj,
                         const glm::mat4& previousViewProj,
                         float animationTime);

    // Shadow path: renders falling blocks into the CSM shadow map.
    // Caller must have already bound the shadow FBO layer.
    void renderToShadowMap(const ecs::GameplayRegistry& registry,
                           const glm::mat4& shadowViewProj,
                           const glm::mat4& shadowView, const glm::mat4& shadowProjection,
                           float animationTime, float shaderTime);

private:
    const renderer::BlockCubeMesh* getOrCreateMesh(BlockStateId stateId);

    ResourceMgr* m_resourceMgr = nullptr;
    Shader* m_gbufferShader = nullptr;   // drop_gbuffer (block path)
    Shader* m_shadowShader = nullptr;    // shadow_depth (block path, uUseModel=1)
    std::unordered_map<BlockStateId, renderer::BlockCubeMesh> m_meshes;
    // Per-object velocity: previous-frame model matrix per entity (by drop ID).
    std::unordered_map<std::size_t, glm::mat4> m_previousModelMatrices;
};

#endif // MECRAFT_FALLING_BLOCK_RENDERER_H
