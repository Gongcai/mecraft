#include "FirstPersonHeldItemRenderer.h"

#include "../gl/GlStateGuard.h"
#include "../mesh/ItemModelMesh.h"
#include "../contracts/MecraftTextureContract.h"
#include "../core/Shader.h"
#include "../debug/RenderDebugLabels.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <fstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include "engine/platform/Window.h"
#include "../../Paths.h"
#include "../../player/Inventory.h"
#include "../../resource/ResourceMgr.h"
#include "../../world/chunk/SubChunk.h"

namespace {
constexpr std::array<std::array<glm::vec3, 4>, 6> kFaceCorners = {{
    {{{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}},
    {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}},
    {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}},
    {{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}},
    {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}},
    {{{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}}
}};

constexpr std::array<glm::vec3, 4> kCrossQuadA = {{{0.1464f, 0.0f, 0.1464f}, {0.8536f, 0.0f, 0.8536f}, {0.8536f, 1.0f, 0.8536f}, {0.1464f, 1.0f, 0.1464f}}};
constexpr std::array<glm::vec3, 4> kCrossQuadB = {{{0.8536f, 0.0f, 0.1464f}, {0.1464f, 0.0f, 0.8536f}, {0.1464f, 1.0f, 0.8536f}, {0.8536f, 1.0f, 0.1464f}}};
constexpr std::array<int, 6> kFaceIndices = {{0, 1, 2, 0, 2, 3}};

constexpr float kCrossBiomeTintMarker = -1.0f;
constexpr float kCrossFlowerMarker = -2.0f;
constexpr float kTorchModelPixel = 1.0f / 16.0f;
constexpr float kTorchModelCoreMin = 7.0f * kTorchModelPixel;
constexpr float kTorchModelCoreMax = 9.0f * kTorchModelPixel;
constexpr float kTorchModelCoreTop = 10.0f * kTorchModelPixel;
constexpr float kPi = 3.14159265358979323846f;

struct FaceUvRect {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
};

int getFaceTextureIndex(const BlockDef& def, const int face) {
    return def.getFaceLayer(face);
}

bool isTorchShape(const BlockDef& def) {
    return def.renderShapeName == "torch";
}

FaceUvRect makeTorchUvRect(const float left,
                           const float top,
                           const float right,
                           const float bottom) {
    return {
        left * kTorchModelPixel,
        1.0f - bottom * kTorchModelPixel,
        right * kTorchModelPixel,
        1.0f - top * kTorchModelPixel
    };
}

