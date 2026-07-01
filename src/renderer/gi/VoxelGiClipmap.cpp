#include "VoxelGiClipmap.h"

#include "../debug/RenderDebugLabels.h"
#include "../../world/IWorldView.h"
#include "../../world/block/Block.h"
#include "../../world/block/BlockStateRegistry.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

namespace {

constexpr int kMinClipmapResolution = 16;
constexpr int kMaxClipmapResolution = 128;
constexpr float kMinVoxelSize = 1.0f;
constexpr float kMaxVoxelSize = 4.0f;

[[nodiscard]] int normalizedResolution(const int resolution) {
    return std::clamp(resolution, kMinClipmapResolution, kMaxClipmapResolution);
}

[[nodiscard]] float normalizedVoxelSize(const float voxelSize) {
    return std::clamp(voxelSize, kMinVoxelSize, kMaxVoxelSize);
}

[[nodiscard]] int normalizedUpdateInterval(const int updateInterval) {
    return std::clamp(updateInterval, 1, 60);
}

[[nodiscard]] int normalizedOriginSnap(const int originSnap) {
    return std::clamp(originSnap, 1, 64);
}

[[nodiscard]] int mipLevelCount(const int resolution) {
    int levels = 1;
    int size = std::max(1, resolution);
    while (size > 1) {
        size /= 2;
        ++levels;
    }
    return levels;
}

[[nodiscard]] bool containsToken(const std::string_view text, const std::string_view token) {
    return text.find(token) != std::string_view::npos;
}

[[nodiscard]] bool hasPrefix(const std::string_view text, const std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] std::optional<glm::vec3> dyedBlockAlbedo(const std::string_view path) {
    struct DyeColor {
        std::string_view prefix;
        glm::vec3 albedo;
    };
    constexpr DyeColor kDyes[] = {
        {"white_", glm::vec3(0.86f, 0.86f, 0.82f)},
        {"light_gray_", glm::vec3(0.58f, 0.58f, 0.56f)},
        {"gray_", glm::vec3(0.32f, 0.34f, 0.34f)},
        {"black_", glm::vec3(0.06f, 0.06f, 0.07f)},
        {"brown_", glm::vec3(0.34f, 0.21f, 0.13f)},
        {"red_", glm::vec3(0.60f, 0.12f, 0.10f)},
        {"orange_", glm::vec3(0.80f, 0.34f, 0.08f)},
        {"yellow_", glm::vec3(0.86f, 0.70f, 0.18f)},
        {"lime_", glm::vec3(0.46f, 0.74f, 0.17f)},
        {"green_", glm::vec3(0.22f, 0.42f, 0.14f)},
        {"cyan_", glm::vec3(0.12f, 0.52f, 0.58f)},
        {"light_blue_", glm::vec3(0.36f, 0.62f, 0.82f)},
        {"blue_", glm::vec3(0.18f, 0.28f, 0.62f)},
        {"purple_", glm::vec3(0.42f, 0.22f, 0.62f)},
        {"magenta_", glm::vec3(0.72f, 0.28f, 0.66f)},
        {"pink_", glm::vec3(0.86f, 0.47f, 0.60f)},
    };

    const bool dyedSurface = containsToken(path, "wool") ||
                             containsToken(path, "concrete") ||
                             containsToken(path, "terracotta") ||
                             containsToken(path, "stained_glass") ||
                             containsToken(path, "carpet");
    if (!dyedSurface) {
        return std::nullopt;
    }

    for (const DyeColor& dye : kDyes) {
        if (hasPrefix(path, dye.prefix)) {
            return dye.albedo;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<glm::vec3> namedBlockAlbedo(const BlockDef& def) {
    const std::string_view path = def.namespacedId.path();
    if (const std::optional<glm::vec3> dyed = dyedBlockAlbedo(path)) {
        return dyed;
    }

    if (path == "grass_block") return glm::vec3(0.42f, 0.55f, 0.24f);
    if (path == "podzol") return glm::vec3(0.36f, 0.25f, 0.14f);
    if (path == "mycelium") return glm::vec3(0.46f, 0.39f, 0.47f);
    if (path == "dirt" || path == "coarse_dirt" || path == "rooted_dirt") return glm::vec3(0.45f, 0.30f, 0.18f);
    if (path == "mud" || containsToken(path, "mud_bricks")) return glm::vec3(0.30f, 0.25f, 0.22f);
    if (path == "clay") return glm::vec3(0.50f, 0.56f, 0.62f);

    if (path == "sand" || containsToken(path, "sandstone")) return glm::vec3(0.76f, 0.68f, 0.45f);
    if (path == "red_sand" || containsToken(path, "red_sandstone")) return glm::vec3(0.70f, 0.35f, 0.16f);
    if (path == "gravel") return glm::vec3(0.46f, 0.44f, 0.42f);
    if (containsToken(path, "snow")) return glm::vec3(0.88f, 0.92f, 0.94f);
    if (containsToken(path, "ice")) return glm::vec3(0.58f, 0.78f, 0.92f);

    if (path == "stone" || containsToken(path, "cobblestone") || containsToken(path, "stone_bricks")) {
        return glm::vec3(0.50f, 0.50f, 0.48f);
    }
    if (containsToken(path, "deepslate") || containsToken(path, "tuff")) return glm::vec3(0.30f, 0.31f, 0.32f);
    if (containsToken(path, "granite")) return glm::vec3(0.58f, 0.38f, 0.31f);
    if (containsToken(path, "diorite")) return glm::vec3(0.70f, 0.70f, 0.68f);
    if (containsToken(path, "andesite")) return glm::vec3(0.46f, 0.48f, 0.46f);
    if (containsToken(path, "calcite")) return glm::vec3(0.78f, 0.76f, 0.70f);
    if (containsToken(path, "dripstone")) return glm::vec3(0.50f, 0.34f, 0.24f);
    if (containsToken(path, "basalt") || containsToken(path, "blackstone")) return glm::vec3(0.12f, 0.12f, 0.13f);
    if (containsToken(path, "netherrack")) return glm::vec3(0.45f, 0.13f, 0.12f);
    if (containsToken(path, "nether_bricks")) return glm::vec3(0.15f, 0.04f, 0.05f);
    if (containsToken(path, "end_stone")) return glm::vec3(0.76f, 0.74f, 0.47f);

    if (containsToken(path, "oak")) return glm::vec3(0.57f, 0.40f, 0.20f);
    if (containsToken(path, "spruce")) return glm::vec3(0.34f, 0.22f, 0.12f);
    if (containsToken(path, "birch")) return glm::vec3(0.72f, 0.62f, 0.40f);
    if (containsToken(path, "jungle")) return glm::vec3(0.50f, 0.31f, 0.16f);
    if (containsToken(path, "acacia")) return glm::vec3(0.64f, 0.29f, 0.11f);
    if (containsToken(path, "dark_oak")) return glm::vec3(0.28f, 0.17f, 0.09f);
    if (containsToken(path, "mangrove")) return glm::vec3(0.45f, 0.12f, 0.08f);
    if (containsToken(path, "cherry")) return glm::vec3(0.86f, 0.55f, 0.62f);
    if (containsToken(path, "crimson")) return glm::vec3(0.50f, 0.08f, 0.18f);
    if (containsToken(path, "warped")) return glm::vec3(0.10f, 0.48f, 0.42f);
    if (containsToken(path, "bamboo")) return glm::vec3(0.62f, 0.58f, 0.18f);
    if (containsToken(path, "leaves") || containsToken(path, "azalea")) return glm::vec3(0.24f, 0.46f, 0.16f);
    if (containsToken(path, "moss")) return glm::vec3(0.22f, 0.42f, 0.12f);

    if (containsToken(path, "diamond")) return glm::vec3(0.35f, 0.78f, 0.82f);
    if (containsToken(path, "emerald")) return glm::vec3(0.18f, 0.70f, 0.32f);
    if (containsToken(path, "lapis")) return glm::vec3(0.16f, 0.26f, 0.72f);
    if (containsToken(path, "redstone")) return glm::vec3(0.65f, 0.08f, 0.06f);
    if (containsToken(path, "copper")) return glm::vec3(0.72f, 0.42f, 0.25f);
    if (containsToken(path, "gold")) return glm::vec3(0.86f, 0.63f, 0.18f);
    if (containsToken(path, "iron")) return glm::vec3(0.70f, 0.68f, 0.62f);
    if (containsToken(path, "coal")) return glm::vec3(0.09f, 0.09f, 0.09f);
    if (containsToken(path, "quartz")) return glm::vec3(0.78f, 0.74f, 0.68f);
    if (containsToken(path, "amethyst")) return glm::vec3(0.62f, 0.42f, 0.82f);
    if (containsToken(path, "prismarine")) return glm::vec3(0.28f, 0.55f, 0.50f);

    return std::nullopt;
}

[[nodiscard]] glm::vec3 materialAlbedo(const BlockDef& def) {
    if (const std::optional<glm::vec3> named = namedBlockAlbedo(def)) {
        return *named;
    }

    switch (def.materialKind) {
        case BlockMaterialKinds::STONE: return glm::vec3(0.50f, 0.50f, 0.48f);
        case BlockMaterialKinds::DIRT: return glm::vec3(0.46f, 0.30f, 0.18f);
        case BlockMaterialKinds::GRASS: return glm::vec3(0.36f, 0.55f, 0.22f);
        case BlockMaterialKinds::WOOD: return glm::vec3(0.55f, 0.34f, 0.17f);
        case BlockMaterialKinds::LEAVES: return glm::vec3(0.25f, 0.48f, 0.16f);
        case BlockMaterialKinds::PLANT: return glm::vec3(0.34f, 0.56f, 0.20f);
        case BlockMaterialKinds::SAND: return glm::vec3(0.76f, 0.68f, 0.45f);
        case BlockMaterialKinds::ORE: return glm::vec3(0.50f, 0.50f, 0.50f);
        case BlockMaterialKinds::METAL: return glm::vec3(0.62f, 0.58f, 0.52f);
        case BlockMaterialKinds::ICE: return glm::vec3(0.58f, 0.78f, 0.92f);
        case BlockMaterialKinds::GLASS:
        case BlockMaterialKinds::STAINED_GLASS:
        case BlockMaterialKinds::WATER:
            return glm::vec3(0.0f);
        default: return glm::vec3(0.52f, 0.50f, 0.46f);
    }
}

[[nodiscard]] glm::vec3 emissiveColor(const BlockDef& def) {
    const std::string_view path = def.namespacedId.path();
    if (path == "lava") return glm::vec3(1.00f, 0.30f, 0.04f);
    if (containsToken(path, "soul")) return glm::vec3(0.24f, 0.68f, 1.00f);
    if (containsToken(path, "sea_lantern")) return glm::vec3(0.55f, 0.92f, 0.88f);
    if (containsToken(path, "glowstone")) return glm::vec3(1.00f, 0.78f, 0.34f);
    if (containsToken(path, "shroomlight")) return glm::vec3(1.00f, 0.45f, 0.18f);
    if (containsToken(path, "jack_o_lantern")) return glm::vec3(1.00f, 0.48f, 0.08f);
    if (containsToken(path, "torch") || containsToken(path, "lantern") || containsToken(path, "campfire")) {
        return glm::vec3(1.00f, 0.62f, 0.22f);
    }
    if (containsToken(path, "froglight")) {
        if (containsToken(path, "verdant")) return glm::vec3(0.72f, 1.00f, 0.56f);
        if (containsToken(path, "pearlescent")) return glm::vec3(1.00f, 0.62f, 0.78f);
        return glm::vec3(1.00f, 0.76f, 0.36f);
    }
    if (containsToken(path, "redstone")) return glm::vec3(1.00f, 0.05f, 0.02f);
    if (containsToken(path, "amethyst")) return glm::vec3(0.66f, 0.46f, 0.95f);

    switch (def.derivativeMaterialId) {
        case DerivativeMaterialIds::LAVA:
        case DerivativeMaterialIds::FIRE:
            return glm::vec3(1.00f, 0.34f, 0.06f);
        case DerivativeMaterialIds::SOUL_FIRE:
            return glm::vec3(0.28f, 0.72f, 1.00f);
        case DerivativeMaterialIds::SEA_LANTERN_LIKE:
            return glm::vec3(0.55f, 0.92f, 0.88f);
        case DerivativeMaterialIds::AMETHYST:
            return glm::vec3(0.66f, 0.46f, 0.95f);
        case DerivativeMaterialIds::REDSTONE:
            return glm::vec3(1.00f, 0.05f, 0.02f);
        default:
            return glm::vec3(1.00f, 0.76f, 0.38f);
    }
}

} // namespace

VoxelGiClipmap::~VoxelGiClipmap() {
    shutdown();
}

void VoxelGiClipmap::shutdown() {
    if (m_texture != 0) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    if (m_shiftScratchTexture != 0) {
        glDeleteTextures(1, &m_shiftScratchTexture);
        m_shiftScratchTexture = 0;
    }
    m_voxels.clear();
    m_valid = false;
    m_resolution = 0;
    m_mipLevels = 1;
    m_lastActiveChunkRevision = 0;
    m_lastBlockContentRevision = 0;
    m_lastUpdateFrame = 0;
    m_stats = {};
}

void VoxelGiClipmap::update(const FrameContext& ctx, const VoxelGiSettings& settings) {
    if (!settings.enabled || ctx.worldView == nullptr) {
        m_valid = false;
        m_stats = {};
        return;
    }

    const int resolution = normalizedResolution(settings.resolution);
    const float voxelSize = normalizedVoxelSize(settings.voxelSize);
    const int mipLevels = mipLevelCount(resolution);
    const int updateInterval = normalizedUpdateInterval(settings.updateInterval);
    const int originSnap = normalizedOriginSnap(settings.originSnap);
    const glm::ivec3 originBlock = computeOrigin(ctx.camera.position, resolution, voxelSize, originSnap);
    const uint64_t activeRevision = ctx.worldView->getActiveChunkRevision();
    const uint64_t blockRevision = ctx.worldView->getBlockContentRevision();

    const bool parametersChanged = resolution != m_resolution ||
                                   mipLevels != m_mipLevels ||
                                   std::abs(voxelSize - m_voxelSize) > 0.0001f;
    const bool originChanged = originBlock != m_originBlock;
    const bool worldChanged = activeRevision != m_lastActiveChunkRevision ||
                              blockRevision != m_lastBlockContentRevision;
    const bool intervalReady = ctx.frameIndex >= m_lastUpdateFrame + static_cast<uint64_t>(updateInterval);
    if (m_valid && !parametersChanged && !originChanged && !(worldChanged && intervalReady)) {
        updateStatsBase(VoxelGiClipmapUpdateMode::Idle, m_originBlock);
        return;
    }

    allocateTexture(resolution);
    glm::ivec3 deltaVoxels(0);
    const bool canReuseVolume = m_valid && !parametersChanged && originChanged && !worldChanged &&
                                computeVoxelDelta(originBlock, deltaVoxels);
    m_resolution = resolution;
    m_mipLevels = mipLevels;
    m_voxelSize = voxelSize;

    const bool shiftedVolume = canReuseVolume && shiftCachedVolume(*ctx.worldView, originBlock, deltaVoxels);
    if (!shiftedVolume) {
        rebuildVolume(*ctx.worldView, originBlock);
    }

    m_originBlock = originBlock;
    m_origin = glm::vec3(originBlock);
    if (shiftedVolume) {
        const uint64_t overlapVoxels = overlapVoxelCount(deltaVoxels);
        const uint64_t totalVoxels = static_cast<uint64_t>(m_resolution) *
                                     static_cast<uint64_t>(m_resolution) *
                                     static_cast<uint64_t>(m_resolution);
        updateStatsBase(VoxelGiClipmapUpdateMode::Shifted, originBlock);
        m_stats.deltaVoxels = deltaVoxels;
        m_stats.reusedVoxels = overlapVoxels;
        m_stats.sampledVoxels = totalVoxels - overlapVoxels;
        m_stats.uploadedVoxels = m_stats.sampledVoxels;
        m_stats.copiedVoxels = overlapVoxels;
        m_stats.uploadedBoxes = uploadShiftedVolume(deltaVoxels);
    } else {
        uploadFullVolume();
        const uint64_t totalVoxels = static_cast<uint64_t>(m_resolution) *
                                     static_cast<uint64_t>(m_resolution) *
                                     static_cast<uint64_t>(m_resolution);
        updateStatsBase(VoxelGiClipmapUpdateMode::Full, originBlock);
        m_stats.sampledVoxels = totalVoxels;
        m_stats.uploadedVoxels = totalVoxels;
        m_stats.uploadedBoxes = 1;
    }
    m_lastActiveChunkRevision = activeRevision;
    m_lastBlockContentRevision = blockRevision;
    m_lastUpdateFrame = ctx.frameIndex;
    m_valid = true;
    m_stats.valid = true;
}

void VoxelGiClipmap::allocateTexture(const int resolution) {
    if (m_texture != 0 && m_resolution == resolution) {
        return;
    }

    if (m_texture != 0) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    if (m_shiftScratchTexture != 0) {
        glDeleteTextures(1, &m_shiftScratchTexture);
        m_shiftScratchTexture = 0;
    }

    glCreateTextures(GL_TEXTURE_3D, 1, &m_texture);
    const int levels = mipLevelCount(resolution);
    glTextureStorage3D(m_texture, levels, GL_RGBA16F, resolution, resolution, resolution);
    glTextureParameteri(m_texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(m_texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_texture, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_texture, GL_TEXTURE_BASE_LEVEL, 0);
    glTextureParameteri(m_texture, GL_TEXTURE_MAX_LEVEL, levels - 1);
    renderer::debug::labelTexture(m_texture, "VoxelGI.ClipmapRadiance");

    glCreateTextures(GL_TEXTURE_3D, 1, &m_shiftScratchTexture);
    glTextureStorage3D(m_shiftScratchTexture, 1, GL_RGBA16F, resolution, resolution, resolution);
    glTextureParameteri(m_shiftScratchTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_shiftScratchTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(m_shiftScratchTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_shiftScratchTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_shiftScratchTexture, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    renderer::debug::labelTexture(m_shiftScratchTexture, "VoxelGI.ShiftScratch");
}

void VoxelGiClipmap::uploadFullVolume() {
    glTextureSubImage3D(m_texture, 0, 0, 0, 0,
                        m_resolution, m_resolution, m_resolution,
                        GL_RGBA, GL_FLOAT, m_voxels.data());
    glGenerateTextureMipmap(m_texture);
}

int VoxelGiClipmap::uploadShiftedVolume(const glm::ivec3& deltaVoxels) {
    copyOverlapThroughScratch(deltaVoxels);

    const auto axisRange = [this](const int delta, int& start, int& end) {
        if (delta > 0) {
            start = 0;
            end = m_resolution - delta;
        } else if (delta < 0) {
            start = -delta;
            end = m_resolution;
        } else {
            start = 0;
            end = m_resolution;
        }
    };

    int x0 = 0;
    int x1 = m_resolution;
    int y0 = 0;
    int y1 = m_resolution;
    int z0 = 0;
    int z1 = m_resolution;
    axisRange(deltaVoxels.x, x0, x1);
    axisRange(deltaVoxels.y, y0, y1);
    axisRange(deltaVoxels.z, z0, z1);

    int uploadedBoxes = 0;
    if (x0 > 0 && uploadSubVolume(0, 0, 0, x0, m_resolution, m_resolution)) ++uploadedBoxes;
    if (x1 < m_resolution && uploadSubVolume(x1, 0, 0, m_resolution - x1, m_resolution, m_resolution)) ++uploadedBoxes;
    if (y0 > 0 && x1 > x0 && uploadSubVolume(x0, 0, 0, x1 - x0, y0, m_resolution)) ++uploadedBoxes;
    if (y1 < m_resolution && x1 > x0 &&
        uploadSubVolume(x0, y1, 0, x1 - x0, m_resolution - y1, m_resolution)) {
        ++uploadedBoxes;
    }
    if (z0 > 0 && x1 > x0 && y1 > y0 && uploadSubVolume(x0, y0, 0, x1 - x0, y1 - y0, z0)) {
        ++uploadedBoxes;
    }
    if (z1 < m_resolution && x1 > x0 && y1 > y0) {
        if (uploadSubVolume(x0, y0, z1, x1 - x0, y1 - y0, m_resolution - z1)) {
            ++uploadedBoxes;
        }
    }

    glGenerateTextureMipmap(m_texture);
    return uploadedBoxes;
}

bool VoxelGiClipmap::uploadSubVolume(const int x,
                                     const int y,
                                     const int z,
                                     const int width,
                                     const int height,
                                     const int depth) {
    if (width <= 0 || height <= 0 || depth <= 0) {
        return false;
    }

    glPixelStorei(GL_UNPACK_ROW_LENGTH, m_resolution);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, m_resolution);
    glTextureSubImage3D(m_texture, 0, x, y, z, width, height, depth,
                        GL_RGBA, GL_FLOAT, m_voxels.data() + voxelIndex(x, y, z));
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    return true;
}

void VoxelGiClipmap::copyOverlapThroughScratch(const glm::ivec3& deltaVoxels) {
    const auto axisRange = [this](const int delta, int& srcStart, int& dstStart, int& size) {
        if (delta > 0) {
            srcStart = delta;
            dstStart = 0;
            size = m_resolution - delta;
        } else if (delta < 0) {
            srcStart = 0;
            dstStart = -delta;
            size = m_resolution + delta;
        } else {
            srcStart = 0;
            dstStart = 0;
            size = m_resolution;
        }
    };

    int srcX = 0;
    int srcY = 0;
    int srcZ = 0;
    int dstX = 0;
    int dstY = 0;
    int dstZ = 0;
    int sizeX = m_resolution;
    int sizeY = m_resolution;
    int sizeZ = m_resolution;
    axisRange(deltaVoxels.x, srcX, dstX, sizeX);
    axisRange(deltaVoxels.y, srcY, dstY, sizeY);
    axisRange(deltaVoxels.z, srcZ, dstZ, sizeZ);

    glCopyImageSubData(m_texture, GL_TEXTURE_3D, 0, srcX, srcY, srcZ,
                       m_shiftScratchTexture, GL_TEXTURE_3D, 0, dstX, dstY, dstZ,
                       sizeX, sizeY, sizeZ);
    glCopyImageSubData(m_shiftScratchTexture, GL_TEXTURE_3D, 0, dstX, dstY, dstZ,
                       m_texture, GL_TEXTURE_3D, 0, dstX, dstY, dstZ,
                       sizeX, sizeY, sizeZ);
}

void VoxelGiClipmap::rebuildVolume(const IWorldView& worldView, const glm::ivec3& originBlock) {
    const size_t voxelCount = static_cast<size_t>(m_resolution) *
                              static_cast<size_t>(m_resolution) *
                              static_cast<size_t>(m_resolution);
    m_voxels.assign(voxelCount, ClipmapVoxel{});
    for (int z = 0; z < m_resolution; ++z) {
        for (int y = 0; y < m_resolution; ++y) {
            for (int x = 0; x < m_resolution; ++x) {
                m_voxels[voxelIndex(x, y, z)] = sampleWorldVoxel(worldView, voxelWorldPosition(originBlock, x, y, z));
            }
        }
    }
}

bool VoxelGiClipmap::shiftCachedVolume(const IWorldView& worldView,
                                       const glm::ivec3& originBlock,
                                       const glm::ivec3& deltaVoxels) {
    const size_t voxelCount = static_cast<size_t>(m_resolution) *
                              static_cast<size_t>(m_resolution) *
                              static_cast<size_t>(m_resolution);
    if (m_voxels.size() != voxelCount) {
        return false;
    }

    if (std::abs(deltaVoxels.x) >= m_resolution ||
        std::abs(deltaVoxels.y) >= m_resolution ||
        std::abs(deltaVoxels.z) >= m_resolution) {
        return false;
    }

    std::vector<ClipmapVoxel> previous;
    previous.swap(m_voxels);
    m_voxels.assign(voxelCount, ClipmapVoxel{});

    for (int z = 0; z < m_resolution; ++z) {
        const int oldZ = z + deltaVoxels.z;
        for (int y = 0; y < m_resolution; ++y) {
            const int oldY = y + deltaVoxels.y;
            for (int x = 0; x < m_resolution; ++x) {
                const int oldX = x + deltaVoxels.x;
                const size_t index = voxelIndex(x, y, z);
                if (oldX >= 0 && oldX < m_resolution &&
                    oldY >= 0 && oldY < m_resolution &&
                    oldZ >= 0 && oldZ < m_resolution) {
                    m_voxels[index] = previous[voxelIndex(oldX, oldY, oldZ)];
                } else {
                    m_voxels[index] = sampleWorldVoxel(worldView, voxelWorldPosition(originBlock, x, y, z));
                }
            }
        }
    }

    return true;
}

bool VoxelGiClipmap::computeVoxelDelta(const glm::ivec3& originBlock, glm::ivec3& outDeltaVoxels) const {
    const glm::vec3 originDelta = glm::vec3(originBlock - m_originBlock) / m_voxelSize;
    outDeltaVoxels = glm::ivec3(
        static_cast<int>(std::round(originDelta.x)),
        static_cast<int>(std::round(originDelta.y)),
        static_cast<int>(std::round(originDelta.z)));
    const glm::vec3 snappedDelta = glm::vec3(outDeltaVoxels) * m_voxelSize;
    return glm::length(snappedDelta - glm::vec3(originBlock - m_originBlock)) <= 0.01f;
}

uint64_t VoxelGiClipmap::overlapVoxelCount(const glm::ivec3& deltaVoxels) const {
    const int extentX = std::max(0, m_resolution - std::abs(deltaVoxels.x));
    const int extentY = std::max(0, m_resolution - std::abs(deltaVoxels.y));
    const int extentZ = std::max(0, m_resolution - std::abs(deltaVoxels.z));
    return static_cast<uint64_t>(extentX) * static_cast<uint64_t>(extentY) * static_cast<uint64_t>(extentZ);
}

void VoxelGiClipmap::updateStatsBase(const VoxelGiClipmapUpdateMode mode, const glm::ivec3& originBlock) {
    m_stats = {};
    m_stats.mode = mode;
    m_stats.valid = m_valid;
    m_stats.resolution = m_resolution;
    m_stats.mipLevels = m_mipLevels;
    m_stats.originBlock = originBlock;
}

size_t VoxelGiClipmap::voxelIndex(const int x, const int y, const int z) const {
    return (static_cast<size_t>(z) * static_cast<size_t>(m_resolution) +
            static_cast<size_t>(y)) * static_cast<size_t>(m_resolution) +
           static_cast<size_t>(x);
}

glm::ivec3 VoxelGiClipmap::voxelWorldPosition(const glm::ivec3& originBlock,
                                              const int x,
                                              const int y,
                                              const int z) const {
    return originBlock + glm::ivec3(
        static_cast<int>(std::floor((static_cast<float>(x) + 0.5f) * m_voxelSize)),
        static_cast<int>(std::floor((static_cast<float>(y) + 0.5f) * m_voxelSize)),
        static_cast<int>(std::floor((static_cast<float>(z) + 0.5f) * m_voxelSize)));
}

glm::ivec3 VoxelGiClipmap::computeOrigin(const glm::vec3& cameraPosition,
                                         const int resolution,
                                         const float voxelSize,
                                         const int originSnap) const {
    const float span = static_cast<float>(resolution) * voxelSize;
    const glm::vec3 minCorner = cameraPosition - glm::vec3(span * 0.5f);
    const int snap = std::max(1, originSnap);
    return glm::ivec3(
        static_cast<int>(std::floor(minCorner.x / static_cast<float>(snap))) * snap,
        static_cast<int>(std::floor(minCorner.y / static_cast<float>(snap))) * snap,
        static_cast<int>(std::floor(minCorner.z / static_cast<float>(snap))) * snap);
}

VoxelGiClipmap::ClipmapVoxel VoxelGiClipmap::sampleWorldVoxel(const IWorldView& worldView,
                                                              const glm::ivec3& blockPos) const {
    ClipmapVoxel voxel;
    const BlockStateId stateId = worldView.getBlockState(blockPos.x, blockPos.y, blockPos.z);
    if (stateId == NULL_BLOCK_STATE) {
        return voxel;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    if (def.renderLayer == BlockRenderLayer::Transparent && !def.isLightSource) {
        return voxel;
    }

    const glm::vec3 albedo = materialAlbedo(def);
    const uint8_t packedLight = worldView.getPackedLight(blockPos.x, blockPos.y, blockPos.z);
    const float blockLight = static_cast<float>(packedLight & 0x0F) * (1.0f / 15.0f);
    const float skyLight = static_cast<float>((packedLight >> 4) & 0x0F) * (1.0f / 15.0f);
    const float lightLevel = static_cast<float>(def.lightLevel) * (1.0f / 15.0f);

    const glm::vec3 skyRadiance = albedo * (0.035f + skyLight * 0.145f);
    const glm::vec3 blockRadiance = albedo * (blockLight * 0.28f);
    const glm::vec3 emission = emissiveColor(def) * (std::pow(lightLevel, 1.35f) * 1.65f);
    const glm::vec3 radiance = skyRadiance + blockRadiance + emission;

    voxel.r = radiance.r;
    voxel.g = radiance.g;
    voxel.b = radiance.b;
    voxel.occupancy = def.renderLayer == BlockRenderLayer::Opaque ? 1.0f : 0.45f;
    return voxel;
}
