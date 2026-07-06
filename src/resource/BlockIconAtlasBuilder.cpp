#include "BlockIconAtlasBuilder.h"

#include "RhiTextureResourceUtils.h"

#include "BlockTextureLibrary.h"
#include "TextureAtlas.h"
#include "../world/block/Block.h"
#include "../world/block/BlockStateRegistry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <glad/glad.h>
#include <glm/vec3.hpp>

namespace {

[[noreturn]] void failBlockIconAtlasBuilder(const std::string& message) {
    std::cerr << message << '\n';
    std::abort();
}

struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;
};

struct IconTint {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

struct IVec3 {
    int x = 0;
    int y = 0;
    int z = 0;
};

struct IconModelFace {
    Vec2f a;
    Vec2f b;
    Vec2f d;
    Vec2f uvA;
    Vec2f uvB;
    Vec2f uvD;
    int tileIndex = -1;
    float shade = 1.0f;
    IconTint tint{};
    float depth = 0.0f;
    int face = 0;
};

constexpr std::array<IVec3, 6> kFaceNormals = {{{0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {-1, 0, 0}, {1, 0, 0}}};

inline int clampInt(const int value, const int minValue, const int maxValue) {
    return std::max(minValue, std::min(maxValue, value));
}

IconTint biomeIconTint(const BiomeTintKind tint) {
    switch (tint) {
        case BiomeTintKind::Grass:
        case BiomeTintKind::Foliage:
            return {0.50f, 0.78f, 0.34f};
        case BiomeTintKind::None:
        default:
            return {};
    }
}

glm::vec3 rotateIconPointX90(const glm::vec3& p, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {p.x, 1.0f - p.z, p.y};
        case 2: return {p.x, 1.0f - p.y, 1.0f - p.z};
        case 3: return {p.x, p.z, 1.0f - p.y};
        case 0:
        default: return p;
    }
}

glm::vec3 rotateIconPointY90(const glm::vec3& p, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {1.0f - p.z, p.y, p.x};
        case 2: return {1.0f - p.x, p.y, 1.0f - p.z};
        case 3: return {p.z, p.y, 1.0f - p.x};
        case 0:
        default: return p;
    }
}

glm::vec3 rotateIconPointZ90(const glm::vec3& p, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {1.0f - p.y, p.x, p.z};
        case 2: return {1.0f - p.x, 1.0f - p.y, p.z};
        case 3: return {p.y, 1.0f - p.x, p.z};
        case 0:
        default: return p;
    }
}

glm::vec3 applyIconModelTransform(glm::vec3 p, const ModelTransform& transform) {
    p = rotateIconPointX90(p, transform.rotX);
    p = rotateIconPointY90(p, transform.rotY);
    p = rotateIconPointZ90(p, transform.rotZ);
    return p;
}

IVec3 rotateIconDirectionX90(const IVec3 direction, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {direction.x, -direction.z, direction.y};
        case 2: return {direction.x, -direction.y, -direction.z};
        case 3: return {direction.x, direction.z, -direction.y};
        case 0:
        default: return direction;
    }
}

IVec3 rotateIconDirectionY90(const IVec3 direction, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {-direction.z, direction.y, direction.x};
        case 2: return {-direction.x, direction.y, -direction.z};
        case 3: return {direction.z, direction.y, -direction.x};
        case 0:
        default: return direction;
    }
}

IVec3 rotateIconDirectionZ90(const IVec3 direction, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {-direction.y, direction.x, direction.z};
        case 2: return {-direction.x, -direction.y, direction.z};
        case 3: return {direction.y, -direction.x, direction.z};
        case 0:
        default: return direction;
    }
}

IVec3 applyIconModelTransformToDirection(IVec3 direction, const ModelTransform& transform) {
    direction = rotateIconDirectionX90(direction, transform.rotX);
    direction = rotateIconDirectionY90(direction, transform.rotY);
    direction = rotateIconDirectionZ90(direction, transform.rotZ);
    return direction;
}

