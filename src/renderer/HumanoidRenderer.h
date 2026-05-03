#ifndef MECRAFT_HUMANOID_RENDERER_H
#define MECRAFT_HUMANOID_RENDERER_H

#include <cstdint>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include "../ecs/components/Components.h"

class Camera;
class Shader;
class ResourceMgr;
class Window;

namespace ecs {
class GameplayRegistry;
}

class HumanoidRenderer {
public:
    enum RenderMode : uint8_t {
        kRenderAll,       // render both Steve (player) and mob entities
        kRenderMobsOnly   // render only mob entities (first-person view)
    };

    void init(ResourceMgr& resourceMgr);
    void shutdown();
    void render(ecs::GameplayRegistry& registry, const Camera& camera, const Window& window,
                RenderMode mode = kRenderAll);

private:
    struct PartMesh {
        GLuint vao = 0;
        GLuint vbo = 0;
        uint32_t vertexCount = 0;
    };

    struct SteveVertex {
        float x, y, z;
        float u, v;
        float nx, ny, nz;
    };

    struct FaceUvRect {
        float u0, v0, u1, v1;
    };

    // ── Player (64x64) meshes ──
    PartMesh m_torsoMesh;
    PartMesh m_headMesh;
    PartMesh m_rightArmMesh;
    PartMesh m_leftArmMesh;
    PartMesh m_rightLegMesh;
    PartMesh m_leftLegMesh;

    // ── Mob (64x32) meshes ──
    // Head and torso share the same UV layout as player.
    // Right arm/leg use the same pixel coords as player.
    // Left arm/leg are mirrored copies of right arm/leg.
    PartMesh m_mobLeftArmMesh;
    PartMesh m_mobLeftLegMesh;

    Shader* m_shader = nullptr;
    ResourceMgr* m_resourceMgr = nullptr;

    static void destroyMesh(PartMesh& mesh);

    PartMesh buildBoxMesh(float hw, float hh, float hd,
                          float offsetY,
                          const FaceUvRect uv[6]) const;

    // Player mesh builders (64x64 skin layout)
    PartMesh buildHeadMesh() const;
    PartMesh buildTorsoMesh() const;
    PartMesh buildRightArmMesh() const;
    PartMesh buildLeftArmMesh() const;
    PartMesh buildRightLegMesh() const;
    PartMesh buildLeftLegMesh() const;

    // Mob mirrored mesh builders (64x32 skin layout)
    // Left arm/leg = right arm/leg with X-flip and UV left/right face swap
    PartMesh buildMirroredArmMesh() const;
    PartMesh buildMirroredLegMesh() const;

    static FaceUvRect pixelRectToUv(float x0, float y0, float x1, float y1);

    PartMesh* getMeshForPart(ecs::StevePartType partType, ecs::SkinTypeComponent::Type skinType);
};

#endif // MECRAFT_HUMANOID_RENDERER_H
