#include "BlockMeshBuilder.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <glad/glad.h>

#include <glm/glm.hpp>

#include "../../resource/ResourceMgr.h"
#include "../rhi/RhiDevice.h"
#include "../../world/block/Block.h"
#include "../../world/block/BlockModelRegistry.h"
#include "../../world/block/BlockStateRegistry.h"
#include "../../world/chunk/SubChunk.h"

namespace renderer {

namespace {

[[noreturn]] void failBlockMeshBuilder(const std::string& message) {
    std::cerr << message << '\n';
    std::abort();
}

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
constexpr float kTorchModelPixel = 1.0f / 16.0f;
constexpr float kTorchModelCoreMin = 7.0f * kTorchModelPixel;
constexpr float kTorchModelCoreMax = 9.0f * kTorchModelPixel;
constexpr float kTorchModelCoreTop = 10.0f * kTorchModelPixel;

struct IVec3 {
    int x = 0;
    int y = 0;
    int z = 0;
};

struct FaceUvRect {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
};

constexpr std::array<IVec3, 6> kFaceNormals = {{{0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {-1, 0, 0}, {1, 0, 0}}};

int getFaceTextureIndex(const BlockDef& def, const int face) {
    return def.getFaceLayer(face);
}

bool isTorchShape(const BlockDef& def) {
    return def.renderShapeName == "torch";
}

bool isModelShape(const BlockDef& def) {
    return def.renderShapeName == "model";
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

glm::vec3 rotatePointX90(const glm::vec3& p, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {p.x, 1.0f - p.z, p.y};
        case 2: return {p.x, 1.0f - p.y, 1.0f - p.z};
        case 3: return {p.x, p.z, 1.0f - p.y};
        case 0:
        default: return p;
    }
}

glm::vec3 rotatePointY90(const glm::vec3& p, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {1.0f - p.z, p.y, p.x};
        case 2: return {1.0f - p.x, p.y, 1.0f - p.z};
        case 3: return {p.z, p.y, 1.0f - p.x};
        case 0:
        default: return p;
    }
}

glm::vec3 rotatePointZ90(const glm::vec3& p, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {1.0f - p.y, p.x, p.z};
        case 2: return {1.0f - p.x, 1.0f - p.y, p.z};
        case 3: return {p.y, 1.0f - p.x, p.z};
        case 0:
        default: return p;
    }
}

glm::vec3 applyModelTransform(glm::vec3 p, const ModelTransform& transform) {
    p = rotatePointX90(p, transform.rotX);
    p = rotatePointY90(p, transform.rotY);
    p = rotatePointZ90(p, transform.rotZ);
    return p;
}

IVec3 rotateDirectionX90(const IVec3 direction, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {direction.x, -direction.z, direction.y};
        case 2: return {direction.x, -direction.y, -direction.z};
        case 3: return {direction.x, direction.z, -direction.y};
        case 0:
        default: return direction;
    }
}

IVec3 rotateDirectionY90(const IVec3 direction, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {-direction.z, direction.y, direction.x};
        case 2: return {-direction.x, direction.y, -direction.z};
        case 3: return {direction.z, direction.y, -direction.x};
        case 0:
        default: return direction;
    }
}

IVec3 rotateDirectionZ90(const IVec3 direction, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {-direction.y, direction.x, direction.z};
        case 2: return {-direction.x, -direction.y, direction.z};
        case 3: return {direction.y, -direction.x, direction.z};
        case 0:
        default: return direction;
    }
}

IVec3 applyModelTransformToDirection(IVec3 direction, const ModelTransform& transform) {
    direction = rotateDirectionX90(direction, transform.rotX);
    direction = rotateDirectionY90(direction, transform.rotY);
    direction = rotateDirectionZ90(direction, transform.rotZ);
    return direction;
}

int faceFromDirection(const IVec3 direction) {
    if (direction.x == 0 && direction.y == 1 && direction.z == 0) return 0;
    if (direction.x == 0 && direction.y == -1 && direction.z == 0) return 1;
    if (direction.x == 0 && direction.y == 0 && direction.z == 1) return 2;
    if (direction.x == 0 && direction.y == 0 && direction.z == -1) return 3;
    if (direction.x == -1 && direction.y == 0 && direction.z == 0) return 4;
    if (direction.x == 1 && direction.y == 0 && direction.z == 0) return 5;
    failBlockMeshBuilder("Model transform produced an invalid face direction");
}

int transformFaceIndex(const int face, const ModelTransform& transform) {
    return faceFromDirection(applyModelTransformToDirection(kFaceNormals[static_cast<size_t>(face)], transform));
}