int iconFaceFromDirection(const IVec3 direction) {
    if (direction.x == 0 && direction.y == 1 && direction.z == 0) return 0;
    if (direction.x == 0 && direction.y == -1 && direction.z == 0) return 1;
    if (direction.x == 0 && direction.y == 0 && direction.z == 1) return 2;
    if (direction.x == 0 && direction.y == 0 && direction.z == -1) return 3;
    if (direction.x == -1 && direction.y == 0 && direction.z == 0) return 4;
    if (direction.x == 1 && direction.y == 0 && direction.z == 0) return 5;
    failBlockIconAtlasBuilder("Model transform produced an invalid icon face direction");
}

int transformIconFaceIndex(const int face, const ModelTransform& transform) {
    return iconFaceFromDirection(applyIconModelTransformToDirection(kFaceNormals[static_cast<size_t>(face)], transform));
}

bool isVisibleIconFace(const int transformedFace) {
    return transformedFace == 0 || transformedFace == 3 || transformedFace == 4;
}

float iconFaceShade(const int transformedFace) {
    if (transformedFace == 0) {
        return 1.0f;
    }
    if (transformedFace == 3) {
        return 0.83f;
    }
    if (transformedFace == 4) {
        return 0.68f;
    }
    return 1.0f;
}

Vec2f projectIconPoint(const glm::vec3& p, const float unit) {
    return {
        (16.0f + p.x * 12.0f - p.z * 12.0f) * unit,
        (1.0f + p.x * 6.0f + p.y * 14.0f + p.z * 6.0f) * unit
    };
}

