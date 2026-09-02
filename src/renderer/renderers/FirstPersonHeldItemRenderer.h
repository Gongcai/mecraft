#ifndef MECRAFT_FIRST_PERSON_HELD_ITEM_RENDERER_H
#define MECRAFT_FIRST_PERSON_HELD_ITEM_RENDERER_H

#include <cstdint>
#include <array>
#include <unordered_map>

#include <glm/mat4x4.hpp>

#include "../../item/Item.h"
#include "../../world/block/Block.h"
#include "../rhi/RhiHandles.h"

class Inventory;
class RhiCommandList;
class RhiDevice;
struct GameResources;

struct FirstPersonHeldItemMotion {
    bool moving = false;
    bool sprinting = false;
    float bobFrequency = 6.0f;
    float bobPhaseOffset = 0.0f;
    float cameraYawDegrees = -90.0f;
    float cameraPitchDegrees = 0.0f;
};

/// Renders the first-person arm / held item / held block as world-space
/// geometry so it receives exactly the same lighting as the world:
/// - Deferred: meshes are written into the GBuffer (renderPreparedToGBuffer)
///   and shaded by the deferred lighting pass.
/// - Forward: meshes are drawn with the vanilla lightmap shading used by
///   forward terrain (renderPreparedForward).
class FirstPersonHeldItemRenderer {
public:
    struct Config {
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

    void init(GameResources& resources, RhiDevice& rhiDevice);
    void shutdown();
    void loadConfig();
    void saveConfig() const;

    [[nodiscard]] const Config& getConfig() const;
    void setConfig(const Config& config);
    void resetConfig();

    void triggerSwing();
    void setContinuousSwing(bool active);
    /// Normalized (sunlight, blockLight) levels sampled from the voxel light
    /// grid at the camera position; consumed as the per-draw light input.
    void setEnvironmentLight(float sunlight, float blockLight);
    void prepareFrameResources(const Inventory& inventory);
    /// Advances animation state and builds the world-space model matrix for
    /// this frame (model_world = inverse(cameraView) * eye-space placement).
    void prepareFrame(const glm::mat4& cameraView, const Inventory& inventory,
                      const FirstPersonHeldItemMotion& motion, float timeSeconds);
    [[nodiscard]] bool hasPreparedDraw() const;

    /// Forward pipeline: vanilla lightmap shading on the scene capture target.
    void renderPreparedForward(RhiCommandList& commandList, const glm::mat4& viewProj, float skyIntensity,
                               float animationTime, int width, int height);
    /// Deferred pipeline: writes the mesh into the GBuffer with real depth and
    /// per-object velocity; shaded afterwards by the deferred lighting pass.
    void renderPreparedToGBuffer(RhiCommandList& commandList, const glm::mat4& viewProj,
                                 const glm::mat4& previousViewProj, float animationTime);

private:
    struct Mesh {
        RhiBufferHandle rhiVertexBuffer;
        RhiDevice* rhiDevice = nullptr;
        uint32_t vertexCount = 0;
        uint32_t materialId = 0;
    };

    Mesh* getOrCreateBlockMesh(BlockID blockId);
    Mesh buildBlockMesh(BlockID blockId) const;
    Mesh* getOrCreateItemMesh(ItemID itemId);
    Mesh buildItemMesh(ItemID itemId) const;
    Mesh buildRightArmMesh() const;
    static void destroyMesh(Mesh& mesh);
    void createRhiTextureResources();
    void destroyRhiTextureResources();
    void createArmRhiResources();
    void destroyArmRhiResources();
    void createItemRhiResources();
    void destroyItemRhiResources();
    void createBlockRhiResources();
    void destroyBlockRhiResources();
    void createArmGBufferResources();
    void destroyArmGBufferResources();
    void createItemGBufferResources();
    void destroyItemGBufferResources();
    void createBlockGBufferResources();
    void destroyBlockGBufferResources();

