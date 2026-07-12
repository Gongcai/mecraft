#include "FirstPersonHeldItemRenderer.h"

#include "../../Diagnostics.h"
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
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include "../../Paths.h"
#include "../../player/Inventory.h"
#include "../../resource/ResourceMgr.h"
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

FaceUvRect pixelRectToUv(const float x0,
                         const float y0,
                         const float x1,
                         const float y1) {
    constexpr float skinW = 64.0f;
    constexpr float skinH = 64.0f;
    return {
        x0 / skinW,
        1.0f - y1 / skinH,
        x1 / skinW,
        1.0f - y0 / skinH
    };
}

void addSteveQuad(std::vector<FirstPersonHeldItemRenderer::SteveVertex>& vertices,
                  const glm::vec3& a,
                  const glm::vec3& b,
                  const glm::vec3& c,
                  const glm::vec3& d,
                  const FaceUvRect& uvRect,
                  const glm::vec3& normal) {
    const std::array<glm::vec2, 4> uv = {{
        {uvRect.u0, uvRect.v0},
        {uvRect.u1, uvRect.v0},
        {uvRect.u1, uvRect.v1},
        {uvRect.u0, uvRect.v1}
    }};
    const std::array<glm::vec3, 4> pos = {{a, b, c, d}};
    for (const int idx : kFaceIndices) {
        const glm::vec3& p = pos[static_cast<size_t>(idx)];
        const glm::vec2& t = uv[static_cast<size_t>(idx)];
        vertices.push_back({p.x, p.y, p.z, t.x, t.y, normal.x, normal.y, normal.z});
    }
}

}

void FirstPersonHeldItemRenderer::init(ResourceMgr& resourceMgr, RhiDevice& rhiDevice) {
    if (m_initialized) {
        shutdown();
    }
    m_resourceMgr = &resourceMgr;
    m_rhiDevice = &rhiDevice;
    createRhiTextureResources();
    createArmRhiResources();
    createItemRhiResources();
    createBlockRhiResources();
    m_rightArmMesh = buildRightArmMesh();
    if (!m_rightArmMesh.rhiVertexBuffer.isValid() || m_rightArmMesh.vertexCount == 0u) {
        std::abort();
    }
    loadConfig();
    m_initialized = true;
}