void emitTorchFace(std::vector<BlockVertex>& vertices,
                   const float layer,
                   const float normal,
                   const std::array<glm::vec3, 4>& corners,
                   const FaceUvRect& uvRect,
                   const uint8_t derivativeMaterialId) {
    const std::array<glm::vec2, 4> uv = {{
        {uvRect.u0, uvRect.v0},
        {uvRect.u1, uvRect.v0},
        {uvRect.u1, uvRect.v1},
        {uvRect.u0, uvRect.v1}
    }};

    for (const int idx : kFaceIndices) {
        const glm::vec3& pos = corners[static_cast<size_t>(idx)];
        const glm::vec2& uvCoord = uv[static_cast<size_t>(idx)];
        vertices.push_back(makeBlockVertex(pos.x,
                                           pos.y,
                                           pos.z,
                                           uvCoord.x,
                                           uvCoord.y,
                                           normal,
                                           1.0f,
                                           0.0f,
                                           3.0f,
                                           layer,
                                           1.0f,
                                           0.0f,
                                           0.0f,
                                           BlockTintKinds::NONE,
                                           0,
                                           0,
                                           derivativeMaterialId));
    }
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

void dumpShadowSamplerStateOnce(const char* label, const Shader& shader) {
#ifdef MECRAFT_DEBUG
    static std::unordered_set<std::string> dumpedLabels;
    if (!dumpedLabels.emplace(label).second) {
        return;
    }

    const char* names[] = {
        "uCsmShadowMap",
        "uCsmShadowDepthRaw",
        "uCsmShadowDepthAll",
        "uCsmShadowDepthAllRaw",
        "uCsmShadowColor0",
        "uCsmShadowColor1",
    };

    GLint previousProgram = 0;
    GLint previousActiveTexture = GL_TEXTURE0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);

    std::cerr << "[held-item-shadow] " << label << " program=" << shader.ID << '\n';
    for (const char* name : names) {
        const GLint loc = glGetUniformLocation(shader.ID, name);
        if (loc < 0) {
            std::cerr << "  " << name << " inactive\n";
            continue;
        }

        GLint unit = -1;
        glGetUniformiv(shader.ID, loc, &unit);
        GLint binding = 0;
        GLint compareMode = 0;
        GLint target = 0;
        glActiveTexture(GL_TEXTURE0 + unit);
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &binding);
        if (binding != 0) {
            glGetTextureParameteriv(static_cast<GLuint>(binding), GL_TEXTURE_COMPARE_MODE, &compareMode);
            glGetTextureLevelParameteriv(static_cast<GLuint>(binding), 0, GL_TEXTURE_INTERNAL_FORMAT, &target);
        }
        std::cerr << "  " << name
                  << " unit=" << unit
                  << " texture=" << binding
                  << " internal=0x" << std::hex << target << std::dec
                  << " compareMode=0x" << std::hex << compareMode << std::dec
                  << '\n';
    }

    glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    if (previousProgram != 0) {
        glUseProgram(static_cast<GLuint>(previousProgram));
    }
#else
    static_cast<void>(label);
    static_cast<void>(shader);
#endif
}
}

void FirstPersonHeldItemRenderer::init(ResourceMgr& resourceMgr) {
    if (m_initialized) {
        shutdown();
    }
    m_resourceMgr = &resourceMgr;
    m_deferredBlockShader = resourceMgr.getShader("block_item_lit");
    m_deferredItemShader = resourceMgr.getShader("item_model");
    m_deferredSteveShader = resourceMgr.getShader("steve");
    m_blockShader = m_deferredBlockShader;
    m_itemShader = m_deferredItemShader;
    m_steveShader = m_deferredSteveShader;
    loadConfig();
    m_initialized = true;
}

