#ifndef MECRAFT_STEVE_RENDERER_H
#define MECRAFT_STEVE_RENDERER_H

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

class SteveRenderer {
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown();
    void render(ecs::GameplayRegistry& registry, const Camera& camera, const Window& window);

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

    // One mesh per StevePartType
    PartMesh m_torsoMesh;
    PartMesh m_headMesh;
    PartMesh m_rightArmMesh;
    PartMesh m_leftArmMesh;
    PartMesh m_rightLegMesh;
    PartMesh m_leftLegMesh;

    Shader* m_shader = nullptr;
    ResourceMgr* m_resourceMgr = nullptr;

    static void destroyMesh(PartMesh& mesh);

    // Build a box mesh with the given dimensions and UV layout.
    // The box is centered at (0,0,0) with the specified offset for pivot alignment.
    PartMesh buildBoxMesh(float hw, float hh, float hd,
                          float offsetY,
                          const FaceUvRect uv[6]) const;

    PartMesh buildHeadMesh() const;
    PartMesh buildTorsoMesh() const;
    PartMesh buildRightArmMesh() const;
    PartMesh buildLeftArmMesh() const;
    PartMesh buildRightLegMesh() const;
    PartMesh buildLeftLegMesh() const;

    // Convert pixel coordinates in the original skin image to UV coords.
    // Assumes texture was loaded with flip=true (standard OpenGL UV convention).
    static FaceUvRect pixelRectToUv(float x0, float y0, float x1, float y1);

    PartMesh* getMeshForPart(ecs::StevePartType partType);
};

#endif // MECRAFT_STEVE_RENDERER_H
