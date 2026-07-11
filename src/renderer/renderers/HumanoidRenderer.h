#ifndef MECRAFT_HUMANOID_RENDERER_H
#define MECRAFT_HUMANOID_RENDERER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include "../../ecs/components/Components.h"
#include "../../ecs/entity/EntitySkinLayout.h"
#include "../../ecs/entity/EntityModelRegistry.h"
#include "../rhi/RhiHandles.h"
#include "HumanoidSkinLayoutCatalog.h"

class Camera;
class IWorldView;
class RhiCommandList;
class RhiDevice;
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

    void init(ResourceMgr& resourceMgr, RhiDevice& rhiDevice);
    void shutdown();
    void prepareFrame(const IWorldView& worldView,
                      ecs::GameplayRegistry& registry,
                      RenderMode mode = kRenderAll);
    void finishFrame();
    void renderPreparedToGBuffer(RhiCommandList& commandList,
                                 const glm::mat4& viewProj,
                                 const glm::mat4& previousViewProj);
    void renderPreparedToShadowMap(RhiCommandList& commandList,
                                   const glm::mat4& shadowViewProj,
                                   const glm::vec3& cameraPos,
                                   float splitNear,
                                   float splitFar);
    void render(ecs::GameplayRegistry& registry, const Camera& camera, const Window& window,
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
        uint32_t vao = 0;
        uint32_t vbo = 0;
        RhiBufferHandle rhiVertexBuffer;
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
    std::unordered_map<std::string, PartMesh> m_entityModelPartMeshes;

    struct TextureResource {
        RhiTextureHandle texture;
        RhiTextureViewHandle view;
        RhiBindGroupHandle gbufferBindGroup;
        RhiBindGroupHandle shadowBindGroup;
    };
    std::unordered_map<std::string, TextureResource> m_textureResources;

    struct PreparedPartDraw {
        const PartMesh* mesh = nullptr;
        const TextureResource* texture = nullptr;
        glm::mat4 model{1.0f};
        glm::mat4 previousModel{1.0f};
        glm::vec3 entityCenter{0.0f};
        glm::vec2 light{1.0f, 0.0f};
        float hurtFlash = 0.0f;
    };
    std::vector<PreparedPartDraw> m_preparedPartDraws;

    Shader* m_shader = nullptr;          // shadow-aware shader for UI/held-item compatible preview paths
    Shader* m_forwardShader = nullptr;   // forward vanilla world entity shader
    ResourceMgr* m_resourceMgr = nullptr;
    RhiDevice* m_rhiDevice = nullptr;
    uint32_t m_neutralShadowDepth = 0;
    uint32_t m_neutralShadowDepthCompare = 0;
    RhiTextureHandle m_neutralShadowDepthHandle;
    RhiTextureViewHandle m_neutralShadowDepthView;
    float m_inventoryPreviewHeadLookX = 0.0f;
    float m_inventoryPreviewHeadLookY = 0.0f;
    float m_inventoryPreviewBodyLookX = 0.0f;
    float m_inventoryPreviewBodyLookY = 0.0f;
    float m_inventoryPreviewLastTime = -1.0f;
    RhiSamplerHandle m_gbufferSampler;
    RhiShaderHandle m_gbufferRhiVertexShader;
    RhiShaderHandle m_gbufferRhiFragmentShader;
    RhiBindGroupLayoutHandle m_gbufferRhiBindGroupLayout;
    RhiPipelineLayoutHandle m_gbufferRhiPipelineLayout;
    RhiPipelineHandle m_gbufferRhiPipeline;
    RhiShaderHandle m_shadowRhiVertexShader;
    RhiShaderHandle m_shadowRhiFragmentShader;
    RhiBindGroupLayoutHandle m_shadowRhiBindGroupLayout;
    RhiPipelineLayoutHandle m_shadowRhiPipelineLayout;
    RhiPipelineHandle m_shadowRhiPipeline;

    void destroyMesh(PartMesh& mesh) const;

    PartMesh buildPartMesh(const renderer::HumanoidPartMeshDefinition& definition,
                           float textureWidth,
                           float textureHeight) const;
    PartMesh buildEntityModelPartMesh(const ecs::EntityModelPartDefinition& definition,
                                      float textureWidth,
                                      float textureHeight) const;

    static FaceUvRect pixelRectToUv(float x0, float y0, float x1, float y1,
                                    float textureWidth, float textureHeight);

    PartMesh* getMeshForPart(ecs::StevePartType partType, ecs::EntitySkinLayoutKind skinLayout);
    PartMesh* getMeshForEntityModelPart(const std::string& modelId, const std::string& partName);
    const TextureResource& requireTextureResource(const std::string& textureKey);
    void createGBufferRhiResources();
    void destroyGBufferRhiResources();
    [[nodiscard]] bool ensureNeutralShadowTextures();
    [[nodiscard]] bool bindDisabledShadowNeutralTextures(Shader& shader);

    // Per-object velocity: stores previous-frame model matrix per entity part.
    std::unordered_map<entt::entity, glm::mat4> m_previousModelMatrices;
    std::unordered_map<entt::entity, glm::mat4> m_currentModelMatrices;

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
    void drawGenericMobParts(entt::registry& reg, entt::entity root,
                             const ecs::MobVisualComponent& visual,
                             const ecs::TransformComponent& rootTransform,
                             Shader& shader, int modelLoc, int prevModelLoc);
};

#endif // MECRAFT_HUMANOID_RENDERER_H
