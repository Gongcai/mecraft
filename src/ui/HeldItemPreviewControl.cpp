#include "HeldItemPreviewControl.h"

#include "../renderer/ItemModelMesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "../player/Inventory.h"
#include "../renderer/Shader.h"
#include "../resource/ResourceMgr.h"
#include "../item/Item.h"
#include "../world/SubChunk.h"

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
constexpr float kTorchU0 = 7.0f / 16.0f;
constexpr float kTorchU1 = 9.0f / 16.0f;
constexpr float kTorchV0 = 6.0f / 16.0f;
constexpr float kTorchV1 = 1.0f;
constexpr float kTorchTopV1 = 8.0f / 16.0f;
constexpr float kTorchHalfWidth = 1.0f / 16.0f;
constexpr float kTorchHeight = 10.0f / 16.0f;
constexpr float kActionAnimDurationSec = 0.380f;
constexpr float kActionPitchAmplitudeDeg = 18.0f;
constexpr float kPi = 6.28318530717958647692f / 2;

int getFaceTextureIndex(const BlockDef& def, const int face) {
    switch (face) {
        case 0: return def.texTop;
        case 1: return def.texBottom;
        case 2: return def.texFront;
        case 3: return def.texBack;
        case 4: return def.texLeft;
        case 5: return def.texRight;
        default: return 0;
    }
}

bool isTorchShape(const BlockDef& def) {
    return def.renderShapeName == "torch";
}
}

void HeldItemPreviewControl::init(ResourceMgr& resourceMgr)
{
    m_resourceMgr = &resourceMgr;
    m_shader = resourceMgr.getShader("block_item_lit");
    m_itemShader = resourceMgr.getShader("item_model");
}

void HeldItemPreviewControl::shutdown()
{
    for (auto& pair : m_blockMeshes) {
        destroyMesh(pair.second);
    }
    m_blockMeshes.clear();
    for (auto& pair : m_itemMeshes) {
        destroyMesh(pair.second);
    }
    m_itemMeshes.clear();
    m_shader = nullptr;
    m_itemShader = nullptr;
    m_resourceMgr = nullptr;
    m_hasPrevSample = false;
    m_prevTimeSeconds = 0.0f;
    m_motionBlend = 0.0f;
    m_swayX = 0.0f;
    m_swayY = 0.0f;
    m_actionAnimActive = false;
    m_actionAnimContinuous = false;
    m_actionAnimElapsed = 0.0f;
}

