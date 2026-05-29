#include "HumanoidRenderer.h"
#include "../gl/GlStateGuard.h"
#include "../core/Shader.h"
#include "../debug/RenderDebugLabels.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/camera/Camera.h"
#include "engine/platform/Window.h"
#include "../../resource/ResourceMgr.h"
#include "../../ecs/GameplayRegistry.h"
#include "../../ecs/components/Components.h"
#include "../../world/World.h"
#include "../../world/chunk/Chunk.h"

namespace {
constexpr float SKIN_W = 64.0f;
constexpr float SKIN_H = 64.0f;

constexpr unsigned int kQuadIndices[] = {0, 1, 2, 0, 2, 3};

constexpr glm::vec3 kFaceNormals[] = {
    {0, 1, 0},   // top
    {0, -1, 0},  // bottom
    {0, 0, 1},   // front
    {0, 0, -1},  // back
    {-1, 0, 0},  // left
    {1, 0, 0}    // right
};

} // anonymous namespace

HumanoidRenderer::FaceUvRect HumanoidRenderer::pixelRectToUv(float x0, float y0, float x1, float y1) {
    return {
        x0 / SKIN_W,
        1.0f - y1 / SKIN_H,
        x1 / SKIN_W,
        1.0f - y0 / SKIN_H
    };
}

