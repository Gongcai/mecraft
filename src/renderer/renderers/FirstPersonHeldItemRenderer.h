#ifndef MECRAFT_FIRST_PERSON_HELD_ITEM_RENDERER_H
#define MECRAFT_FIRST_PERSON_HELD_ITEM_RENDERER_H

#include <cstdint>
#include <unordered_map>

#include <glad/glad.h>
#include <glm/mat4x4.hpp>

#include "../../item/Item.h"
#include "../../world/block/Block.h"
#include "../core/FrameOutput.h"

class Inventory;
class ResourceMgr;
class Shader;
class Window;
struct HeldItemPreviewMotion;

class FirstPersonHeldItemRenderer {
public:
    struct Config {
        float fovDegrees = 70.0f;

        float armPosX = 0.72f;
        float armPosY = -0.82f;
        float armPosZ = -1.08f;
        float armPitchDegrees = -18.0f;
        float armYawDegrees = 18.0f;
        float armRollDegrees = 12.0f;
        float armScale = 1.0f;

        float itemPosX = 0.54f;
        float itemPosY = -0.56f;
        float itemPosZ = -1.16f;
        float itemPitchDegrees = -28.0f;
        float itemYawDegrees = 44.0f;
        float itemRollDegrees = -18.0f;
        float itemScale = 0.62f;
        float blockPitchDegrees = -28.0f;
        float blockYawDegrees = 44.0f;
        float blockScale = 0.46f;

        float equipDrop = 0.52f;
        float equipSpeed = 5.6f;
        float bobOffsetX = 0.055f;
        float bobOffsetY = -0.045f;
        float bobRollDegrees = 2.2f;
        float viewLagFollowSpeed = 14.0f;
        float viewLagMaxDegrees = 14.0f;
        float viewLagOffsetX = 0.010f;
        float viewLagOffsetY = 0.010f;
        float viewLagYawDegrees = 0.65f;
        float viewLagPitchDegrees = 0.55f;

        float swingDurationSeconds = 0.34f;
        float armSwingX = -0.10f;
        float armSwingY = -0.14f;
        float armSwingZ = -0.10f;
        float armSwingPitchDegrees = -38.0f;
        float armSwingYawDegrees = 25.0f;
        float armSwingRollDegrees = -25.0f;
        float itemSwingX = -0.18f;
        float itemSwingY = -0.22f;
        float itemSwingZ = -0.22f;
        float itemSwingPitchDegrees = -52.0f;
        float itemSwingYawDegrees = 36.0f;
        float itemSwingRollDegrees = -32.0f;
    };

    struct SteveVertex {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        float nx = 0.0f;
        float ny = 0.0f;
        float nz = 0.0f;
    };

    void init(ResourceMgr& resourceMgr);
    void shutdown();
    /// Switch to forward vanilla shaders (no CSM shadow / held_item_shadow contract).
    /// Must be called after init(). Reverts to deferred shaders if false.
    void setForwardMode(bool forward);
    void loadConfig();
    void saveConfig() const;

    [[nodiscard]] const Config& getConfig() const;
    void setConfig(const Config& config);
    void resetConfig();

    void triggerSwing();
    void setContinuousSwing(bool active);

    // Shadow data from Renderer — must be set before render() each frame.
    struct ShadowData {
        glm::mat4 cascadeViewProj[4]{};
        float cascadeSplitFar[4]{};
        float cascadeTexelWorldSize[4]{};
        GLuint shadowTexture = 0;        // sampler2DArrayShadow (shadowtex1)
        GLuint shadowDepthRaw = 0;       // sampler2DArray
        GLuint shadowDepthAll = 0;       // sampler2DArrayShadow (shadowtex0)
        GLuint shadowDepthAllRaw = 0;    // sampler2DArray
        GLuint shadowColor0 = 0;         // sampler2DArray
        GLuint shadowColor1 = 0;         // sampler2DArray
        glm::vec3 cameraPos = glm::vec3(0.0f);
        glm::vec3 sunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
        float shadowDistance = 192.0f;
        float constantBias = 0.0007f;
        float slopeBias = 0.0022f;
        float normalOffset = 0.035f;
        float softness = 1.0f;
        float pcssStrength = 0.72f;
        int cascadeCount = 4;
        int softShadowsEnabled = 1;
        int pcssShadowsEnabled = 1;
        int shadowsEnabled = 1;
        float skyIntensity = 1.0f;
        float ambientStrength = 0.55f;
    };
    void setShadowData(const ShadowData& data);

    /// Convert FirstPersonShadowData (from FrameOutput) to ShadowData.
    static ShadowData fromFirstPersonShadowData(const FirstPersonShadowData& sd);

    void render(const Window& window,
                const Inventory& inventory,
                const HeldItemPreviewMotion& motion,
                float timeSeconds);

private:
    struct Mesh {
        GLuint vao = 0;
        GLuint vbo = 0;
        uint32_t vertexCount = 0;
    };

    Mesh* getOrCreateBlockMesh(BlockID blockId);
    Mesh buildBlockMesh(BlockID blockId) const;
    Mesh* getOrCreateItemMesh(ItemID itemId);
    Mesh buildItemMesh(ItemID itemId) const;
    Mesh buildRightArmMesh() const;
    static void destroyMesh(Mesh& mesh);

    void drawArm(const glm::mat4& viewProj, const glm::mat4& model);
    void bindShadowUniforms(Shader& shader) const;
    void drawItem(ItemID itemId,
                  const glm::mat4& view,
                  const glm::mat4& viewProj,
                  const glm::mat4& model);

    ResourceMgr* m_resourceMgr = nullptr;
    Shader* m_blockShader = nullptr;
    Shader* m_itemShader = nullptr;
    Shader* m_steveShader = nullptr;
    Shader* m_deferredBlockShader = nullptr;  // Original deferred shader (block_item_lit)
    Shader* m_deferredItemShader = nullptr;   // Original deferred shader (item_model)
    Shader* m_deferredSteveShader = nullptr;  // Original deferred shader (steve)
    Mesh m_rightArmMesh;
    std::unordered_map<BlockID, Mesh> m_blockMeshes;
    std::unordered_map<ItemID, Mesh> m_itemMeshes;

    bool m_hasPrevSample = false;
    float m_prevTimeSeconds = 0.0f;
    ItemID m_visibleItemId = 0;
    ItemID m_lastSelectedItemId = 0;
    float m_equipProgress = 1.0f;
    float m_walkBobBlend = 0.0f;
    bool m_hasLagSample = false;
    float m_lagYawDegrees = -90.0f;
    float m_lagPitchDegrees = 0.0f;
    bool m_swingActive = false;
    bool m_continuousSwing = false;
    float m_swingElapsed = 0.0f;
    Config m_config;
    ShadowData m_shadowData{};
    bool m_initialized = false;
};

#endif // MECRAFT_FIRST_PERSON_HELD_ITEM_RENDERER_H