void HeldItemPreviewControl::renderSelf(const UIRenderContext& context) const
{
    if (!m_resourceMgr || !context.inventory) {
        return;
    }
    if (context.screenWidth <= 0 || context.screenHeight <= 0) {
        return;
    }

    const ItemID selectedItem = context.inventory->getSelectedItem();
    if (selectedItem == 0) {
        return;
    }

    const ItemDef& itemDef = ItemRegistry::get(selectedItem);
    const int itemTileIndex = m_resourceMgr->getItemTextureIndex(itemDef.iconTextureName);
    const TextureAtlas& itemAtlas = m_resourceMgr->getItemTextureAtlas();
    const TextureArray& texArray = m_resourceMgr->getTextureArray();
    const BlockID renderBlock = ItemRegistry::toRenderBlock(selectedItem);
    const bool preferBlockMesh = (renderBlock != 0 && isTorchShape(BlockRegistry::get(renderBlock)));

    const bool useItemMesh = (!preferBlockMesh && itemTileIndex >= 0 && itemAtlas.textureID != 0 && m_itemShader != nullptr);
    const bool useBlockMesh = (!useItemMesh && renderBlock != 0 && texArray.textureID != 0 && m_shader != nullptr);
    if (!useItemMesh && !useBlockMesh) {
        return;
    }

    Mesh* mesh = nullptr;
    if (useItemMesh) {
        mesh = const_cast<HeldItemPreviewControl*>(this)->getOrCreateItemMesh(selectedItem);
    } else {
        mesh = const_cast<HeldItemPreviewControl*>(this)->getOrCreateBlockMesh(renderBlock);
    }
    if (!mesh || mesh->vao == 0 || mesh->vertexCount == 0) {
        return;
    }

    const float screenW = static_cast<float>(context.screenWidth);
    const float screenH = static_cast<float>(context.screenHeight);

    const float centerX = screenW * m_layout.centerXRatio;
    const float centerY = screenH * m_layout.centerYRatio;
    const float sizePx = std::min(screenW, screenH) * m_layout.sizeRatio;
    const float scaleX = (sizePx * 2.0f) / screenW;
    const float scaleY = (sizePx * 2.0f) / screenH;
    const float scaleZ = std::min(scaleX, scaleY);

    const float yaw = glm::radians(m_layout.yawDegrees);
    const float basePitch = glm::radians(m_layout.pitchDegrees);

    const float now = context.timeSeconds;
    float dt = 0.0f;
    if (m_hasPrevSample) {
        dt = std::clamp(now - m_prevTimeSeconds, 0.0f, 0.1f);
    }
    m_prevTimeSeconds = now;
    m_hasPrevSample = true;

    constexpr HeldItemPreviewMotion motion{};
    const float targetBlend = motion.moving ? 1.0f : 0.0f;
    const float blendSpeed = motion.moving ? 10.0f : 8.0f;
    const float blendT = std::clamp(dt * blendSpeed, 0.0f, 1.0f);
    m_motionBlend += (targetBlend - m_motionBlend) * blendT;

    const float bobFrequency = std::max(0.0f, motion.bobFrequency);
    const float phase = now * bobFrequency;
    const float sprintMul = motion.sprinting ? 1.25f : 1.0f;

    const float targetSwayX = std::cos(phase + motion.bobPhaseOffset) * m_layout.swayAmplitudeX * m_motionBlend * sprintMul;
    const float targetSwayY = std::sin(phase) * m_layout.swayAmplitudeY * m_motionBlend * sprintMul;

    const float swayT = std::clamp(dt * 18.0f, 0.0f, 1.0f);
    m_swayX += (targetSwayX - m_swayX) * swayT;
    m_swayY += (targetSwayY - m_swayY) * swayT;

    float actionPitchOffset = 0.0f;
    if (m_actionAnimActive) {
        m_actionAnimElapsed += dt;
        if (m_actionAnimContinuous && m_actionAnimElapsed > kActionAnimDurationSec) {
            m_actionAnimElapsed = std::fmod(m_actionAnimElapsed, kActionAnimDurationSec);
        }

        const float t = std::clamp(m_actionAnimElapsed / kActionAnimDurationSec, 0.0f, 1.0f);
        actionPitchOffset = -std::sin(t * kPi) * glm::radians(kActionPitchAmplitudeDeg);
        if (!m_actionAnimContinuous && t >= 1.0f) {
            m_actionAnimActive = false;
            m_actionAnimElapsed = 0.0f;
        }
    }

    const float ndcX = (centerX / screenW) * 2.0f - 1.0f;
    const float ndcY = (centerY / screenH) * 2.0f - 1.0f;

    glm::mat4 model(1.0f);
    model = glm::translate(model, glm::vec3(ndcX + m_swayX, ndcY + m_swayY, 0.0f));
    model = glm::scale(model, glm::vec3(scaleX, scaleY, scaleZ));
    model = glm::rotate(model, basePitch + actionPitchOffset, glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::translate(model, glm::vec3(-0.5f, -0.5f, -0.5f));

    const glm::mat4 view(1.0f);
    const glm::mat4 viewProj = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, -2.0f, 2.0f);

    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    glActiveTexture(GL_TEXTURE0);
    if (useItemMesh) {
        m_itemShader->use();
        m_itemShader->setMat4("model", model);
        m_itemShader->setMat4("viewProj", viewProj);
        m_itemShader->setInt("uAtlas", 0);
        glBindTexture(GL_TEXTURE_2D, itemAtlas.textureID);
    } else {
        m_shader->use();
        m_shader->setMat4("model", model);
        m_shader->setInt("uUseModel", 1);
        m_shader->setMat4("view", view);
        m_shader->setMat4("viewProj", viewProj);
        m_shader->setFloat("uWindTime", 0.0f);
        m_shader->setFloat("uWindStrength", 0.0f);
        m_shader->setFloat("uWindSpeed", 0.0f);
        m_shader->setFloat("uWindSpatialFreq", 1.0f);
        m_shader->setInt("texArray", 0);
        m_shader->setInt("uForceBaseLod", 1);
        m_shader->setInt("uGrassColormap", 3);
        m_shader->setInt("uFoliageColormap", 4);
        m_shader->setInt("uFogEnabled", 0);
        m_shader->setFloat("uSkyIntensity", 1.0f);
        m_shader->setInt("uLightmapDay", 1);
        m_shader->setInt("uLightmapNight", 2);
        glBindTexture(GL_TEXTURE_2D_ARRAY, texArray.textureID);
        // Bind lightmap textures for held item rendering
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getLightmapDay());
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getLightmapNight());
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getGrassColormap());
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getFoliageColormap());
    }

    glBindVertexArray(mesh->vao);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));

    glBindVertexArray(0);
    if (useBlockMesh) {
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    if (useBlockMesh) {
        m_shader->setInt("uUseModel", 0);
    }

    // Restore UI depth/blend state for subsequent widgets
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void HeldItemPreviewControl::setLayout(const HeldItemPreviewLayout& layout)
{
    m_layout = layout;
    m_layout.centerXRatio = std::clamp(m_layout.centerXRatio, 0.0f, 1.0f);
    m_layout.centerYRatio = std::clamp(m_layout.centerYRatio, 0.0f, 1.0f);
    m_layout.sizeRatio = std::clamp(m_layout.sizeRatio, 0.02f, 0.6f);
    m_layout.swayAmplitudeX = std::clamp(m_layout.swayAmplitudeX, 0.0f, 0.08f);
    m_layout.swayAmplitudeY = std::clamp(m_layout.swayAmplitudeY, 0.0f, 0.08f);
}

const HeldItemPreviewLayout& HeldItemPreviewControl::getLayout() const
{
    return m_layout;
}

void HeldItemPreviewControl::triggerActionAnimation()
{
    m_actionAnimContinuous = false;
    m_actionAnimActive = true;
    m_actionAnimElapsed = 0.0f;
}

void HeldItemPreviewControl::setActionAnimationActive(const bool active)
{
    if (!active) {
        m_actionAnimContinuous = false;
        m_actionAnimActive = false;
        m_actionAnimElapsed = 0.0f;
        return;
    }

    m_actionAnimContinuous = true;
    m_actionAnimActive = true;
}

HeldItemPreviewControl::Mesh* HeldItemPreviewControl::getOrCreateBlockMesh(const BlockID blockId)
{
    const auto it = m_blockMeshes.find(blockId);
    if (it != m_blockMeshes.end()) {
        return &it->second;
    }

    Mesh mesh = buildBlockMesh(blockId);
    auto inserted = m_blockMeshes.emplace(blockId, std::move(mesh));
    return &inserted.first->second;
}

HeldItemPreviewControl::Mesh* HeldItemPreviewControl::getOrCreateItemMesh(const ItemID itemId)
{
    const auto it = m_itemMeshes.find(itemId);
    if (it != m_itemMeshes.end()) {
        return &it->second;
    }

    Mesh mesh = buildItemMesh(itemId);
    auto inserted = m_itemMeshes.emplace(itemId, std::move(mesh));
    return &inserted.first->second;
}

HeldItemPreviewControl::Mesh HeldItemPreviewControl::buildBlockMesh(const BlockID blockId) const
{
    Mesh mesh;
    if (!m_resourceMgr || blockId == 0) {
        return mesh;
    }

    const BlockDef& def = BlockRegistry::get(blockId);

    std::vector<BlockVertex> vertices;
    vertices.reserve(36);

    if (def.renderShape == BlockRenderShape::Cross) {
        int tileIndex = def.texTop;
        if (tileIndex < 0) {
            tileIndex = def.texFront;
        }
        if (tileIndex < 0) {
            tileIndex = 0;
        }

        const float layer = static_cast<float>(tileIndex);
        const std::array<glm::vec2, 4> quadUV = {{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
        uint8_t tintU = 0;
        uint8_t tintV = 0;
        computeDefaultBlockTintMapPosition(tintU, tintV);
        const uint8_t tintKind = blockTintKindFromBiomeTint(def.biomeTint);
        const float crossMarker = tintKind != BlockTintKinds::NONE ? kCrossBiomeTintMarker : kCrossFlowerMarker;

        const auto emitQuad = [&](const std::array<glm::vec3, 4>& corners) {
            for (const int idx : kFaceIndices) {
                const glm::vec3& pos = corners[idx];
                const glm::vec2& uvCoord = quadUV[idx];
                vertices.push_back(makeBlockVertex(
                    pos.x,
                    pos.y,
                    pos.z,
                    uvCoord.x,
                    uvCoord.y,
                    crossMarker,
                    1.0f,  // sunlight: full brightness for UI
                    0.0f,   // blockLight
                    3.0f,   // ao: no occlusion
                    layer,
                    1.0f,
                    0.0f,
                    0.0f,
                    tintKind,
                    tintU,
                    tintV
                ));
            }
        };

        emitQuad(kCrossQuadA);
        emitQuad(kCrossQuadB);
    } else if (isTorchShape(def)) {
        int tileIndex = def.texTop;
        if (tileIndex < 0) {
            tileIndex = def.texFront;
        }
        if (tileIndex < 0) {
            tileIndex = 0;
        }

        const float layer = static_cast<float>(tileIndex);
        const std::array<glm::vec2, 4> sideUV = {{
            {kTorchU0, kTorchV0},
            {kTorchU1, kTorchV0},
            {kTorchU1, kTorchV1},
            {kTorchU0, kTorchV1}
        }};
        const std::array<glm::vec2, 4> topUV = {{
            {kTorchU0, 14.0f / 16.0f},
            {kTorchU1, 14.0f / 16.0f},
            {kTorchU1, 1.0f},
            {kTorchU0, 1.0f}
        }};
        const float x0 = 0.5f - kTorchHalfWidth;
        const float x1 = 0.5f + kTorchHalfWidth;
        const float z0 = 0.5f - kTorchHalfWidth;
        const float z1 = 0.5f + kTorchHalfWidth;
        const float y0 = 0.0f;
        const float y1 = kTorchHeight;

        const auto emitFace = [&](const std::array<glm::vec3, 4>& corners,
                                  const float normal,
                                  const std::array<glm::vec2, 4>& uv) {
            for (const int idx : kFaceIndices) {
                const glm::vec3& pos = corners[idx];
                const glm::vec2& uvCoord = uv[idx];
                vertices.push_back(makeBlockVertex(
                    pos.x,
                    pos.y,
                    pos.z,
                    uvCoord.x,
                    uvCoord.y,
                    normal,
                    1.0f,
                    0.0f,
                    3.0f,
                    layer
                ));
            }
        };

        emitFace({{
            {x0, y1, z1},
            {x1, y1, z1},
            {x1, y1, z0},
            {x0, y1, z0}
        }}, 0.0f, topUV);
        emitFace({{
            {x0, y0, z0},
            {x1, y0, z0},
            {x1, y0, z1},
            {x0, y0, z1}
        }}, 1.0f, topUV);
        emitFace({{
            {x0, y0, z1},
            {x1, y0, z1},
            {x1, y1, z1},
            {x0, y1, z1}
        }}, 2.0f, sideUV);
        emitFace({{
            {x1, y0, z0},
            {x0, y0, z0},
            {x0, y1, z0},
            {x1, y1, z0}
        }}, 3.0f, sideUV);
        emitFace({{
            {x0, y0, z0},
            {x0, y0, z1},
            {x0, y1, z1},
            {x0, y1, z0}
        }}, 4.0f, sideUV);
        emitFace({{
            {x1, y0, z1},
            {x1, y0, z0},
            {x1, y1, z0},
            {x1, y1, z1}
        }}, 5.0f, sideUV);
    } else {
        for (int face = 0; face < 6; ++face) {
            int tileIndex = getFaceTextureIndex(def, face);
            if (tileIndex < 0) {
                tileIndex = 0;
            }

            const float layer = static_cast<float>(tileIndex);
            const std::array<glm::vec2, 4> faceUV = {{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
            uint8_t tintU = 0;
            uint8_t tintV = 0;
            computeDefaultBlockTintMapPosition(tintU, tintV);
            const uint8_t tintKind = blockTintKindFromBiomeTint(def.biomeTint);

            for (const int idx : kFaceIndices) {
                const glm::vec3& pos = kFaceCorners[face][idx];
                const glm::vec2& uvCoord = faceUV[idx];
                vertices.push_back(makeBlockVertex(
                    pos.x,
                    pos.y,
                    pos.z,
                    uvCoord.x,
                    uvCoord.y,
                    static_cast<float>(face),
                    1.0f,  // sunlight: full brightness for UI
                    0.0f,   // blockLight
                    3.0f,   // ao: no occlusion
                    layer,
                    1.0f,
                    0.0f,
                    0.0f,
                    tintKind,
                    tintU,
                    tintV
                ));
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

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    return mesh;
}

HeldItemPreviewControl::Mesh HeldItemPreviewControl::buildItemMesh(const ItemID itemId) const
{
    Mesh mesh;
    if (!m_resourceMgr || itemId == 0) {
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

void HeldItemPreviewControl::destroyMesh(Mesh& mesh)
{
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

