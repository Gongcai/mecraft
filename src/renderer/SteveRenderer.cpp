#include "SteveRenderer.h"
#include "Shader.h"

#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "../core/Camera.h"
#include "../core/Window.h"
#include "../resource/ResourceMgr.h"
#include "../ecs/GameplayRegistry.h"
#include "../ecs/components/Components.h"

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

SteveRenderer::FaceUvRect SteveRenderer::pixelRectToUv(float x0, float y0, float x1, float y1) {
    return {
        x0 / SKIN_W,              // u0
        1.0f - y1 / SKIN_H,       // v0
        x1 / SKIN_W,              // u1
        1.0f - y0 / SKIN_H        // v1
    };
}

SteveRenderer::PartMesh SteveRenderer::buildBoxMesh(float hw, float hh, float hd,
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
        {{
            {xmin, ymax, zmax},
            {xmax, ymax, zmax},
            {xmax, ymax, zmin},
            {xmin, ymax, zmin}
        }},
        // Bottom (-Y)
        {{
            {xmin, ymin, zmin},
            {xmax, ymin, zmin},
            {xmax, ymin, zmax},
            {xmin, ymin, zmax}
        }},
        // Front (+Z)
        {{
            {xmin, ymin, zmax},
            {xmax, ymin, zmax},
            {xmax, ymax, zmax},
            {xmin, ymax, zmax}
        }},
        // Back (-Z)
        {{
            {xmax, ymin, zmin},
            {xmin, ymin, zmin},
            {xmin, ymax, zmin},
            {xmax, ymax, zmin}
        }},
        // Left (-X)
        {{
            {xmin, ymin, zmin},
            {xmin, ymin, zmax},
            {xmin, ymax, zmax},
            {xmin, ymax, zmin}
        }},
        // Right (+X)
        {{
            {xmax, ymin, zmax},
            {xmax, ymin, zmin},
            {xmax, ymax, zmin},
            {xmax, ymax, zmax}
        }}
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

SteveRenderer::PartMesh SteveRenderer::buildHeadMesh() const {
    const float hw = 0.25f, hh = 0.25f, hd = 0.25f;
    const FaceUvRect uv[6] = {
        pixelRectToUv(8, 0, 16, 8),      // top
        pixelRectToUv(16, 0, 24, 8),     // bottom
        pixelRectToUv(8, 8, 16, 16),     // front
        pixelRectToUv(24, 8, 32, 16),    // back
        pixelRectToUv(0, 8, 8, 16),      // left(-X) = player right → skin right
        pixelRectToUv(16, 8, 24, 16)     // right(+X) = player left → skin left
    };
    return buildBoxMesh(hw, hh, hd, hh, uv);
}

SteveRenderer::PartMesh SteveRenderer::buildTorsoMesh() const {
    const float hw = 0.25f, hh = 0.375f, hd = 0.125f;
    const FaceUvRect uv[6] = {
        pixelRectToUv(20, 16, 28, 20),    // top
        pixelRectToUv(28, 16, 36, 20),   // bottom
        pixelRectToUv(20, 20, 28, 32),   // front
        pixelRectToUv(32, 20, 40, 32),   // back
        pixelRectToUv(16, 20, 20, 32),   // left(-X) = player right → skin right
        pixelRectToUv(28, 20, 32, 32)    // right(+X) = player left → skin left
    };
    return buildBoxMesh(hw, hh, hd, 0.0f, uv);
}

SteveRenderer::PartMesh SteveRenderer::buildRightArmMesh() const {
    const float hw = 0.125f, hh = 0.375f, hd = 0.125f;
    const FaceUvRect uv[6] = {
        pixelRectToUv(44, 16, 48, 20),    // top
        pixelRectToUv(48, 16, 52, 20),   // bottom
        pixelRectToUv(44, 20, 48, 32),   // front
        pixelRectToUv(52, 20, 56, 32),   // back
        pixelRectToUv(40, 20, 44, 32),   // left(-X) = player right → skin outer
        pixelRectToUv(48, 20, 52, 32)    // right(+X) = player left → skin inner
    };
    return buildBoxMesh(hw, hh, hd, -hh, uv);
}

SteveRenderer::PartMesh SteveRenderer::buildLeftArmMesh() const {
    const float hw = 0.125f, hh = 0.375f, hd = 0.125f;
    const FaceUvRect uv[6] = {
        pixelRectToUv(36, 48, 40, 52),    // top
        pixelRectToUv(40, 48, 44, 52),   // bottom
        pixelRectToUv(36, 52, 40, 64),   // front
        pixelRectToUv(44, 52, 48, 64),   // back
        pixelRectToUv(32, 52, 36, 64),   // left(-X) = player right → skin inner
        pixelRectToUv(40, 52, 44, 64)    // right(+X) = player left → skin outer
    };
    return buildBoxMesh(hw, hh, hd, -hh, uv);
}

SteveRenderer::PartMesh SteveRenderer::buildRightLegMesh() const {
    const float hw = 0.125f, hh = 0.375f, hd = 0.125f;
    const FaceUvRect uv[6] = {
        pixelRectToUv(4, 16, 8, 20),      // top
        pixelRectToUv(8, 16, 12, 20),    // bottom
        pixelRectToUv(4, 20, 8, 32),     // front
        pixelRectToUv(12, 20, 16, 32),   // back
        pixelRectToUv(0, 20, 4, 32),     // left(-X) = player right → skin outer
        pixelRectToUv(8, 20, 12, 32)     // right(+X) = player left → skin inner
    };
    return buildBoxMesh(hw, hh, hd, -hh, uv);
}

SteveRenderer::PartMesh SteveRenderer::buildLeftLegMesh() const {
    const float hw = 0.125f, hh = 0.375f, hd = 0.125f;
    const FaceUvRect uv[6] = {
        pixelRectToUv(20, 48, 24, 52),    // top
        pixelRectToUv(24, 48, 28, 52),   // bottom
        pixelRectToUv(20, 52, 24, 64),   // front
        pixelRectToUv(28, 52, 32, 64),   // back
        pixelRectToUv(16, 52, 20, 64),   // left(-X) = player right → skin inner
        pixelRectToUv(24, 52, 28, 64)    // right(+X) = player left → skin outer
    };
    return buildBoxMesh(hw, hh, hd, -hh, uv);
}

void SteveRenderer::destroyMesh(PartMesh& mesh) {
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

SteveRenderer::PartMesh* SteveRenderer::getMeshForPart(ecs::StevePartType partType) {
    switch (partType) {
    case ecs::StevePartType::Torso:     return &m_torsoMesh;
    case ecs::StevePartType::Head:      return &m_headMesh;
    case ecs::StevePartType::RightArm:  return &m_rightArmMesh;
    case ecs::StevePartType::LeftArm:   return &m_leftArmMesh;
    case ecs::StevePartType::RightLeg:  return &m_rightLegMesh;
    case ecs::StevePartType::LeftLeg:   return &m_leftLegMesh;
    default: return nullptr;
    }
}

void SteveRenderer::init(ResourceMgr& resourceMgr) {
    m_resourceMgr = &resourceMgr;
    m_shader = resourceMgr.getShader("steve");

    m_headMesh = buildHeadMesh();
    m_torsoMesh = buildTorsoMesh();
    m_rightArmMesh = buildRightArmMesh();
    m_leftArmMesh = buildLeftArmMesh();
    m_rightLegMesh = buildRightLegMesh();
    m_leftLegMesh = buildLeftLegMesh();
}

void SteveRenderer::shutdown() {
    destroyMesh(m_headMesh);
    destroyMesh(m_torsoMesh);
    destroyMesh(m_rightArmMesh);
    destroyMesh(m_leftArmMesh);
    destroyMesh(m_rightLegMesh);
    destroyMesh(m_leftLegMesh);
    m_shader = nullptr;
    m_resourceMgr = nullptr;
}

void SteveRenderer::render(ecs::GameplayRegistry& gameplayReg, const Camera& camera, const Window& window) {
    if (m_shader == nullptr || m_resourceMgr == nullptr) return;

    auto& reg = gameplayReg.registry();

    auto steveView = reg.view<ecs::SteveTag, ecs::ChildrenComponent>();
    if (steveView.begin() == steveView.end()) return;

    const GLuint steveTex = m_resourceMgr->getGuiTexture("steve");
    if (steveTex == 0) return;

    const glm::mat4 viewProj = camera.getProjectionMatrix(window.getAspectRatio()) * camera.getViewMatrix();
    const int modelLoc = m_shader->getUniformLocation("model");
    const int viewProjLoc = m_shader->getUniformLocation("viewProj");

    m_shader->use();
    m_shader->setMat4(viewProjLoc, viewProj);
    m_shader->setInt("uTexture", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, steveTex);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    for (auto steveRoot : steveView) {
        auto& rootChildren = reg.get<ecs::ChildrenComponent>(steveRoot);

        // Render torso (direct child of root)
        for (auto child : rootChildren.children) {
            if (!reg.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(child)) continue;
            auto& part = reg.get<ecs::StevePartComponent>(child);
            if (part.partType != ecs::StevePartType::Torso) continue;

            auto& world = reg.get<ecs::WorldTransformComponent>(child);
            PartMesh* mesh = getMeshForPart(ecs::StevePartType::Torso);
            if (mesh == nullptr || mesh->vao == 0) continue;

            m_shader->setMat4(modelLoc, world.worldMatrix);
            glBindVertexArray(mesh->vao);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
        }

        // Render limb parts (children of torso)
        for (auto child : rootChildren.children) {
            if (!reg.all_of<ecs::ChildrenComponent>(child)) continue;
            auto& partChildren = reg.get<ecs::ChildrenComponent>(child);

            for (auto partEntity : partChildren.children) {
                if (!reg.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(partEntity)) continue;

                auto& part = reg.get<ecs::StevePartComponent>(partEntity);
                auto& world = reg.get<ecs::WorldTransformComponent>(partEntity);

                PartMesh* mesh = getMeshForPart(part.partType);
                if (mesh == nullptr || mesh->vao == 0 || mesh->vertexCount == 0) continue;

                m_shader->setMat4(modelLoc, world.worldMatrix);
                glBindVertexArray(mesh->vao);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
            }
        }
    }

    glBindVertexArray(0);
    glDisable(GL_CULL_FACE);
}