HumanoidRenderer::PartMesh HumanoidRenderer::buildBoxMesh(float hw, float hh, float hd,
                                                           float offsetY,
                                                           const FaceUvRect uv[6]) const {
    PartMesh mesh;

    const float ymin = -hh + offsetY;
    const float ymax =  hh + offsetY;
    const float xmin = -hw;
    const float xmax =  hw;
    const float zmin = -hd;
    const float zmax =  hd;

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

HumanoidRenderer::PartMesh HumanoidRenderer::buildHeadMesh() const {
    const float hw = 0.25f, hh = 0.25f, hd = 0.25f;
    const FaceUvRect uv[6] = {
        pixelRectToUv(8, 0, 16, 8),
        pixelRectToUv(16, 0, 24, 8),
        pixelRectToUv(8, 8, 16, 16),
        pixelRectToUv(24, 8, 32, 16),
        pixelRectToUv(0, 8, 8, 16),
        pixelRectToUv(16, 8, 24, 16)
    };
    return buildBoxMesh(hw, hh, hd, hh, uv);
}

HumanoidRenderer::PartMesh HumanoidRenderer::buildTorsoMesh() const {
    const float hw = 0.25f, hh = 0.375f, hd = 0.125f;
    const FaceUvRect uv[6] = {
        pixelRectToUv(20, 16, 28, 20),
        pixelRectToUv(28, 16, 36, 20),
        pixelRectToUv(20, 20, 28, 32),
        pixelRectToUv(32, 20, 40, 32),
        pixelRectToUv(16, 20, 20, 32),
        pixelRectToUv(28, 20, 32, 32)
    };
    return buildBoxMesh(hw, hh, hd, 0.0f, uv);
}

HumanoidRenderer::PartMesh HumanoidRenderer::buildRightArmMesh() const {
    const float hw = 0.125f, hh = 0.375f, hd = 0.125f;
    const FaceUvRect uv[6] = {
        pixelRectToUv(44, 16, 48, 20),
        pixelRectToUv(48, 16, 52, 20),
        pixelRectToUv(44, 20, 48, 32),
        pixelRectToUv(52, 20, 56, 32),
        pixelRectToUv(40, 20, 44, 32),
        pixelRectToUv(48, 20, 52, 32)
    };
    return buildBoxMesh(hw, hh, hd, -hh, uv);
}

HumanoidRenderer::PartMesh HumanoidRenderer::buildLeftArmMesh() const {
    const float hw = 0.125f, hh = 0.375f, hd = 0.125f;
    const FaceUvRect uv[6] = {
        pixelRectToUv(36, 48, 40, 52),
        pixelRectToUv(40, 48, 44, 52),
        pixelRectToUv(36, 52, 40, 64),
        pixelRectToUv(44, 52, 48, 64),
        pixelRectToUv(32, 52, 36, 64),
        pixelRectToUv(40, 52, 44, 64)
    };
    return buildBoxMesh(hw, hh, hd, -hh, uv);
}

HumanoidRenderer::PartMesh HumanoidRenderer::buildRightLegMesh() const {
    const float hw = 0.125f, hh = 0.375f, hd = 0.125f;
    const FaceUvRect uv[6] = {
        pixelRectToUv(4, 16, 8, 20),
        pixelRectToUv(8, 16, 12, 20),
        pixelRectToUv(4, 20, 8, 32),
        pixelRectToUv(12, 20, 16, 32),
        pixelRectToUv(0, 20, 4, 32),
        pixelRectToUv(8, 20, 12, 32)
    };
    return buildBoxMesh(hw, hh, hd, -hh, uv);
}

HumanoidRenderer::PartMesh HumanoidRenderer::buildLeftLegMesh() const {
    const float hw = 0.125f, hh = 0.375f, hd = 0.125f;
    const FaceUvRect uv[6] = {
        pixelRectToUv(20, 48, 24, 52),
        pixelRectToUv(24, 48, 28, 52),
        pixelRectToUv(20, 52, 24, 64),
        pixelRectToUv(28, 52, 32, 64),
        pixelRectToUv(16, 52, 20, 64),
        pixelRectToUv(24, 52, 28, 64)
    };
    return buildBoxMesh(hw, hh, hd, -hh, uv);
}

// ── Mob mirrored meshes (64x32 skin: left limbs = mirrored right limbs) ──

HumanoidRenderer::PartMesh HumanoidRenderer::buildMirroredArmMesh() const {
    // Mirror of right arm: X negated, UV left/right faces swapped.
    // Negating X automatically reverses winding → normals stay outward.
    PartMesh mesh;

    const float hw = 0.125f, hh = 0.375f, hd = 0.125f;
    const float offsetY = -hh;
    const float ymin = -hh + offsetY;
    const float ymax =  hh + offsetY;

    // Right arm UV (same pixel regions as player right arm)
    const FaceUvRect rightArmUv[6] = {
        pixelRectToUv(44, 16, 48, 20),   // top
        pixelRectToUv(48, 16, 52, 20),   // bottom
        pixelRectToUv(44, 20, 48, 32),   // front
        pixelRectToUv(52, 20, 56, 32),   // back
        pixelRectToUv(40, 20, 44, 32),   // left(-X) → becomes right face after mirror
        pixelRectToUv(48, 20, 52, 32)    // right(+X) → becomes left face after mirror
    };

    // UV with left/right swapped for the mirrored mesh
    const FaceUvRect uv[6] = {
        rightArmUv[0], rightArmUv[1], rightArmUv[2], rightArmUv[3],
        rightArmUv[5], rightArmUv[4]   // swapped: left←right, right←left
    };

    // X-negated face corners — same winding order as original (negate X auto-reverses winding)
    struct FaceCorners { glm::vec3 pos[4]; };
    const FaceCorners faces[6] = {
        {{{-hw, ymax,  hd}, { hw, ymax,  hd}, { hw, ymax, -hd}, {-hw, ymax, -hd}}},  // top
        {{{-hw, ymin, -hd}, { hw, ymin, -hd}, { hw, ymin,  hd}, {-hw, ymin,  hd}}},  // bottom
        {{{-hw, ymin,  hd}, { hw, ymin,  hd}, { hw, ymax,  hd}, {-hw, ymax,  hd}}},  // front
        {{{ hw, ymin, -hd}, {-hw, ymin, -hd}, {-hw, ymax, -hd}, { hw, ymax, -hd}}},  // back
        {{{-hw, ymin, -hd}, {-hw, ymin,  hd}, {-hw, ymax,  hd}, {-hw, ymax, -hd}}},  // left → now right(+X)
        {{{ hw, ymin,  hd}, { hw, ymin, -hd}, { hw, ymax, -hd}, { hw, ymax,  hd}}}   // right → now left(-X)
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
                 vertices.data(), GL_STATIC_DRAW);
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

HumanoidRenderer::PartMesh HumanoidRenderer::buildMirroredLegMesh() const {
    // Mirror of right leg: X negated, UV left/right faces swapped.
    // Negating X automatically reverses winding → normals stay outward.
    PartMesh mesh;

    const float hw = 0.125f, hh = 0.375f, hd = 0.125f;
    const float offsetY = -hh;
    const float ymin = -hh + offsetY;
    const float ymax =  hh + offsetY;

    const FaceUvRect rightLegUv[6] = {
        pixelRectToUv(4, 16, 8, 20),
        pixelRectToUv(8, 16, 12, 20),
        pixelRectToUv(4, 20, 8, 32),
        pixelRectToUv(12, 20, 16, 32),
        pixelRectToUv(0, 20, 4, 32),
        pixelRectToUv(8, 20, 12, 32)
    };

    const FaceUvRect uv[6] = {
        rightLegUv[0], rightLegUv[1], rightLegUv[2], rightLegUv[3],
        rightLegUv[5], rightLegUv[4]
    };

    struct FaceCorners { glm::vec3 pos[4]; };
    const FaceCorners faces[6] = {
        {{{-hw, ymax,  hd}, { hw, ymax,  hd}, { hw, ymax, -hd}, {-hw, ymax, -hd}}},
        {{{-hw, ymin, -hd}, { hw, ymin, -hd}, { hw, ymin,  hd}, {-hw, ymin,  hd}}},
        {{{-hw, ymin,  hd}, { hw, ymin,  hd}, { hw, ymax,  hd}, {-hw, ymax,  hd}}},
        {{{ hw, ymin, -hd}, {-hw, ymin, -hd}, {-hw, ymax, -hd}, { hw, ymax, -hd}}},
        {{{-hw, ymin, -hd}, {-hw, ymin,  hd}, {-hw, ymax,  hd}, {-hw, ymax, -hd}}},
        {{{ hw, ymin,  hd}, { hw, ymin, -hd}, { hw, ymax, -hd}, { hw, ymax,  hd}}}
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
                 vertices.data(), GL_STATIC_DRAW);
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
                                                              ecs::SkinTypeComponent::Type skinType) {
    switch (partType) {
    case ecs::StevePartType::Torso:     return &m_torsoMesh;
    case ecs::StevePartType::Head:      return &m_headMesh;
    case ecs::StevePartType::RightArm:  return &m_rightArmMesh;
    case ecs::StevePartType::RightLeg:  return &m_rightLegMesh;
    case ecs::StevePartType::LeftArm:
        return (skinType == ecs::SkinTypeComponent::Type::Mob) ? &m_mobLeftArmMesh : &m_leftArmMesh;
    case ecs::StevePartType::LeftLeg:
        return (skinType == ecs::SkinTypeComponent::Type::Mob) ? &m_mobLeftLegMesh : &m_leftLegMesh;
    default: return nullptr;
    }
}

void HumanoidRenderer::init(ResourceMgr& resourceMgr) {
    m_resourceMgr = &resourceMgr;
    m_shader = resourceMgr.getShader("steve");
    m_gbufferShader = resourceMgr.getShader("entity_gbuffer");
    m_shadowShader = resourceMgr.getShader("entity_shadow");

    // Player meshes (64x64 skin)
    m_headMesh = buildHeadMesh();
    m_torsoMesh = buildTorsoMesh();
    m_rightArmMesh = buildRightArmMesh();
    m_leftArmMesh = buildLeftArmMesh();
    m_rightLegMesh = buildRightLegMesh();
    m_leftLegMesh = buildLeftLegMesh();

    // Mob meshes (64x32 skin — left limbs are mirrored right limbs)
    m_mobLeftArmMesh = buildMirroredArmMesh();
    m_mobLeftLegMesh = buildMirroredLegMesh();
}

void HumanoidRenderer::shutdown() {
    destroyMesh(m_headMesh);
    destroyMesh(m_torsoMesh);
    destroyMesh(m_rightArmMesh);
    destroyMesh(m_leftArmMesh);
    destroyMesh(m_rightLegMesh);
    destroyMesh(m_leftLegMesh);
    destroyMesh(m_mobLeftArmMesh);
    destroyMesh(m_mobLeftLegMesh);
    if (m_fallbackShadowDepth != 0) {
        glDeleteTextures(1, &m_fallbackShadowDepth);
        m_fallbackShadowDepth = 0;
    }
    if (m_fallbackShadowDepthCompare != 0) {
        glDeleteTextures(1, &m_fallbackShadowDepthCompare);
        m_fallbackShadowDepthCompare = 0;
    }
    m_shader = nullptr;
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
    bindDisabledShadowFallback(*m_shader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, steveTex);

    const auto drawPart = [&](ecs::StevePartType partType, const glm::mat4& model) {
        PartMesh* mesh = getMeshForPart(partType, ecs::SkinTypeComponent::Type::Player);
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

void HumanoidRenderer::drawEntities(ecs::GameplayRegistry& gameplayReg, Shader& shader,
                                     int modelLoc, int viewProjLoc, int prevModelLoc,
                                     const glm::mat4& viewProj, RenderMode mode) {
    auto& reg = gameplayReg.registry();

    shader.use();
    shader.setMat4(viewProjLoc, viewProj);
    shader.setInt("uTexture", 0);
    bindDisabledShadowFallback(shader);

    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // ── Render Steve (player) entities ──
    if (mode == kRenderAll) {
    const GLuint steveTex = m_resourceMgr->getGuiTexture("steve");
    if (steveTex != 0) {
        glBindTexture(GL_TEXTURE_2D, steveTex);

        auto steveView = reg.view<ecs::SteveTag, ecs::ChildrenComponent>();
        for (auto steveRoot : steveView) {
            auto& rootChildren = reg.get<ecs::ChildrenComponent>(steveRoot);

            // Torso
            for (auto child : rootChildren.children) {
                if (!reg.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(child)) continue;
                auto& part = reg.get<ecs::StevePartComponent>(child);
                if (part.partType != ecs::StevePartType::Torso) continue;

                auto& world = reg.get<ecs::WorldTransformComponent>(child);
                PartMesh* mesh = getMeshForPart(ecs::StevePartType::Torso,
                                                ecs::SkinTypeComponent::Type::Player);
                if (mesh == nullptr || mesh->vao == 0) continue;

                if (prevModelLoc >= 0) {
                    auto it = m_previousModelMatrices.find(child);
                    shader.setMat4(prevModelLoc, it != m_previousModelMatrices.end() ? it->second : glm::mat4(1.0f));
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
                                                    ecs::SkinTypeComponent::Type::Player);
                    if (mesh == nullptr || mesh->vao == 0 || mesh->vertexCount == 0) continue;

                    if (prevModelLoc >= 0) {
                        auto it = m_previousModelMatrices.find(partEntity);
                        shader.setMat4(prevModelLoc, it != m_previousModelMatrices.end() ? it->second : glm::mat4(1.0f));
                    }
                    shader.setMat4(modelLoc, world.worldMatrix);
                    glBindVertexArray(mesh->vao);
                    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
                    m_previousModelMatrices[partEntity] = world.worldMatrix;
                }
            }
        }
    }
    } // kRenderAll

    // ── Render Mob entities ──
    const GLuint zombieTex = m_resourceMgr->getGuiTexture("zombie");
    if (zombieTex != 0) {
        glBindTexture(GL_TEXTURE_2D, zombieTex);

        auto mobView = reg.view<ecs::MobTag, ecs::ChildrenComponent>();
        for (auto mobRoot : mobView) {
            auto& rootChildren = reg.get<ecs::ChildrenComponent>(mobRoot);

            // Torso
            for (auto child : rootChildren.children) {
                if (!reg.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(child)) continue;
                auto& part = reg.get<ecs::StevePartComponent>(child);
                if (part.partType != ecs::StevePartType::Torso) continue;

                auto& world = reg.get<ecs::WorldTransformComponent>(child);
                PartMesh* mesh = getMeshForPart(ecs::StevePartType::Torso,
                                                ecs::SkinTypeComponent::Type::Mob);
                if (mesh == nullptr || mesh->vao == 0) continue;

                if (prevModelLoc >= 0) {
                    auto it = m_previousModelMatrices.find(child);
                    shader.setMat4(prevModelLoc, it != m_previousModelMatrices.end() ? it->second : glm::mat4(1.0f));
                }
                shader.setMat4(modelLoc, world.worldMatrix);
                glBindVertexArray(mesh->vao);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
                m_previousModelMatrices[child] = world.worldMatrix;
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
                                                    ecs::SkinTypeComponent::Type::Mob);
                    if (mesh == nullptr || mesh->vao == 0 || mesh->vertexCount == 0) continue;

                    if (prevModelLoc >= 0) {
                        auto it = m_previousModelMatrices.find(partEntity);
                        shader.setMat4(prevModelLoc, it != m_previousModelMatrices.end() ? it->second : glm::mat4(1.0f));
                    }
                    shader.setMat4(modelLoc, world.worldMatrix);
                    glBindVertexArray(mesh->vao);
                    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
                    m_previousModelMatrices[partEntity] = world.worldMatrix;
                }
            }
        }
    }

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glActiveTexture(GL_TEXTURE0);
}

void HumanoidRenderer::drawEntities(const World& world, ecs::GameplayRegistry& gameplayReg,
                                     Shader& shader, int modelLoc, int viewProjLoc, int prevModelLoc,
                                     const glm::mat4& viewProj, RenderMode mode) {
    auto& reg = gameplayReg.registry();

    shader.use();
    shader.setMat4(viewProjLoc, viewProj);
    shader.setInt("uTexture", 0);
    bindDisabledShadowFallback(shader);

    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // ── Render Steve (player) entities ──
    if (mode == kRenderAll) {
    const GLuint steveTex = m_resourceMgr->getGuiTexture("steve");
    if (steveTex != 0) {
        glBindTexture(GL_TEXTURE_2D, steveTex);

        auto steveView = reg.view<ecs::SteveTag, ecs::ChildrenComponent>();
        for (auto steveRoot : steveView) {
            auto& rootChildren = reg.get<ecs::ChildrenComponent>(steveRoot);

            // Query world light at torso position for this entity.
            for (auto child : rootChildren.children) {
                if (!reg.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(child)) continue;
                auto& part = reg.get<ecs::StevePartComponent>(child);
                if (part.partType != ecs::StevePartType::Torso) continue;
                auto& wt = reg.get<ecs::WorldTransformComponent>(child);
                glm::vec3 entityPos = glm::vec3(wt.worldMatrix[3]);
                glm::vec2 light = queryWorldLight(world, entityPos);
                shader.setFloat("uEntitySunlight", light.x);
                shader.setFloat("uEntityBlockLight", light.y);
                break;
            }

            // Torso
            for (auto child : rootChildren.children) {
                if (!reg.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(child)) continue;
                auto& part = reg.get<ecs::StevePartComponent>(child);
                if (part.partType != ecs::StevePartType::Torso) continue;

                auto& wt = reg.get<ecs::WorldTransformComponent>(child);
                PartMesh* mesh = getMeshForPart(ecs::StevePartType::Torso,
                                                ecs::SkinTypeComponent::Type::Player);
                if (mesh == nullptr || mesh->vao == 0) continue;

                if (prevModelLoc >= 0) {
                    auto it = m_previousModelMatrices.find(child);
                    shader.setMat4(prevModelLoc, it != m_previousModelMatrices.end() ? it->second : glm::mat4(1.0f));
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
                                                    ecs::SkinTypeComponent::Type::Player);
                    if (mesh == nullptr || mesh->vao == 0 || mesh->vertexCount == 0) continue;

                    if (prevModelLoc >= 0) {
                        auto it = m_previousModelMatrices.find(partEntity);
                        shader.setMat4(prevModelLoc, it != m_previousModelMatrices.end() ? it->second : glm::mat4(1.0f));
                    }
                    shader.setMat4(modelLoc, wt.worldMatrix);
                    glBindVertexArray(mesh->vao);
                    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
                    m_previousModelMatrices[partEntity] = wt.worldMatrix;
                }
            }
        }
    }
    } // kRenderAll

    // ── Render Mob entities ──
    const GLuint zombieTex = m_resourceMgr->getGuiTexture("zombie");
    if (zombieTex != 0) {
        glBindTexture(GL_TEXTURE_2D, zombieTex);

        auto mobView = reg.view<ecs::MobTag, ecs::ChildrenComponent>();
        for (auto mobRoot : mobView) {
            auto& rootChildren = reg.get<ecs::ChildrenComponent>(mobRoot);

            // Query world light at torso position for this entity.
            for (auto child : rootChildren.children) {
                if (!reg.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(child)) continue;
                auto& part = reg.get<ecs::StevePartComponent>(child);
                if (part.partType != ecs::StevePartType::Torso) continue;
                auto& wt = reg.get<ecs::WorldTransformComponent>(child);
                glm::vec3 entityPos = glm::vec3(wt.worldMatrix[3]);
                glm::vec2 light = queryWorldLight(world, entityPos);
                shader.setFloat("uEntitySunlight", light.x);
                shader.setFloat("uEntityBlockLight", light.y);
                break;
            }

            // Torso
            for (auto child : rootChildren.children) {
                if (!reg.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(child)) continue;
                auto& part = reg.get<ecs::StevePartComponent>(child);
                if (part.partType != ecs::StevePartType::Torso) continue;

                auto& wt = reg.get<ecs::WorldTransformComponent>(child);
                PartMesh* mesh = getMeshForPart(ecs::StevePartType::Torso,
                                                ecs::SkinTypeComponent::Type::Mob);
                if (mesh == nullptr || mesh->vao == 0) continue;

                if (prevModelLoc >= 0) {
                    auto it = m_previousModelMatrices.find(child);
                    shader.setMat4(prevModelLoc, it != m_previousModelMatrices.end() ? it->second : glm::mat4(1.0f));
                }
                shader.setMat4(modelLoc, wt.worldMatrix);
                glBindVertexArray(mesh->vao);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
                m_previousModelMatrices[child] = wt.worldMatrix;
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
                                                    ecs::SkinTypeComponent::Type::Mob);
                    if (mesh == nullptr || mesh->vao == 0 || mesh->vertexCount == 0) continue;

                    if (prevModelLoc >= 0) {
                        auto it = m_previousModelMatrices.find(partEntity);
                        shader.setMat4(prevModelLoc, it != m_previousModelMatrices.end() ? it->second : glm::mat4(1.0f));
                    }
                    shader.setMat4(modelLoc, wt.worldMatrix);
                    glBindVertexArray(mesh->vao);
                    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
                    m_previousModelMatrices[partEntity] = wt.worldMatrix;
                }
            }
        }
    }

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glActiveTexture(GL_TEXTURE0);
}

