#include "FirstPersonHeldItemRenderer.h"

#include "../../Diagnostics.h"
#include "../contracts/SceneIdentityContract.h"
#include "../mesh/BlockMeshBuilder.h"
#include "../mesh/ItemModelMesh.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiShaderSourceLoader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include "../../Paths.h"
#include "../../player/Inventory.h"
#include "../../resource/GameResources.h"
#include "../../world/chunk/SubChunk.h"

namespace {
constexpr std::array<int, 6> kFaceIndices = {{0, 1, 2, 0, 2, 3}};

constexpr float kPi = 3.14159265358979323846f;

struct FaceUvRect {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
};

/// Forward (vanilla lightmap) push constants for the held_*_rhi shaders.
struct ForwardPushConstants {
    glm::mat4 viewProj;
    glm::mat4 model;
    glm::vec4 lightingParams; // (skyLight, blockLight, skyIntensity, animationTime)
};

/// GBuffer push constants — byte-identical to the shared world shaders
/// (entity_gbuffer_rhi / item_drop_gbuffer_rhi / falling_block_gbuffer_rhi).
struct ArmGBufferPushConstants {
    glm::mat4 modelViewProj;
    glm::mat4 previousModelViewProj;
    glm::mat4 model;
    glm::vec2 light;
    float hurtFlash;
    uint32_t objectId;
};

struct ItemGBufferPushConstants {
    glm::mat4 modelViewProj;
    glm::mat4 previousModelViewProj;
    glm::mat4 model;
    glm::vec2 light;
    glm::uvec2 identity;
};

struct BlockGBufferPushConstants {
    glm::mat4 modelViewProj;
    glm::mat4 previousModelViewProj;
    glm::mat4 model;
    glm::vec2 light;
    float animationTime;
    uint32_t objectId;
};

static_assert(sizeof(ArmGBufferPushConstants) == 208u);
static_assert(sizeof(ItemGBufferPushConstants) == 208u);
static_assert(sizeof(BlockGBufferPushConstants) == 208u);

/// Vertex attributes consumed by held_block_rhi.vert (forward vanilla path):
/// no face normal and no per-vertex light — light levels arrive per draw.
void setHeldBlockForwardVertexInputLayout(RhiGraphicsPipelineDesc& pipelineDesc) {
    pipelineDesc.vertexInput.bindings.push_back(
        {0u, static_cast<uint32_t>(sizeof(BlockVertex)), RhiVertexInputRate::Vertex});
    pipelineDesc.vertexInput.attributes = {
        {0u, 0u, RhiVertexFormat::Float3, static_cast<uint32_t>(offsetof(BlockVertex, x))},
        {1u, 0u, RhiVertexFormat::Float2, static_cast<uint32_t>(offsetof(BlockVertex, u))},
        {5u, 0u, RhiVertexFormat::Uint8, static_cast<uint32_t>(offsetof(BlockVertex, ao))},
        {6u, 0u, RhiVertexFormat::Uint16, static_cast<uint32_t>(offsetof(BlockVertex, layer))},
        {7u, 0u, RhiVertexFormat::Uint16, static_cast<uint32_t>(offsetof(BlockVertex, animationFrameCount))},
        {8u, 0u, RhiVertexFormat::Uint8, static_cast<uint32_t>(offsetof(BlockVertex, animationFps))},
        {9u, 0u, RhiVertexFormat::Uint8, static_cast<uint32_t>(offsetof(BlockVertex, animationAndFlags))},
        {10u, 0u, RhiVertexFormat::Uint16, static_cast<uint32_t>(offsetof(BlockVertex, tintPacked))}};
}

bool isTorchShape(const BlockDef& def) {
    return def.renderShapeName == "torch";
}

bool prefersBlockMeshForItem(const BlockID renderBlock) {
    if (renderBlock == 0) {
        return false;
    }
    const BlockDef& def = BlockRegistry::get(renderBlock);
    return isTorchShape(def) || def.renderShapeName == "model";
}

FaceUvRect pixelRectToUv(const float x0, const float y0, const float x1, const float y1) {
    constexpr float skinW = 64.0f;
    constexpr float skinH = 64.0f;
    return {x0 / skinW, 1.0f - y1 / skinH, x1 / skinW, 1.0f - y0 / skinH};
}

void addSteveQuad(std::vector<FirstPersonHeldItemRenderer::SteveVertex>& vertices, const glm::vec3& a,
                  const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const FaceUvRect& uvRect,
                  const glm::vec3& normal) {
    const std::array<glm::vec2, 4> uv = {
        {{uvRect.u0, uvRect.v0}, {uvRect.u1, uvRect.v0}, {uvRect.u1, uvRect.v1}, {uvRect.u0, uvRect.v1}}};
    const std::array<glm::vec3, 4> pos = {{a, b, c, d}};
    for (const int idx : kFaceIndices) {
        const glm::vec3& p = pos[static_cast<size_t>(idx)];
        const glm::vec2& t = uv[static_cast<size_t>(idx)];
        vertices.push_back({p.x, p.y, p.z, t.x, t.y, normal.x, normal.y, normal.z});
    }
}

} // namespace

void FirstPersonHeldItemRenderer::init(GameResources& resources, RhiDevice& rhiDevice) {
    if (m_initialized) {
        shutdown();
    }
    m_resources = &resources;
    m_rhiDevice = &rhiDevice;
    const std::optional<renderer::contracts::StableObjectId> objectId =
        renderer::contracts::allocateStableSceneId<renderer::contracts::StableObjectIdTag>();
    const std::optional<renderer::contracts::StableMaterialId> armMaterialId =
        renderer::contracts::allocateStableSceneId<renderer::contracts::StableMaterialIdTag>();
    if (!objectId.has_value() || !armMaterialId.has_value()) {
        std::abort();
    }
    m_objectId = objectId->value;
    m_armMaterialId = armMaterialId->value;
    createRhiTextureResources();
    createArmRhiResources();
    createItemRhiResources();
    createBlockRhiResources();
    createArmGBufferResources();
    createItemGBufferResources();
    createBlockGBufferResources();
    m_rightArmMesh = buildRightArmMesh();
    if (!m_rightArmMesh.rhiVertexBuffer.isValid() || m_rightArmMesh.vertexCount == 0u) {
        std::abort();
    }
    loadConfig();
    m_initialized = true;
}

void FirstPersonHeldItemRenderer::shutdown() {
    if (!m_initialized && m_resources == nullptr && !m_rightArmMesh.rhiVertexBuffer.isValid() &&
        m_blockMeshes.empty() && m_itemMeshes.empty()) {
        return;
    }
    destroyMesh(m_rightArmMesh);
    for (auto& pair : m_blockMeshes) {
        destroyMesh(pair.second);
    }
    m_blockMeshes.clear();
    for (auto& pair : m_itemMeshes) {
        destroyMesh(pair.second);
    }
    m_itemMeshes.clear();
    destroyBlockGBufferResources();
    destroyItemGBufferResources();
    destroyArmGBufferResources();
    destroyBlockRhiResources();
    destroyItemRhiResources();
    destroyArmRhiResources();
    destroyRhiTextureResources();
    m_resources = nullptr;
    m_rhiDevice = nullptr;
    m_hasPrevSample = false;
    m_prevTimeSeconds = 0.0f;
    m_visibleItemId = 0;
    m_lastSelectedItemId = 0;
    m_equipProgress = 1.0f;
    m_walkBobBlend = 0.0f;
    m_hasLagSample = false;
    m_lagYawDegrees = -90.0f;
    m_lagPitchDegrees = 0.0f;
    m_swingActive = false;
    m_continuousSwing = false;
    m_swingElapsed = 0.0f;
    m_preparedFrame = {};
    m_hasPreparedHistory = false;
    m_objectId = 0u;
    m_armMaterialId = 0u;
    m_initialized = false;
}

namespace {
float readJsonFloat(const nlohmann::json& json, const char* key, const float defaultValue) {
    if (!json.contains(key) || !json[key].is_number()) {
        return defaultValue;
    }
    return json[key].get<float>();
}

float wrapDegrees(float degrees) {
    while (degrees > 180.0f) {
        degrees -= 360.0f;
    }
    while (degrees < -180.0f) {
        degrees += 360.0f;
    }
    return degrees;
}
} // namespace