std::array<glm::vec3, 4> buildIconModelFaceCorners(const ModelElement& element, const int face) {
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

std::array<Vec2f, 4> buildIconModelFaceUv(const ModelFace& face) {
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

std::string resolveIconModelFaceTextureName(const BlockModel& model, const ModelFace& face) {
    const std::string textureKey = face.textureVar.substr(1);
    const auto it = model.textures.find(textureKey);
    if (it == model.textures.end()) {
        failBlockIconAtlasBuilder("Model icon face references unknown texture variable: " + model.name + "." + textureKey);
    }
    return it->second;
}

std::array<uint8_t, 4> sampleTileNearest(const TextureAtlas& atlas,
                                         const std::vector<unsigned char>& pixels,
                                         const int tileIndex,
                                         const int tx,
                                         const int ty) {
    if (atlas.tilesPerRow <= 0 || atlas.atlasWidth <= 0 || atlas.atlasHeight <= 0 || atlas.tileSize <= 0) {
        return {0, 0, 0, 0};
    }

    const int stride = std::max(1, atlas.tileStride);
    const int rows = atlas.atlasHeight / stride;
    const int tileCount = atlas.tilesPerRow * std::max(0, rows);
    if (tileIndex < 0 || tileIndex >= tileCount) {
        return {0, 0, 0, 0};
    }

    const int tileCol = tileIndex % atlas.tilesPerRow;
    const int tileRow = tileIndex / atlas.tilesPerRow;
    const int startX = tileCol * stride + atlas.tilePadding;
    const int startY = tileRow * stride + atlas.tilePadding;
    const int x = startX + clampInt(tx, 0, atlas.tileSize - 1);
    const int y = startY + clampInt(ty, 0, atlas.tileSize - 1);
    const size_t idx = static_cast<size_t>(y * atlas.atlasWidth + x) * 4;
    if (idx + 3 >= pixels.size()) {
        return {0, 0, 0, 0};
    }

    return {pixels[idx + 0], pixels[idx + 1], pixels[idx + 2], pixels[idx + 3]};
}

void alphaBlendOver(std::vector<unsigned char>& dst,
                    const int dstWidth,
                    const int dstHeight,
                    const int x,
                    const int y,
                    const std::array<uint8_t, 4>& src) {
    if (x < 0 || y < 0 || x >= dstWidth || y >= dstHeight || src[3] == 0) {
        return;
    }

    const size_t idx = static_cast<size_t>(y * dstWidth + x) * 4;
    const float srcA = static_cast<float>(src[3]) / 255.0f;
    const float dstA = static_cast<float>(dst[idx + 3]) / 255.0f;
    const float outA = srcA + dstA * (1.0f - srcA);
    if (outA <= 1e-6f) {
        dst[idx + 0] = 0;
        dst[idx + 1] = 0;
        dst[idx + 2] = 0;
        dst[idx + 3] = 0;
        return;
    }

    const auto blendChannel = [&](const int channel) {
        const float srcC = static_cast<float>(src[channel]) / 255.0f;
        const float dstC = static_cast<float>(dst[idx + channel]) / 255.0f;
        const float outC = (srcC * srcA + dstC * dstA * (1.0f - srcA)) / outA;
        return static_cast<unsigned char>(std::round(std::clamp(outC, 0.0f, 1.0f) * 255.0f));
    };

    dst[idx + 0] = blendChannel(0);
    dst[idx + 1] = blendChannel(1);
    dst[idx + 2] = blendChannel(2);
    dst[idx + 3] = static_cast<unsigned char>(std::round(std::clamp(outA, 0.0f, 1.0f) * 255.0f));
}

void drawTexturedParallelogram(std::vector<unsigned char>& iconAtlasPixels,
                               const int atlasWidth,
                               const int atlasHeight,
                               const TextureAtlas& srcAtlas,
                               const std::vector<unsigned char>& srcPixels,
                               const int tileIndex,
                               const int iconOriginX,
                               const int iconOriginY,
                               const Vec2f& a,
                               const Vec2f& b,
                               const Vec2f& d,
                               const Vec2f& uvA,
                               const Vec2f& uvB,
                               const Vec2f& uvD,
                               const float shade,
                               const IconTint& tint) {
    if (tileIndex < 0) {
        return;
    }

    const Vec2f ab { b.x - a.x, b.y - a.y };
    const Vec2f ad { d.x - a.x, d.y - a.y };
    const float det = ab.x * ad.y - ab.y * ad.x;
    if (std::abs(det) < 1e-6f) {
        return;
    }

    const Vec2f c { b.x + d.x - a.x, b.y + d.y - a.y };
    const float minX = std::floor(std::min(std::min(a.x, b.x), std::min(c.x, d.x)));
    const float maxX = std::ceil(std::max(std::max(a.x, b.x), std::max(c.x, d.x)));
    const float minY = std::floor(std::min(std::min(a.y, b.y), std::min(c.y, d.y)));
    const float maxY = std::ceil(std::max(std::max(a.y, b.y), std::max(c.y, d.y)));

    for (int py = static_cast<int>(minY); py <= static_cast<int>(maxY); ++py) {
        for (int px = static_cast<int>(minX); px <= static_cast<int>(maxX); ++px) {
            const float fx = static_cast<float>(px) + 0.5f;
            const float fy = static_cast<float>(py) + 0.5f;
            const float rx = fx - a.x;
            const float ry = fy - a.y;

            const float u = (rx * ad.y - ry * ad.x) / det;
            const float v = (ab.x * ry - ab.y * rx) / det;
            if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
                continue;
            }

            const float sampleU = uvA.x + (uvB.x - uvA.x) * u + (uvD.x - uvA.x) * v;
            const float sampleV = uvA.y + (uvB.y - uvA.y) * u + (uvD.y - uvA.y) * v;
            const int tx = static_cast<int>(std::round(std::clamp(sampleU, 0.0f, 1.0f) * static_cast<float>(srcAtlas.tileSize - 1)));
            const int ty = static_cast<int>(std::round(std::clamp(sampleV, 0.0f, 1.0f) * static_cast<float>(srcAtlas.tileSize - 1)));

            auto rgba = sampleTileNearest(srcAtlas, srcPixels, tileIndex, tx, ty);
            if (rgba[3] == 0) {
                continue;
            }

            rgba[0] = static_cast<uint8_t>(std::round(std::clamp(static_cast<float>(rgba[0]) * shade * tint.r, 0.0f, 255.0f)));
            rgba[1] = static_cast<uint8_t>(std::round(std::clamp(static_cast<float>(rgba[1]) * shade * tint.g, 0.0f, 255.0f)));
            rgba[2] = static_cast<uint8_t>(std::round(std::clamp(static_cast<float>(rgba[2]) * shade * tint.b, 0.0f, 255.0f)));

            alphaBlendOver(iconAtlasPixels,
                           atlasWidth,
                           atlasHeight,
                           iconOriginX + px,
                           iconOriginY + py,
                           rgba);
        }
    }
}

void drawFaceParallelogram(std::vector<unsigned char>& iconAtlasPixels,
                           const int atlasWidth,
                           const int atlasHeight,
                           const TextureAtlas& srcAtlas,
                           const std::vector<unsigned char>& srcPixels,
                           const int tileIndex,
                           const int iconOriginX,
                           const int iconOriginY,
                           const Vec2f& a,
                           const Vec2f& b,
                           const Vec2f& d,
                           const float shade,
                           const bool flipV = false,
                           const IconTint& tint = {}) {
    const Vec2f uvA {0.0f, flipV ? 1.0f : 0.0f};
    const Vec2f uvB {1.0f, flipV ? 1.0f : 0.0f};
    const Vec2f uvD {0.0f, flipV ? 0.0f : 1.0f};
    drawTexturedParallelogram(iconAtlasPixels,
                              atlasWidth,
                              atlasHeight,
                              srcAtlas,
                              srcPixels,
                              tileIndex,
                              iconOriginX,
                              iconOriginY,
                              a,
                              b,
                              d,
                              uvA,
                              uvB,
                              uvD,
                              shade,
                              tint);
}

void drawCrossPlantIcon(std::vector<unsigned char>& iconAtlasPixels,
                        const int atlasWidth,
                        const int atlasHeight,
                        const TextureAtlas& srcAtlas,
                        const std::vector<unsigned char>& srcPixels,
                        const int tileIndex,
                        const int iconOriginX,
                        const int iconOriginY,
                        const float unit,
                        const bool applyBiomeTint) {
    const IconTint tint = applyBiomeTint ? biomeIconTint(BiomeTintKind::Foliage) : IconTint{};

    const Vec2f a1 { 8.0f * unit, 5.0f * unit };
    const Vec2f b1 { 22.0f * unit, 11.0f * unit };
    const Vec2f d1 { 8.0f * unit, 29.0f * unit };

    const Vec2f a2 { 24.0f * unit, 5.0f * unit };
    const Vec2f b2 { 10.0f * unit, 11.0f * unit };
    const Vec2f d2 { 24.0f * unit, 29.0f * unit };

    drawFaceParallelogram(iconAtlasPixels, atlasWidth, atlasHeight,
                          srcAtlas, srcPixels, tileIndex,
                          iconOriginX, iconOriginY,
                          a1, b1, d1,
                          0.92f,
                          false,
                          tint);
    drawFaceParallelogram(iconAtlasPixels, atlasWidth, atlasHeight,
                          srcAtlas, srcPixels, tileIndex,
                          iconOriginX, iconOriginY,
                          a2, b2, d2,
                          0.78f,
                          false,
                          tint);
}

void drawModelBlockIcon(std::vector<unsigned char>& iconAtlasPixels,
                        const int atlasWidth,
                        const int atlasHeight,
                        const TextureAtlas& srcAtlas,
                        const std::vector<unsigned char>& srcPixels,
                        const BlockTextureLibrary& blockTextures,
                        const BlockID blockId,
                        const BlockDef& def,
                        const int iconOriginX,
                        const int iconOriginY,
                        const float unit) {
    const BlockStateId stateId = BlockStateRegistry::getDefaultState(blockId);
    const ModelVariant* variant = BlockStateRegistry::getModelVariant(stateId);
    if (variant == nullptr || variant->model == nullptr) {
        failBlockIconAtlasBuilder("Model block is missing an icon model variant: " +
                                 BlockRegistry::getNamespacedId(blockId).full());
    }

    std::vector<IconModelFace> faces;
    const BlockModel& model = *variant->model;
    for (const ModelElement& element : model.elements) {
        for (int faceIndex = 0; faceIndex < 6; ++faceIndex) {
            const std::unique_ptr<ModelFace>& facePtr = element.faces[static_cast<size_t>(faceIndex)];
            if (!facePtr) {
                continue;
            }

            const int transformedFace = transformIconFaceIndex(faceIndex, variant->transform);
            if (!isVisibleIconFace(transformedFace)) {
                continue;
            }

            const ModelFace& face = *facePtr;
            std::array<glm::vec3, 4> corners = buildIconModelFaceCorners(element, faceIndex);
            for (glm::vec3& corner : corners) {
                corner = applyIconModelTransform(corner, variant->transform);
            }

            const std::string textureName = resolveIconModelFaceTextureName(model, face);
            const TextureAnimationInfo textureInfo = blockTextures.textureAnimation(textureName);
            const int tileIndex = blockTextures.arrayLayerToAtlasTile(textureInfo.firstLayer);
            if (tileIndex < 0) {
                failBlockIconAtlasBuilder("Model icon texture is not present in the block atlas: " + textureName);
            }

            const std::array<Vec2f, 4> uv = buildIconModelFaceUv(face);
            const glm::vec3 center = (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25f;

            IconModelFace iconFace;
            iconFace.a = projectIconPoint(corners[0], unit);
            iconFace.b = projectIconPoint(corners[1], unit);
            iconFace.d = projectIconPoint(corners[3], unit);
            iconFace.uvA = uv[0];
            iconFace.uvB = uv[1];
            iconFace.uvD = uv[3];
            iconFace.tileIndex = tileIndex;
            iconFace.shade = iconFaceShade(transformedFace);
            iconFace.tint = face.tintIndex >= 0 ? biomeIconTint(def.biomeTint) : IconTint{};
            iconFace.depth = center.x + center.z;
            iconFace.face = transformedFace;
            faces.push_back(iconFace);
        }
    }

    std::sort(faces.begin(), faces.end(), [](const IconModelFace& lhs, const IconModelFace& rhs) {
        if (std::abs(lhs.depth - rhs.depth) > 1e-5f) {
            return lhs.depth > rhs.depth;
        }
        return lhs.face != 0 && rhs.face == 0;
    });

    for (const IconModelFace& face : faces) {
        drawTexturedParallelogram(iconAtlasPixels,
                                  atlasWidth,
                                  atlasHeight,
                                  srcAtlas,
                                  srcPixels,
                                  face.tileIndex,
                                  iconOriginX,
                                  iconOriginY,
                                  face.a,
                                  face.b,
                                  face.d,
                                  face.uvA,
                                  face.uvB,
                                  face.uvD,
                                  face.shade,
                                  face.tint);
    }
}

} // namespace

namespace resource {

TextureAtlas buildBlockIconAtlas(int iconSize,
                                 const TextureAtlas& blockAtlas,
                                 const std::vector<unsigned char>& blockAtlasPixels,
                                 const BlockTextureLibrary& blockTextures) {
    if (iconSize < 16) {
        iconSize = 16;
    }
    if ((iconSize % 2) != 0) {
        ++iconSize;
    }

    const int kBlockTypeCount = static_cast<int>(BlockRegistry::getBlockCount());
    const int tilesPerRow = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(kBlockTypeCount))));
    const int numRows = static_cast<int>(std::ceil(static_cast<float>(kBlockTypeCount) / static_cast<float>(tilesPerRow)));
    const int atlasWidth = tilesPerRow * iconSize;
    const int atlasHeight = numRows * iconSize;

    std::vector<unsigned char> iconAtlasPixels(static_cast<size_t>(atlasWidth) * static_cast<size_t>(atlasHeight) * 4, 0);

    const float unit = static_cast<float>(iconSize) / 32.0f;
    const Vec2f topA { 4.0f * unit, 21.0f * unit };
    const Vec2f topB { 16.0f * unit, 27.0f * unit };
    const Vec2f topD { 16.0f * unit, 15.0f * unit };
    const Vec2f rightA { 16.0f * unit, 15.0f * unit };
    const Vec2f rightB { 28.0f * unit, 21.0f * unit };
    const Vec2f rightD { 16.0f * unit, 1.0f * unit };
    const Vec2f leftA { 4.0f * unit, 21.0f * unit };
    const Vec2f leftB { 16.0f * unit, 15.0f * unit };
    const Vec2f leftD { 4.0f * unit, 7.0f * unit };

    for (int id = 0; id < kBlockTypeCount; ++id) {
        const BlockID blockId = static_cast<BlockID>(id);
        if (blockId == 0) {
            continue;
        }

        const BlockDef& def = BlockRegistry::get(blockId);
        const int tileCol = id % tilesPerRow;
        const int tileRow = id / tilesPerRow;
        const int iconOriginX = tileCol * iconSize;
        const int iconOriginY = tileRow * iconSize;

        if (def.renderShapeName == "model") {
            drawModelBlockIcon(iconAtlasPixels,
                               atlasWidth,
                               atlasHeight,
                               blockAtlas,
                               blockAtlasPixels,
                               blockTextures,
                               blockId,
                               def,
                               iconOriginX,
                               iconOriginY,
                               unit);
            continue;
        }

        if (def.renderShape == BlockRenderShape::Cross) {
            int crossTex = blockTextures.arrayLayerToAtlasTile(def.faceTop.firstLayer);
            if (crossTex < 0) {
                crossTex = blockTextures.arrayLayerToAtlasTile(def.faceFront.firstLayer);
            }
            if (crossTex < 0) {
                continue;
            }

            drawCrossPlantIcon(iconAtlasPixels,
                               atlasWidth,
                               atlasHeight,
                               blockAtlas,
                               blockAtlasPixels,
                               crossTex,
                               iconOriginX,
                               iconOriginY,
                               unit,
                               def.biomeTint != BiomeTintKind::None);
            continue;
        }

        int topTex = blockTextures.arrayLayerToAtlasTile(def.faceTop.firstLayer);
        int rightTex = blockTextures.arrayLayerToAtlasTile(def.faceRight.firstLayer);
        int leftTex = blockTextures.arrayLayerToAtlasTile(def.faceFront.firstLayer);

        if (topTex < 0) {
            topTex = blockTextures.arrayLayerToAtlasTile(def.faceFront.firstLayer);
        }
        if (rightTex < 0) {
            rightTex = blockTextures.arrayLayerToAtlasTile(def.faceFront.firstLayer);
        }
        if (leftTex < 0) {
            leftTex = blockTextures.arrayLayerToAtlasTile(def.faceTop.firstLayer);
        }
        if (topTex < 0 || rightTex < 0 || leftTex < 0) {
            continue;
        }

        const IconTint tint = biomeIconTint(def.biomeTint);

        drawFaceParallelogram(iconAtlasPixels, atlasWidth, atlasHeight,
                              blockAtlas, blockAtlasPixels, leftTex,
                              iconOriginX, iconOriginY,
                              leftA, leftB, leftD,
                              0.68f,
                              true,
                              tint);
        drawFaceParallelogram(iconAtlasPixels, atlasWidth, atlasHeight,
                              blockAtlas, blockAtlasPixels, rightTex,
                              iconOriginX, iconOriginY,
                              rightA, rightB, rightD,
                              0.83f,
                              true,
                              tint);
        drawFaceParallelogram(iconAtlasPixels, atlasWidth, atlasHeight,
                              blockAtlas, blockAtlasPixels, topTex,
                              iconOriginX, iconOriginY,
                              topA, topB, topD,
                              1.0f,
                              false,
                              tint);
    }

    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlasWidth, atlasHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, iconAtlasPixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    TextureAtlas atlas;
    atlas.textureID = textureID;
    atlas.atlasWidth = atlasWidth;
    atlas.atlasHeight = atlasHeight;
    atlas.tileSize = iconSize;
    atlas.tileStride = iconSize;
    atlas.tilePadding = 0;
    atlas.tilesPerRow = tilesPerRow;
    if (!registerTextureAtlas(atlas)) {
        glDeleteTextures(1, &textureID);
        failBlockIconAtlasBuilder("Failed to register block icon atlas RHI handle");
    }
    return atlas;
}

} // namespace resource