std::array<glm::vec3, 4> buildModelFaceCorners(const ModelElement& element, const int face) {
    const float x0 = element.from[0] / 16.0f;
    const float y0 = element.from[1] / 16.0f;
    const float z0 = element.from[2] / 16.0f;
    const float x1 = element.to[0] / 16.0f;
    const float y1 = element.to[1] / 16.0f;
    const float z1 = element.to[2] / 16.0f;

    switch (face) {
        case 0:
            return {{{x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}}};
        case 1:
            return {{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}};
        case 2:
            return {{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}};
        case 3:
            return {{{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}}};
        case 4:
            return {{{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}};
        case 5:
        default:
            return {{{x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}}};
    }
}

std::array<glm::vec2, 4> buildModelFaceUv(const ModelFace& face) {
    const float x0 = face.uv[0] / 16.0f;
    const float y0 = face.uv[1] / 16.0f;
    const float x1 = face.uv[2] / 16.0f;
    const float y1 = face.uv[3] / 16.0f;

    switch ((face.uvRotation / 90u) % 4u) {
        case 1:
            return {{{x1, y0}, {x1, y1}, {x0, y1}, {x0, y0}}};
        case 2:
            return {{{x1, y1}, {x0, y1}, {x0, y0}, {x1, y0}}};
        case 3:
            return {{{x0, y1}, {x0, y0}, {x1, y0}, {x1, y1}}};
        case 0:
        default:
            return {{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}};
    }
}

std::array<glm::vec2, 4> applyHorizontalUvLock(std::array<glm::vec2, 4> uv,
                                               const int face,
                                               const ModelTransform& transform) {
    if (!transform.uvLock || (face != 0 && face != 1)) {
        return uv;
    }

    const uint16_t turns = static_cast<uint16_t>((transform.rotY / 90u) % 4u);
    if (turns == 0) {
        return uv;
    }

    std::array<glm::vec2, 4> locked{};
    for (size_t i = 0; i < uv.size(); ++i) {
        locked[i] = uv[(i + uv.size() - turns) % uv.size()];
    }
    return locked;
}

std::string resolveModelFaceTextureName(const BlockModel& model, const ModelFace& face) {
    const std::string textureKey = face.textureVar.substr(1);
    const auto it = model.textures.find(textureKey);
    if (it == model.textures.end()) {
        failBlockMeshBuilder("Model face references unknown texture variable: " + model.name + "." + textureKey);
    }
    return it->second;
}

AnimatedTextureRef resolveModelTextureRef(const ResourceMgr& resourceMgr, const std::string& textureName) {
    const TextureAnimationInfo info = resourceMgr.getTextureAnimation(textureName);
    AnimatedTextureRef ref;
    ref.firstLayer = info.firstLayer;
    ref.frameCount = static_cast<uint16_t>(std::max(1, info.frameCount));
    ref.fps = info.fps;
    ref.isAnimated = info.isAnimated;
    return ref;
}

void emitFace(std::vector<BlockVertex>& vertices,
              const std::array<glm::vec3, 4>& corners,
              const std::array<glm::vec2, 4>& uv,
              const float normal,
              const AnimatedTextureRef& textureRef,
              const uint8_t tintKind,
              const uint8_t tintU,
              const uint8_t tintV,
              const uint8_t derivativeMaterialId) {
    const float layer = static_cast<float>(textureRef.firstLayer);
    const float frameCount = static_cast<float>(std::max<uint16_t>(1, textureRef.frameCount));
    const float animationFps = textureRef.isAnimated ? textureRef.fps : 0.0f;
    const float animated = textureRef.isAnimated ? 1.0f : 0.0f;

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
                                           frameCount,
                                           animationFps,
                                           animated,
                                           tintKind,
                                           tintU,
                                           tintV,
                                           derivativeMaterialId));
    }
}