void FirstPersonHeldItemRenderer::shutdown() {
    if (!m_initialized && m_resourceMgr == nullptr && !m_rightArmMesh.rhiVertexBuffer.isValid() &&
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
    destroyBlockRhiResources();
    destroyItemRhiResources();
    destroyArmRhiResources();
    destroyRhiTextureResources();
    m_resourceMgr = nullptr;
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
    m_sceneHdrScale = 1.0f;
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
}

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
    config.fovDegrees = readJsonFloat(json, "fovDegrees", config.fovDegrees);
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
    if (m_resourceMgr == nullptr) {
        return;
    }

    nlohmann::json json;
    json["fovDegrees"] = m_config.fovDegrees;
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
    m_config.fovDegrees = std::clamp(m_config.fovDegrees, 20.0f, 120.0f);
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

void FirstPersonHeldItemRenderer::setSceneHdrScale(const float scale) {
    m_sceneHdrScale = std::clamp(scale, 1.0f, 8.0f);
}

void FirstPersonHeldItemRenderer::prepareFrameResources(const Inventory& inventory) {
    if (!m_initialized || m_resourceMgr == nullptr) {
        return;
    }
    const ItemID selectedItem = inventory.getSelectedItem();
    if (selectedItem == 0) {
        return;
    }
    const ItemDef& itemDef = ItemRegistry::get(selectedItem);
    const int itemTileIndex = m_resourceMgr->getItemTextureIndex(itemDef.iconTextureName);
    const BlockID renderBlock = ItemRegistry::toRenderBlock(selectedItem);
    if (!prefersBlockMeshForItem(renderBlock) && itemTileIndex >= 0) {
        getOrCreateItemMesh(selectedItem);
    } else if (renderBlock != 0) {
        getOrCreateBlockMesh(renderBlock);
    }
}

void FirstPersonHeldItemRenderer::createRhiTextureResources() {
    const RhiTextureHandle textures[] = {
        m_resourceMgr->getGuiTextureHandle("steve"),
        m_resourceMgr->getItemTextureAtlas().texture,
        m_resourceMgr->getTextureArray().texture,
        m_resourceMgr->getLightmapDay(),
        m_resourceMgr->getLightmapNight(),
        m_resourceMgr->getGrassColormap(),
        m_resourceMgr->getFoliageColormap()
    };
    RhiTextureViewHandle* views[] = {
        &m_steveTextureView,
        &m_itemAtlasView,
        &m_blockTextureArrayView,
        &m_lightmapDayView,
        &m_lightmapNightView,
        &m_grassColormapView,
        &m_foliageColormapView
    };
    for (uint32_t index = 0u; index < 7u; ++index) {
        if (!textures[index].isValid()) {
            std::abort();
        }
        RhiTextureViewDesc viewDesc;
        viewDesc.texture = textures[index];
        viewDesc.viewType = index == 2u ? RhiTextureViewType::Texture2DArray
                                       : RhiTextureViewType::Texture2D;
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
    samplerDesc.addressU = RhiAddressMode::ClampToBorder;
    samplerDesc.addressV = RhiAddressMode::ClampToBorder;
    samplerDesc.addressW = RhiAddressMode::ClampToBorder;
    samplerDesc.borderColor = RhiBorderColor::OpaqueWhite;
    samplerDesc.compareEnabled = true;
    samplerDesc.compareOp = RhiCompareOp::LessOrEqual;
    m_shadowCompareSampler = m_rhiDevice->createSampler(samplerDesc);
    samplerDesc.compareEnabled = false;
    m_shadowRawSampler = m_rhiDevice->createSampler(samplerDesc);
    RhiBufferDesc uniformBufferDesc;
    uniformBufferDesc.debugName = "FirstPerson.ShadowUniformBuffer";
    uniformBufferDesc.size = sizeof(ShadowUniforms);
    uniformBufferDesc.usage = rhiFlag(RhiBufferUsage::Uniform) |
                              rhiFlag(RhiBufferUsage::TransferDst);
    uniformBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    uniformBufferDesc.initialState = RhiResourceState::UniformBuffer;
    m_shadowUniformBuffer = m_rhiDevice->createBuffer(uniformBufferDesc, nullptr, 0u);
    if (!m_textureSampler.isValid() || !m_blockTextureSampler.isValid() ||
        !m_shadowCompareSampler.isValid() || !m_shadowRawSampler.isValid() ||
        !m_shadowUniformBuffer.isValid()) {
        std::abort();
    }
}

void FirstPersonHeldItemRenderer::synchronizeShadowTextureViews() {
    const std::array<RhiTextureHandle, 6> textures = {
        m_shadowData.shadowTexture, m_shadowData.shadowDepthRaw,
        m_shadowData.shadowDepthAll, m_shadowData.shadowDepthAllRaw,
        m_shadowData.shadowColor0, m_shadowData.shadowColor1
    };
    if (m_shadowData.shadowsEnabled == 0) {
        destroyShadowTextureViews();
        return;
    }
    for (const RhiTextureHandle texture : textures) {
        if (!texture.isValid()) {
            std::abort();
        }
    }
    bool unchanged = true;
    for (std::size_t index = 0u; index < textures.size(); ++index) {
        unchanged = unchanged &&
            textures[index].index == m_shadowTextureHandles[index].index &&
            textures[index].generation == m_shadowTextureHandles[index].generation;
    }
    if (unchanged) {
        return;
    }
    destroyShadowTextureViews();
    m_shadowTextureHandles = textures;
    for (std::size_t index = 0u; index < textures.size(); ++index) {
        RhiTextureViewDesc viewDesc;
        viewDesc.texture = textures[index];
        viewDesc.viewType = RhiTextureViewType::Texture2DArray;
        m_shadowTextureViews[index] = m_rhiDevice->createTextureView(viewDesc);
        if (!m_shadowTextureViews[index].isValid()) {
            std::abort();
        }
    }
}

void FirstPersonHeldItemRenderer::destroyShadowTextureViews() {
    if (m_rhiDevice != nullptr) {
        for (RhiTextureViewHandle& view : m_shadowTextureViews) {
            if (view.isValid()) {
                m_rhiDevice->destroyTextureView(view);
            }
            view = {};
        }
    }
    m_shadowTextureHandles = {};
}

void FirstPersonHeldItemRenderer::destroyRhiTextureResources() {
    destroyShadowTextureViews();
    if (m_shadowUniformBuffer.isValid()) m_rhiDevice->destroyBuffer(m_shadowUniformBuffer);
    if (m_shadowRawSampler.isValid()) m_rhiDevice->destroySampler(m_shadowRawSampler);
    if (m_shadowCompareSampler.isValid()) m_rhiDevice->destroySampler(m_shadowCompareSampler);
    if (m_blockTextureSampler.isValid()) m_rhiDevice->destroySampler(m_blockTextureSampler);
    if (m_textureSampler.isValid()) m_rhiDevice->destroySampler(m_textureSampler);
    if (m_foliageColormapView.isValid()) m_rhiDevice->destroyTextureView(m_foliageColormapView);
    if (m_grassColormapView.isValid()) m_rhiDevice->destroyTextureView(m_grassColormapView);
    if (m_lightmapNightView.isValid()) m_rhiDevice->destroyTextureView(m_lightmapNightView);
    if (m_lightmapDayView.isValid()) m_rhiDevice->destroyTextureView(m_lightmapDayView);
    if (m_blockTextureArrayView.isValid()) m_rhiDevice->destroyTextureView(m_blockTextureArrayView);
    if (m_itemAtlasView.isValid()) m_rhiDevice->destroyTextureView(m_itemAtlasView);
    if (m_steveTextureView.isValid()) m_rhiDevice->destroyTextureView(m_steveTextureView);
    m_blockTextureSampler = {};
    m_textureSampler = {};
    m_foliageColormapView = {};
    m_grassColormapView = {};
    m_lightmapNightView = {};
    m_lightmapDayView = {};
    m_blockTextureArrayView = {};
    m_itemAtlasView = {};
    m_steveTextureView = {};
    m_shadowUniformBuffer = {};
    m_shadowRawSampler = {};
    m_shadowCompareSampler = {};
}

void FirstPersonHeldItemRenderer::createArmRhiResources() {
    const auto vertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/held_arm_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/held_arm_rhi.frag");
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
    bindGroupLayoutDesc.entries.push_back({0u, RhiBindingType::CombinedTextureSampler,
                                           rhiFlag(RhiShaderStage::Fragment), 1u});
    m_armBindGroupLayout = m_rhiDevice->createBindGroupLayout(bindGroupLayoutDesc);

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "FirstPerson.Arm.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_armBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(glm::mat4) * 2u + sizeof(glm::vec4);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) |
                                            rhiFlag(RhiShaderStage::Fragment);
    m_armPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "FirstPerson.Arm.Pipeline";
    pipelineDesc.vertexShader = m_armVertexShader;
    pipelineDesc.fragmentShader = m_armFragmentShader;
    pipelineDesc.layout = m_armPipelineLayout;
    pipelineDesc.vertexInput.bindings = {{0u, sizeof(SteveVertex), RhiVertexInputRate::Vertex}};
    pipelineDesc.vertexInput.attributes = {
        {0u, 0u, RhiVertexFormat::Float3, offsetof(SteveVertex, x)},
        {1u, 0u, RhiVertexFormat::Float2, offsetof(SteveVertex, u)},
        {2u, 0u, RhiVertexFormat::Float3, offsetof(SteveVertex, nx)}
    };
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
    RhiBindGroupEntry textureEntry;
    textureEntry.binding = 0u;
    textureEntry.resource.combinedTextureSampler = {m_steveTextureView, m_textureSampler};
    bindGroupDesc.entries.push_back(textureEntry);
    m_armBindGroup = m_rhiDevice->createBindGroup(bindGroupDesc);
    if (!m_armVertexShader.isValid() || !m_armFragmentShader.isValid() ||
        !m_armBindGroupLayout.isValid() || !m_armPipelineLayout.isValid() ||
        !m_armPipeline.isValid() || !m_armBindGroup.isValid()) {
        std::abort();
    }
}

void FirstPersonHeldItemRenderer::destroyArmRhiResources() {
    if (m_armBindGroup.isValid()) m_rhiDevice->destroyBindGroup(m_armBindGroup);
    if (m_armPipeline.isValid()) m_rhiDevice->destroyPipeline(m_armPipeline);
    if (m_armPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_armPipelineLayout);
    if (m_armBindGroupLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_armBindGroupLayout);
    if (m_armFragmentShader.isValid()) m_rhiDevice->destroyShader(m_armFragmentShader);
    if (m_armVertexShader.isValid()) m_rhiDevice->destroyShader(m_armVertexShader);
    m_armBindGroup = {};
    m_armPipeline = {};
    m_armPipelineLayout = {};
    m_armBindGroupLayout = {};
    m_armFragmentShader = {};
    m_armVertexShader = {};
}

void FirstPersonHeldItemRenderer::createItemRhiResources() {
    const auto vertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/held_item_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/held_item_rhi.frag");
    if (!vertexSource || !fragmentSource) std::abort();
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
    bindGroupLayoutDesc.entries.push_back({0u, RhiBindingType::CombinedTextureSampler,
                                           rhiFlag(RhiShaderStage::Fragment), 1u});
    m_itemBindGroupLayout = m_rhiDevice->createBindGroupLayout(bindGroupLayoutDesc);
    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "FirstPerson.Item.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_itemBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(glm::mat4) * 2u + sizeof(glm::vec4);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) |
                                            rhiFlag(RhiShaderStage::Fragment);
    m_itemPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "FirstPerson.Item.Pipeline";
    pipelineDesc.vertexShader = m_itemVertexShader;
    pipelineDesc.fragmentShader = m_itemFragmentShader;
    pipelineDesc.layout = m_itemPipelineLayout;
    pipelineDesc.vertexInput.bindings = {{0u, sizeof(ItemModelVertex), RhiVertexInputRate::Vertex}};
    pipelineDesc.vertexInput.attributes = {
        {0u, 0u, RhiVertexFormat::Float3, offsetof(ItemModelVertex, x)},
        {1u, 0u, RhiVertexFormat::Float2, offsetof(ItemModelVertex, u)},
        {2u, 0u, RhiVertexFormat::Float, offsetof(ItemModelVertex, shade)},
        {3u, 0u, RhiVertexFormat::Float3, offsetof(ItemModelVertex, nx)}
    };
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
    RhiBindGroupEntry textureEntry;
    textureEntry.binding = 0u;
    textureEntry.resource.combinedTextureSampler = {m_itemAtlasView, m_textureSampler};
    bindGroupDesc.entries.push_back(textureEntry);
    m_itemBindGroup = m_rhiDevice->createBindGroup(bindGroupDesc);
    if (!m_itemVertexShader.isValid() || !m_itemFragmentShader.isValid() ||
        !m_itemBindGroupLayout.isValid() || !m_itemPipelineLayout.isValid() ||
        !m_itemPipeline.isValid() || !m_itemBindGroup.isValid()) std::abort();
}

void FirstPersonHeldItemRenderer::destroyItemRhiResources() {
    if (m_itemBindGroup.isValid()) m_rhiDevice->destroyBindGroup(m_itemBindGroup);
    if (m_itemPipeline.isValid()) m_rhiDevice->destroyPipeline(m_itemPipeline);
    if (m_itemPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_itemPipelineLayout);
    if (m_itemBindGroupLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_itemBindGroupLayout);
    if (m_itemFragmentShader.isValid()) m_rhiDevice->destroyShader(m_itemFragmentShader);
    if (m_itemVertexShader.isValid()) m_rhiDevice->destroyShader(m_itemVertexShader);
    m_itemBindGroup = {};
    m_itemPipeline = {};
    m_itemPipelineLayout = {};
    m_itemBindGroupLayout = {};
    m_itemFragmentShader = {};
    m_itemVertexShader = {};
}

void FirstPersonHeldItemRenderer::createBlockRhiResources() {
    const auto vertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/held_block_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/held_block_rhi.frag");
    if (!vertexSource || !fragmentSource) std::abort();
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
    for (uint32_t binding = 0u; binding < 3u; ++binding) {
        bindGroupLayoutDesc.entries.push_back({binding, RhiBindingType::CombinedTextureSampler,
                                               rhiFlag(RhiShaderStage::Fragment), 1u});
    }
    m_blockBindGroupLayout = m_rhiDevice->createBindGroupLayout(bindGroupLayoutDesc);
    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "FirstPerson.Block.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_blockBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(glm::mat4) * 2u + sizeof(glm::vec4);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) |
                                            rhiFlag(RhiShaderStage::Fragment);
    m_blockPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "FirstPerson.Block.Pipeline";
    pipelineDesc.vertexShader = m_blockVertexShader;
    pipelineDesc.fragmentShader = m_blockFragmentShader;
    pipelineDesc.layout = m_blockPipelineLayout;
    renderer::setBlockVertexInputLayout(pipelineDesc);
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
    const std::array<RhiTextureViewHandle, 3> views = {
        m_blockTextureArrayView, m_grassColormapView, m_foliageColormapView
    };
    for (uint32_t binding = 0u; binding < views.size(); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler = {
            views[binding], binding == 0u ? m_blockTextureSampler : m_textureSampler
        };
        bindGroupDesc.entries.push_back(entry);
    }
    m_blockBindGroup = m_rhiDevice->createBindGroup(bindGroupDesc);
    if (!m_blockVertexShader.isValid() || !m_blockFragmentShader.isValid() ||
        !m_blockBindGroupLayout.isValid() || !m_blockPipelineLayout.isValid() ||
        !m_blockPipeline.isValid() || !m_blockBindGroup.isValid()) std::abort();
}

void FirstPersonHeldItemRenderer::destroyBlockRhiResources() {
    if (m_blockBindGroup.isValid()) m_rhiDevice->destroyBindGroup(m_blockBindGroup);
    if (m_blockPipeline.isValid()) m_rhiDevice->destroyPipeline(m_blockPipeline);
    if (m_blockPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_blockPipelineLayout);
    if (m_blockBindGroupLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_blockBindGroupLayout);
    if (m_blockFragmentShader.isValid()) m_rhiDevice->destroyShader(m_blockFragmentShader);
    if (m_blockVertexShader.isValid()) m_rhiDevice->destroyShader(m_blockVertexShader);
    m_blockBindGroup = {};
    m_blockPipeline = {};
    m_blockPipelineLayout = {};
    m_blockBindGroupLayout = {};
    m_blockFragmentShader = {};
    m_blockVertexShader = {};
}

void FirstPersonHeldItemRenderer::setShadowData(const ShadowData& data) {
    m_shadowData = data;
    synchronizeShadowTextureViews();
}

FirstPersonHeldItemRenderer::ShadowData FirstPersonHeldItemRenderer::fromFirstPersonShadowData(const FirstPersonShadowData& sd) {
    ShadowData shadow{};
    for (int i = 0; i < 4; ++i) {
        shadow.cascadeViewProj[i] = sd.cascadeViewProj[i];
        shadow.cascadeSplitFar[i] = sd.cascadeSplitFar[i];
        shadow.cascadeTexelWorldSize[i] = sd.cascadeTexelWorldSize[i];
        shadow.cascadeDepthExtent[i] = sd.cascadeDepthExtent[i];
    }
    shadow.shadowTexture = sd.shadowTextureHandle;
    shadow.shadowDepthRaw = sd.shadowDepthRawHandle;
    shadow.shadowDepthAll = sd.shadowDepthAllHandle;
    shadow.shadowDepthAllRaw = sd.shadowDepthAllRawHandle;
    shadow.shadowColor0 = sd.shadowColor0Handle;
    shadow.shadowColor1 = sd.shadowColor1Handle;
    shadow.cameraPos = sd.cameraPos;
    shadow.sunDirection = sd.sunDirection;
    shadow.shadowDistance = sd.shadowDistance;
    shadow.constantBias = sd.constantBias;
    shadow.slopeBias = sd.slopeBias;
    shadow.normalOffset = sd.normalOffset;
    shadow.softness = sd.softness;
    shadow.pcssStrength = sd.pcssStrength;
    shadow.cascadeCount = sd.cascadeCount;
    shadow.softShadowsEnabled = sd.softShadowsEnabled;
    shadow.pcssShadowsEnabled = sd.pcssShadowsEnabled;
    shadow.shadowsEnabled = sd.shadowsEnabled;
    shadow.skyIntensity = sd.skyIntensity;
    shadow.ambientStrength = 0.55f;
    return shadow;
}

void FirstPersonHeldItemRenderer::prepareFrame(const int width,
                                               const int height,
                                               const Inventory& inventory,
                                               const FirstPersonHeldItemMotion& motion,
                                               const float timeSeconds) {
    m_preparedFrame = {};
    if (!m_initialized || m_resourceMgr == nullptr) {
        return;
    }
    if (width <= 0 || height <= 0) {
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

    const float yawLag = std::clamp(wrapDegrees(motion.cameraYawDegrees - m_lagYawDegrees),
                                    -m_config.viewLagMaxDegrees,
                                    m_config.viewLagMaxDegrees);
    const float pitchLag = std::clamp(motion.cameraPitchDegrees - m_lagPitchDegrees,
                                      -m_config.viewLagMaxDegrees,
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

    const float aspect = static_cast<float>(width) / static_cast<float>(std::max(1, height));
    const glm::mat4 projection = glm::perspective(glm::radians(m_config.fovDegrees), aspect, 0.05f, 10.0f);
    const glm::mat4 view(1.0f);
    const glm::mat4 viewProj = projection * view;

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

    if (m_visibleItemId == 0) {
        glm::mat4 armModel(1.0f);
        armModel = glm::translate(armModel, glm::vec3(m_config.armPosX + bobX + lagX + swingSin * m_config.armSwingX,
                                                      m_config.armPosY + bobY + lagY - equipDrop + swingSinFull * m_config.armSwingY,
                                                      m_config.armPosZ + swingSin * m_config.armSwingZ));
        armModel = glm::rotate(armModel, glm::radians(m_config.armPitchDegrees - pitchLag * m_config.viewLagPitchDegrees) + swingSin * glm::radians(m_config.armSwingPitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
        armModel = glm::rotate(armModel, glm::radians(m_config.armYawDegrees - yawLag * m_config.viewLagYawDegrees) + swingSin * glm::radians(m_config.armSwingYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
        armModel = glm::rotate(armModel, glm::radians(m_config.armRollDegrees) + bobRoll + swingSinFull * glm::radians(m_config.armSwingRollDegrees), glm::vec3(0.0f, 0.0f, 1.0f));
        armModel = glm::scale(armModel, glm::vec3(m_config.armScale));
        m_preparedFrame = {PreparedDrawKind::Arm, view, viewProj, armModel, 0, width, height};
        return;
    }

    const ItemDef& itemDef = ItemRegistry::get(m_visibleItemId);
    const int itemTileIndex = m_resourceMgr->getItemTextureIndex(itemDef.iconTextureName);
    const BlockID renderBlock = ItemRegistry::toRenderBlock(m_visibleItemId);
    const bool preferBlockMesh = prefersBlockMeshForItem(renderBlock);
    const bool useItemMesh = !preferBlockMesh && itemTileIndex >= 0 && m_itemAtlasView.isValid();
    const bool useBlockMesh = !useItemMesh && renderBlock != 0 && m_blockTextureArrayView.isValid();

    const float pitchDegrees = useBlockMesh ? m_config.blockPitchDegrees : m_config.itemPitchDegrees;
    const float yawDegrees = useBlockMesh ? m_config.blockYawDegrees : m_config.itemYawDegrees;

    glm::mat4 itemModel(1.0f);
    itemModel = glm::translate(itemModel, glm::vec3(m_config.itemPosX + bobX + lagX + swingSin * m_config.itemSwingX,
                                                    m_config.itemPosY + bobY + lagY - equipDrop + swingSinFull * m_config.itemSwingY,
                                                    m_config.itemPosZ + swingSin * m_config.itemSwingZ));
    itemModel = glm::rotate(itemModel, glm::radians(pitchDegrees - pitchLag * m_config.viewLagPitchDegrees) + swingSin * glm::radians(m_config.itemSwingPitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
    itemModel = glm::rotate(itemModel, glm::radians(yawDegrees - yawLag * m_config.viewLagYawDegrees) + swingSin * glm::radians(m_config.itemSwingYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    itemModel = glm::rotate(itemModel, glm::radians(m_config.itemRollDegrees) + bobRoll + swingSinFull * glm::radians(m_config.itemSwingRollDegrees), glm::vec3(0.0f, 0.0f, 1.0f));

    if (useBlockMesh) {
        itemModel = glm::scale(itemModel, glm::vec3(m_config.blockScale));
    } else {
        itemModel = glm::scale(itemModel, glm::vec3(m_config.itemScale));
    }
    itemModel = glm::translate(itemModel, glm::vec3(-0.5f, -0.5f, -0.5f));

    if (!useItemMesh && !useBlockMesh) {
        return;
    }
    const PreparedDrawKind drawKind = useBlockMesh ? PreparedDrawKind::Block
                                                   : PreparedDrawKind::Item;
    m_preparedFrame = {drawKind, view, viewProj, itemModel, m_visibleItemId, width, height};
}

void FirstPersonHeldItemRenderer::prepareRhiFrame(RhiCommandList& commandList) {
    if (!m_initialized || !m_shadowUniformBuffer.isValid()) {
        std::abort();
    }

    ShadowUniforms uniforms;
    for (std::size_t index = 0u; index < uniforms.cascades.size(); ++index) {
        const float splitNear = index == 0u ? 0.0f : m_shadowData.cascadeSplitFar[index - 1u];
        uniforms.cascades[index].viewProj = m_shadowData.cascadeViewProj[index];
        uniforms.cascades[index].splitNearFarTexelResolution = {
            splitNear,
            m_shadowData.cascadeSplitFar[index],
            m_shadowData.cascadeTexelWorldSize[index],
            index >= 2u ? 0.5f : 1.0f
        };
        uniforms.cascades[index].depthExtentPadding.x = m_shadowData.cascadeDepthExtent[index];
    }
    uniforms.cameraPosShadowDistance = {
        m_shadowData.cameraPos, m_shadowData.shadowDistance
    };
    uniforms.sunDirectionConstantBias = {
        m_shadowData.sunDirection, m_shadowData.constantBias
    };
    uniforms.shadowParams = {
        m_shadowData.slopeBias,
        m_shadowData.normalOffset,
        m_shadowData.softness,
        m_shadowData.pcssStrength
    };
    uniforms.shadowFlags = {
        m_shadowData.cascadeCount,
        m_shadowData.softShadowsEnabled,
        m_shadowData.pcssShadowsEnabled,
        m_shadowData.shadowsEnabled
    };
    uniforms.lighting = {
        m_shadowData.skyIntensity,
        m_shadowData.ambientStrength,
        m_environmentSunlight,
        m_environmentBlockLight
    };
    uniforms.hdrScalePadding.x = m_sceneHdrScale;
    commandList.bufferBarrier({m_shadowUniformBuffer, RhiResourceState::UniformBuffer,
                               RhiResourceState::TransferDst});
    commandList.updateBuffer(m_shadowUniformBuffer, 0u, &uniforms, sizeof(uniforms));
    commandList.bufferBarrier({m_shadowUniformBuffer, RhiResourceState::TransferDst,
                               RhiResourceState::UniformBuffer});
}

void FirstPersonHeldItemRenderer::renderPrepared(RhiCommandList& commandList) {
    if (m_preparedFrame.kind == PreparedDrawKind::None) {
        return;
    }
    if (m_preparedFrame.kind == PreparedDrawKind::Arm) {
        struct PushConstants {
            glm::mat4 viewProj;
            glm::mat4 model;
            glm::vec4 lighting;
        };
        const PushConstants constants{
            m_preparedFrame.viewProj,
            m_preparedFrame.model,
            {m_environmentSunlight, m_environmentBlockLight,
             m_shadowData.skyIntensity, m_sceneHdrScale}
        };
        commandList.setViewport({0.0f, 0.0f,
            static_cast<float>(m_preparedFrame.width),
            static_cast<float>(m_preparedFrame.height), 0.0f, 0.08f});
        commandList.setScissor({0, 0, static_cast<uint32_t>(m_preparedFrame.width),
                                static_cast<uint32_t>(m_preparedFrame.height)});
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
        struct PushConstants {
            glm::mat4 viewProj;
            glm::mat4 model;
            glm::vec4 lighting;
        };
        const PushConstants constants{
            m_preparedFrame.viewProj,
            m_preparedFrame.model,
            {m_environmentSunlight, m_environmentBlockLight,
             m_shadowData.skyIntensity, m_sceneHdrScale}
        };
        commandList.setViewport({0.0f, 0.0f,
            static_cast<float>(m_preparedFrame.width),
            static_cast<float>(m_preparedFrame.height), 0.0f, 0.08f});
        commandList.setScissor({0, 0, static_cast<uint32_t>(m_preparedFrame.width),
                                static_cast<uint32_t>(m_preparedFrame.height)});
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
    if (blockId == 0 || meshIt == m_blockMeshes.end() ||
        !meshIt->second.rhiVertexBuffer.isValid() || meshIt->second.vertexCount == 0u) {
        std::abort();
    }
    struct PushConstants {
        glm::mat4 viewProj;
        glm::mat4 model;
        glm::vec4 lighting;
    };
    const PushConstants constants{
        m_preparedFrame.viewProj,
        m_preparedFrame.model,
        {m_environmentSunlight, m_environmentBlockLight,
         m_shadowData.skyIntensity, m_sceneHdrScale}
    };
    commandList.setViewport({0.0f, 0.0f,
        static_cast<float>(m_preparedFrame.width),
        static_cast<float>(m_preparedFrame.height), 0.0f, 0.08f});
    commandList.setScissor({0, 0, static_cast<uint32_t>(m_preparedFrame.width),
                            static_cast<uint32_t>(m_preparedFrame.height)});
    commandList.setGraphicsPipeline(m_blockPipeline);
    commandList.setBindGroup(0u, m_blockBindGroup);
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
    auto inserted = m_itemMeshes.emplace(itemId, std::move(mesh));
    return &inserted.first->second;
}

FirstPersonHeldItemRenderer::Mesh FirstPersonHeldItemRenderer::buildBlockMesh(const BlockID blockId) const {
    Mesh mesh;
    if (m_resourceMgr == nullptr || blockId == 0) {
        return mesh;
    }

    renderer::BlockCubeMesh shared = renderer::buildBlockCubeMesh(blockId, *m_resourceMgr);
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
    if (m_resourceMgr == nullptr || itemId == 0) {
        return mesh;
    }

    const ItemDef& itemDef = ItemRegistry::get(itemId);
    const int tileIndex = m_resourceMgr->getItemTextureIndex(itemDef.iconTextureName);
    if (tileIndex < 0) {
        return mesh;
    }

    std::vector<ItemModelVertex> vertices;
    if (!buildExtrudedItemMesh(m_resourceMgr->getItemTextureAtlas(),
                               m_resourceMgr->getItemTexturePixels(),
                               tileIndex,
                               vertices)) {
        return mesh;
    }

    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "FirstPerson.ItemMesh.VertexBuffer";
    bufferDesc.size = vertices.size() * sizeof(ItemModelVertex);
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    bufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    bufferDesc.initialState = RhiResourceState::VertexBuffer;
    mesh.rhiVertexBuffer = m_rhiDevice->createBuffer(
        bufferDesc, vertices.data(), vertices.size() * sizeof(ItemModelVertex));
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
    const FaceUvRect uv[6] = {
        pixelRectToUv(44, 16, 48, 20),
        pixelRectToUv(48, 16, 52, 20),
        pixelRectToUv(44, 20, 48, 32),
        pixelRectToUv(52, 20, 56, 32),
        pixelRectToUv(40, 20, 44, 32),
        pixelRectToUv(48, 20, 52, 32)
    };

    addSteveQuad(vertices, {xmin, ymax, zmax}, {xmax, ymax, zmax}, {xmax, ymax, zmin}, {xmin, ymax, zmin}, uv[0], {0.0f, 1.0f, 0.0f});
    addSteveQuad(vertices, {xmin, ymin, zmin}, {xmax, ymin, zmin}, {xmax, ymin, zmax}, {xmin, ymin, zmax}, uv[1], {0.0f, -1.0f, 0.0f});
    addSteveQuad(vertices, {xmin, ymin, zmax}, {xmax, ymin, zmax}, {xmax, ymax, zmax}, {xmin, ymax, zmax}, uv[2], {0.0f, 0.0f, 1.0f});
    addSteveQuad(vertices, {xmax, ymin, zmin}, {xmin, ymin, zmin}, {xmin, ymax, zmin}, {xmax, ymax, zmin}, uv[3], {0.0f, 0.0f, -1.0f});
    addSteveQuad(vertices, {xmin, ymin, zmin}, {xmin, ymin, zmax}, {xmin, ymax, zmax}, {xmin, ymax, zmin}, uv[4], {-1.0f, 0.0f, 0.0f});
    addSteveQuad(vertices, {xmax, ymin, zmax}, {xmax, ymin, zmin}, {xmax, ymax, zmin}, {xmax, ymax, zmax}, uv[5], {1.0f, 0.0f, 0.0f});

    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "FirstPerson.ArmMesh.VertexBuffer";
    bufferDesc.size = vertices.size() * sizeof(SteveVertex);
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    bufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    bufferDesc.initialState = RhiResourceState::VertexBuffer;
    mesh.rhiVertexBuffer = m_rhiDevice->createBuffer(
        bufferDesc, vertices.data(), vertices.size() * sizeof(SteveVertex));
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
