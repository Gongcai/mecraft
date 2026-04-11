//
// Created by Caiwe on 2026/3/21.
//

#include "ResourceMgr.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <filesystem>
#include <vector>
#include "../third_party/stb/stb_image.h"
#include "../world/Block.h"

namespace {
struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;
};

inline int clampInt(const int value, const int minValue, const int maxValue) {
    return std::max(minValue, std::min(maxValue, value));
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
                           const bool flipV = false) {
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

            const int tx = static_cast<int>(std::round(u * static_cast<float>(srcAtlas.tileSize - 1)));
            const float sampleV = flipV ? (1.0f - v) : v;
            const int ty = static_cast<int>(std::round(sampleV * static_cast<float>(srcAtlas.tileSize - 1)));

            auto rgba = sampleTileNearest(srcAtlas, srcPixels, tileIndex, tx, ty);
            if (rgba[3] == 0) {
                continue;
            }

            rgba[0] = static_cast<uint8_t>(std::round(std::clamp(static_cast<float>(rgba[0]) * shade, 0.0f, 255.0f)));
            rgba[1] = static_cast<uint8_t>(std::round(std::clamp(static_cast<float>(rgba[1]) * shade, 0.0f, 255.0f)));
            rgba[2] = static_cast<uint8_t>(std::round(std::clamp(static_cast<float>(rgba[2]) * shade, 0.0f, 255.0f)));

            alphaBlendOver(iconAtlasPixels,
                           atlasWidth,
                           atlasHeight,
                           iconOriginX + px,
                           iconOriginY + py,
                           rgba);
        }
    }
}
}

std::pair<glm::vec2, glm::vec2> TextureAtlas::getUV(int tileIndex) const {
    if (tilesPerRow == 0 || atlasWidth == 0 || atlasHeight == 0 || tileSize <= 0) {
        return {{0,0}, {0,0}};
    }

    const int stride = std::max(1, tileStride);
    int tileCol = tileIndex % tilesPerRow;
    int tileRow = tileIndex / tilesPerRow;

    const int startX = tileCol * stride + tilePadding;
    const int startY = tileRow * stride + tilePadding;

    // Use tile boundaries (not texel centers) so the full 16x16 area maps onto a face.
    // A tiny inset keeps UVs strictly inside the tile to avoid border tie-break artifacts.
    const float innerInsetPx = 1e-3f;
    float uMin = (static_cast<float>(startX) + innerInsetPx) / static_cast<float>(atlasWidth);
    float vMin = (static_cast<float>(startY) + innerInsetPx) / static_cast<float>(atlasHeight);
    float uMax = (static_cast<float>(startX + tileSize) - innerInsetPx) / static_cast<float>(atlasWidth);
    float vMax = (static_cast<float>(startY + tileSize) - innerInsetPx) / static_cast<float>(atlasHeight);

    if (uMin > uMax) {
        const float uCenter = (uMin + uMax) * 0.5f;
        uMin = uCenter;
        uMax = uCenter;
    }
    if (vMin > vMax) {
        const float vCenter = (vMin + vMax) * 0.5f;
        vMin = vCenter;
        vMax = vCenter;
    }

    // 返回 pair： { 左下角UV(uMin, vMin), 右上角UV(uMax, vMax) }
    return { glm::vec2(uMin, vMin), glm::vec2(uMax, vMax) };
}

void ResourceMgr::init() {
    loadShader("chunk", "../assets/shaders/chunk.vs", "../assets/shaders/chunk.fs");
    loadShader("outline", "../assets/shaders/outline.vs", "../assets/shaders/outline.fs");
    loadShader("break_overlay", "../assets/shaders/break_overlay.vs", "../assets/shaders/break_overlay.fs");
    loadShader("crosshair", "../assets/shaders/crosshair.vs", "../assets/shaders/crosshair.fs");
    loadShader("inventory", "../assets/shaders/inventory.vs", "../assets/shaders/inventory.fs");
    loadShader("text", "../assets/shaders/text.vs", "../assets/shaders/text.fs");
    loadShader("particle", "../assets/shaders/particle.vs", "../assets/shaders/particle.fs");
    loadShader("postprocess", "../assets/shaders/postprocess.vs", "../assets/shaders/postprocess.fs");
}

void ResourceMgr::shutdown() {
    for (auto& [_, textureId] : m_guiTextures) {
        if (textureId != 0) {
            glDeleteTextures(1, &textureId);
        }
    }
    m_guiTextures.clear();

    if (m_atlas.textureID != 0) {
        glDeleteTextures(1, &m_atlas.textureID);
        m_atlas.textureID = 0;
    }

    if (m_itemIconAtlas.textureID != 0) {
        glDeleteTextures(1, &m_itemIconAtlas.textureID);
        m_itemIconAtlas.textureID = 0;
    }

    m_blockAtlasPixels.clear();
}