void appendTorchVertices(std::vector<BlockVertex>& vertices, const BlockDef& def) {
    int tileIndex = def.faceTop.firstLayer;
    if (tileIndex < 0) {
        tileIndex = def.faceFront.firstLayer;
    }
    if (tileIndex < 0) {
        tileIndex = 0;
    }

    AnimatedTextureRef textureRef;
    textureRef.firstLayer = tileIndex;
    textureRef.frameCount = 1;
    textureRef.fps = 0.0f;
    textureRef.isAnimated = false;

    const FaceUvRect topUv = makeTorchUvRect(7.0f, 6.0f, 9.0f, 8.0f);
    const FaceUvRect bottomUv = makeTorchUvRect(7.0f, 13.0f, 9.0f, 15.0f);
    const FaceUvRect fullUv = makeTorchUvRect(0.0f, 0.0f, 16.0f, 16.0f);
    const auto uvArray = [](const FaceUvRect& rect) {
        return std::array<glm::vec2, 4>{{
            {rect.u0, rect.v0},
            {rect.u1, rect.v0},
            {rect.u1, rect.v1},
            {rect.u0, rect.v1}
        }};
    };

    emitFace(vertices, {{{kTorchModelCoreMin, kTorchModelCoreTop, kTorchModelCoreMax}, {kTorchModelCoreMax, kTorchModelCoreTop, kTorchModelCoreMax}, {kTorchModelCoreMax, kTorchModelCoreTop, kTorchModelCoreMin}, {kTorchModelCoreMin, kTorchModelCoreTop, kTorchModelCoreMin}}},
             uvArray(topUv), 0.0f, textureRef, BlockTintKinds::NONE, 0, 0, def.derivativeMaterialId);
    emitFace(vertices, {{{kTorchModelCoreMin, 0.0f, kTorchModelCoreMin}, {kTorchModelCoreMax, 0.0f, kTorchModelCoreMin}, {kTorchModelCoreMax, 0.0f, kTorchModelCoreMax}, {kTorchModelCoreMin, 0.0f, kTorchModelCoreMax}}},
             uvArray(bottomUv), 1.0f, textureRef, BlockTintKinds::NONE, 0, 0, def.derivativeMaterialId);
    emitFace(vertices, {{{kTorchModelCoreMin, 0.0f, 0.0f}, {kTorchModelCoreMin, 0.0f, 1.0f}, {kTorchModelCoreMin, 1.0f, 1.0f}, {kTorchModelCoreMin, 1.0f, 0.0f}}},
             uvArray(fullUv), 4.0f, textureRef, BlockTintKinds::NONE, 0, 0, def.derivativeMaterialId);
    emitFace(vertices, {{{kTorchModelCoreMax, 0.0f, 1.0f}, {kTorchModelCoreMax, 0.0f, 0.0f}, {kTorchModelCoreMax, 1.0f, 0.0f}, {kTorchModelCoreMax, 1.0f, 1.0f}}},
             uvArray(fullUv), 5.0f, textureRef, BlockTintKinds::NONE, 0, 0, def.derivativeMaterialId);
    emitFace(vertices, {{{0.0f, 0.0f, kTorchModelCoreMax}, {1.0f, 0.0f, kTorchModelCoreMax}, {1.0f, 1.0f, kTorchModelCoreMax}, {0.0f, 1.0f, kTorchModelCoreMax}}},
             uvArray(fullUv), 2.0f, textureRef, BlockTintKinds::NONE, 0, 0, def.derivativeMaterialId);
    emitFace(vertices, {{{1.0f, 0.0f, kTorchModelCoreMin}, {0.0f, 0.0f, kTorchModelCoreMin}, {0.0f, 1.0f, kTorchModelCoreMin}, {1.0f, 1.0f, kTorchModelCoreMin}}},
             uvArray(fullUv), 3.0f, textureRef, BlockTintKinds::NONE, 0, 0, def.derivativeMaterialId);
}

