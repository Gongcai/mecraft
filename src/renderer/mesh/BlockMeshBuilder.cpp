#include "BlockMeshBuilder.h"

#include <array>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "../../resource/ResourceMgr.h"
#include "../../world/block/Block.h"
#include "../../world/chunk/SubChunk.h"

namespace renderer {

namespace {

constexpr std::array<std::array<glm::vec3, 4>, 6> kFaceCorners = {{
    {{{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}}, // top
    {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}}, // bottom
    {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}}, // front (+z)
    {{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}}, // back (-z)
    {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}}, // left (-x)
    {{{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}}  // right (+x)
}};

constexpr std::array<glm::vec3, 4> kCrossQuadA = {{{0.1464f, 0.0f, 0.1464f}, {0.8536f, 0.0f, 0.8536f}, {0.8536f, 1.0f, 0.8536f}, {0.1464f, 1.0f, 0.1464f}}};
constexpr std::array<glm::vec3, 4> kCrossQuadB = {{{0.8536f, 0.0f, 0.1464f}, {0.1464f, 0.0f, 0.8536f}, {0.1464f, 1.0f, 0.8536f}, {0.8536f, 1.0f, 0.1464f}}};

constexpr std::array<int, 6> kFaceIndices = {{0, 1, 2, 0, 2, 3}};
constexpr float kCrossBiomeTintMarker = -1.0f;
constexpr float kCrossFlowerMarker = -2.0f;

int getFaceTextureIndex(const BlockDef& def, const int face) {
    return def.getFaceLayer(face);
}

} // namespace

BlockCubeMesh uploadBlockCubeMesh(const std::vector<BlockVertex>& vertices) {
    BlockCubeMesh mesh;
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

BlockCubeMesh buildBlockCubeMesh(const BlockID blockId, const ResourceMgr& /*resourceMgr*/) {
    BlockCubeMesh mesh;
    if (blockId == 0) {
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
                const glm::vec3& pos = corners[idx];
                const glm::vec2& uvCoord = quadUV[idx];
                vertices.push_back(makeBlockVertex(
                    pos.x, pos.y, pos.z, uvCoord.x, uvCoord.y, crossMarker,
                    1.0f, 0.0f, 3.0f, layer, 1.0f, 0.0f, 0.0f,
                    tintKind, tintU, tintV, def.derivativeMaterialId));
            }
        };
        emitQuad(kCrossQuadA);
        emitQuad(kCrossQuadB);
    } else {
        // Standard cube (covers torch shape too via its renderShapeName, but
        // falling blocks are always full cubes — kept simple here).
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
                const glm::vec3& pos = kFaceCorners[face][idx];
                const glm::vec2& uvCoord = faceUV[idx];
                vertices.push_back(makeBlockVertex(
                    pos.x, pos.y, pos.z, uvCoord.x, uvCoord.y, static_cast<float>(face),
                    1.0f, 0.0f, 3.0f, layer, 1.0f, 0.0f, 0.0f,
                    tintKind, tintU, tintV, def.derivativeMaterialId));
            }
        }
    }

    if (vertices.empty()) {
        return mesh;
    }

    return uploadBlockCubeMesh(vertices);
}

void destroyBlockCubeMesh(BlockCubeMesh& mesh) {
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

} // namespace renderer