    GameResources* m_resources = nullptr;
    RhiDevice* m_rhiDevice = nullptr;
    RhiTextureViewHandle m_steveTextureView;
    RhiTextureViewHandle m_itemAtlasView;
    RhiTextureViewHandle m_blockTextureArrayView;
    RhiTextureViewHandle m_lightmapDayView;
    RhiTextureViewHandle m_lightmapNightView;
    RhiTextureViewHandle m_grassColormapView;
    RhiTextureViewHandle m_foliageColormapView;
    RhiSamplerHandle m_textureSampler;
    RhiSamplerHandle m_blockTextureSampler;
    RhiSamplerHandle m_armNearestSampler;

    // Forward (vanilla lightmap) pipelines.
    RhiShaderHandle m_armVertexShader;
    RhiShaderHandle m_armFragmentShader;
    RhiBindGroupLayoutHandle m_armBindGroupLayout;
    RhiPipelineLayoutHandle m_armPipelineLayout;
    RhiPipelineHandle m_armPipeline;
    RhiBindGroupHandle m_armBindGroup;
    RhiShaderHandle m_itemVertexShader;
    RhiShaderHandle m_itemFragmentShader;
    RhiBindGroupLayoutHandle m_itemBindGroupLayout;
    RhiPipelineLayoutHandle m_itemPipelineLayout;
    RhiPipelineHandle m_itemPipeline;
    RhiBindGroupHandle m_itemBindGroup;
    RhiShaderHandle m_blockVertexShader;
    RhiShaderHandle m_blockFragmentShader;
    RhiBindGroupLayoutHandle m_blockBindGroupLayout;
    RhiPipelineLayoutHandle m_blockPipelineLayout;
    RhiPipelineHandle m_blockPipeline;
    RhiBindGroupHandle m_blockBindGroup;

    // Deferred GBuffer pipelines (shared world shaders).
    RhiShaderHandle m_armGBufferVertexShader;
    RhiShaderHandle m_armGBufferFragmentShader;
    RhiBindGroupLayoutHandle m_armGBufferBindGroupLayout;
    RhiPipelineLayoutHandle m_armGBufferPipelineLayout;
    RhiPipelineHandle m_armGBufferPipeline;
    RhiBindGroupHandle m_armGBufferBindGroup;
    RhiBufferHandle m_armMaterialIdentityBuffer;
    RhiShaderHandle m_itemGBufferVertexShader;
    RhiShaderHandle m_itemGBufferFragmentShader;
    RhiBindGroupLayoutHandle m_itemGBufferBindGroupLayout;
    RhiPipelineLayoutHandle m_itemGBufferPipelineLayout;
    RhiPipelineHandle m_itemGBufferPipeline;
    RhiBindGroupHandle m_itemGBufferBindGroup;
    RhiShaderHandle m_blockGBufferVertexShader;
    RhiShaderHandle m_blockGBufferFragmentShader;
    RhiBindGroupLayoutHandle m_blockGBufferBindGroupLayout;
    RhiPipelineLayoutHandle m_blockGBufferPipelineLayout;
    RhiPipelineHandle m_blockGBufferPipeline;
    RhiBindGroupHandle m_blockGBufferBindGroup;
    uint32_t m_objectId = 0;
    uint32_t m_armMaterialId = 0;

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
    float m_environmentSunlight = 1.0f;
    float m_environmentBlockLight = 0.0f;
    bool m_initialized = false;

    enum class PreparedDrawKind : uint8_t { None, Arm, Item, Block };
    struct PreparedHeldItemFrame {
        PreparedDrawKind kind = PreparedDrawKind::None;
        glm::mat4 model{1.0f};
        glm::mat4 previousModel{1.0f};
        ItemID itemId = 0;
    };
    PreparedHeldItemFrame m_preparedFrame;
    bool m_hasPreparedHistory = false;
};

#endif // MECRAFT_FIRST_PERSON_HELD_ITEM_RENDERER_H