Shader *ResourceMgr::loadShader(const std::string &name, const char *vertPath, const char *fragPath) {
    std::pair<std::string, std::unique_ptr<Shader>> key;
    key.first = name;
    key.second = std::make_unique<Shader>(vertPath,fragPath);
    m_shaders.insert(std::move(key));
    return m_shaders[name].get();
}

Shader *ResourceMgr::getShader(const std::string &name) {
    auto it = m_shaders.find(name);
    if (it != m_shaders.end()) {
        return it->second.get();
    }
    return nullptr;
}

GLuint ResourceMgr::loadTexture(const std::string &path, bool alpha) {
    //TODO: 完善贴图读取
    return 0;
}

GLuint ResourceMgr::loadGuiTexture(const std::string& name, const std::string& path, bool flipVertically) {
    auto existing = m_guiTextures.find(name);
    if (existing != m_guiTextures.end()) {
        return existing->second;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(flipVertically ? 1 : 0);
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) {
#ifndef NDEBUG
        std::cerr << "Failed to load GUI texture: " << path << "\n";
#endif
        return 0;
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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);

    m_guiTextures[name] = textureID;
    return textureID;
}

GLuint ResourceMgr::getGuiTexture(const std::string& name) const {
    auto it = m_guiTextures.find(name);
    if (it != m_guiTextures.end()) {
        return it->second;
    }
    return 0;
}

GLuint ResourceMgr::getTexture(const std::string &name) const {
    auto it = m_textures.find(name);
    if (it != m_textures.end()) {
        return it->second;
    }
    return 0;
}

#if defined(GL_TEXTURE_MAX_ANISOTROPY)
constexpr GLenum kTextureMaxAnisotropyPName = GL_TEXTURE_MAX_ANISOTROPY;
#elif defined(GL_TEXTURE_MAX_ANISOTROPY_EXT)
constexpr GLenum kTextureMaxAnisotropyPName = GL_TEXTURE_MAX_ANISOTROPY_EXT;
#else
constexpr GLenum kTextureMaxAnisotropyPName = 0;
#endif

#if defined(GL_MAX_TEXTURE_MAX_ANISOTROPY)
constexpr GLenum kMaxTextureMaxAnisotropyPName = GL_MAX_TEXTURE_MAX_ANISOTROPY;
#elif defined(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT)
constexpr GLenum kMaxTextureMaxAnisotropyPName = GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT;
#else
constexpr GLenum kMaxTextureMaxAnisotropyPName = 0;
#endif

inline bool supportsAnisotropicFiltering() {
    return kTextureMaxAnisotropyPName != 0 && kMaxTextureMaxAnisotropyPName != 0;
}