void FirstPersonHeldItemRenderer::loadConfig() {
    std::ifstream file(FIRST_PERSON_HELD_ITEM_CONFIG_PATH);
    if (!file.is_open()) {
        return;
    }

    nlohmann::json json = nlohmann::json::parse(file, nullptr, false);
    if (json.is_discarded()) {
#ifdef MECRAFT_DEBUG
        MECRAFT_LOG_STREAM(std::cerr << "[FirstPersonHeldItemRenderer] Failed to parse config: "
                                     << FIRST_PERSON_HELD_ITEM_CONFIG_PATH << std::endl);
#endif
        return;
    }

    Config config = m_config;
    config.armPosX = readJsonFloat(json, "armPosX", config.armPosX);
    config.armPosY = readJsonFloat(json, "armPosY", config.armPosY);
    config.armPosZ = readJsonFloat(json, "armPosZ", config.armPosZ);
    config.armPitchDegrees = readJsonFloat(json, "armPitchDegrees", config.armPitchDegrees);
    config.armYawDegrees = readJsonFloat(json, "armYawDegrees", config.armYawDegrees);
    config.armRollDegrees = readJsonFloat(json, "armRollDegrees", config.armRollDegrees);
    config.armScale = readJsonFloat(json, "armScale", config.armScale);
    config.itemPosX = readJsonFloat(json, "itemPosX", config.itemPosX);
    config.itemPosY = readJsonFloat(json, "itemPosY", config.itemPosY);
    config.itemPosZ = readJsonFloat(json, "itemPosZ", config.itemPosZ);
    config.itemPitchDegrees = readJsonFloat(json, "itemPitchDegrees", config.itemPitchDegrees);
    config.itemYawDegrees = readJsonFloat(json, "itemYawDegrees", config.itemYawDegrees);
    config.itemRollDegrees = readJsonFloat(json, "itemRollDegrees", config.itemRollDegrees);
    config.itemScale = readJsonFloat(json, "itemScale", config.itemScale);
    config.blockPitchDegrees = readJsonFloat(json, "blockPitchDegrees", config.itemPitchDegrees);
    config.blockYawDegrees = readJsonFloat(json, "blockYawDegrees", config.itemYawDegrees);
    config.blockScale = readJsonFloat(json, "blockScale", config.blockScale);
    config.equipDrop = readJsonFloat(json, "equipDrop", config.equipDrop);
    config.equipSpeed = readJsonFloat(json, "equipSpeed", config.equipSpeed);
    config.bobOffsetX = readJsonFloat(json, "bobOffsetX", config.bobOffsetX);
    config.bobOffsetY = readJsonFloat(json, "bobOffsetY", config.bobOffsetY);
    config.bobRollDegrees = readJsonFloat(json, "bobRollDegrees", config.bobRollDegrees);
    config.viewLagFollowSpeed = readJsonFloat(json, "viewLagFollowSpeed", config.viewLagFollowSpeed);
    config.viewLagMaxDegrees = readJsonFloat(json, "viewLagMaxDegrees", config.viewLagMaxDegrees);
    config.viewLagOffsetX = readJsonFloat(json, "viewLagOffsetX", config.viewLagOffsetX);
    config.viewLagOffsetY = readJsonFloat(json, "viewLagOffsetY", config.viewLagOffsetY);
    config.viewLagYawDegrees = readJsonFloat(json, "viewLagYawDegrees", config.viewLagYawDegrees);
    config.viewLagPitchDegrees = readJsonFloat(json, "viewLagPitchDegrees", config.viewLagPitchDegrees);
    config.swingDurationSeconds = readJsonFloat(json, "swingDurationSeconds", config.swingDurationSeconds);
    config.armSwingX = readJsonFloat(json, "armSwingX", config.armSwingX);
    config.armSwingY = readJsonFloat(json, "armSwingY", config.armSwingY);
    config.armSwingZ = readJsonFloat(json, "armSwingZ", config.armSwingZ);
    config.armSwingPitchDegrees = readJsonFloat(json, "armSwingPitchDegrees", config.armSwingPitchDegrees);
    config.armSwingYawDegrees = readJsonFloat(json, "armSwingYawDegrees", config.armSwingYawDegrees);
    config.armSwingRollDegrees = readJsonFloat(json, "armSwingRollDegrees", config.armSwingRollDegrees);
    config.itemSwingX = readJsonFloat(json, "itemSwingX", config.itemSwingX);
    config.itemSwingY = readJsonFloat(json, "itemSwingY", config.itemSwingY);
    config.itemSwingZ = readJsonFloat(json, "itemSwingZ", config.itemSwingZ);
    config.itemSwingPitchDegrees = readJsonFloat(json, "itemSwingPitchDegrees", config.itemSwingPitchDegrees);
    config.itemSwingYawDegrees = readJsonFloat(json, "itemSwingYawDegrees", config.itemSwingYawDegrees);
    config.itemSwingRollDegrees = readJsonFloat(json, "itemSwingRollDegrees", config.itemSwingRollDegrees);
    setConfig(config);
}

void FirstPersonHeldItemRenderer::saveConfig() const {
    if (m_resources == nullptr) {
        return;
    }

    nlohmann::json json;
    json["armPosX"] = m_config.armPosX;
    json["armPosY"] = m_config.armPosY;
    json["armPosZ"] = m_config.armPosZ;
    json["armPitchDegrees"] = m_config.armPitchDegrees;
    json["armYawDegrees"] = m_config.armYawDegrees;
    json["armRollDegrees"] = m_config.armRollDegrees;
    json["armScale"] = m_config.armScale;
    json["itemPosX"] = m_config.itemPosX;
    json["itemPosY"] = m_config.itemPosY;
    json["itemPosZ"] = m_config.itemPosZ;
    json["itemPitchDegrees"] = m_config.itemPitchDegrees;
    json["itemYawDegrees"] = m_config.itemYawDegrees;
    json["itemRollDegrees"] = m_config.itemRollDegrees;
    json["itemScale"] = m_config.itemScale;
    json["blockPitchDegrees"] = m_config.blockPitchDegrees;
    json["blockYawDegrees"] = m_config.blockYawDegrees;
    json["blockScale"] = m_config.blockScale;
    json["equipDrop"] = m_config.equipDrop;
    json["equipSpeed"] = m_config.equipSpeed;
    json["bobOffsetX"] = m_config.bobOffsetX;
    json["bobOffsetY"] = m_config.bobOffsetY;
    json["bobRollDegrees"] = m_config.bobRollDegrees;
    json["viewLagFollowSpeed"] = m_config.viewLagFollowSpeed;
    json["viewLagMaxDegrees"] = m_config.viewLagMaxDegrees;
    json["viewLagOffsetX"] = m_config.viewLagOffsetX;
    json["viewLagOffsetY"] = m_config.viewLagOffsetY;
    json["viewLagYawDegrees"] = m_config.viewLagYawDegrees;
    json["viewLagPitchDegrees"] = m_config.viewLagPitchDegrees;
    json["swingDurationSeconds"] = m_config.swingDurationSeconds;
    json["armSwingX"] = m_config.armSwingX;
    json["armSwingY"] = m_config.armSwingY;
    json["armSwingZ"] = m_config.armSwingZ;
    json["armSwingPitchDegrees"] = m_config.armSwingPitchDegrees;
    json["armSwingYawDegrees"] = m_config.armSwingYawDegrees;
    json["armSwingRollDegrees"] = m_config.armSwingRollDegrees;
    json["itemSwingX"] = m_config.itemSwingX;
    json["itemSwingY"] = m_config.itemSwingY;
    json["itemSwingZ"] = m_config.itemSwingZ;
    json["itemSwingPitchDegrees"] = m_config.itemSwingPitchDegrees;
    json["itemSwingYawDegrees"] = m_config.itemSwingYawDegrees;
    json["itemSwingRollDegrees"] = m_config.itemSwingRollDegrees;

    std::ofstream file(FIRST_PERSON_HELD_ITEM_CONFIG_PATH);
    if (file.is_open()) {
        file << json.dump(4) << '\n';
    }
}

const FirstPersonHeldItemRenderer::Config& FirstPersonHeldItemRenderer::getConfig() const {
    return m_config;
}

void FirstPersonHeldItemRenderer::setConfig(const Config& config) {
    m_config = config;
    m_config.armScale = std::clamp(m_config.armScale, 0.05f, 5.0f);
    m_config.itemScale = std::clamp(m_config.itemScale, 0.05f, 5.0f);
    m_config.blockScale = std::clamp(m_config.blockScale, 0.05f, 5.0f);
    m_config.equipDrop = std::clamp(m_config.equipDrop, 0.0f, 3.0f);
    m_config.equipSpeed = std::clamp(m_config.equipSpeed, 0.1f, 40.0f);
    m_config.viewLagFollowSpeed = std::clamp(m_config.viewLagFollowSpeed, 0.1f, 80.0f);
    m_config.viewLagMaxDegrees = std::clamp(m_config.viewLagMaxDegrees, 0.0f, 90.0f);
    m_config.swingDurationSeconds = std::clamp(m_config.swingDurationSeconds, 0.05f, 2.0f);
}

void FirstPersonHeldItemRenderer::resetConfig() {
    setConfig(Config{});
}

void FirstPersonHeldItemRenderer::triggerSwing() {
    m_swingActive = true;
    m_swingElapsed = 0.0f;
}

void FirstPersonHeldItemRenderer::setContinuousSwing(const bool active) {
    m_continuousSwing = active;
    if (active) {
        m_swingActive = true;
        if (m_swingElapsed >= m_config.swingDurationSeconds) {
            m_swingElapsed = 0.0f;
        }
        return;
    }

    if (!m_swingActive) {
        m_swingElapsed = 0.0f;
    }
}