void appendModelVertices(std::vector<BlockVertex>& vertices,
                         const BlockStateId stateId,
                         const BlockDef& def,
                         const ResourceMgr& resourceMgr) {
    const ModelVariant* variant = BlockStateRegistry::getModelVariant(stateId);
    if (variant == nullptr || variant->model == nullptr) {
        failBlockMeshBuilder("Model block is missing a model variant: " +
                                 BlockRegistry::getNamespacedId(BlockStateRegistry::getBlockId(stateId)).full());
    }

    uint8_t tintU = 0;
    uint8_t tintV = 0;
    computeDefaultBlockTintMapPosition(tintU, tintV);

    const BlockModel& model = *variant->model;
    for (const ModelElement& element : model.elements) {
        for (int faceIndex = 0; faceIndex < 6; ++faceIndex) {
            const std::unique_ptr<ModelFace>& facePtr = element.faces[static_cast<size_t>(faceIndex)];
            if (!facePtr) {
                continue;
            }

            const ModelFace& face = *facePtr;
            std::array<glm::vec3, 4> corners = buildModelFaceCorners(element, faceIndex);
            for (glm::vec3& corner : corners) {
                corner = applyModelTransform(corner, variant->transform);
            }

            const std::string textureName = resolveModelFaceTextureName(model, face);
            const AnimatedTextureRef textureRef = resolveModelTextureRef(resourceMgr, textureName);
            const uint8_t tintKind = face.tintIndex >= 0
                ? blockTintKindFromBiomeTint(def.biomeTint)
                : BlockTintKinds::NONE;

            emitFace(vertices,
                     corners,
                     applyHorizontalUvLock(buildModelFaceUv(face), faceIndex, variant->transform),
                     static_cast<float>(transformFaceIndex(faceIndex, variant->transform)),
                     textureRef,
                     tintKind,
                     tintU,
                     tintV,
                     def.derivativeMaterialId);
        }
    }
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

std::vector<BlockVertex> buildBlockMeshVerticesForState(const BlockStateId stateId, const ResourceMgr& resourceMgr) {
    std::vector<BlockVertex> vertices;
    if (stateId == NULL_BLOCK_STATE) {
        return vertices;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::get(blockId);
    vertices.reserve(36);

    if (isModelShape(def)) {
        appendModelVertices(vertices, stateId, def, resourceMgr);
    } else if (def.renderShape == BlockRenderShape::Cross) {
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
    } else if (isTorchShape(def)) {
        appendTorchVertices(vertices, def);
    } else {
        // Standard cube geometry for full block-backed item/entity meshes.
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

    return vertices;
}

std::vector<BlockVertex> buildBlockMeshVertices(const BlockID blockId, const ResourceMgr& resourceMgr) {
    if (blockId == 0) {
        return {};
    }
    return buildBlockMeshVerticesForState(BlockStateRegistry::getDefaultState(blockId), resourceMgr);
}

BlockCubeMesh buildBlockCubeMesh(const BlockID blockId, const ResourceMgr& resourceMgr) {
    BlockCubeMesh mesh;
    std::vector<BlockVertex> vertices = buildBlockMeshVertices(blockId, resourceMgr);
    if (vertices.empty()) {
        return mesh;
    }

    return uploadBlockCubeMesh(vertices);
}

BlockCubeMesh buildBlockStateCubeMesh(const BlockStateId stateId, ResourceMgr& resourceMgr) {
    BlockCubeMesh mesh;
    std::vector<BlockVertex> vertices = buildBlockMeshVerticesForState(stateId, resourceMgr);
    if (vertices.empty()) {
        return mesh;
    }

    mesh = uploadBlockCubeMesh(vertices);
    RhiDevice& rhiDevice = resourceMgr.rhiDevice();
    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "BlockStateCubeMesh.VertexBuffer";
    bufferDesc.size = vertices.size() * sizeof(BlockVertex);
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex);
    bufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    mesh.rhiVertexBuffer = rhiDevice.createBuffer(
        bufferDesc, vertices.data(), vertices.size() * sizeof(BlockVertex));
    mesh.rhiDevice = &rhiDevice;
    if (!mesh.rhiVertexBuffer.isValid()) {
        destroyBlockCubeMesh(mesh);
    }
    return mesh;
}

void destroyBlockCubeMesh(BlockCubeMesh& mesh) {
    if (mesh.rhiDevice != nullptr && mesh.rhiVertexBuffer.isValid()) {
        mesh.rhiDevice->destroyBuffer(mesh.rhiVertexBuffer);
        mesh.rhiVertexBuffer = {};
    }
    if (mesh.vbo != 0) {
        glDeleteBuffers(1, &mesh.vbo);
        mesh.vbo = 0;
    }
    if (mesh.vao != 0) {
        glDeleteVertexArrays(1, &mesh.vao);
        mesh.vao = 0;
    }
    mesh.vertexCount = 0;
    mesh.rhiDevice = nullptr;
}

void setBlockVertexInputLayout(RhiGraphicsPipelineDesc& pipelineDesc) {
    pipelineDesc.vertexInput.bindings.push_back({
        0u,
        static_cast<uint32_t>(sizeof(BlockVertex)),
        RhiVertexInputRate::Vertex
    });
    pipelineDesc.vertexInput.attributes = {
        {0u, 0u, RhiVertexFormat::Float3, static_cast<uint32_t>(offsetof(BlockVertex, x))},
        {1u, 0u, RhiVertexFormat::Float2, static_cast<uint32_t>(offsetof(BlockVertex, u))},
        {2u, 0u, RhiVertexFormat::Sint8, static_cast<uint32_t>(offsetof(BlockVertex, normal))},
        {3u, 0u, RhiVertexFormat::Unorm8, static_cast<uint32_t>(offsetof(BlockVertex, sunlight))},
        {4u, 0u, RhiVertexFormat::Unorm8, static_cast<uint32_t>(offsetof(BlockVertex, blockLight))},
        {5u, 0u, RhiVertexFormat::Uint8, static_cast<uint32_t>(offsetof(BlockVertex, ao))},
        {6u, 0u, RhiVertexFormat::Uint16, static_cast<uint32_t>(offsetof(BlockVertex, layer))},
        {7u, 0u, RhiVertexFormat::Uint16, static_cast<uint32_t>(offsetof(BlockVertex, animationFrameCount))},
        {8u, 0u, RhiVertexFormat::Uint8, static_cast<uint32_t>(offsetof(BlockVertex, animationFps))},
        {9u, 0u, RhiVertexFormat::Uint8, static_cast<uint32_t>(offsetof(BlockVertex, animated))},
        {10u, 0u, RhiVertexFormat::Uint16, static_cast<uint32_t>(offsetof(BlockVertex, tintPacked))}
    };
}

} // namespace renderer
