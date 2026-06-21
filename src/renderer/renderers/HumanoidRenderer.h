#ifndef MECRAFT_HUMANOID_RENDERER_H
#define MECRAFT_HUMANOID_RENDERER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include "../../ecs/components/Components.h"
#include "../../ecs/entity/EntitySkinLayout.h"
#include "HumanoidSkinLayoutCatalog.h"

class Camera;
class IWorldView;
class Shader;
class ResourceMgr;
class Window;
class World;

namespace ecs {
class GameplayRegistry;
}

class HumanoidRenderer {
public:
    enum RenderMode : uint8_t {
        kRenderAll,       // render local Steve, remote Steve, and mob entities
        kRenderMobsOnly   // hide only the local Steve model (first-person view)
    };

    void init(ResourceMgr& resourceMgr);
    void shutdown();
    void render(ecs::GameplayRegistry& registry, const Camera& camera, const Window& window,
                RenderMode mode = kRenderAll);
    // GBuffer path: renders entities into the deferred GBuffer (5 MRT).
    // Caller must have already bound the GBuffer FBO with terrain depth.
    void renderToGBuffer(ecs::GameplayRegistry& registry,
                         const glm::mat4& jitteredViewProj,
                         const glm::mat4& previousViewProj,
                         RenderMode mode = kRenderAll);
    void renderToGBuffer(const IWorldView& worldView, ecs::GameplayRegistry& registry,
                         const glm::mat4& jitteredViewProj,
                         const glm::mat4& previousViewProj,
                         RenderMode mode = kRenderAll);
    // Shadow path: renders entities into the CSM shadow map.
    // Caller must have already bound the shadow FBO layer.
    void renderToShadowMap(ecs::GameplayRegistry& registry,
                           const glm::mat4& shadowViewProj,
                           RenderMode mode = kRenderAll);
    void renderToShadowMap(const IWorldView& worldView, ecs::GameplayRegistry& registry,
                           const glm::mat4& shadowViewProj,
                           const glm::vec3& cameraPos, float splitNear, float splitFar,
                           RenderMode mode = kRenderAll);
    void renderInventoryPreview(float x,
                                float y,
                                float width,
                                float height,
                                float uiScale,
                                float pointerX,
                                float pointerY,
                                float timeSeconds);

private:
    struct PartMesh {
        GLuint vao = 0;
        GLuint vbo = 0;
        uint32_t vertexCount = 0;
    };

    struct SteveVertex {
        float x, y, z;
        float u, v;
        float nx, ny, nz;
    };

    struct FaceUvRect {
        float u0, v0, u1, v1;
    };

    std::array<std::array<PartMesh, renderer::kHumanoidPartTypeCount>, renderer::kHumanoidSkinLayoutCount> m_skinLayoutMeshes{};

    Shader* m_shader = nullptr;          // shadow-aware shader for UI/held-item compatible preview paths
    Shader* m_forwardShader = nullptr;   // forward vanilla world entity shader
    Shader* m_gbufferShader = nullptr;   // entity GBuffer shader (entity_gbuffer.fs)
    Shader* m_shadowShader = nullptr;    // entity shadow shader (entity_shadow.fs)
    ResourceMgr* m_resourceMgr = nullptr;
    GLuint m_fallbackShadowDepth = 0;
    GLuint m_fallbackShadowDepthCompare = 0;
    float m_inventoryPreviewHeadLookX = 0.0f;
    float m_inventoryPreviewHeadLookY = 0.0f;
    float m_inventoryPreviewBodyLookX = 0.0f;
    float m_inventoryPreviewBodyLookY = 0.0f;
    float m_inventoryPreviewLastTime = -1.0f;

    static void destroyMesh(PartMesh& mesh);

    PartMesh buildPartMesh(const renderer::HumanoidPartMeshDefinition& definition) const;

    static FaceUvRect pixelRectToUv(float x0, float y0, float x1, float y1);

    PartMesh* getMeshForPart(ecs::StevePartType partType, ecs::EntitySkinLayoutKind skinLayout);
    void ensureShadowFallbackTextures();
    void bindDisabledShadowFallback(Shader& shader);

    // Per-object velocity: stores previous-frame model matrix per entity part.
    std::unordered_map<entt::entity, glm::mat4> m_previousModelMatrices;

    // Shared entity draw helper — iterates ECS and draws body parts with the given shader.
    // prevModelLoc: uniform location for previous-frame model matrix (-1 to skip).
    void drawEntities(ecs::GameplayRegistry& gameplayReg, Shader& shader,
                      int modelLoc, int viewProjLoc, int prevModelLoc,
                      const glm::mat4& viewProj, RenderMode mode);
    // Overload with world light query — sets uEntitySunlight/uEntityBlockLight per entity.
    void drawEntities(const IWorldView& worldView, ecs::GameplayRegistry& gameplayReg, Shader& shader,
                      int modelLoc, int viewProjLoc, int prevModelLoc,
                      const glm::mat4& viewProj, RenderMode mode,
                      const glm::vec3& cameraPos = glm::vec3(0.0f),
                      float splitNear = 0.0f,
                      float splitFar = FLT_MAX);
    // Query world light at a block position. Returns (sunlight, blocklight) normalized to [0,1].
    static glm::vec2 queryWorldLight(const IWorldView& worldView, const glm::vec3& position);
};

#endif // MECRAFT_HUMANOID_RENDERER_H