void FirstPersonHeldItemRenderer::setEnvironmentLight(const float sunlight, const float blockLight) {
    m_environmentSunlight = std::clamp(sunlight, 0.0f, 1.0f);
    m_environmentBlockLight = std::clamp(blockLight, 0.0f, 1.0f);
}

void FirstPersonHeldItemRenderer::prepareFrameResources(const Inventory& inventory) {
    if (!m_initialized || m_resources == nullptr) {
        return;
    }
    const ItemID selectedItem = inventory.getSelectedItem();
    if (selectedItem == 0) {
        return;
    }
    const ItemDef& itemDef = ItemRegistry::get(selectedItem);
    const int itemTileIndex = m_resources->uiTextures.itemTextureIndex(itemDef.iconTextureName);
    const BlockID renderBlock = ItemRegistry::toRenderBlock(selectedItem);
    if (!prefersBlockMeshForItem(renderBlock) && itemTileIndex >= 0) {
        getOrCreateItemMesh(selectedItem);
    } else if (renderBlock != 0) {
        getOrCreateBlockMesh(renderBlock);
    }
}

void FirstPersonHeldItemRenderer::createRhiTextureResources() {
    const RhiTextureHandle textures[] = {
        m_resources->texture2D.getGuiHandle("steve"), m_resources->uiTextures.itemTextureAtlas().texture,
        m_resources->blockTextures.textureArray().texture,    m_resources->environmentTextures.getLightmapDay(),
        m_resources->environmentTextures.getLightmapNight(),           m_resources->environmentTextures.getGrassColormap(),
        m_resources->environmentTextures.getFoliageColormap()};
    RhiTextureViewHandle* views[] = {&m_steveTextureView,   &m_itemAtlasView,     &m_blockTextureArrayView,
                                     &m_lightmapDayView,    &m_lightmapNightView, &m_grassColormapView,
                                     &m_foliageColormapView};
    for (uint32_t index = 0u; index < 7u; ++index) {
        if (!textures[index].isValid()) {
            std::abort();
        }
        RhiTextureViewDesc viewDesc;
        viewDesc.texture = textures[index];
        viewDesc.viewType = index == 2u ? RhiTextureViewType::Texture2DArray : RhiTextureViewType::Texture2D;
        if (viewDesc.viewType == RhiTextureViewType::Texture2DArray) {
            viewDesc.mipCount = kRhiRemainingMipLevels;
            viewDesc.layerCount = kRhiRemainingArrayLayers;
        }
        *views[index] = m_rhiDevice->createTextureView(viewDesc);
        if (!views[index]->isValid()) {
            std::abort();
        }
    }
    RhiSamplerDesc samplerDesc;
    samplerDesc.addressU = RhiAddressMode::ClampToEdge;
    samplerDesc.addressV = RhiAddressMode::ClampToEdge;
    m_textureSampler = m_rhiDevice->createSampler(samplerDesc);
    samplerDesc.addressU = RhiAddressMode::Repeat;
    samplerDesc.addressV = RhiAddressMode::Repeat;
    samplerDesc.addressW = RhiAddressMode::Repeat;
    m_blockTextureSampler = m_rhiDevice->createSampler(samplerDesc);
    // Pixel-art skin needs nearest filtering, matching the humanoid renderer.
    samplerDesc.minFilter = RhiFilter::Nearest;
    samplerDesc.magFilter = RhiFilter::Nearest;
    samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    samplerDesc.addressU = RhiAddressMode::ClampToEdge;
    samplerDesc.addressV = RhiAddressMode::ClampToEdge;
    samplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_armNearestSampler = m_rhiDevice->createSampler(samplerDesc);
    if (!m_textureSampler.isValid() || !m_blockTextureSampler.isValid() || !m_armNearestSampler.isValid()) {
        std::abort();
    }
}

void FirstPersonHeldItemRenderer::destroyRhiTextureResources() {
    if (m_armNearestSampler.isValid())
        m_rhiDevice->destroySampler(m_armNearestSampler);
    if (m_blockTextureSampler.isValid())
        m_rhiDevice->destroySampler(m_blockTextureSampler);
    if (m_textureSampler.isValid())
        m_rhiDevice->destroySampler(m_textureSampler);
    if (m_foliageColormapView.isValid())
        m_rhiDevice->destroyTextureView(m_foliageColormapView);
    if (m_grassColormapView.isValid())
        m_rhiDevice->destroyTextureView(m_grassColormapView);
    if (m_lightmapNightView.isValid())
        m_rhiDevice->destroyTextureView(m_lightmapNightView);
    if (m_lightmapDayView.isValid())
        m_rhiDevice->destroyTextureView(m_lightmapDayView);
    if (m_blockTextureArrayView.isValid())
        m_rhiDevice->destroyTextureView(m_blockTextureArrayView);
    if (m_itemAtlasView.isValid())
        m_rhiDevice->destroyTextureView(m_itemAtlasView);
    if (m_steveTextureView.isValid())
        m_rhiDevice->destroyTextureView(m_steveTextureView);
    m_armNearestSampler = {};
    m_blockTextureSampler = {};
    m_textureSampler = {};
    m_foliageColormapView = {};
    m_grassColormapView = {};
    m_lightmapNightView = {};
    m_lightmapDayView = {};
    m_blockTextureArrayView = {};
    m_itemAtlasView = {};
    m_steveTextureView = {};
}

namespace {
/// Shared GBuffer attachment formats (identical for every world object pass).
constexpr std::array<RhiTextureFormat, 8> kGBufferColorFormats = {
    RhiTextureFormat::Rgba8Unorm, RhiTextureFormat::Rgb10A2Unorm, RhiTextureFormat::Rg8Unorm,
    RhiTextureFormat::Rgba8Unorm, RhiTextureFormat::Rgba8Unorm,   RhiTextureFormat::Rgba8Unorm,
    RhiTextureFormat::Rg32Uint,   RhiTextureFormat::Rg16Float};
} // namespace

void FirstPersonHeldItemRenderer::createArmRhiResources() {
    const auto vertexSource = renderer::rhi::loadShaderSource("assets/shaders/held_arm_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource("assets/shaders/held_arm_rhi.frag");
    if (!vertexSource || !fragmentSource) {
        std::abort();
    }
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "FirstPerson.Arm.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str();
    shaderDesc.sourceSize = vertexSource->size();
    m_armVertexShader = m_rhiDevice->createShader(shaderDesc);
    shaderDesc.debugName = "FirstPerson.Arm.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = fragmentSource->c_str();
    shaderDesc.sourceSize = fragmentSource->size();
    m_armFragmentShader = m_rhiDevice->createShader(shaderDesc);

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "FirstPerson.Arm.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 3u; ++binding) {
        bindGroupLayoutDesc.entries.push_back(
            {binding, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u});
    }
    m_armBindGroupLayout = m_rhiDevice->createBindGroupLayout(bindGroupLayoutDesc);

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "FirstPerson.Arm.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_armBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(ForwardPushConstants);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    m_armPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "FirstPerson.Arm.Pipeline";
    pipelineDesc.vertexShader = m_armVertexShader;
    pipelineDesc.fragmentShader = m_armFragmentShader;
    pipelineDesc.layout = m_armPipelineLayout;
    pipelineDesc.vertexInput.bindings = {{0u, sizeof(SteveVertex), RhiVertexInputRate::Vertex}};
    pipelineDesc.vertexInput.attributes = {{0u, 0u, RhiVertexFormat::Float3, offsetof(SteveVertex, x)},
                                           {1u, 0u, RhiVertexFormat::Float2, offsetof(SteveVertex, u)}};
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::Always;
    pipelineDesc.colorFormats = {RhiTextureFormat::Rgba16Float};
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    RhiBlendAttachmentState blend;
    blend.blendEnabled = true;
    blend.srcColor = RhiBlendFactor::SrcAlpha;
    blend.dstColor = RhiBlendFactor::OneMinusSrcAlpha;
    blend.srcAlpha = RhiBlendFactor::One;
    blend.dstAlpha = RhiBlendFactor::OneMinusSrcAlpha;
    pipelineDesc.blend.attachments.push_back(blend);
    m_armPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_armBindGroupLayout;
    const std::array<std::pair<RhiTextureViewHandle, RhiSamplerHandle>, 3> armTextures = {
        std::make_pair(m_steveTextureView, m_armNearestSampler),
        std::make_pair(m_lightmapDayView, m_textureSampler),
        std::make_pair(m_lightmapNightView, m_textureSampler)};
    for (uint32_t binding = 0u; binding < armTextures.size(); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler = {armTextures[binding].first, armTextures[binding].second};
        bindGroupDesc.entries.push_back(entry);
    }
    m_armBindGroup = m_rhiDevice->createBindGroup(bindGroupDesc);
    if (!m_armVertexShader.isValid() || !m_armFragmentShader.isValid() || !m_armBindGroupLayout.isValid() ||
        !m_armPipelineLayout.isValid() || !m_armPipeline.isValid() || !m_armBindGroup.isValid()) {
        std::abort();
    }
}