void ResourceMgr::buildTextureAtlas(const std::string &directory, int tileSize) {
    namespace fs = std::filesystem;
    std::vector<fs::path> imagePaths;
    m_textures.clear();

    if (fs::exists(directory)) {
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.path().extension() == ".png") {
                imagePaths.push_back(entry.path());
            }
        }
    }

    // Keep atlas indices stable across runs/platforms.
    std::sort(imagePaths.begin(), imagePaths.end(),
              [](const fs::path& a, const fs::path& b) {
                  return a.filename().string() < b.filename().string();
              });

    if (imagePaths.empty()) {
#ifndef NDEBUG
        std::cerr << "Texture Atlas generated with 0 images!\n";
#endif
        return;
    }

    const int numTiles = static_cast<int>(imagePaths.size());
    const int tilesPerRow = static_cast<int>(std::ceil(std::sqrt(numTiles)));
    const int numRows = static_cast<int>(std::ceil(static_cast<float>(numTiles) / static_cast<float>(tilesPerRow)));

    // Per-tile gutters prevent cross-tile color bleeding in mip levels.
    constexpr int kTilePadding = 2;
    const int tileStride = tileSize + kTilePadding * 2;

    const int atlasWidth = tilesPerRow * tileStride;
    const int atlasHeight = numRows * tileStride;
    std::vector<unsigned char> atlasPixels(static_cast<size_t>(atlasWidth) * static_cast<size_t>(atlasHeight) * 4, 0);

    stbi_set_flip_vertically_on_load(true);

    for (int i = 0; i < numTiles; ++i) {
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* data = stbi_load(imagePaths[i].string().c_str(), &width, &height, &channels, 4);

        if (!data) {
#ifndef NDEBUG
            std::cerr << "Failed to load image: " << imagePaths[i] << "\n";
#endif
            continue;
        }

        if (width != tileSize || height != tileSize) {
#ifndef NDEBUG
            std::cerr << "Warning: Texture size mismatch! " << imagePaths[i] << "\n";
#endif
        }

        const int copyWidth = std::min(tileSize, width);
        const int copyHeight = std::min(tileSize, height);

        const int tileCol = i % tilesPerRow;
        const int tileRow = i / tilesPerRow;
        const int tileBaseX = tileCol * tileStride;
        const int tileBaseY = tileRow * tileStride;
        const int innerStartX = tileBaseX + kTilePadding;
        const int innerStartY = tileBaseY + kTilePadding;

        for (int y = 0; y < copyHeight; ++y) {
            for (int x = 0; x < copyWidth; ++x) {
                const int destIndex = ((innerStartY + y) * atlasWidth + (innerStartX + x)) * 4;
                const int srcIndex = (y * width + x) * 4;
                atlasPixels[destIndex + 0] = data[srcIndex + 0];
                atlasPixels[destIndex + 1] = data[srcIndex + 1];
                atlasPixels[destIndex + 2] = data[srcIndex + 2];
                atlasPixels[destIndex + 3] = data[srcIndex + 3];
            }
        }

        // Duplicate edge texels into padding so lower mip levels stay inside the same tile.
        for (int y = 0; y < copyHeight; ++y) {
            const int srcY = innerStartY + y;
            const int leftSrcX = innerStartX;
            const int rightSrcX = innerStartX + copyWidth - 1;
            for (int p = 1; p <= kTilePadding; ++p) {
                const int dstLeftX = innerStartX - p;
                const int dstRightX = innerStartX + copyWidth - 1 + p;
                const int leftSrcIndex = (srcY * atlasWidth + leftSrcX) * 4;
                const int rightSrcIndex = (srcY * atlasWidth + rightSrcX) * 4;
                const int dstLeftIndex = (srcY * atlasWidth + dstLeftX) * 4;
                const int dstRightIndex = (srcY * atlasWidth + dstRightX) * 4;
                for (int c = 0; c < 4; ++c) {
                    atlasPixels[dstLeftIndex + c] = atlasPixels[leftSrcIndex + c];
                    atlasPixels[dstRightIndex + c] = atlasPixels[rightSrcIndex + c];
                }
            }
        }

        for (int x = -kTilePadding; x < copyWidth + kTilePadding; ++x) {
            const int srcX = innerStartX + std::clamp(x, 0, copyWidth - 1);
            const int topSrcY = innerStartY;
            const int bottomSrcY = innerStartY + copyHeight - 1;
            for (int p = 1; p <= kTilePadding; ++p) {
                const int dstTopY = innerStartY - p;
                const int dstBottomY = innerStartY + copyHeight - 1 + p;
                const int topSrcIndex = (topSrcY * atlasWidth + srcX) * 4;
                const int bottomSrcIndex = (bottomSrcY * atlasWidth + srcX) * 4;
                const int dstTopIndex = (dstTopY * atlasWidth + srcX) * 4;
                const int dstBottomIndex = (dstBottomY * atlasWidth + srcX) * 4;
                for (int c = 0; c < 4; ++c) {
                    atlasPixels[dstTopIndex + c] = atlasPixels[topSrcIndex + c];
                    atlasPixels[dstBottomIndex + c] = atlasPixels[bottomSrcIndex + c];
                }
            }
        }

        stbi_image_free(data);

        std::string texName = imagePaths[i].stem().string();
        m_textures[texName] = i;
    }

    if (m_atlas.textureID != 0) {
        glDeleteTextures(1, &m_atlas.textureID);
        m_atlas.textureID = 0;
    }

    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlasWidth, atlasHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlasPixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);

    // Guard atlas tiles from cross-bleeding on very low mip levels.
    const int fullChainMaxLevel = static_cast<int>(std::floor(std::log2(static_cast<float>(std::max(atlasWidth, atlasHeight)))));
    const int paddingSafeMaxLevel = (kTilePadding > 0)
        ? static_cast<int>(std::floor(std::log2(static_cast<float>(kTilePadding))))
        : 0;
    const int clampedMaxLevel = std::max(0, std::min(fullChainMaxLevel, paddingSafeMaxLevel));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, clampedMaxLevel);

    if (supportsAnisotropicFiltering()) {
        GLfloat maxAniso = 1.0f;
        glGetFloatv(kMaxTextureMaxAnisotropyPName, &maxAniso);
        m_atlasMaxAnisotropy = std::max(1.0f, static_cast<float>(maxAniso));
        m_atlasAnisotropy = std::clamp(m_atlasAnisotropy, 1.0f, m_atlasMaxAnisotropy);
        glTexParameterf(GL_TEXTURE_2D, kTextureMaxAnisotropyPName, m_atlasAnisotropy);
    } else {
        m_atlasMaxAnisotropy = 1.0f;
        m_atlasAnisotropy = 1.0f;
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    m_atlas.textureID = textureID;
    m_atlas.atlasWidth = atlasWidth;
    m_atlas.atlasHeight = atlasHeight;
    m_atlas.tileSize = tileSize;
    m_atlas.tileStride = tileStride;
    m_atlas.tilePadding = kTilePadding;
    m_atlas.tilesPerRow = tilesPerRow;
    m_blockAtlasPixels = std::move(atlasPixels);
}

