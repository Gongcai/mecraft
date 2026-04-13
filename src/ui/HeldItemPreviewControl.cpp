#include "HeldItemPreviewControl.h"

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

namespace {
struct BlockVertex {
    float x;
    float y;
    float z;
    float u;
    float v;
    float normal;
    float windWeight;
    float layer;
};

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

constexpr float kCrossGrassMarker = -1.0f;
constexpr float kCrossFlowerMarker = -2.0f;
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
}

void HeldItemPreviewControl::init(ResourceMgr& resourceMgr)
{
    m_resourceMgr = &resourceMgr;
    m_shader = resourceMgr.getShader("chunk");
}

void HeldItemPreviewControl::shutdown()
{
    for (auto& pair : m_blockMeshes) {
        destroyMesh(pair.second);
    }
    m_blockMeshes.clear();
    m_shader = nullptr;
    m_resourceMgr = nullptr;
    m_hasPrevSample = false;
    m_prevTimeSeconds = 0.0f;
    m_motionBlend = 0.0f;
    m_swayX = 0.0f;
    m_swayY = 0.0f;
    m_actionAnimActive = false;
    m_actionAnimElapsed = 0.0f;
}

void HeldItemPreviewControl::render(const UIRenderContext& context) const
{
    if (!m_visible || !m_resourceMgr || !m_shader || !context.inventory) {
        return;
    }
    if (context.screenWidth <= 0 || context.screenHeight <= 0) {
        return;
    }

    const BlockID selectedBlock = context.inventory->getSelectedBlock();
    if (selectedBlock == BlockType::AIR) {
        return;
    }

    const TextureArray& texArray = m_resourceMgr->getTextureArray();
    if (texArray.textureID == 0) {
        return;
    }

    Mesh* mesh = const_cast<HeldItemPreviewControl*>(this)->getOrCreateBlockMesh(selectedBlock);
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

    const float targetBlend = context.heldItemPreviewMotion.moving ? 1.0f : 0.0f;
    const float blendSpeed = context.heldItemPreviewMotion.moving ? 10.0f : 8.0f;
    const float blendT = std::clamp(dt * blendSpeed, 0.0f, 1.0f);
    m_motionBlend += (targetBlend - m_motionBlend) * blendT;

    const float bobFrequency = std::max(0.0f, context.heldItemPreviewMotion.bobFrequency);
    const float phase = now * bobFrequency;
    const float sprintMul = context.heldItemPreviewMotion.sprinting ? 1.25f : 1.0f;

    const float targetSwayX = std::cos(phase + context.heldItemPreviewMotion.bobPhaseOffset) * m_layout.swayAmplitudeX * m_motionBlend * sprintMul;
    const float targetSwayY = std::sin(phase) * m_layout.swayAmplitudeY * m_motionBlend * sprintMul;

    const float swayT = std::clamp(dt * 18.0f, 0.0f, 1.0f);
    m_swayX += (targetSwayX - m_swayX) * swayT;
    m_swayY += (targetSwayY - m_swayY) * swayT;

    float actionPitchOffset = 0.0f;
    if (m_actionAnimActive) {
        m_actionAnimElapsed += dt;
        const float t = std::clamp(m_actionAnimElapsed / kActionAnimDurationSec, 0.0f, 1.0f);
        actionPitchOffset = -std::sin(t * kPi) * glm::radians(kActionPitchAmplitudeDeg);
        if (t >= 1.0f) {
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

    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    m_shader->use();
    m_shader->setMat4("model", model);
    m_shader->setMat4("view", view);
    m_shader->setMat4("viewProj", viewProj);
    m_shader->setFloat("uWindTime", 0.0f);
    m_shader->setFloat("uWindStrength", 0.0f);
    m_shader->setFloat("uWindSpeed", 0.0f);
    m_shader->setFloat("uWindSpatialFreq", 1.0f);
    m_shader->setInt("texArray", 0);
    m_shader->setInt("uForceBaseLod", 1);
    m_shader->setVec3("uGrassTintColor", glm::vec3(0.50f, 0.78f, 0.34f));
    m_shader->setInt("uFogEnabled", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texArray.textureID);
    glBindVertexArray(mesh->vao);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

UIEventResult HeldItemPreviewControl::onInput(const UIInputEvent&)
{
    return UIEventResult::Ignored;
}

bool HeldItemPreviewControl::isVisible() const
{
    return m_visible;
}

void HeldItemPreviewControl::setVisible(const bool visible)
{
    m_visible = visible;
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
    m_actionAnimActive = true;
    m_actionAnimElapsed = 0.0f;
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

HeldItemPreviewControl::Mesh HeldItemPreviewControl::buildBlockMesh(const BlockID blockId) const
{
    Mesh mesh;
    if (!m_resourceMgr || blockId == BlockType::AIR) {
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
        const float crossMarker = def.useGrassTint ? kCrossGrassMarker : kCrossFlowerMarker;

        const auto emitQuad = [&](const std::array<glm::vec3, 4>& corners) {
            for (const int idx : kFaceIndices) {
                const glm::vec3& pos = corners[idx];
                const glm::vec2& uvCoord = quadUV[idx];
                vertices.push_back({
                    pos.x,
                    pos.y,
                    pos.z,
                    uvCoord.x,
                    uvCoord.y,
                    crossMarker,
                    pos.y,
                    layer
                });
            }
        };

        emitQuad(kCrossQuadA);
        emitQuad(kCrossQuadB);
    } else {
        for (int face = 0; face < 6; ++face) {
            int tileIndex = getFaceTextureIndex(def, face);
            if (tileIndex < 0) {
                tileIndex = 0;
            }

            const float layer = static_cast<float>(tileIndex);
            const std::array<glm::vec2, 4> faceUV = {{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};

            for (const int idx : kFaceIndices) {
                const glm::vec3& pos = kFaceCorners[face][idx];
                const glm::vec2& uvCoord = faceUV[idx];
                vertices.push_back({
                    pos.x,
                    pos.y,
                    pos.z,
                    uvCoord.x,
                    uvCoord.y,
                    static_cast<float>(face),
                    0.0f,
                    layer
                });
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
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, normal)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, windWeight)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, layer)));

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