void FirstPersonHeldItemRenderer::destroyArmRhiResources() {
    if (m_armBindGroup.isValid())
        m_rhiDevice->destroyBindGroup(m_armBindGroup);
    if (m_armPipeline.isValid())
        m_rhiDevice->destroyPipeline(m_armPipeline);
    if (m_armPipelineLayout.isValid())
        m_rhiDevice->destroyPipelineLayout(m_armPipelineLayout);
    if (m_armBindGroupLayout.isValid())
        m_rhiDevice->destroyBindGroupLayout(m_armBindGroupLayout);
    if (m_armFragmentShader.isValid())
        m_rhiDevice->destroyShader(m_armFragmentShader);
    if (m_armVertexShader.isValid())
        m_rhiDevice->destroyShader(m_armVertexShader);
    m_armBindGroup = {};
    m_armPipeline = {};
    m_armPipelineLayout = {};
    m_armBindGroupLayout = {};
    m_armFragmentShader = {};
    m_armVertexShader = {};
}

void FirstPersonHeldItemRenderer::createItemRhiResources() {
    const auto vertexSource = renderer::rhi::loadShaderSource("assets/shaders/held_item_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource("assets/shaders/held_item_rhi.frag");
    if (!vertexSource || !fragmentSource)
        std::abort();
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "FirstPerson.Item.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str();
    shaderDesc.sourceSize = vertexSource->size();
    m_itemVertexShader = m_rhiDevice->createShader(shaderDesc);
    shaderDesc.debugName = "FirstPerson.Item.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = fragmentSource->c_str();
    shaderDesc.sourceSize = fragmentSource->size();
    m_itemFragmentShader = m_rhiDevice->createShader(shaderDesc);
    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "FirstPerson.Item.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 3u; ++binding) {
        bindGroupLayoutDesc.entries.push_back(
            {binding, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u});
    }
    m_itemBindGroupLayout = m_rhiDevice->createBindGroupLayout(bindGroupLayoutDesc);
    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "FirstPerson.Item.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_itemBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(ForwardPushConstants);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    m_itemPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "FirstPerson.Item.Pipeline";
    pipelineDesc.vertexShader = m_itemVertexShader;
    pipelineDesc.fragmentShader = m_itemFragmentShader;
    pipelineDesc.layout = m_itemPipelineLayout;
    pipelineDesc.vertexInput.bindings = {{0u, sizeof(ItemModelVertex), RhiVertexInputRate::Vertex}};
    pipelineDesc.vertexInput.attributes = {{0u, 0u, RhiVertexFormat::Float3, offsetof(ItemModelVertex, x)},
                                           {1u, 0u, RhiVertexFormat::Float2, offsetof(ItemModelVertex, u)},
                                           {2u, 0u, RhiVertexFormat::Float, offsetof(ItemModelVertex, shade)}};
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::Always;
    pipelineDesc.colorFormats = {RhiTextureFormat::Rgba16Float};
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    RhiBlendAttachmentState blend;
    blend.blendEnabled = true;
    blend.srcColor = RhiBlendFactor::SrcAlpha;
    blend.dstColor = RhiBlendFactor::OneMinusSrcAlpha;
    blend.srcAlpha = RhiBlendFactor::One;
    blend.dstAlpha = RhiBlendFactor::OneMinusSrcAlpha;
    pipelineDesc.blend.attachments.push_back(blend);
    m_itemPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_itemBindGroupLayout;
    const std::array<std::pair<RhiTextureViewHandle, RhiSamplerHandle>, 3> itemTextures = {
        std::make_pair(m_itemAtlasView, m_textureSampler),
        std::make_pair(m_lightmapDayView, m_textureSampler),
        std::make_pair(m_lightmapNightView, m_textureSampler)};
    for (uint32_t binding = 0u; binding < itemTextures.size(); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler = {itemTextures[binding].first, itemTextures[binding].second};
        bindGroupDesc.entries.push_back(entry);
    }
    m_itemBindGroup = m_rhiDevice->createBindGroup(bindGroupDesc);
    if (!m_itemVertexShader.isValid() || !m_itemFragmentShader.isValid() || !m_itemBindGroupLayout.isValid() ||
        !m_itemPipelineLayout.isValid() || !m_itemPipeline.isValid() || !m_itemBindGroup.isValid())
        std::abort();
}

void FirstPersonHeldItemRenderer::destroyItemRhiResources() {
    if (m_itemBindGroup.isValid())
        m_rhiDevice->destroyBindGroup(m_itemBindGroup);
    if (m_itemPipeline.isValid())
        m_rhiDevice->destroyPipeline(m_itemPipeline);
    if (m_itemPipelineLayout.isValid())
        m_rhiDevice->destroyPipelineLayout(m_itemPipelineLayout);
    if (m_itemBindGroupLayout.isValid())
        m_rhiDevice->destroyBindGroupLayout(m_itemBindGroupLayout);
    if (m_itemFragmentShader.isValid())
        m_rhiDevice->destroyShader(m_itemFragmentShader);
    if (m_itemVertexShader.isValid())
        m_rhiDevice->destroyShader(m_itemVertexShader);
    m_itemBindGroup = {};
    m_itemPipeline = {};
    m_itemPipelineLayout = {};
    m_itemBindGroupLayout = {};
    m_itemFragmentShader = {};
    m_itemVertexShader = {};
}

void FirstPersonHeldItemRenderer::createBlockRhiResources() {
    const auto vertexSource = renderer::rhi::loadShaderSource("assets/shaders/held_block_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource("assets/shaders/held_block_rhi.frag");
    if (!vertexSource || !fragmentSource)
        std::abort();
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "FirstPerson.Block.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str();
    shaderDesc.sourceSize = vertexSource->size();
    m_blockVertexShader = m_rhiDevice->createShader(shaderDesc);
    shaderDesc.debugName = "FirstPerson.Block.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = fragmentSource->c_str();
    shaderDesc.sourceSize = fragmentSource->size();
    m_blockFragmentShader = m_rhiDevice->createShader(shaderDesc);
    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "FirstPerson.Block.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 5u; ++binding) {
        bindGroupLayoutDesc.entries.push_back(
            {binding, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u});
    }
    m_blockBindGroupLayout = m_rhiDevice->createBindGroupLayout(bindGroupLayoutDesc);
    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "FirstPerson.Block.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_blockBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(ForwardPushConstants);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    m_blockPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "FirstPerson.Block.Pipeline";
    pipelineDesc.vertexShader = m_blockVertexShader;
    pipelineDesc.fragmentShader = m_blockFragmentShader;
    pipelineDesc.layout = m_blockPipelineLayout;
    setHeldBlockForwardVertexInputLayout(pipelineDesc);
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::Always;
    pipelineDesc.colorFormats = {RhiTextureFormat::Rgba16Float};
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    RhiBlendAttachmentState blend;
    blend.blendEnabled = true;
    blend.srcColor = RhiBlendFactor::SrcAlpha;
    blend.dstColor = RhiBlendFactor::OneMinusSrcAlpha;
    blend.srcAlpha = RhiBlendFactor::One;
    blend.dstAlpha = RhiBlendFactor::OneMinusSrcAlpha;
    pipelineDesc.blend.attachments.push_back(blend);
    m_blockPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_blockBindGroupLayout;
    const std::array<std::pair<RhiTextureViewHandle, RhiSamplerHandle>, 5> blockTextures = {
        std::make_pair(m_blockTextureArrayView, m_blockTextureSampler),
        std::make_pair(m_grassColormapView, m_textureSampler),
        std::make_pair(m_foliageColormapView, m_textureSampler),
        std::make_pair(m_lightmapDayView, m_textureSampler),
        std::make_pair(m_lightmapNightView, m_textureSampler)};
    for (uint32_t binding = 0u; binding < blockTextures.size(); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler = {blockTextures[binding].first, blockTextures[binding].second};
        bindGroupDesc.entries.push_back(entry);
    }
    m_blockBindGroup = m_rhiDevice->createBindGroup(bindGroupDesc);
    if (!m_blockVertexShader.isValid() || !m_blockFragmentShader.isValid() || !m_blockBindGroupLayout.isValid() ||
        !m_blockPipelineLayout.isValid() || !m_blockPipeline.isValid() || !m_blockBindGroup.isValid())
        std::abort();
}

void FirstPersonHeldItemRenderer::destroyBlockRhiResources() {
    if (m_blockBindGroup.isValid())
        m_rhiDevice->destroyBindGroup(m_blockBindGroup);
    if (m_blockPipeline.isValid())
        m_rhiDevice->destroyPipeline(m_blockPipeline);
    if (m_blockPipelineLayout.isValid())
        m_rhiDevice->destroyPipelineLayout(m_blockPipelineLayout);
    if (m_blockBindGroupLayout.isValid())
        m_rhiDevice->destroyBindGroupLayout(m_blockBindGroupLayout);
    if (m_blockFragmentShader.isValid())
        m_rhiDevice->destroyShader(m_blockFragmentShader);
    if (m_blockVertexShader.isValid())
        m_rhiDevice->destroyShader(m_blockVertexShader);
    m_blockBindGroup = {};
    m_blockPipeline = {};
    m_blockPipelineLayout = {};
    m_blockBindGroupLayout = {};
    m_blockFragmentShader = {};
    m_blockVertexShader = {};
}