void HumanoidRenderer::render(ecs::GameplayRegistry& gameplayReg, const Camera& camera,
                               const Window& window, RenderMode mode) {
    if (m_shader == nullptr || m_resourceMgr == nullptr) return;

    const glm::mat4 viewProj = camera.getProjectionMatrix(window.getAspectRatio()) * camera.getViewMatrix();
    const int modelLoc = m_shader->getUniformLocation("model");
    const int viewProjLoc = m_shader->getUniformLocation("viewProj");

    drawEntities(gameplayReg, *m_shader, modelLoc, viewProjLoc, -1, viewProj, mode);
    glDisable(GL_CULL_FACE);
}

void HumanoidRenderer::renderToGBuffer(ecs::GameplayRegistry& gameplayReg,
                                        const glm::mat4& jitteredViewProj,
                                        RenderMode mode) {
    if (m_gbufferShader == nullptr || m_resourceMgr == nullptr) return;

    const int modelLoc = m_gbufferShader->getUniformLocation("model");
    const int viewProjLoc = m_gbufferShader->getUniformLocation("viewProj");
    const int prevModelLoc = m_gbufferShader->getUniformLocation("prevModel");

    // GBuffer FBO is already bound by the caller (Renderer).
    // Depth test/write enabled, blend disabled — set by caller.
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

void HumanoidRenderer::renderToGBuffer(const World& world, ecs::GameplayRegistry& gameplayReg,
                                        const glm::mat4& jitteredViewProj,
                                        RenderMode mode) {
    if (m_gbufferShader == nullptr || m_resourceMgr == nullptr) return;

    const int modelLoc = m_gbufferShader->getUniformLocation("model");
    const int viewProjLoc = m_gbufferShader->getUniformLocation("viewProj");
    const int prevModelLoc = m_gbufferShader->getUniformLocation("prevModel");

    drawEntities(world, gameplayReg, *m_gbufferShader, modelLoc, viewProjLoc, prevModelLoc, jitteredViewProj, mode);
}

void HumanoidRenderer::renderToShadowMap(const World& world, ecs::GameplayRegistry& gameplayReg,
                                          const glm::mat4& shadowViewProj,
                                          RenderMode mode) {
    if (m_shadowShader == nullptr || m_resourceMgr == nullptr) return;

    const int modelLoc = m_shadowShader->getUniformLocation("model");
    const int viewProjLoc = m_shadowShader->getUniformLocation("viewProj");

    drawEntities(world, gameplayReg, *m_shadowShader, modelLoc, viewProjLoc, -1, shadowViewProj, mode);
}

glm::vec2 HumanoidRenderer::queryWorldLight(const World& world, const glm::vec3& position) {
    const int bx = static_cast<int>(std::floor(position.x));
    const int by = static_cast<int>(std::floor(position.y));
    const int bz = static_cast<int>(std::floor(position.z));

    if (!world.isChunkLoadedForBlock(bx, by, bz)) {
        return {1.0f, 0.0f};
    }

    const glm::ivec2 cc = world.getChunkCoords(bx, bz);
    const auto& chunks = world.getActiveChunks();
    const auto it = chunks.find(World::chunkKey(cc.x, cc.y));
    if (it == chunks.end()) {
        return {1.0f, 0.0f};
    }

    const glm::ivec3 local = Chunk::worldToLocal(bx, by, bz);
    const uint8_t sun = it->second->getSunlight(local.x, local.y, local.z);
    const uint8_t block = it->second->getBlockLight(local.x, local.y, local.z);
    return {sun / 15.0f, block / 15.0f};
}