const TextureAtlas & ResourceMgr::getAtlas() const {
    return m_atlas;
}

void ResourceMgr::buildBlockIconAtlas(int iconSize) {
    if (iconSize < 16) {
        iconSize = 16;
    }
    if ((iconSize % 2) != 0) {
        ++iconSize;
    }
    if (m_atlas.textureID == 0 || m_blockAtlasPixels.empty() || m_atlas.tileSize <= 0 || m_atlas.tilesPerRow <= 0) {
        return;
    }

    constexpr int kBlockTypeCount = static_cast<int>(BlockType::COUNT);
    const int tilesPerRow = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(kBlockTypeCount))));
    const int numRows = static_cast<int>(std::ceil(static_cast<float>(kBlockTypeCount) / static_cast<float>(tilesPerRow)));
    const int atlasWidth = tilesPerRow * iconSize;
    const int atlasHeight = numRows * iconSize;

    std::vector<unsigned char> iconAtlasPixels(static_cast<size_t>(atlasWidth) * static_cast<size_t>(atlasHeight) * 4, 0);

    const float unit = static_cast<float>(iconSize) / 32.0f;
    // Minecraft-like item icon projection: larger footprint and taller side faces.
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
        if (blockId == BlockType::AIR) {
            continue;
        }

        const BlockDef& def = BlockRegistry::get(blockId);
        int topTex = def.texTop;
        int rightTex = def.texRight;
        int leftTex = def.texFront;

        if (topTex < 0) {
            topTex = def.texFront;
        }
        if (rightTex < 0) {
            rightTex = def.texFront;
        }
        if (leftTex < 0) {
            leftTex = def.texTop;
        }
        if (topTex < 0 || rightTex < 0 || leftTex < 0) {
            continue;
        }

        const int tileCol = id % tilesPerRow;
        const int tileRow = id / tilesPerRow;
        const int iconOriginX = tileCol * iconSize;
        const int iconOriginY = tileRow * iconSize;

        drawFaceParallelogram(iconAtlasPixels, atlasWidth, atlasHeight,
                              m_atlas, m_blockAtlasPixels, leftTex,
                              iconOriginX, iconOriginY,
                              leftA, leftB, leftD,
                              0.68f,
                              true);
        drawFaceParallelogram(iconAtlasPixels, atlasWidth, atlasHeight,
                              m_atlas, m_blockAtlasPixels, rightTex,
                              iconOriginX, iconOriginY,
                              rightA, rightB, rightD,
                              0.83f,
                              true);
        drawFaceParallelogram(iconAtlasPixels, atlasWidth, atlasHeight,
                              m_atlas, m_blockAtlasPixels, topTex,
                              iconOriginX, iconOriginY,
                              topA, topB, topD,
                              1.0f);
    }

    if (m_itemIconAtlas.textureID != 0) {
        glDeleteTextures(1, &m_itemIconAtlas.textureID);
        m_itemIconAtlas.textureID = 0;
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

    m_itemIconAtlas.textureID = textureID;
    m_itemIconAtlas.atlasWidth = atlasWidth;
    m_itemIconAtlas.atlasHeight = atlasHeight;
    m_itemIconAtlas.tileSize = iconSize;
    m_itemIconAtlas.tileStride = iconSize;
    m_itemIconAtlas.tilePadding = 0;
    m_itemIconAtlas.tilesPerRow = tilesPerRow;
}

const TextureAtlas& ResourceMgr::getItemIconAtlas() const {
    return m_itemIconAtlas;
}

float ResourceMgr::getAtlasAnisotropy() const {
    return m_atlasAnisotropy;
}

float ResourceMgr::getAtlasMaxAnisotropy() const {
    return m_atlasMaxAnisotropy;
}

void ResourceMgr::setAtlasAnisotropy(const float anisotropy) {
    const float clamped = std::clamp(anisotropy, 1.0f, std::max(1.0f, m_atlasMaxAnisotropy));
    if (std::abs(clamped - m_atlasAnisotropy) < 1e-4f) {
        return;
    }

    m_atlasAnisotropy = clamped;
    if (m_atlas.textureID == 0 || !supportsAnisotropicFiltering()) {
        return;
    }

    glBindTexture(GL_TEXTURE_2D, m_atlas.textureID);
    glTexParameterf(GL_TEXTURE_2D, kTextureMaxAnisotropyPName, m_atlasAnisotropy);
    glBindTexture(GL_TEXTURE_2D, 0);
}