void FirstPersonHeldItemRenderer::createArmGBufferResources() {
    const auto vertexSource = renderer::rhi::loadShaderSource("assets/shaders/entity_gbuffer_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource("assets/shaders/entity_gbuffer_rhi.frag");
    if (!vertexSource || !fragmentSource) {
        std::abort();
    }
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "FirstPerson.ArmGBuffer.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str();
    shaderDesc.sourceSize = vertexSource->size();
    m_armGBufferVertexShader = m_rhiDevice->createShader(shaderDesc);
    shaderDesc.debugName = "FirstPerson.ArmGBuffer.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = fragmentSource->c_str();
    shaderDesc.sourceSize = fragmentSource->size();
    m_armGBufferFragmentShader = m_rhiDevice->createShader(shaderDesc);

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "FirstPerson.ArmGBuffer.BindGroupLayout";
    bindGroupLayoutDesc.entries.push_back(
        {0u, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u});
    bindGroupLayoutDesc.entries.push_back({1u, RhiBindingType::UniformBuffer, rhiFlag(RhiShaderStage::Fragment), 1u});
    m_armGBufferBindGroupLayout = m_rhiDevice->createBindGroupLayout(bindGroupLayoutDesc);

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "FirstPerson.ArmGBuffer.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_armGBufferBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(ArmGBufferPushConstants);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    m_armGBufferPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "FirstPerson.ArmGBuffer.Pipeline";
    pipelineDesc.vertexShader = m_armGBufferVertexShader;
    pipelineDesc.fragmentShader = m_armGBufferFragmentShader;
    pipelineDesc.layout = m_armGBufferPipelineLayout;
    pipelineDesc.vertexInput.bindings = {{0u, sizeof(SteveVertex), RhiVertexInputRate::Vertex}};
    pipelineDesc.vertexInput.attributes = {{0u, 0u, RhiVertexFormat::Float3, offsetof(SteveVertex, x)},
                                           {1u, 0u, RhiVertexFormat::Float2, offsetof(SteveVertex, u)},
                                           {2u, 0u, RhiVertexFormat::Float3, offsetof(SteveVertex, nx)}};
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::Always;
    pipelineDesc.colorFormats.assign(kGBufferColorFormats.begin(), kGBufferColorFormats.end());
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    pipelineDesc.blend.attachments.resize(8u);
    m_armGBufferPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);

    const glm::uvec4 materialIdentity(m_armMaterialId, 0u, 0u, 0u);
    RhiBufferDesc identityBufferDesc;
    identityBufferDesc.debugName = "FirstPerson.ArmMaterialIdentity";
    identityBufferDesc.size = sizeof(materialIdentity);
    identityBufferDesc.usage = rhiFlag(RhiBufferUsage::Uniform) | rhiFlag(RhiBufferUsage::TransferDst);
    identityBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    identityBufferDesc.initialState = RhiResourceState::UniformBuffer;
    identityBufferDesc.memoryCategory = RhiMemoryCategory::Uniform;
    m_armMaterialIdentityBuffer = m_rhiDevice->createBuffer(identityBufferDesc, &materialIdentity,
                                                            sizeof(materialIdentity));

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_armGBufferBindGroupLayout;
    RhiBindGroupEntry textureEntry;
    textureEntry.binding = 0u;
    textureEntry.resource.combinedTextureSampler = {m_steveTextureView, m_armNearestSampler};
    bindGroupDesc.entries.push_back(textureEntry);
    RhiBindGroupEntry identityEntry;
    identityEntry.binding = 1u;
    identityEntry.resource.buffer.buffer = m_armMaterialIdentityBuffer;
    identityEntry.resource.buffer.offset = 0u;
    identityEntry.resource.buffer.range = sizeof(materialIdentity);
    bindGroupDesc.entries.push_back(identityEntry);
    m_armGBufferBindGroup = m_rhiDevice->createBindGroup(bindGroupDesc);
    if (!m_armGBufferVertexShader.isValid() || !m_armGBufferFragmentShader.isValid() ||
        !m_armGBufferBindGroupLayout.isValid() || !m_armGBufferPipelineLayout.isValid() ||
        !m_armGBufferPipeline.isValid() || !m_armMaterialIdentityBuffer.isValid() ||
        !m_armGBufferBindGroup.isValid()) {
        std::abort();
    }
}

void FirstPersonHeldItemRenderer::destroyArmGBufferResources() {
    if (m_armGBufferBindGroup.isValid())
        m_rhiDevice->destroyBindGroup(m_armGBufferBindGroup);
    if (m_armMaterialIdentityBuffer.isValid())
        m_rhiDevice->destroyBuffer(m_armMaterialIdentityBuffer);
    if (m_armGBufferPipeline.isValid())
        m_rhiDevice->destroyPipeline(m_armGBufferPipeline);
    if (m_armGBufferPipelineLayout.isValid())
        m_rhiDevice->destroyPipelineLayout(m_armGBufferPipelineLayout);
    if (m_armGBufferBindGroupLayout.isValid())
        m_rhiDevice->destroyBindGroupLayout(m_armGBufferBindGroupLayout);
    if (m_armGBufferFragmentShader.isValid())
        m_rhiDevice->destroyShader(m_armGBufferFragmentShader);
    if (m_armGBufferVertexShader.isValid())
        m_rhiDevice->destroyShader(m_armGBufferVertexShader);
    m_armGBufferBindGroup = {};
    m_armMaterialIdentityBuffer = {};
    m_armGBufferPipeline = {};
    m_armGBufferPipelineLayout = {};
    m_armGBufferBindGroupLayout = {};
    m_armGBufferFragmentShader = {};
    m_armGBufferVertexShader = {};
}