void FirstPersonHeldItemRenderer::shutdown() {
    if (!m_initialized && m_resourceMgr == nullptr && m_rightArmMesh.vao == 0 &&
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
    MecraftTextureContract::destroyFallbacks();
    m_resourceMgr = nullptr;
    m_blockShader = nullptr;
    m_itemShader = nullptr;
    m_steveShader = nullptr;
    m_deferredBlockShader = nullptr;
    m_deferredItemShader = nullptr;
    m_deferredSteveShader = nullptr;
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
    m_initialized = false;
}

void FirstPersonHeldItemRenderer::setForwardMode(bool forward) {
    if (m_resourceMgr == nullptr) return;
    if (forward) {
        Shader* fwdBlock = m_resourceMgr->getShader("block_item_forward");
        Shader* fwdItem = m_resourceMgr->getShader("item_model_forward");
        Shader* fwdSteve = m_resourceMgr->getShader("steve_forward");
        m_blockShader = fwdBlock ? fwdBlock : m_deferredBlockShader;
        m_itemShader = fwdItem ? fwdItem : m_deferredItemShader;
        m_steveShader = fwdSteve ? fwdSteve : m_deferredSteveShader;
    } else {
        m_blockShader = m_deferredBlockShader;
        m_itemShader = m_deferredItemShader;
        m_steveShader = m_deferredSteveShader;
    }
}

namespace {
float readJsonFloat(const nlohmann::json& json, const char* key, const float fallback) {
    if (!json.contains(key) || !json[key].is_number()) {
        return fallback;
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

    nlohmann::json json;
    try {
        file >> json;
    } catch (...) {
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

void FirstPersonHeldItemRenderer::setShadowData(const ShadowData& data) {
    m_shadowData = data;
}

FirstPersonHeldItemRenderer::ShadowData FirstPersonHeldItemRenderer::fromFirstPersonShadowData(const FirstPersonShadowData& sd) {
    ShadowData shadow{};
    for (int i = 0; i < 4; ++i) {
        shadow.cascadeViewProj[i] = sd.cascadeViewProj[i];
        shadow.cascadeSplitFar[i] = sd.cascadeSplitFar[i];
        shadow.cascadeTexelWorldSize[i] = sd.cascadeTexelWorldSize[i];
    }
    shadow.shadowTexture = sd.shadowTexture;
    shadow.shadowDepthRaw = sd.shadowDepthRaw;
    shadow.shadowDepthAll = sd.shadowDepthAll;
    shadow.shadowDepthAllRaw = sd.shadowDepthAllRaw;
    shadow.shadowColor0 = sd.shadowColor0;
    shadow.shadowColor1 = sd.shadowColor1;
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

void FirstPersonHeldItemRenderer::bindShadowUniforms(Shader& shader) const {
    const bool shadowInputsValid =
        m_shadowData.shadowTexture != 0 &&
        m_shadowData.shadowDepthRaw != 0 &&
        m_shadowData.shadowDepthAll != 0 &&
        m_shadowData.shadowDepthAllRaw != 0 &&
        m_shadowData.cascadeCount > 0;

    // Cascade data
    shader.setFloat("uShadowDistance", m_shadowData.shadowDistance);
    shader.setFloat("uShadowConstantBias", m_shadowData.constantBias);
    shader.setFloat("uShadowSlopeBias", m_shadowData.slopeBias);
    shader.setFloat("uShadowNormalOffset", m_shadowData.normalOffset);
    shader.setFloat("uShadowSoftness", m_shadowData.softness);
    shader.setFloat("uShadowPcssStrength", m_shadowData.pcssStrength);
    shader.setInt("uSoftShadowsEnabled", m_shadowData.softShadowsEnabled);
    shader.setInt("uPcssShadowsEnabled", m_shadowData.pcssShadowsEnabled);
    shader.setInt("uShadowsEnabled", (m_shadowData.shadowsEnabled != 0 && shadowInputsValid) ? 1 : 0);
    shader.setVec3("uCameraPos", m_shadowData.cameraPos);
    shader.setVec3("uSunDirection", m_shadowData.sunDirection);
    shader.setInt("uCsmCascadeCount", m_shadowData.cascadeCount);
    shader.setFloat("uSkyIntensity", m_shadowData.skyIntensity);
    shader.setFloat("uAmbientStrength", m_shadowData.ambientStrength);

    // Cascade matrices and split data
    static const std::string prefix_viewProj[] = {
        "uCsmCascades[0].viewProj", "uCsmCascades[1].viewProj",
        "uCsmCascades[2].viewProj", "uCsmCascades[3].viewProj"
    };
    static const std::string prefix_splitNear[] = {
        "uCsmCascades[0].splitNear", "uCsmCascades[1].splitNear",
        "uCsmCascades[2].splitNear", "uCsmCascades[3].splitNear"
    };
    static const std::string prefix_splitFar[] = {
        "uCsmCascades[0].splitFar", "uCsmCascades[1].splitFar",
        "uCsmCascades[2].splitFar", "uCsmCascades[3].splitFar"
    };
    static const std::string prefix_texelWorldSize[] = {
        "uCsmCascades[0].texelWorldSize", "uCsmCascades[1].texelWorldSize",
        "uCsmCascades[2].texelWorldSize", "uCsmCascades[3].texelWorldSize"
    };
    static const std::string prefix_resolutionScale[] = {
        "uCsmCascades[0].resolutionScale", "uCsmCascades[1].resolutionScale",
        "uCsmCascades[2].resolutionScale", "uCsmCascades[3].resolutionScale"
    };

    for (int i = 0; i < m_shadowData.cascadeCount && i < 4; ++i) {
        shader.setMat4(prefix_viewProj[i], m_shadowData.cascadeViewProj[i]);
        shader.setFloat(prefix_splitNear[i], i == 0 ? 0.0f : m_shadowData.cascadeSplitFar[i - 1]);
        shader.setFloat(prefix_splitFar[i], m_shadowData.cascadeSplitFar[i]);
        shader.setFloat(prefix_texelWorldSize[i], m_shadowData.cascadeTexelWorldSize[i]);
        shader.setFloat(prefix_resolutionScale[i], (i >= 2) ? 0.5f : 1.0f);
    }

    // Sampler unit assignment is handled by MecraftTextureContract::bindShadowSamplers().
}

void FirstPersonHeldItemRenderer::render(const Window& window,
                                         const Inventory& inventory,
                                         const FirstPersonHeldItemMotion& motion,
                                         const float timeSeconds) {
    render(window.getWidth(), window.getHeight(), inventory, motion, timeSeconds);
}

void FirstPersonHeldItemRenderer::render(const int width,
                                         const int height,
                                         const Inventory& inventory,
                                         const FirstPersonHeldItemMotion& motion,
                                         const float timeSeconds) {
    if (!m_initialized || m_resourceMgr == nullptr || m_steveShader == nullptr) {
        return;
    }
    if (width <= 0 || height <= 0) {
        return;
    }

    if (m_rightArmMesh.vao == 0) {
        m_rightArmMesh = buildRightArmMesh();
        if (m_rightArmMesh.vao == 0 || m_rightArmMesh.vertexCount == 0) {
            return;
        }
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

    const renderer::gl::ScopedStateSnapshot stateGuard;
    glViewport(0, 0, width, height);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    // View-model geometry is not in world space; keep it in front while still writing scene depth.
    glDepthFunc(GL_ALWAYS);
    glDepthRange(0.0, 0.08);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

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
        drawArm(viewProj, armModel);

        glBindVertexArray(0);
        return;
    }

    const ItemDef& itemDef = ItemRegistry::get(m_visibleItemId);
    const int itemTileIndex = m_resourceMgr->getItemTextureIndex(itemDef.iconTextureName);
    const TextureAtlas& itemAtlas = m_resourceMgr->getItemTextureAtlas();
    const TextureArray& texArray = m_resourceMgr->getTextureArray();
    const BlockID renderBlock = ItemRegistry::toRenderBlock(m_visibleItemId);
    const bool preferBlockMesh = (renderBlock != 0 && isTorchShape(BlockRegistry::get(renderBlock)));
    const bool useItemMesh = (!preferBlockMesh && itemTileIndex >= 0 && itemAtlas.textureID != 0 && m_itemShader != nullptr);
    const bool useBlockMesh = (!useItemMesh && renderBlock != 0 && texArray.textureID != 0 && m_blockShader != nullptr);

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

    drawItem(m_visibleItemId, view, viewProj, itemModel);

    glBindVertexArray(0);
}

void FirstPersonHeldItemRenderer::drawArm(const glm::mat4& viewProj,
                                          const glm::mat4& model) {
    if (m_resourceMgr == nullptr || m_steveShader == nullptr || m_rightArmMesh.vao == 0 || m_rightArmMesh.vertexCount == 0) {
        return;
    }

    const GLuint steveTex = m_resourceMgr->getGuiTexture("steve");
    if (steveTex == 0) {
        return;
    }

    m_steveShader->use();
    m_steveShader->setMat4("viewProj", viewProj);
    m_steveShader->setMat4("model", model);
    m_steveShader->setInt("uTexture", 0);
    m_steveShader->setFloat("uHeldSunlight", m_environmentSunlight);
    m_steveShader->setFloat("uHeldBlockLight", m_environmentBlockLight);
    bindShadowUniforms(*m_steveShader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, steveTex);
    // Shadow textures (units 5-10)
    MecraftTextureContract::ShadowTextureBundle shadowBundle{
        m_shadowData.shadowTexture != 0 ? m_shadowData.shadowTexture : MecraftTextureContract::fallbackDepthCompare(),
        m_shadowData.shadowDepthRaw != 0 ? m_shadowData.shadowDepthRaw : MecraftTextureContract::fallbackDepthRaw(),
        m_shadowData.shadowDepthAll != 0 ? m_shadowData.shadowDepthAll : MecraftTextureContract::fallbackDepthCompare(),
        m_shadowData.shadowDepthAllRaw != 0 ? m_shadowData.shadowDepthAllRaw : MecraftTextureContract::fallbackDepthRaw(),
        m_shadowData.shadowColor0 != 0 ? m_shadowData.shadowColor0 : MecraftTextureContract::fallbackColor0(),
        m_shadowData.shadowColor1 != 0 ? m_shadowData.shadowColor1 : MecraftTextureContract::fallbackColor1(),
    };
    MecraftTextureContract::bindShadowSamplers(m_steveShader->ID, 5, shadowBundle);
    glBindVertexArray(m_rightArmMesh.vao);
    {
        renderer::debug::ScopedDebugGroup group("HeldItem.Arm");
        dumpShadowSamplerStateOnce("HeldItem.Arm", *m_steveShader);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_rightArmMesh.vertexCount));
    }
}

void FirstPersonHeldItemRenderer::drawItem(const ItemID itemId,
                                           const glm::mat4& view,
                                           const glm::mat4& viewProj,
                                           const glm::mat4& model) {
    if (m_resourceMgr == nullptr || itemId == 0) {
        return;
    }

    const ItemDef& itemDef = ItemRegistry::get(itemId);
    const int itemTileIndex = m_resourceMgr->getItemTextureIndex(itemDef.iconTextureName);
    const TextureAtlas& itemAtlas = m_resourceMgr->getItemTextureAtlas();
    const TextureArray& texArray = m_resourceMgr->getTextureArray();
    const BlockID renderBlock = ItemRegistry::toRenderBlock(itemId);
    const bool preferBlockMesh = (renderBlock != 0 && isTorchShape(BlockRegistry::get(renderBlock)));
    const bool useItemMesh = (!preferBlockMesh && itemTileIndex >= 0 && itemAtlas.textureID != 0 && m_itemShader != nullptr);
    const bool useBlockMesh = (!useItemMesh && renderBlock != 0 && texArray.textureID != 0 && m_blockShader != nullptr);

    if (useBlockMesh) {
        Mesh* mesh = getOrCreateBlockMesh(renderBlock);
        if (mesh == nullptr || mesh->vao == 0 || mesh->vertexCount == 0) {
            return;
        }

        m_blockShader->use();
        m_blockShader->setMat4("model", model);
        m_blockShader->setInt("uUseModel", 1);
        m_blockShader->setInt("uVertexFormat", 0);
        m_blockShader->setMat4("view", view);
        m_blockShader->setMat4("viewProj", viewProj);
        m_blockShader->setFloat("uWindTime", 0.0f);
        m_blockShader->setFloat("uWindStrength", 0.0f);
        m_blockShader->setFloat("uWindSpeed", 0.0f);
        m_blockShader->setFloat("uWindSpatialFreq", 1.0f);
        m_blockShader->setInt("texArray", 0);
        m_blockShader->setInt("uForceBaseLod", 1);
        m_blockShader->setInt("uGrassColormap", 3);
        m_blockShader->setInt("uFoliageColormap", 4);
        m_blockShader->setInt("uFogEnabled", 0);
        m_blockShader->setInt("uDebugLightMode", 0);
        m_blockShader->setFloat("uSkyIntensity", 1.0f);
        m_blockShader->setFloat("uHeldSunlight", m_environmentSunlight);
        m_blockShader->setFloat("uHeldBlockLight", m_environmentBlockLight);
        m_blockShader->setFloat("uAnimationTime", 0.0f);
        m_blockShader->setInt("uLightmapDay", 1);
        m_blockShader->setInt("uLightmapNight", 2);
        bindShadowUniforms(*m_blockShader);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, texArray.textureID);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getLightmapDay());
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getLightmapNight());
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getGrassColormap());
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getFoliageColormap());
        // Shadow textures (units 5-10)
        MecraftTextureContract::ShadowTextureBundle shadowBundle{
            m_shadowData.shadowTexture != 0 ? m_shadowData.shadowTexture : MecraftTextureContract::fallbackDepthCompare(),
            m_shadowData.shadowDepthRaw != 0 ? m_shadowData.shadowDepthRaw : MecraftTextureContract::fallbackDepthRaw(),
            m_shadowData.shadowDepthAll != 0 ? m_shadowData.shadowDepthAll : MecraftTextureContract::fallbackDepthCompare(),
            m_shadowData.shadowDepthAllRaw != 0 ? m_shadowData.shadowDepthAllRaw : MecraftTextureContract::fallbackDepthRaw(),
            m_shadowData.shadowColor0 != 0 ? m_shadowData.shadowColor0 : MecraftTextureContract::fallbackColor0(),
            m_shadowData.shadowColor1 != 0 ? m_shadowData.shadowColor1 : MecraftTextureContract::fallbackColor1(),
        };
        MecraftTextureContract::bindShadowSamplers(m_blockShader->ID, 5, shadowBundle);
        glBindVertexArray(mesh->vao);
        {
            renderer::debug::ScopedDebugGroup group("HeldItem.Block");
            dumpShadowSamplerStateOnce("HeldItem.Block", *m_blockShader);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
        }
        m_blockShader->setInt("uUseModel", 0);
        m_blockShader->setInt("uVertexFormat", 1);
        return;
    }

    if (!useItemMesh) {
        return;
    }

    Mesh* mesh = getOrCreateItemMesh(itemId);
    if (mesh == nullptr || mesh->vao == 0 || mesh->vertexCount == 0) {
        return;
    }

    m_itemShader->use();
    m_itemShader->setMat4("model", model);
    m_itemShader->setMat4("viewProj", viewProj);
    m_itemShader->setInt("uAtlas", 0);
    m_itemShader->setFloat("uHeldSunlight", m_environmentSunlight);
    m_itemShader->setFloat("uHeldBlockLight", m_environmentBlockLight);
    bindShadowUniforms(*m_itemShader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, itemAtlas.textureID);
    // Shadow textures (units 5-10)
    MecraftTextureContract::ShadowTextureBundle shadowBundle{
        m_shadowData.shadowTexture != 0 ? m_shadowData.shadowTexture : MecraftTextureContract::fallbackDepthCompare(),
        m_shadowData.shadowDepthRaw != 0 ? m_shadowData.shadowDepthRaw : MecraftTextureContract::fallbackDepthRaw(),
        m_shadowData.shadowDepthAll != 0 ? m_shadowData.shadowDepthAll : MecraftTextureContract::fallbackDepthCompare(),
        m_shadowData.shadowDepthAllRaw != 0 ? m_shadowData.shadowDepthAllRaw : MecraftTextureContract::fallbackDepthRaw(),
        m_shadowData.shadowColor0 != 0 ? m_shadowData.shadowColor0 : MecraftTextureContract::fallbackColor0(),
        m_shadowData.shadowColor1 != 0 ? m_shadowData.shadowColor1 : MecraftTextureContract::fallbackColor1(),
    };
    MecraftTextureContract::bindShadowSamplers(m_itemShader->ID, 5, shadowBundle);
    glBindVertexArray(mesh->vao);
    {
        renderer::debug::ScopedDebugGroup group("HeldItem.Item");
        dumpShadowSamplerStateOnce("HeldItem.Item", *m_itemShader);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
    }
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

    const BlockDef& def = BlockRegistry::get(blockId);
    std::vector<BlockVertex> vertices;
    vertices.reserve(36);

    if (def.renderShape == BlockRenderShape::Cross) {
        int tileIndex = def.faceTop.firstLayer;
        if (tileIndex < 0) tileIndex = def.faceFront.firstLayer;
        if (tileIndex < 0) tileIndex = 0;

        const float layer = static_cast<float>(tileIndex);
        const std::array<glm::vec2, 4> quadUV = {{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
        uint8_t tintU = 0;
        uint8_t tintV = 0;
        computeDefaultBlockTintMapPosition(tintU, tintV);
        const uint8_t tintKind = blockTintKindFromBiomeTint(def.biomeTint);
        const float crossMarker = tintKind != BlockTintKinds::NONE ? kCrossBiomeTintMarker : kCrossFlowerMarker;

        const auto emitQuad = [&](const std::array<glm::vec3, 4>& corners) {
            for (const int idx : kFaceIndices) {
                const glm::vec3& pos = corners[static_cast<size_t>(idx)];
                const glm::vec2& uvCoord = quadUV[static_cast<size_t>(idx)];
                vertices.push_back(makeBlockVertex(pos.x,
                                                   pos.y,
                                                   pos.z,
                                                   uvCoord.x,
                                                   uvCoord.y,
                                                   crossMarker,
                                                   1.0f,
                                                   0.0f,
                                                   3.0f,
                                                   layer,
                                                   1.0f,
                                                   0.0f,
                                                   0.0f,
                                                   tintKind,
                                                   tintU,
                                                   tintV,
                                                   def.derivativeMaterialId));
            }
        };
        emitQuad(kCrossQuadA);
        emitQuad(kCrossQuadB);
    } else if (isTorchShape(def)) {
        int tileIndex = def.faceTop.firstLayer;
        if (tileIndex < 0) tileIndex = def.faceFront.firstLayer;
        if (tileIndex < 0) tileIndex = 0;

        const float layer = static_cast<float>(tileIndex);
        const FaceUvRect topUv = makeTorchUvRect(7.0f, 6.0f, 9.0f, 8.0f);
        const FaceUvRect bottomUv = makeTorchUvRect(7.0f, 13.0f, 9.0f, 15.0f);
        const FaceUvRect fullUv = makeTorchUvRect(0.0f, 0.0f, 16.0f, 16.0f);
        emitTorchFace(vertices, layer, 0.0f, {{{kTorchModelCoreMin, kTorchModelCoreTop, kTorchModelCoreMax}, {kTorchModelCoreMax, kTorchModelCoreTop, kTorchModelCoreMax}, {kTorchModelCoreMax, kTorchModelCoreTop, kTorchModelCoreMin}, {kTorchModelCoreMin, kTorchModelCoreTop, kTorchModelCoreMin}}}, topUv, def.derivativeMaterialId);
        emitTorchFace(vertices, layer, 1.0f, {{{kTorchModelCoreMin, 0.0f, kTorchModelCoreMin}, {kTorchModelCoreMax, 0.0f, kTorchModelCoreMin}, {kTorchModelCoreMax, 0.0f, kTorchModelCoreMax}, {kTorchModelCoreMin, 0.0f, kTorchModelCoreMax}}}, bottomUv, def.derivativeMaterialId);
        emitTorchFace(vertices, layer, 4.0f, {{{kTorchModelCoreMin, 0.0f, 0.0f}, {kTorchModelCoreMin, 0.0f, 1.0f}, {kTorchModelCoreMin, 1.0f, 1.0f}, {kTorchModelCoreMin, 1.0f, 0.0f}}}, fullUv, def.derivativeMaterialId);
        emitTorchFace(vertices, layer, 5.0f, {{{kTorchModelCoreMax, 0.0f, 1.0f}, {kTorchModelCoreMax, 0.0f, 0.0f}, {kTorchModelCoreMax, 1.0f, 0.0f}, {kTorchModelCoreMax, 1.0f, 1.0f}}}, fullUv, def.derivativeMaterialId);
        emitTorchFace(vertices, layer, 2.0f, {{{0.0f, 0.0f, kTorchModelCoreMax}, {1.0f, 0.0f, kTorchModelCoreMax}, {1.0f, 1.0f, kTorchModelCoreMax}, {0.0f, 1.0f, kTorchModelCoreMax}}}, fullUv, def.derivativeMaterialId);
        emitTorchFace(vertices, layer, 3.0f, {{{1.0f, 0.0f, kTorchModelCoreMin}, {0.0f, 0.0f, kTorchModelCoreMin}, {0.0f, 1.0f, kTorchModelCoreMin}, {1.0f, 1.0f, kTorchModelCoreMin}}}, fullUv, def.derivativeMaterialId);
    } else {
        for (int face = 0; face < 6; ++face) {
            int tileIndex = getFaceTextureIndex(def, face);
            if (tileIndex < 0) tileIndex = 0;

            const float layer = static_cast<float>(tileIndex);
            const std::array<glm::vec2, 4> faceUV = {{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
            uint8_t tintU = 0;
            uint8_t tintV = 0;
            computeDefaultBlockTintMapPosition(tintU, tintV);
            const uint8_t tintKind = blockTintKindFromBiomeTint(def.biomeTint);

            for (const int idx : kFaceIndices) {
                const glm::vec3& pos = kFaceCorners[static_cast<size_t>(face)][static_cast<size_t>(idx)];
                const glm::vec2& uvCoord = faceUV[static_cast<size_t>(idx)];
                vertices.push_back(makeBlockVertex(pos.x,
                                                   pos.y,
                                                   pos.z,
                                                   uvCoord.x,
                                                   uvCoord.y,
                                                   static_cast<float>(face),
                                                   1.0f,
                                                   0.0f,
                                                   3.0f,
                                                   layer,
                                                   1.0f,
                                                   0.0f,
                                                   0.0f,
                                                   tintKind,
                                                   tintU,
                                                   tintV,
                                                   def.derivativeMaterialId));
            }
        }
    }

    if (vertices.empty()) {
        return mesh;
    }

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    mesh.vertexCount = static_cast<uint32_t>(vertices.size());

    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(BlockVertex)),
                 vertices.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_BYTE, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, normal)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, sunlight)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, blockLight)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, ao)));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 1, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, layer)));
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 1, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, animationFrameCount)));
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, animationFps)));
    glEnableVertexAttribArray(9);
    glVertexAttribPointer(9, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, animated)));
    glEnableVertexAttribArray(10);
    glVertexAttribIPointer(10, 1, GL_UNSIGNED_SHORT, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, tintPacked)));
    for (GLuint attrib = 11; attrib <= 14; ++attrib) {
        glDisableVertexAttribArray(attrib);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
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

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    mesh.vertexCount = static_cast<uint32_t>(vertices.size());

    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(ItemModelVertex)),
                 vertices.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ItemModelVertex), reinterpret_cast<void*>(offsetof(ItemModelVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ItemModelVertex), reinterpret_cast<void*>(offsetof(ItemModelVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(ItemModelVertex), reinterpret_cast<void*>(offsetof(ItemModelVertex, shade)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(ItemModelVertex), reinterpret_cast<void*>(offsetof(ItemModelVertex, nx)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
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

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(SteveVertex)),
                 vertices.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SteveVertex), reinterpret_cast<void*>(offsetof(SteveVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SteveVertex), reinterpret_cast<void*>(offsetof(SteveVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(SteveVertex), reinterpret_cast<void*>(offsetof(SteveVertex, nx)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    return mesh;
}

void FirstPersonHeldItemRenderer::destroyMesh(Mesh& mesh) {
    if (mesh.vbo != 0) {
        glDeleteBuffers(1, &mesh.vbo);
        mesh.vbo = 0;
    }
    if (mesh.vao != 0) {
        glDeleteVertexArrays(1, &mesh.vao);
        mesh.vao = 0;
    }
    mesh.vertexCount = 0;
}
