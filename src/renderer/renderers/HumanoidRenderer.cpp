#include "HumanoidRenderer.h"
#include "../gl/GlStateGuard.h"
#include "../core/Shader.h"
#include "../debug/RenderDebugLabels.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <string>
#include <utility>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/camera/Camera.h"
#include "engine/platform/Window.h"
#include "../../resource/ResourceMgr.h"
#include "../../ecs/GameplayRegistry.h"
#include "../../ecs/entity/EntitySkinLayout.h"
#include "../../world/IWorldView.h"
#include "../../ecs/components/Components.h"
#include "../../ecs/components/NetworkComponents.h"
#include "../../world/World.h"
#include "../../world/chunk/Chunk.h"

namespace {
constexpr unsigned int kQuadIndices[] = {0, 1, 2, 0, 2, 3};

constexpr glm::vec3 kFaceNormals[] = {
    {0, 1, 0},   // top
    {0, -1, 0},  // bottom
    {0, 0, 1},   // front
    {0, 0, -1},  // back
    {-1, 0, 0},  // left
    {1, 0, 0}    // right
};

bool shouldRenderSteveRoot(const entt::registry& reg,
                           entt::entity steveRoot,
                           HumanoidRenderer::RenderMode mode) {
    return mode == HumanoidRenderer::kRenderAll
        || reg.all_of<ecs::EntityNetIdComponent>(steveRoot);
}

float hurtFlashForRoot(const entt::registry& reg, const entt::entity root) {
    const auto* hurt = reg.try_get<ecs::HurtEffectComponent>(root);
    if (hurt == nullptr || hurt->flashDurationSeconds <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(hurt->flashSecondsRemaining / hurt->flashDurationSeconds, 0.0f, 1.0f);
}

void setHurtFlash(Shader& shader, const float value) {
    const int location = shader.getUniformLocation("uHurtFlash");
    if (location >= 0) {
        glUniform1f(location, value);
    }
}

glm::mat4 applyMobVisualScale(const glm::mat4& model,
                              const glm::vec3& pivot,
                              const float scale) {
    assert(scale > 0.0f);
    if (std::abs(scale - 1.0f) <= 0.0001f) {
        return model;
    }

    return glm::translate(glm::mat4(1.0f), pivot) *
           glm::scale(glm::mat4(1.0f), glm::vec3(scale)) *
           glm::translate(glm::mat4(1.0f), -pivot) *
           model;
}

} // anonymous namespace

HumanoidRenderer::FaceUvRect HumanoidRenderer::pixelRectToUv(float x0, float y0, float x1, float y1,
                                                             float textureWidth, float textureHeight) {
    return {
        x0 / textureWidth,
        1.0f - y1 / textureHeight,
        x1 / textureWidth,
        1.0f - y0 / textureHeight
    };
}

HumanoidRenderer::PartMesh HumanoidRenderer::buildPartMesh(const renderer::HumanoidPartMeshDefinition& definition,
                                                           const float textureWidth,
                                                           const float textureHeight) const {
    PartMesh mesh;

    std::array<FaceUvRect, 6> uv{};
    for (std::size_t i = 0; i < uv.size(); ++i) {
        const renderer::HumanoidSkinPixelRect& rect = definition.faceUvs[i];
        uv[i] = pixelRectToUv(rect.x0, rect.y0, rect.x1, rect.y1, textureWidth, textureHeight);
    }

    const float ymin = -definition.halfHeight + definition.offsetY;
    const float ymax =  definition.halfHeight + definition.offsetY;
    const float xmin = -definition.halfWidth;
    const float xmax =  definition.halfWidth;
    const float zmin = -definition.halfDepth;
    const float zmax =  definition.halfDepth;

    struct FaceCorners {
        glm::vec3 pos[4];
    };

    const FaceCorners faces[6] = {
        // Top (+Y)
        {{{xmin, ymax, zmax}, {xmax, ymax, zmax}, {xmax, ymax, zmin}, {xmin, ymax, zmin}}},
        // Bottom (-Y)
        {{{xmin, ymin, zmin}, {xmax, ymin, zmin}, {xmax, ymin, zmax}, {xmin, ymin, zmax}}},
        // Front (+Z)
        {{{xmin, ymin, zmax}, {xmax, ymin, zmax}, {xmax, ymax, zmax}, {xmin, ymax, zmax}}},
        // Back (-Z)
        {{{xmax, ymin, zmin}, {xmin, ymin, zmin}, {xmin, ymax, zmin}, {xmax, ymax, zmin}}},
        // Left (-X)
        {{{xmin, ymin, zmin}, {xmin, ymin, zmax}, {xmin, ymax, zmax}, {xmin, ymax, zmin}}},
        // Right (+X)
        {{{xmax, ymin, zmax}, {xmax, ymin, zmin}, {xmax, ymax, zmin}, {xmax, ymax, zmax}}}
    };

    std::vector<SteveVertex> vertices;
    vertices.reserve(36);

    for (int f = 0; f < 6; ++f) {
        const glm::vec2 faceUvs[4] = {
            {uv[f].u0, uv[f].v0},
            {uv[f].u1, uv[f].v0},
            {uv[f].u1, uv[f].v1},
            {uv[f].u0, uv[f].v1}
        };

        for (int idx : kQuadIndices) {
            const auto& p = faces[f].pos[idx];
            const auto& t = faceUvs[idx];
            const auto& n = kFaceNormals[f];
            vertices.push_back({p.x, p.y, p.z, t.x, t.y, n.x, n.y, n.z});
        }
    }

    mesh.vertexCount = static_cast<uint32_t>(vertices.size());

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);

    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(SteveVertex)),
                 vertices.data(),
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SteveVertex),
                          reinterpret_cast<void*>(offsetof(SteveVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SteveVertex),
                          reinterpret_cast<void*>(offsetof(SteveVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(SteveVertex),
                          reinterpret_cast<void*>(offsetof(SteveVertex, nx)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    return mesh;
}

HumanoidRenderer::PartMesh HumanoidRenderer::buildEntityModelPartMesh(
    const ecs::EntityModelPartDefinition& definition,
    const float textureWidth,
    const float textureHeight) const {
    PartMesh mesh;

    std::vector<SteveVertex> vertices;
    for (const ecs::EntityModelBoxDefinition& box : definition.boxes) {
        std::array<FaceUvRect, 6> uv{};
        for (std::size_t i = 0; i < uv.size(); ++i) {
            const ecs::EntityModelPixelRect& rect = box.faceUvs[i];
            uv[i] = pixelRectToUv(rect.x0, rect.y0, rect.x1, rect.y1, textureWidth, textureHeight);
        }

        const float xmin = (box.origin.x - box.inflate) / 16.0f;
        const float ymin = (box.origin.y - box.inflate) / 16.0f;
        const float zmin = (box.origin.z - box.inflate) / 16.0f;
        const float xmax = (box.origin.x + box.size.x + box.inflate) / 16.0f;
        const float ymax = (box.origin.y + box.size.y + box.inflate) / 16.0f;
        const float zmax = (box.origin.z + box.size.z + box.inflate) / 16.0f;

        struct FaceCorners {
            glm::vec3 pos[4];
        };

        const FaceCorners faces[6] = {
            {{{xmin, ymax, zmax}, {xmax, ymax, zmax}, {xmax, ymax, zmin}, {xmin, ymax, zmin}}},
            {{{xmin, ymin, zmin}, {xmax, ymin, zmin}, {xmax, ymin, zmax}, {xmin, ymin, zmax}}},
            {{{xmin, ymin, zmax}, {xmax, ymin, zmax}, {xmax, ymax, zmax}, {xmin, ymax, zmax}}},
            {{{xmax, ymin, zmin}, {xmin, ymin, zmin}, {xmin, ymax, zmin}, {xmax, ymax, zmin}}},
            {{{xmin, ymin, zmin}, {xmin, ymin, zmax}, {xmin, ymax, zmax}, {xmin, ymax, zmin}}},
            {{{xmax, ymin, zmax}, {xmax, ymin, zmin}, {xmax, ymax, zmin}, {xmax, ymax, zmax}}}
        };

        for (int f = 0; f < 6; ++f) {
            const glm::vec2 faceUvs[4] = {
                {uv[f].u0, uv[f].v0},
                {uv[f].u1, uv[f].v0},
                {uv[f].u1, uv[f].v1},
                {uv[f].u0, uv[f].v1}
            };

            for (int idx : kQuadIndices) {
                const auto& p = faces[f].pos[idx];
                const auto& t = faceUvs[idx];
                const auto& n = kFaceNormals[f];
                vertices.push_back({p.x, p.y, p.z, t.x, t.y, n.x, n.y, n.z});
            }
        }
    }

    if (vertices.empty()) {
        return mesh;
    }

    mesh.vertexCount = static_cast<uint32_t>(vertices.size());

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);

    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(SteveVertex)),
                 vertices.data(),
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SteveVertex),
                          reinterpret_cast<void*>(offsetof(SteveVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SteveVertex),
                          reinterpret_cast<void*>(offsetof(SteveVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(SteveVertex),
                          reinterpret_cast<void*>(offsetof(SteveVertex, nx)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    return mesh;
}

void HumanoidRenderer::destroyMesh(PartMesh& mesh) {
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

HumanoidRenderer::PartMesh* HumanoidRenderer::getMeshForPart(ecs::StevePartType partType,
                                                              ecs::EntitySkinLayoutKind skinLayout) {
    return &m_skinLayoutMeshes[renderer::humanoidSkinLayoutIndex(skinLayout)]
                              [renderer::humanoidPartTypeIndex(partType)];
}

HumanoidRenderer::PartMesh* HumanoidRenderer::getMeshForEntityModelPart(const std::string& modelId,
                                                                         const std::string& partName) {
    const std::string key = modelId + "#" + partName;
    const auto existing = m_entityModelPartMeshes.find(key);
    if (existing != m_entityModelPartMeshes.end()) {
        return &existing->second;
    }

    const ecs::EntityModelDefinition* model = ecs::EntityModelRegistry::instance().findModel(modelId);
    if (model == nullptr) {
        return nullptr;
    }

    const ecs::EntityModelPartDefinition* part = model->findPart(partName);
    if (part == nullptr) {
        return nullptr;
    }

    PartMesh mesh = buildEntityModelPartMesh(*part, model->textureWidth, model->textureHeight);
    auto inserted = m_entityModelPartMeshes.emplace(key, std::move(mesh));
    return &inserted.first->second;
}

void HumanoidRenderer::init(ResourceMgr& resourceMgr) {
    m_resourceMgr = &resourceMgr;
    m_shader = resourceMgr.getShader("steve");
    m_forwardShader = resourceMgr.getShader("steve_forward");
    m_gbufferShader = resourceMgr.getShader("entity_gbuffer");
    m_shadowShader = resourceMgr.getShader("entity_shadow");

    const renderer::HumanoidSkinLayoutDefinitions& skinLayouts = renderer::humanoidSkinLayoutDefinitions();
    for (std::size_t layout = 0; layout < skinLayouts.size(); ++layout) {
        const renderer::HumanoidSkinLayoutDefinition& skinLayout = skinLayouts[layout];
        for (std::size_t part = 0; part < skinLayout.parts.size(); ++part) {
            m_skinLayoutMeshes[layout][part] = buildPartMesh(skinLayout.parts[part],
                                                             skinLayout.textureWidth,
                                                             skinLayout.textureHeight);
        }
    }
}

void HumanoidRenderer::shutdown() {
    for (auto& layoutMeshes : m_skinLayoutMeshes) {
        for (PartMesh& mesh : layoutMeshes) {
            destroyMesh(mesh);
        }
    }
    for (auto& pair : m_entityModelPartMeshes) {
        destroyMesh(pair.second);
    }
    m_entityModelPartMeshes.clear();
    if (m_fallbackShadowDepth != 0) {
        glDeleteTextures(1, &m_fallbackShadowDepth);
        m_fallbackShadowDepth = 0;
    }
    if (m_fallbackShadowDepthCompare != 0) {
        glDeleteTextures(1, &m_fallbackShadowDepthCompare);
        m_fallbackShadowDepthCompare = 0;
    }
    m_shader = nullptr;
    m_forwardShader = nullptr;
    m_resourceMgr = nullptr;
}

void HumanoidRenderer::ensureShadowFallbackTextures() {
    if (m_fallbackShadowDepth != 0 && m_fallbackShadowDepthCompare != 0) {
        return;
    }

    if (m_fallbackShadowDepth == 0) {
        glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &m_fallbackShadowDepth);
        glTextureStorage3D(m_fallbackShadowDepth, 1, GL_DEPTH_COMPONENT32F, 1, 1, 1);
        glTextureParameteri(m_fallbackShadowDepth, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(m_fallbackShadowDepth, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(m_fallbackShadowDepth, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_fallbackShadowDepth, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_fallbackShadowDepth, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        constexpr float depth = 1.0f;
        glClearTexImage(m_fallbackShadowDepth, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    }

    if (m_fallbackShadowDepthCompare == 0) {
        glGenTextures(1, &m_fallbackShadowDepthCompare);
        glTextureView(m_fallbackShadowDepthCompare,
                      GL_TEXTURE_2D_ARRAY,
                      m_fallbackShadowDepth,
                      GL_DEPTH_COMPONENT32F,
                      0,
                      1,
                      0,
                      1);
        glTextureParameteri(m_fallbackShadowDepthCompare, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(m_fallbackShadowDepthCompare, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(m_fallbackShadowDepthCompare, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_fallbackShadowDepthCompare, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_fallbackShadowDepthCompare, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTextureParameteri(m_fallbackShadowDepthCompare, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    }
}

void HumanoidRenderer::bindDisabledShadowFallback(Shader& shader) {
    ensureShadowFallbackTextures();

    shader.setInt("uShadowsEnabled", 0);
    shader.setInt("uSoftShadowsEnabled", 0);
    shader.setInt("uPcssShadowsEnabled", 0);
    shader.setInt("uCsmCascadeCount", 0);
    shader.setFloat("uShadowDistance", 0.0f);
    shader.setFloat("uShadowConstantBias", 0.0f);
    shader.setFloat("uShadowSlopeBias", 0.0f);
    shader.setFloat("uShadowNormalOffset", 0.0f);
    shader.setFloat("uShadowSoftness", 1.0f);
    shader.setFloat("uShadowPcssStrength", 0.0f);
    shader.setVec3("uCameraPos", 0.0f, 0.0f, 0.0f);
    shader.setVec3("uSunDirection", 0.25f, 0.9f, 0.35f);
    shader.setFloat("uAmbientStrength", 0.35f);
    shader.setInt("uCsmShadowMap", 5);
    shader.setInt("uCsmShadowDepthRaw", 6);

    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_fallbackShadowDepthCompare);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_fallbackShadowDepth);
}

void HumanoidRenderer::renderInventoryPreview(const float x,
                                              const float y,
                                              const float width,
                                              const float height,
                                              const float uiScale,
                                              const float pointerX,
                                              const float pointerY,
                                              const float timeSeconds) {
    if (m_shader == nullptr || m_resourceMgr == nullptr || uiScale <= 0.0f || width <= 0.0f || height <= 0.0f) {
        return;
    }

    const GLuint steveTex = m_resourceMgr->getGuiTexture("steve");
    if (steveTex == 0) {
        return;
    }

    const GLint viewportX = static_cast<GLint>(std::lround(x * uiScale));
    const GLint viewportY = static_cast<GLint>(std::lround(y * uiScale));
    const GLsizei viewportW = std::max<GLsizei>(1, static_cast<GLsizei>(std::lround(width * uiScale)));
    const GLsizei viewportH = std::max<GLsizei>(1, static_cast<GLsizei>(std::lround(height * uiScale)));

    const renderer::gl::ScopedStateSnapshot stateGuard;

    glViewport(viewportX, viewportY, viewportW, viewportH);
    glEnable(GL_SCISSOR_TEST);
    glScissor(viewportX, viewportY, viewportW, viewportH);
    glDepthMask(GL_TRUE);
    glClearDepth(1.0);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    const float aspect = static_cast<float>(viewportW) / static_cast<float>(viewportH);
    const glm::mat4 projection = glm::perspective(glm::radians(28.0f), aspect, 0.1f, 20.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, -0.02f, 4.9f),
                                       glm::vec3(0.0f, -0.02f, 0.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
    const float previewCenterX = x + width * 0.5f;
    const float previewCenterY = y + height * 0.58f;
    const float lookX = std::clamp((pointerX - previewCenterX) / std::max(1.0f, width * 1.2f), -1.0f, 1.0f);
    const float lookY = std::clamp((pointerY - previewCenterY) / std::max(1.0f, height * 1.2f), -1.0f, 1.0f);
    const float dt = m_inventoryPreviewLastTime >= 0.0f
        ? std::clamp(timeSeconds - m_inventoryPreviewLastTime, 0.0f, 0.1f)
        : 0.0f;
    m_inventoryPreviewLastTime = timeSeconds;

    const float headAlpha = (dt > 0.0f) ? (1.0f - std::exp(-dt * 18.0f)) : 1.0f;
    const float bodyAlpha = (dt > 0.0f) ? (1.0f - std::exp(-dt * 7.0f)) : 1.0f;
    m_inventoryPreviewHeadLookX += (lookX - m_inventoryPreviewHeadLookX) * headAlpha;
    m_inventoryPreviewHeadLookY += (lookY - m_inventoryPreviewHeadLookY) * headAlpha;
    m_inventoryPreviewBodyLookX += (lookX - m_inventoryPreviewBodyLookX) * bodyAlpha;
    m_inventoryPreviewBodyLookY += (lookY - m_inventoryPreviewBodyLookY) * bodyAlpha;

    const float bodyYaw = glm::radians(12.0f) * m_inventoryPreviewBodyLookX;
    const float bodyPitch = glm::radians(-5.0f) * m_inventoryPreviewBodyLookY;
    const float headYaw = glm::radians(28.0f) * m_inventoryPreviewHeadLookX;
    const float headPitch = glm::radians(-13.0f) * m_inventoryPreviewHeadLookY;

    const glm::mat4 root =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.96f, 0.0f))
        * glm::rotate(glm::mat4(1.0f), bodyYaw, glm::vec3(0.0f, 1.0f, 0.0f))
        * glm::rotate(glm::mat4(1.0f), bodyPitch, glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::mat4 torso = root * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.125f, 0.0f));

    const int modelLoc = m_shader->getUniformLocation("model");
    const int viewProjLoc = m_shader->getUniformLocation("viewProj");

    m_shader->use();
    m_shader->setMat4(viewProjLoc, projection * view);
    m_shader->setInt("uTexture", 0);
    setHurtFlash(*m_shader, 0.0f);
    bindDisabledShadowFallback(*m_shader);

    // Set lighting uniforms for UI preview (full bright, no world light sampling)
    m_shader->setFloat("uHeldSunlight", 1.0f);
    m_shader->setFloat("uHeldBlockLight", 0.0f);
    m_shader->setFloat("uSkyIntensity", 1.0f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, steveTex);

    const auto drawPart = [&](ecs::StevePartType partType, const glm::mat4& model) {
        PartMesh* mesh = getMeshForPart(partType, ecs::EntitySkinLayoutKind::Steve64x64);
        if (mesh == nullptr || mesh->vao == 0 || mesh->vertexCount == 0) {
            return;
        }
        m_shader->setMat4(modelLoc, model);
        glBindVertexArray(mesh->vao);
        {
            renderer::debug::ScopedDebugGroup group("UI.InventoryPreview.Steve");
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
        }
    };

    drawPart(ecs::StevePartType::Torso, torso);
    drawPart(ecs::StevePartType::Head,
             torso
             * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.375f, 0.0f))
             * glm::rotate(glm::mat4(1.0f), headYaw, glm::vec3(0.0f, 1.0f, 0.0f))
             * glm::rotate(glm::mat4(1.0f), headPitch, glm::vec3(1.0f, 0.0f, 0.0f)));
    drawPart(ecs::StevePartType::RightArm,
             torso * glm::translate(glm::mat4(1.0f), glm::vec3(-0.3125f, 0.375f, 0.0f)));
    drawPart(ecs::StevePartType::LeftArm,
             torso * glm::translate(glm::mat4(1.0f), glm::vec3(0.3125f, 0.375f, 0.0f)));
    drawPart(ecs::StevePartType::RightLeg,
             torso * glm::translate(glm::mat4(1.0f), glm::vec3(-0.125f, -0.375f, 0.0f)));
    drawPart(ecs::StevePartType::LeftLeg,
             torso * glm::translate(glm::mat4(1.0f), glm::vec3(0.125f, -0.375f, 0.0f)));

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    // GL state (including scissor) restored by ScopedStateSnapshot destructor
}

void HumanoidRenderer::drawGenericMobParts(entt::registry& reg,
                                           const entt::entity root,
                                           const ecs::MobVisualComponent& visual,
                                           const ecs::TransformComponent& rootTransform,
                                           Shader& shader,
                                           const int modelLoc,
                                           const int prevModelLoc) {
    const ecs::EntityModelComponent* modelComponent = reg.try_get<ecs::EntityModelComponent>(root);
    const ecs::ChildrenComponent* rootChildren = reg.try_get<ecs::ChildrenComponent>(root);
    if (modelComponent == nullptr || rootChildren == nullptr) {
        return;
    }

    std::vector<entt::entity> queue;
    queue.reserve(rootChildren->children.size());
    for (const entt::entity child : rootChildren->children) {
        queue.push_back(child);
    }

    std::size_t index = 0;
    while (index < queue.size()) {
        const entt::entity partEntity = queue[index++];
        if (const auto* children = reg.try_get<ecs::ChildrenComponent>(partEntity)) {
            for (const entt::entity child : children->children) {
                queue.push_back(child);
            }
        }

        if (!reg.all_of<ecs::EntityModelPartComponent, ecs::WorldTransformComponent>(partEntity)) {
            continue;
        }

        const auto& part = reg.get<ecs::EntityModelPartComponent>(partEntity);
        const auto& world = reg.get<ecs::WorldTransformComponent>(partEntity);
        PartMesh* mesh = getMeshForEntityModelPart(modelComponent->modelId, part.partName);
        if (mesh == nullptr || mesh->vao == 0 || mesh->vertexCount == 0) {
            continue;
        }

        const glm::mat4 model = applyMobVisualScale(world.worldMatrix,
                                                    rootTransform.position,
                                                    visual.scale);
        if (prevModelLoc >= 0) {
            const auto it = m_previousModelMatrices.find(partEntity);
            shader.setMat4(prevModelLoc, it != m_previousModelMatrices.end() ? it->second : model);
        }
        shader.setMat4(modelLoc, model);
        glBindVertexArray(mesh->vao);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
        m_previousModelMatrices[partEntity] = model;
    }
}

void HumanoidRenderer::drawEntities(ecs::GameplayRegistry& gameplayReg, Shader& shader,
                                     int modelLoc, int viewProjLoc, int prevModelLoc,
                                     const glm::mat4& viewProj, RenderMode mode) {
    auto& reg = gameplayReg.registry();

    shader.use();
    shader.setMat4(viewProjLoc, viewProj);
    shader.setInt("uTexture", 0);
    setHurtFlash(shader, 0.0f);

    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // ── Render Steve (player) entities ──
    const GLuint steveTex = m_resourceMgr->getGuiTexture("steve");
    if (steveTex != 0) {
        glBindTexture(GL_TEXTURE_2D, steveTex);

        auto steveView = reg.view<ecs::SteveTag, ecs::ChildrenComponent>();
        for (auto steveRoot : steveView) {
            if (!shouldRenderSteveRoot(reg, steveRoot, mode)) continue;

            setHurtFlash(shader, hurtFlashForRoot(reg, steveRoot));
            auto& rootChildren = reg.get<ecs::ChildrenComponent>(steveRoot);

            // Torso
            for (auto child : rootChildren.children) {
                if (!reg.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(child)) continue;
                auto& part = reg.get<ecs::StevePartComponent>(child);
                if (part.partType != ecs::StevePartType::Torso) continue;

                auto& world = reg.get<ecs::WorldTransformComponent>(child);
                PartMesh* mesh = getMeshForPart(ecs::StevePartType::Torso,
                                                ecs::EntitySkinLayoutKind::Steve64x64);
                if (mesh == nullptr || mesh->vao == 0) continue;

                if (prevModelLoc >= 0) {
                    auto it = m_previousModelMatrices.find(child);
                    shader.setMat4(prevModelLoc, it != m_previousModelMatrices.end() ? it->second : world.worldMatrix);
                }
                shader.setMat4(modelLoc, world.worldMatrix);
                glBindVertexArray(mesh->vao);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
                m_previousModelMatrices[child] = world.worldMatrix;
            }

            // Limbs (children of torso)
            for (auto child : rootChildren.children) {
                if (!reg.all_of<ecs::ChildrenComponent>(child)) continue;
                auto& partChildren = reg.get<ecs::ChildrenComponent>(child);

                for (auto partEntity : partChildren.children) {
                    if (!reg.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(partEntity)) continue;

                    auto& part = reg.get<ecs::StevePartComponent>(partEntity);
                    auto& world = reg.get<ecs::WorldTransformComponent>(partEntity);

                    PartMesh* mesh = getMeshForPart(part.partType,
                                                    ecs::EntitySkinLayoutKind::Steve64x64);
                    if (mesh == nullptr || mesh->vao == 0 || mesh->vertexCount == 0) continue;

                    if (prevModelLoc >= 0) {
                        auto it = m_previousModelMatrices.find(partEntity);
                        shader.setMat4(prevModelLoc, it != m_previousModelMatrices.end() ? it->second : world.worldMatrix);
                    }
                    shader.setMat4(modelLoc, world.worldMatrix);
                    glBindVertexArray(mesh->vao);
                    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
                    m_previousModelMatrices[partEntity] = world.worldMatrix;
                }
            }
        }
    }

    // ── Render Mob entities ──
    GLuint boundMobTex = 0;
    auto mobView = reg.view<ecs::MobTag,
                            ecs::ChildrenComponent,
                            ecs::MobVisualComponent,
                            ecs::TransformComponent>();
    for (auto mobRoot : mobView) {
        const auto& visual = mobView.get<ecs::MobVisualComponent>(mobRoot);
        const auto& rootTransform = mobView.get<ecs::TransformComponent>(mobRoot);
        const GLuint mobTex = m_resourceMgr->getGuiTexture(visual.textureKey);
        if (mobTex == 0) {
            continue;
        }
        if (boundMobTex != mobTex) {
            glBindTexture(GL_TEXTURE_2D, mobTex);
            boundMobTex = mobTex;
        }

        setHurtFlash(shader, hurtFlashForRoot(reg, mobRoot));
        auto& rootChildren = reg.get<ecs::ChildrenComponent>(mobRoot);
        if (reg.all_of<ecs::EntityModelComponent>(mobRoot)) {
            drawGenericMobParts(reg, mobRoot, visual, rootTransform, shader, modelLoc, prevModelLoc);
            continue;
        }

        // Torso
        for (auto child : rootChildren.children) {
            if (!reg.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(child)) continue;
            auto& part = reg.get<ecs::StevePartComponent>(child);
            if (part.partType != ecs::StevePartType::Torso) continue;

            auto& world = reg.get<ecs::WorldTransformComponent>(child);
            PartMesh* mesh = getMeshForPart(ecs::StevePartType::Torso,
                                            visual.skinLayout);
            if (mesh == nullptr || mesh->vao == 0) continue;

            const glm::mat4 model = applyMobVisualScale(world.worldMatrix,
                                                        rootTransform.position,
                                                        visual.scale);

            if (prevModelLoc >= 0) {
                auto it = m_previousModelMatrices.find(child);
                shader.setMat4(prevModelLoc, it != m_previousModelMatrices.end() ? it->second : model);
            }
            shader.setMat4(modelLoc, model);
            glBindVertexArray(mesh->vao);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
            m_previousModelMatrices[child] = model;
        }

        // Limbs
        for (auto child : rootChildren.children) {
            if (!reg.all_of<ecs::ChildrenComponent>(child)) continue;
            auto& partChildren = reg.get<ecs::ChildrenComponent>(child);

            for (auto partEntity : partChildren.children) {
                if (!reg.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(partEntity)) continue;

                auto& part = reg.get<ecs::StevePartComponent>(partEntity);
                auto& world = reg.get<ecs::WorldTransformComponent>(partEntity);

                PartMesh* mesh = getMeshForPart(part.partType,
                                                visual.skinLayout);
                if (mesh == nullptr || mesh->vao == 0 || mesh->vertexCount == 0) continue;

                const glm::mat4 model = applyMobVisualScale(world.worldMatrix,
                                                            rootTransform.position,
                                                            visual.scale);

                if (prevModelLoc >= 0) {
                    auto it = m_previousModelMatrices.find(partEntity);
                    shader.setMat4(prevModelLoc, it != m_previousModelMatrices.end() ? it->second : model);
                }
                shader.setMat4(modelLoc, model);
                glBindVertexArray(mesh->vao);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
                m_previousModelMatrices[partEntity] = model;
            }
        }
    }

    setHurtFlash(shader, 0.0f);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glActiveTexture(GL_TEXTURE0);
}

void HumanoidRenderer::drawEntities(const IWorldView& worldView, ecs::GameplayRegistry& gameplayReg,
                                     Shader& shader, int modelLoc, int viewProjLoc, int prevModelLoc,
                                     const glm::mat4& viewProj, RenderMode mode,
                                     const glm::vec3& cameraPos, float splitNear, float splitFar) {
    auto& reg = gameplayReg.registry();

    shader.use();
    shader.setMat4(viewProjLoc, viewProj);
    shader.setInt("uTexture", 0);
    setHurtFlash(shader, 0.0f);

    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // ── Render Steve (player) entities ──
    const GLuint steveTex = m_resourceMgr->getGuiTexture("steve");
    if (steveTex != 0) {
        glBindTexture(GL_TEXTURE_2D, steveTex);

        auto steveView = reg.view<ecs::SteveTag, ecs::ChildrenComponent>();
        for (auto steveRoot : steveView) {
            if (!shouldRenderSteveRoot(reg, steveRoot, mode)) continue;

            setHurtFlash(shader, hurtFlashForRoot(reg, steveRoot));
            auto& rootChildren = reg.get<ecs::ChildrenComponent>(steveRoot);

            // Query world light at torso position for this entity.
            glm::vec3 entityPos(0.0f);
            bool foundTorso = false;
            for (auto child : rootChildren.children) {
                if (!reg.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(child)) continue;
                auto& part = reg.get<ecs::StevePartComponent>(child);
                if (part.partType != ecs::StevePartType::Torso) continue;
                auto& wt = reg.get<ecs::WorldTransformComponent>(child);
                entityPos = glm::vec3(wt.worldMatrix[3]);
                glm::vec2 light = queryWorldLight(worldView, entityPos);
                shader.setFloat("uEntitySunlight", light.x);
                shader.setFloat("uEntityBlockLight", light.y);
                foundTorso = true;
                break;
            }

            // Cascade Culling: skip rendering if entity is outside this cascade's bounds (plus a small buffer)
            if (foundTorso && splitFar < FLT_MAX - 1.0f) {
                float dist = glm::length(entityPos - cameraPos);
                if (dist < splitNear - 4.0f || dist > splitFar + 4.0f) {
                    continue;
                }
            }

            // Torso
            for (auto child : rootChildren.children) {
                if (!reg.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(child)) continue;
                auto& part = reg.get<ecs::StevePartComponent>(child);
                if (part.partType != ecs::StevePartType::Torso) continue;

                auto& wt = reg.get<ecs::WorldTransformComponent>(child);
                PartMesh* mesh = getMeshForPart(ecs::StevePartType::Torso,
                                                ecs::EntitySkinLayoutKind::Steve64x64);
                if (mesh == nullptr || mesh->vao == 0) continue;

                if (prevModelLoc >= 0) {
                    auto it = m_previousModelMatrices.find(child);
                    shader.setMat4(prevModelLoc, it != m_previousModelMatrices.end() ? it->second : wt.worldMatrix);
                }
                shader.setMat4(modelLoc, wt.worldMatrix);
                glBindVertexArray(mesh->vao);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
                m_previousModelMatrices[child] = wt.worldMatrix;
            }

            // Limbs (children of torso)
            for (auto child : rootChildren.children) {
                if (!reg.all_of<ecs::ChildrenComponent>(child)) continue;
                auto& partChildren = reg.get<ecs::ChildrenComponent>(child);

                for (auto partEntity : partChildren.children) {
                    if (!reg.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(partEntity)) continue;

                    auto& part = reg.get<ecs::StevePartComponent>(partEntity);
                    auto& wt = reg.get<ecs::WorldTransformComponent>(partEntity);

                    PartMesh* mesh = getMeshForPart(part.partType,
                                                    ecs::EntitySkinLayoutKind::Steve64x64);
                    if (mesh == nullptr || mesh->vao == 0 || mesh->vertexCount == 0) continue;

                    if (prevModelLoc >= 0) {
                        auto it = m_previousModelMatrices.find(partEntity);
                        shader.setMat4(prevModelLoc, it != m_previousModelMatrices.end() ? it->second : wt.worldMatrix);
                    }
                    shader.setMat4(modelLoc, wt.worldMatrix);
                    glBindVertexArray(mesh->vao);
                    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
                    m_previousModelMatrices[partEntity] = wt.worldMatrix;
                }
            }
        }
    }

    // ── Render Mob entities ──
    GLuint boundMobTex = 0;
    auto mobView = reg.view<ecs::MobTag,
                            ecs::ChildrenComponent,
                            ecs::MobVisualComponent,
                            ecs::TransformComponent>();
    for (auto mobRoot : mobView) {
        const auto& visual = mobView.get<ecs::MobVisualComponent>(mobRoot);
        const auto& rootTransform = mobView.get<ecs::TransformComponent>(mobRoot);
        const GLuint mobTex = m_resourceMgr->getGuiTexture(visual.textureKey);
        if (mobTex == 0) {
            continue;
        }
        if (boundMobTex != mobTex) {
            glBindTexture(GL_TEXTURE_2D, mobTex);
            boundMobTex = mobTex;
        }

        setHurtFlash(shader, hurtFlashForRoot(reg, mobRoot));
        auto& rootChildren = reg.get<ecs::ChildrenComponent>(mobRoot);
        const bool genericModel = reg.all_of<ecs::EntityModelComponent>(mobRoot);

        // Query world light at the main visible volume for this entity.
        glm::vec3 entityPos = rootTransform.position + glm::vec3(0.0f, rootTransform.eyeHeight * 0.5f, 0.0f);
        bool foundTorso = false;
        if (genericModel) {
            const glm::vec2 light = queryWorldLight(worldView, entityPos);
            shader.setFloat("uEntitySunlight", light.x);
            shader.setFloat("uEntityBlockLight", light.y);
            foundTorso = true;
        } else {
            for (auto child : rootChildren.children) {
                if (!reg.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(child)) continue;
                auto& part = reg.get<ecs::StevePartComponent>(child);
                if (part.partType != ecs::StevePartType::Torso) continue;
                auto& wt = reg.get<ecs::WorldTransformComponent>(child);
                entityPos = glm::vec3(wt.worldMatrix[3]);
                glm::vec2 light = queryWorldLight(worldView, entityPos);
                shader.setFloat("uEntitySunlight", light.x);
                shader.setFloat("uEntityBlockLight", light.y);
                foundTorso = true;
                break;
            }
        }

        // Cascade Culling: skip rendering if entity is outside this cascade's bounds (plus a small buffer)
        if (foundTorso && splitFar < FLT_MAX - 1.0f) {
            float dist = glm::length(entityPos - cameraPos);
            if (dist < splitNear - 4.0f || dist > splitFar + 4.0f) {
                continue;
            }
        }

        if (genericModel) {
            drawGenericMobParts(reg, mobRoot, visual, rootTransform, shader, modelLoc, prevModelLoc);
            continue;
        }

        // Torso
        for (auto child : rootChildren.children) {
            if (!reg.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(child)) continue;
            auto& part = reg.get<ecs::StevePartComponent>(child);
            if (part.partType != ecs::StevePartType::Torso) continue;

            auto& wt = reg.get<ecs::WorldTransformComponent>(child);
            PartMesh* mesh = getMeshForPart(ecs::StevePartType::Torso,
                                            visual.skinLayout);
            if (mesh == nullptr || mesh->vao == 0) continue;

            const glm::mat4 model = applyMobVisualScale(wt.worldMatrix,
                                                        rootTransform.position,
                                                        visual.scale);

            if (prevModelLoc >= 0) {
                auto it = m_previousModelMatrices.find(child);
                shader.setMat4(prevModelLoc, it != m_previousModelMatrices.end() ? it->second : model);
            }
            shader.setMat4(modelLoc, model);
            glBindVertexArray(mesh->vao);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
            m_previousModelMatrices[child] = model;
        }

        // Limbs
        for (auto child : rootChildren.children) {
            if (!reg.all_of<ecs::ChildrenComponent>(child)) continue;
            auto& partChildren = reg.get<ecs::ChildrenComponent>(child);

            for (auto partEntity : partChildren.children) {
                if (!reg.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(partEntity)) continue;

                auto& part = reg.get<ecs::StevePartComponent>(partEntity);
                auto& wt = reg.get<ecs::WorldTransformComponent>(partEntity);

                PartMesh* mesh = getMeshForPart(part.partType,
                                                visual.skinLayout);
                if (mesh == nullptr || mesh->vao == 0 || mesh->vertexCount == 0) continue;

                const glm::mat4 model = applyMobVisualScale(wt.worldMatrix,
                                                            rootTransform.position,
                                                            visual.scale);

                if (prevModelLoc >= 0) {
                    auto it = m_previousModelMatrices.find(partEntity);
                    shader.setMat4(prevModelLoc, it != m_previousModelMatrices.end() ? it->second : model);
                }
                shader.setMat4(modelLoc, model);
                glBindVertexArray(mesh->vao);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
                m_previousModelMatrices[partEntity] = model;
            }
        }
    }

    setHurtFlash(shader, 0.0f);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glActiveTexture(GL_TEXTURE0);
}

void HumanoidRenderer::render(ecs::GameplayRegistry& gameplayReg, const Camera& camera,
                               const Window& window, RenderMode mode) {
    if (m_forwardShader == nullptr || m_resourceMgr == nullptr) return;

    const glm::mat4 viewProj = camera.getProjectionMatrix(window.getAspectRatio()) * camera.getViewMatrix();
    const int modelLoc = m_forwardShader->getUniformLocation("model");
    const int viewProjLoc = m_forwardShader->getUniformLocation("viewProj");

    drawEntities(gameplayReg, *m_forwardShader, modelLoc, viewProjLoc, -1, viewProj, mode);
    glDisable(GL_CULL_FACE);
}

void HumanoidRenderer::renderToGBuffer(ecs::GameplayRegistry& gameplayReg,
                                        const glm::mat4& jitteredViewProj,
                                        const glm::mat4& previousViewProj,
                                        RenderMode mode) {
    if (m_gbufferShader == nullptr || m_resourceMgr == nullptr) return;

    const int modelLoc = m_gbufferShader->getUniformLocation("model");
    const int viewProjLoc = m_gbufferShader->getUniformLocation("viewProj");
    const int prevModelLoc = m_gbufferShader->getUniformLocation("prevModel");

    // GBuffer FBO is already bound by the caller (Renderer).
    // Depth test/write enabled, blend disabled — set by caller.
    m_gbufferShader->use();
    m_gbufferShader->setMat4("prevViewProj", previousViewProj);
    m_gbufferShader->setInt("uForceZeroVelocity", 0);
    drawEntities(gameplayReg, *m_gbufferShader, modelLoc, viewProjLoc, prevModelLoc, jitteredViewProj, mode);
}

void HumanoidRenderer::renderToShadowMap(ecs::GameplayRegistry& gameplayReg,
                                          const glm::mat4& shadowViewProj,
                                          RenderMode mode) {
    if (m_shadowShader == nullptr || m_resourceMgr == nullptr) return;

    const int modelLoc = m_shadowShader->getUniformLocation("model");
    const int viewProjLoc = m_shadowShader->getUniformLocation("viewProj");

    // Shadow FBO is already bound by the caller (Renderer).
    // Depth test/write enabled, blend disabled — set by caller.
    drawEntities(gameplayReg, *m_shadowShader, modelLoc, viewProjLoc, -1, shadowViewProj, mode);
}

void HumanoidRenderer::renderToGBuffer(const IWorldView& worldView, ecs::GameplayRegistry& gameplayReg,
                                        const glm::mat4& jitteredViewProj,
                                        const glm::mat4& previousViewProj,
                                        RenderMode mode) {
    if (m_gbufferShader == nullptr || m_resourceMgr == nullptr) return;

    const int modelLoc = m_gbufferShader->getUniformLocation("model");
    const int viewProjLoc = m_gbufferShader->getUniformLocation("viewProj");
    const int prevModelLoc = m_gbufferShader->getUniformLocation("prevModel");

    m_gbufferShader->use();
    m_gbufferShader->setMat4("prevViewProj", previousViewProj);
    m_gbufferShader->setInt("uForceZeroVelocity", 0);
    drawEntities(worldView, gameplayReg, *m_gbufferShader, modelLoc, viewProjLoc, prevModelLoc, jitteredViewProj, mode);
}

void HumanoidRenderer::renderToShadowMap(const IWorldView& worldView, ecs::GameplayRegistry& gameplayReg,
                                          const glm::mat4& shadowViewProj,
                                          const glm::vec3& cameraPos, float splitNear, float splitFar,
                                          RenderMode mode) {
    if (m_shadowShader == nullptr || m_resourceMgr == nullptr) return;

    const int modelLoc = m_shadowShader->getUniformLocation("model");
    const int viewProjLoc = m_shadowShader->getUniformLocation("viewProj");

    drawEntities(worldView, gameplayReg, *m_shadowShader, modelLoc, viewProjLoc, -1, shadowViewProj, mode, cameraPos, splitNear, splitFar);
}

glm::vec2 HumanoidRenderer::queryWorldLight(const IWorldView& worldView, const glm::vec3& position) {
    const int bx = static_cast<int>(std::floor(position.x));
    const int by = static_cast<int>(std::floor(position.y));
    const int bz = static_cast<int>(std::floor(position.z));

    if (!worldView.isChunkLoadedForBlock(bx, by, bz)) {
        return {1.0f, 0.0f};
    }

    const glm::ivec2 cc = worldView.getChunkCoords(bx, bz);
    const auto& chunks = worldView.getActiveChunks();
    const auto it = chunks.find(IWorldView::chunkKey(cc.x, cc.y));
    if (it == chunks.end()) {
        return {1.0f, 0.0f};
    }

    const glm::ivec3 local = Chunk::worldToLocal(bx, by, bz);
    const uint8_t sun = it->second->getSunlight(local.x, local.y, local.z);
    const uint8_t block = it->second->getBlockLight(local.x, local.y, local.z);
    return {sun / 15.0f, block / 15.0f};
}