void FirstPersonHeldItemRenderer::createItemGBufferResources() {
    const auto vertexSource = renderer::rhi::loadShaderSource("assets/shaders/item_drop_gbuffer_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource("assets/shaders/item_drop_gbuffer_rhi.frag");
    if (!vertexSource || !fragmentSource) {
        std::abort();
    }
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "FirstPerson.ItemGBuffer.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str();
    shaderDesc.sourceSize = vertexSource->size();
    m_itemGBufferVertexShader = m_rhiDevice->createShader(shaderDesc);
    shaderDesc.debugName = "FirstPerson.ItemGBuffer.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = fragmentSource->c_str();
    shaderDesc.sourceSize = fragmentSource->size();
    m_itemGBufferFragmentShader = m_rhiDevice->createShader(shaderDesc);

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "FirstPerson.ItemGBuffer.BindGroupLayout";
    bindGroupLayoutDesc.entries.push_back(
        {0u, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u});
    m_itemGBufferBindGroupLayout = m_rhiDevice->createBindGroupLayout(bindGroupLayoutDesc);

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "FirstPerson.ItemGBuffer.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_itemGBufferBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(ItemGBufferPushConstants);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    m_itemGBufferPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "FirstPerson.ItemGBuffer.Pipeline";
    pipelineDesc.vertexShader = m_itemGBufferVertexShader;
    pipelineDesc.fragmentShader = m_itemGBufferFragmentShader;
    pipelineDesc.layout = m_itemGBufferPipelineLayout;
    pipelineDesc.vertexInput.bindings = {{0u, sizeof(ItemModelVertex), RhiVertexInputRate::Vertex}};
    pipelineDesc.vertexInput.attributes = {{0u, 0u, RhiVertexFormat::Float3, offsetof(ItemModelVertex, x)},
                                           {1u, 0u, RhiVertexFormat::Float2, offsetof(ItemModelVertex, u)},
                                           {2u, 0u, RhiVertexFormat::Float, offsetof(ItemModelVertex, shade)},
                                           {3u, 0u, RhiVertexFormat::Float3, offsetof(ItemModelVertex, nx)}};
    // depthCompare is Always so the held item stays on top of world geometry; that disables
    // depth-based self-occlusion, so back faces must be culled or they overwrite front faces.
    pipelineDesc.raster.cullMode = RhiCullMode::Back;
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::Always;
    pipelineDesc.colorFormats.assign(kGBufferColorFormats.begin(), kGBufferColorFormats.end());
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    pipelineDesc.blend.attachments.resize(8u);
    m_itemGBufferPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_itemGBufferBindGroupLayout;
    RhiBindGroupEntry textureEntry;
    textureEntry.binding = 0u;
    textureEntry.resource.combinedTextureSampler = {m_itemAtlasView, m_textureSampler};
    bindGroupDesc.entries.push_back(textureEntry);
    m_itemGBufferBindGroup = m_rhiDevice->createBindGroup(bindGroupDesc);
    if (!m_itemGBufferVertexShader.isValid() || !m_itemGBufferFragmentShader.isValid() ||
        !m_itemGBufferBindGroupLayout.isValid() || !m_itemGBufferPipelineLayout.isValid() ||
        !m_itemGBufferPipeline.isValid() || !m_itemGBufferBindGroup.isValid()) {
        std::abort();
    }
}

void FirstPersonHeldItemRenderer::destroyItemGBufferResources() {
    if (m_itemGBufferBindGroup.isValid())
        m_rhiDevice->destroyBindGroup(m_itemGBufferBindGroup);
    if (m_itemGBufferPipeline.isValid())
        m_rhiDevice->destroyPipeline(m_itemGBufferPipeline);
    if (m_itemGBufferPipelineLayout.isValid())
        m_rhiDevice->destroyPipelineLayout(m_itemGBufferPipelineLayout);
    if (m_itemGBufferBindGroupLayout.isValid())
        m_rhiDevice->destroyBindGroupLayout(m_itemGBufferBindGroupLayout);
    if (m_itemGBufferFragmentShader.isValid())
        m_rhiDevice->destroyShader(m_itemGBufferFragmentShader);
    if (m_itemGBufferVertexShader.isValid())
        m_rhiDevice->destroyShader(m_itemGBufferVertexShader);
    m_itemGBufferBindGroup = {};
    m_itemGBufferPipeline = {};
    m_itemGBufferPipelineLayout = {};
    m_itemGBufferBindGroupLayout = {};
    m_itemGBufferFragmentShader = {};
    m_itemGBufferVertexShader = {};
}

void FirstPersonHeldItemRenderer::createBlockGBufferResources() {
    const auto vertexSource = renderer::rhi::loadShaderSource("assets/shaders/falling_block_gbuffer_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource("assets/shaders/falling_block_gbuffer_rhi.frag");
    if (!vertexSource || !fragmentSource) {
        std::abort();
    }
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "FirstPerson.BlockGBuffer.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str();
    shaderDesc.sourceSize = vertexSource->size();
    m_blockGBufferVertexShader = m_rhiDevice->createShader(shaderDesc);
    shaderDesc.debugName = "FirstPerson.BlockGBuffer.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = fragmentSource->c_str();
    shaderDesc.sourceSize = fragmentSource->size();
    m_blockGBufferFragmentShader = m_rhiDevice->createShader(shaderDesc);

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "FirstPerson.BlockGBuffer.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 3u; ++binding) {
        bindGroupLayoutDesc.entries.push_back(
            {binding, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u});
    }
    m_blockGBufferBindGroupLayout = m_rhiDevice->createBindGroupLayout(bindGroupLayoutDesc);

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "FirstPerson.BlockGBuffer.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_blockGBufferBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(BlockGBufferPushConstants);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    m_blockGBufferPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "FirstPerson.BlockGBuffer.Pipeline";
    pipelineDesc.vertexShader = m_blockGBufferVertexShader;
    pipelineDesc.fragmentShader = m_blockGBufferFragmentShader;
    pipelineDesc.layout = m_blockGBufferPipelineLayout;
    renderer::setBlockVertexInputLayout(pipelineDesc);
    // See the item pipeline: Always depth needs back-face culling for self-occlusion.
    pipelineDesc.raster.cullMode = RhiCullMode::Back;
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::Always;
    pipelineDesc.colorFormats.assign(kGBufferColorFormats.begin(), kGBufferColorFormats.end());
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    pipelineDesc.blend.attachments.resize(8u);
    m_blockGBufferPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_blockGBufferBindGroupLayout;
    const std::array<std::pair<RhiTextureViewHandle, RhiSamplerHandle>, 3> blockTextures = {
        std::make_pair(m_blockTextureArrayView, m_blockTextureSampler),
        std::make_pair(m_grassColormapView, m_textureSampler),
        std::make_pair(m_foliageColormapView, m_textureSampler)};
    for (uint32_t binding = 0u; binding < blockTextures.size(); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler = {blockTextures[binding].first, blockTextures[binding].second};
        bindGroupDesc.entries.push_back(entry);
    }
    m_blockGBufferBindGroup = m_rhiDevice->createBindGroup(bindGroupDesc);
    if (!m_blockGBufferVertexShader.isValid() || !m_blockGBufferFragmentShader.isValid() ||
        !m_blockGBufferBindGroupLayout.isValid() || !m_blockGBufferPipelineLayout.isValid() ||
        !m_blockGBufferPipeline.isValid() || !m_blockGBufferBindGroup.isValid()) {
        std::abort();
    }
}

void FirstPersonHeldItemRenderer::destroyBlockGBufferResources() {
    if (m_blockGBufferBindGroup.isValid())
        m_rhiDevice->destroyBindGroup(m_blockGBufferBindGroup);
    if (m_blockGBufferPipeline.isValid())
        m_rhiDevice->destroyPipeline(m_blockGBufferPipeline);
    if (m_blockGBufferPipelineLayout.isValid())
        m_rhiDevice->destroyPipelineLayout(m_blockGBufferPipelineLayout);
    if (m_blockGBufferBindGroupLayout.isValid())
        m_rhiDevice->destroyBindGroupLayout(m_blockGBufferBindGroupLayout);
    if (m_blockGBufferFragmentShader.isValid())
        m_rhiDevice->destroyShader(m_blockGBufferFragmentShader);
    if (m_blockGBufferVertexShader.isValid())
        m_rhiDevice->destroyShader(m_blockGBufferVertexShader);
    m_blockGBufferBindGroup = {};
    m_blockGBufferPipeline = {};
    m_blockGBufferPipelineLayout = {};
    m_blockGBufferBindGroupLayout = {};
    m_blockGBufferFragmentShader = {};
    m_blockGBufferVertexShader = {};
}

void FirstPersonHeldItemRenderer::prepareFrame(const glm::mat4& cameraView, const Inventory& inventory,
                                               const FirstPersonHeldItemMotion& motion, const float timeSeconds) {
    if (!m_initialized) {
        return;
    }

    const ItemID selectedItem = inventory.getSelectedItem();
    if (!m_hasPrevSample) {
        m_prevTimeSeconds = timeSeconds;
        m_visibleItemId = selectedItem;
        m_lastSelectedItemId = selectedItem;
        m_hasPrevSample = true;
    }

    const float dt = std::clamp(timeSeconds - m_prevTimeSeconds, 0.0f, 0.1f);
    m_prevTimeSeconds = timeSeconds;

    if (!m_hasLagSample) {
        m_lagYawDegrees = motion.cameraYawDegrees;
        m_lagPitchDegrees = motion.cameraPitchDegrees;
        m_hasLagSample = true;
    }
    const float lagBlend = std::clamp(dt * m_config.viewLagFollowSpeed, 0.0f, 1.0f);
    m_lagYawDegrees += wrapDegrees(motion.cameraYawDegrees - m_lagYawDegrees) * lagBlend;
    m_lagPitchDegrees += (motion.cameraPitchDegrees - m_lagPitchDegrees) * lagBlend;

    const float yawLag = std::clamp(wrapDegrees(motion.cameraYawDegrees - m_lagYawDegrees), -m_config.viewLagMaxDegrees,
                                    m_config.viewLagMaxDegrees);
    const float pitchLag = std::clamp(motion.cameraPitchDegrees - m_lagPitchDegrees, -m_config.viewLagMaxDegrees,
                                      m_config.viewLagMaxDegrees);
    const float lagX = yawLag * m_config.viewLagOffsetX;
    const float lagY = -pitchLag * m_config.viewLagOffsetY;

    if (selectedItem != m_lastSelectedItemId) {
        m_lastSelectedItemId = selectedItem;
        m_visibleItemId = selectedItem;
        m_equipProgress = 0.0f;
    }
    m_equipProgress = std::clamp(m_equipProgress + dt * m_config.equipSpeed, 0.0f, 1.0f);

    const float targetBob = motion.moving ? 1.0f : 0.0f;
    const float bobSpeed = motion.moving ? 10.0f : 8.0f;
    m_walkBobBlend += (targetBob - m_walkBobBlend) * std::clamp(dt * bobSpeed, 0.0f, 1.0f);

    if (m_swingActive) {
        m_swingElapsed += dt;
        if (m_continuousSwing && m_swingElapsed > m_config.swingDurationSeconds) {
            m_swingElapsed = std::fmod(m_swingElapsed, m_config.swingDurationSeconds);
        } else if (!m_continuousSwing && m_swingElapsed >= m_config.swingDurationSeconds) {
            m_swingActive = false;
            m_swingElapsed = 0.0f;
        }
    }

    const float bobPhase = timeSeconds * std::max(0.0f, motion.bobFrequency);
    const float sprintMul = motion.sprinting ? 1.18f : 1.0f;
    const float bobX = std::cos(bobPhase + motion.bobPhaseOffset) * m_config.bobOffsetX * m_walkBobBlend * sprintMul;
    const float bobY = std::abs(std::sin(bobPhase)) * m_config.bobOffsetY * m_walkBobBlend * sprintMul;
    const float bobRoll = std::sin(bobPhase) * glm::radians(m_config.bobRollDegrees) * m_walkBobBlend * sprintMul;

    float swing01 = 0.0f;
    if (m_swingActive || m_continuousSwing) {
        swing01 = std::clamp(m_swingElapsed / m_config.swingDurationSeconds, 0.0f, 1.0f);
    }
    const float swingRoot = std::sqrt(swing01);
    const float swingSin = std::sin(swingRoot * kPi);
    const float swingSinFull = std::sin(swing01 * kPi);

    const float equipDrop = (1.0f - m_equipProgress) * m_config.equipDrop;

    // The animated placement is authored in eye space; convert to world space
    // so the mesh can flow through the world pipelines (GBuffer, velocity).
    const glm::mat4 invView = glm::inverse(cameraView);
    const auto commitPreparedFrame = [this, &invView](const PreparedDrawKind kind, const glm::mat4& eyeModel,
                                                      const ItemID itemId) {
        const glm::mat4 worldModel = invView * eyeModel;
        const bool historyValid =
            m_hasPreparedHistory && m_preparedFrame.kind == kind && m_preparedFrame.itemId == itemId;
        m_preparedFrame = {kind, worldModel, historyValid ? m_preparedFrame.model : worldModel, itemId};
        m_hasPreparedHistory = true;
    };

    if (m_visibleItemId == 0) {
        glm::mat4 armModel(1.0f);
        armModel = glm::translate(
            armModel, glm::vec3(m_config.armPosX + bobX + lagX + swingSin * m_config.armSwingX,
                                m_config.armPosY + bobY + lagY - equipDrop + swingSinFull * m_config.armSwingY,
                                m_config.armPosZ + swingSin * m_config.armSwingZ));
        armModel = glm::rotate(armModel,
                               glm::radians(m_config.armPitchDegrees - pitchLag * m_config.viewLagPitchDegrees) +
                                   swingSin * glm::radians(m_config.armSwingPitchDegrees),
                               glm::vec3(1.0f, 0.0f, 0.0f));
        armModel = glm::rotate(armModel,
                               glm::radians(m_config.armYawDegrees - yawLag * m_config.viewLagYawDegrees) +
                                   swingSin * glm::radians(m_config.armSwingYawDegrees),
                               glm::vec3(0.0f, 1.0f, 0.0f));
        armModel = glm::rotate(armModel,
                               glm::radians(m_config.armRollDegrees) + bobRoll +
                                   swingSinFull * glm::radians(m_config.armSwingRollDegrees),
                               glm::vec3(0.0f, 0.0f, 1.0f));
        armModel = glm::scale(armModel, glm::vec3(m_config.armScale));
        commitPreparedFrame(PreparedDrawKind::Arm, armModel, 0);
        return;
    }

    const ItemDef& itemDef = ItemRegistry::get(m_visibleItemId);
    const int itemTileIndex = m_resources->uiTextures.itemTextureIndex(itemDef.iconTextureName);
    const BlockID renderBlock = ItemRegistry::toRenderBlock(m_visibleItemId);
    const bool preferBlockMesh = prefersBlockMeshForItem(renderBlock);
    const bool useItemMesh = !preferBlockMesh && itemTileIndex >= 0 && m_itemAtlasView.isValid();
    const bool useBlockMesh = !useItemMesh && renderBlock != 0 && m_blockTextureArrayView.isValid();

    if (!useItemMesh && !useBlockMesh) {
        m_preparedFrame.kind = PreparedDrawKind::None;
        m_hasPreparedHistory = false;
        return;
    }

    const float pitchDegrees = useBlockMesh ? m_config.blockPitchDegrees : m_config.itemPitchDegrees;
    const float yawDegrees = useBlockMesh ? m_config.blockYawDegrees : m_config.itemYawDegrees;

    glm::mat4 itemModel(1.0f);
    itemModel = glm::translate(
        itemModel, glm::vec3(m_config.itemPosX + bobX + lagX + swingSin * m_config.itemSwingX,
                             m_config.itemPosY + bobY + lagY - equipDrop + swingSinFull * m_config.itemSwingY,
                             m_config.itemPosZ + swingSin * m_config.itemSwingZ));
    itemModel = glm::rotate(itemModel,
                            glm::radians(pitchDegrees - pitchLag * m_config.viewLagPitchDegrees) +
                                swingSin * glm::radians(m_config.itemSwingPitchDegrees),
                            glm::vec3(1.0f, 0.0f, 0.0f));
    itemModel = glm::rotate(itemModel,
                            glm::radians(yawDegrees - yawLag * m_config.viewLagYawDegrees) +
                                swingSin * glm::radians(m_config.itemSwingYawDegrees),
                            glm::vec3(0.0f, 1.0f, 0.0f));
    itemModel = glm::rotate(itemModel,
                            glm::radians(m_config.itemRollDegrees) + bobRoll +
                                swingSinFull * glm::radians(m_config.itemSwingRollDegrees),
                            glm::vec3(0.0f, 0.0f, 1.0f));

    if (useBlockMesh) {
        itemModel = glm::scale(itemModel, glm::vec3(m_config.blockScale));
    } else {
        itemModel = glm::scale(itemModel, glm::vec3(m_config.itemScale));
    }
    itemModel = glm::translate(itemModel, glm::vec3(-0.5f, -0.5f, -0.5f));

    const PreparedDrawKind drawKind = useBlockMesh ? PreparedDrawKind::Block : PreparedDrawKind::Item;
    commitPreparedFrame(drawKind, itemModel, m_visibleItemId);
}

bool FirstPersonHeldItemRenderer::hasPreparedDraw() const {
    return m_initialized && m_preparedFrame.kind != PreparedDrawKind::None;
}

void FirstPersonHeldItemRenderer::renderPreparedForward(RhiCommandList& commandList, const glm::mat4& viewProj,
                                                        const float skyIntensity, const float animationTime,
                                                        const int width, const int height) {
    if (m_preparedFrame.kind == PreparedDrawKind::None || width <= 0 || height <= 0) {
        return;
    }
    const ForwardPushConstants constants{
        viewProj, m_preparedFrame.model,
        glm::vec4(m_environmentSunlight, m_environmentBlockLight, skyIntensity, animationTime)};
    commandList.setViewport({0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f});
    commandList.setScissor({0, 0, static_cast<uint32_t>(width), static_cast<uint32_t>(height)});

    if (m_preparedFrame.kind == PreparedDrawKind::Arm) {
        commandList.setGraphicsPipeline(m_armPipeline);
        commandList.setBindGroup(0u, m_armBindGroup);
        commandList.setVertexBuffer(0u, m_rightArmMesh.rhiVertexBuffer, 0u);
        commandList.pushConstants(&constants, sizeof(constants),
                                  rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(m_rightArmMesh.vertexCount, 1u, 0u, 0u);
        return;
    }
    if (m_preparedFrame.kind == PreparedDrawKind::Item) {
        const auto meshIt = m_itemMeshes.find(m_preparedFrame.itemId);
        if (meshIt == m_itemMeshes.end() || !meshIt->second.rhiVertexBuffer.isValid() ||
            meshIt->second.vertexCount == 0u) {
            std::abort();
        }
        commandList.setGraphicsPipeline(m_itemPipeline);
        commandList.setBindGroup(0u, m_itemBindGroup);
        commandList.setVertexBuffer(0u, meshIt->second.rhiVertexBuffer, 0u);
        commandList.pushConstants(&constants, sizeof(constants),
                                  rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(meshIt->second.vertexCount, 1u, 0u, 0u);
        return;
    }
    const BlockID blockId = ItemRegistry::toRenderBlock(m_preparedFrame.itemId);
    const auto meshIt = m_blockMeshes.find(blockId);
    if (blockId == 0 || meshIt == m_blockMeshes.end() || !meshIt->second.rhiVertexBuffer.isValid() ||
        meshIt->second.vertexCount == 0u) {
        std::abort();
    }
    commandList.setGraphicsPipeline(m_blockPipeline);
    commandList.setBindGroup(0u, m_blockBindGroup);
    commandList.setVertexBuffer(0u, meshIt->second.rhiVertexBuffer, 0u);
    commandList.pushConstants(&constants, sizeof(constants),
                              rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(meshIt->second.vertexCount, 1u, 0u, 0u);
}

void FirstPersonHeldItemRenderer::renderPreparedToGBuffer(RhiCommandList& commandList, const glm::mat4& viewProj,
                                                          const glm::mat4& previousViewProj,
                                                          const float animationTime) {
    if (m_preparedFrame.kind == PreparedDrawKind::None) {
        return;
    }
    const glm::vec2 light(m_environmentSunlight, m_environmentBlockLight);
    const glm::mat4 modelViewProj = viewProj * m_preparedFrame.model;
    const glm::mat4 previousModelViewProj = previousViewProj * m_preparedFrame.previousModel;

    if (m_preparedFrame.kind == PreparedDrawKind::Arm) {
        if (!m_armGBufferPipeline.isValid() || !m_armGBufferBindGroup.isValid() ||
            !m_rightArmMesh.rhiVertexBuffer.isValid() || m_rightArmMesh.vertexCount == 0u) {
            std::abort();
        }
        const ArmGBufferPushConstants constants{modelViewProj, previousModelViewProj, m_preparedFrame.model, light,
                                                0.0f, m_objectId};
        commandList.setGraphicsPipeline(m_armGBufferPipeline);
        commandList.setBindGroup(0u, m_armGBufferBindGroup);
        commandList.setVertexBuffer(0u, m_rightArmMesh.rhiVertexBuffer, 0u);
        commandList.pushConstants(&constants, sizeof(constants),
                                  rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(m_rightArmMesh.vertexCount, 1u, 0u, 0u);
        return;
    }
    if (m_preparedFrame.kind == PreparedDrawKind::Item) {
        const auto meshIt = m_itemMeshes.find(m_preparedFrame.itemId);
        if (meshIt == m_itemMeshes.end() || !meshIt->second.rhiVertexBuffer.isValid() ||
            meshIt->second.vertexCount == 0u) {
            std::abort();
        }
        const ItemGBufferPushConstants constants{modelViewProj, previousModelViewProj, m_preparedFrame.model, light,
                                                 glm::uvec2(m_objectId, meshIt->second.materialId)};
        commandList.setGraphicsPipeline(m_itemGBufferPipeline);
        commandList.setBindGroup(0u, m_itemGBufferBindGroup);
        commandList.setVertexBuffer(0u, meshIt->second.rhiVertexBuffer, 0u);
        commandList.pushConstants(&constants, sizeof(constants),
                                  rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(meshIt->second.vertexCount, 1u, 0u, 0u);
        return;
    }
    const BlockID blockId = ItemRegistry::toRenderBlock(m_preparedFrame.itemId);
    const auto meshIt = m_blockMeshes.find(blockId);
    if (blockId == 0 || meshIt == m_blockMeshes.end() || !meshIt->second.rhiVertexBuffer.isValid() ||
        meshIt->second.vertexCount == 0u) {
        std::abort();
    }
    const BlockGBufferPushConstants constants{modelViewProj, previousModelViewProj, m_preparedFrame.model, light,
                                              animationTime, m_objectId};
    commandList.setGraphicsPipeline(m_blockGBufferPipeline);
    commandList.setBindGroup(0u, m_blockGBufferBindGroup);
    commandList.setVertexBuffer(0u, meshIt->second.rhiVertexBuffer, 0u);
    commandList.pushConstants(&constants, sizeof(constants),
                              rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(meshIt->second.vertexCount, 1u, 0u, 0u);
}

FirstPersonHeldItemRenderer::Mesh* FirstPersonHeldItemRenderer::getOrCreateBlockMesh(const BlockID blockId) {
    const auto it = m_blockMeshes.find(blockId);
    if (it != m_blockMeshes.end()) {
        return &it->second;
    }

    Mesh mesh = buildBlockMesh(blockId);
    auto inserted = m_blockMeshes.emplace(blockId, std::move(mesh));
    return &inserted.first->second;
}

FirstPersonHeldItemRenderer::Mesh* FirstPersonHeldItemRenderer::getOrCreateItemMesh(const ItemID itemId) {
    const auto it = m_itemMeshes.find(itemId);
    if (it != m_itemMeshes.end()) {
        return &it->second;
    }

    Mesh mesh = buildItemMesh(itemId);
    if (mesh.rhiVertexBuffer.isValid()) {
        const std::optional<renderer::contracts::StableMaterialId> materialId =
            renderer::contracts::allocateStableSceneId<renderer::contracts::StableMaterialIdTag>();
        if (!materialId.has_value()) {
            std::abort();
        }
        mesh.materialId = materialId->value;
    }
    auto inserted = m_itemMeshes.emplace(itemId, std::move(mesh));
    return &inserted.first->second;
}

FirstPersonHeldItemRenderer::Mesh FirstPersonHeldItemRenderer::buildBlockMesh(const BlockID blockId) const {
    Mesh mesh;
    if (m_resources == nullptr || blockId == 0) {
        return mesh;
    }

    renderer::BlockCubeMesh shared = renderer::buildBlockCubeMesh(blockId, *m_resources, *m_rhiDevice);
    mesh.rhiVertexBuffer = shared.rhiVertexBuffer;
    mesh.rhiDevice = shared.rhiDevice;
    mesh.vertexCount = shared.vertexCount;
    shared.rhiVertexBuffer = {};
    shared.rhiDevice = nullptr;
    shared.vertexCount = 0;
    return mesh;
}

FirstPersonHeldItemRenderer::Mesh FirstPersonHeldItemRenderer::buildItemMesh(const ItemID itemId) const {
    Mesh mesh;
    if (m_resources == nullptr || itemId == 0) {
        return mesh;
    }

    const ItemDef& itemDef = ItemRegistry::get(itemId);
    const int tileIndex = m_resources->uiTextures.itemTextureIndex(itemDef.iconTextureName);
    if (tileIndex < 0) {
        return mesh;
    }

    std::vector<ItemModelVertex> vertices;
    if (!buildExtrudedItemMesh(m_resources->uiTextures.itemTextureAtlas(), m_resources->uiTextures.itemTexturePixels(), tileIndex,
                               vertices)) {
        return mesh;
    }

    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "FirstPerson.ItemMesh.VertexBuffer";
    bufferDesc.size = vertices.size() * sizeof(ItemModelVertex);
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex) | rhiFlag(RhiBufferUsage::TransferDst);
    bufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    bufferDesc.initialState = RhiResourceState::VertexBuffer;
    bufferDesc.memoryCategory = RhiMemoryCategory::Geometry;
    mesh.rhiVertexBuffer =
        m_rhiDevice->createBuffer(bufferDesc, vertices.data(), vertices.size() * sizeof(ItemModelVertex));
    mesh.rhiDevice = m_rhiDevice;
    if (!mesh.rhiVertexBuffer.isValid()) {
        destroyMesh(mesh);
    }
    return mesh;
}

FirstPersonHeldItemRenderer::Mesh FirstPersonHeldItemRenderer::buildRightArmMesh() const {
    Mesh mesh;
    std::vector<SteveVertex> vertices;
    vertices.reserve(36);

    const float xmin = -0.125f;
    const float xmax = 0.125f;
    const float ymin = -0.75f;
    const float ymax = 0.0f;
    const float zmin = -0.125f;
    const float zmax = 0.125f;
    const FaceUvRect uv[6] = {pixelRectToUv(44, 16, 48, 20), pixelRectToUv(48, 16, 52, 20),
                              pixelRectToUv(44, 20, 48, 32), pixelRectToUv(52, 20, 56, 32),
                              pixelRectToUv(40, 20, 44, 32), pixelRectToUv(48, 20, 52, 32)};

    addSteveQuad(vertices, {xmin, ymax, zmax}, {xmax, ymax, zmax}, {xmax, ymax, zmin}, {xmin, ymax, zmin}, uv[0],
                 {0.0f, 1.0f, 0.0f});
    addSteveQuad(vertices, {xmin, ymin, zmin}, {xmax, ymin, zmin}, {xmax, ymin, zmax}, {xmin, ymin, zmax}, uv[1],
                 {0.0f, -1.0f, 0.0f});
    addSteveQuad(vertices, {xmin, ymin, zmax}, {xmax, ymin, zmax}, {xmax, ymax, zmax}, {xmin, ymax, zmax}, uv[2],
                 {0.0f, 0.0f, 1.0f});
    addSteveQuad(vertices, {xmax, ymin, zmin}, {xmin, ymin, zmin}, {xmin, ymax, zmin}, {xmax, ymax, zmin}, uv[3],
                 {0.0f, 0.0f, -1.0f});
    addSteveQuad(vertices, {xmin, ymin, zmin}, {xmin, ymin, zmax}, {xmin, ymax, zmax}, {xmin, ymax, zmin}, uv[4],
                 {-1.0f, 0.0f, 0.0f});
    addSteveQuad(vertices, {xmax, ymin, zmax}, {xmax, ymin, zmin}, {xmax, ymax, zmin}, {xmax, ymax, zmax}, uv[5],
                 {1.0f, 0.0f, 0.0f});

    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "FirstPerson.ArmMesh.VertexBuffer";
    bufferDesc.size = vertices.size() * sizeof(SteveVertex);
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex) | rhiFlag(RhiBufferUsage::TransferDst);
    bufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    bufferDesc.initialState = RhiResourceState::VertexBuffer;
    bufferDesc.memoryCategory = RhiMemoryCategory::Geometry;
    mesh.rhiVertexBuffer =
        m_rhiDevice->createBuffer(bufferDesc, vertices.data(), vertices.size() * sizeof(SteveVertex));
    mesh.rhiDevice = m_rhiDevice;
    if (!mesh.rhiVertexBuffer.isValid()) {
        destroyMesh(mesh);
    }
    return mesh;
}

void FirstPersonHeldItemRenderer::destroyMesh(Mesh& mesh) {
    if (mesh.rhiDevice != nullptr && mesh.rhiVertexBuffer.isValid()) {
        mesh.rhiDevice->destroyBuffer(mesh.rhiVertexBuffer);
        mesh.rhiVertexBuffer = {};
    }
    mesh.vertexCount = 0;
    mesh.rhiDevice = nullptr;
}
