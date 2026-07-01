#include "VoxelGiClipmap.h"

#include "../debug/RenderDebugLabels.h"
#include "../../world/IWorldView.h"
#include "../../world/block/Block.h"
#include "../../world/block/BlockStateRegistry.h"

#include <algorithm>
#include <cmath>

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

[[nodiscard]] int mipLevelCount(const int resolution) {
    int levels = 1;
    int size = std::max(1, resolution);
    while (size > 1) {
        size /= 2;
        ++levels;
    }
    return levels;
}

[[nodiscard]] glm::vec3 materialAlbedo(const BlockDef& def) {
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
    m_valid = false;
    m_resolution = 0;
    m_mipLevels = 1;
    m_lastActiveChunkRevision = 0;
    m_lastBlockContentRevision = 0;
    m_lastUpdateFrame = 0;
}

void VoxelGiClipmap::update(const FrameContext& ctx, const VoxelGiSettings& settings) {
    if (!settings.enabled || ctx.worldView == nullptr) {
        m_valid = false;
        return;
    }

    const int resolution = normalizedResolution(settings.resolution);
    const float voxelSize = normalizedVoxelSize(settings.voxelSize);
    const int mipLevels = mipLevelCount(resolution);
    const int updateInterval = normalizedUpdateInterval(settings.updateInterval);
    const glm::ivec3 originBlock = computeOrigin(ctx.camera.position, resolution, voxelSize);
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
        return;
    }

    allocateTexture(resolution);
    m_resolution = resolution;
    m_mipLevels = mipLevels;
    m_voxelSize = voxelSize;
    m_originBlock = originBlock;
    m_origin = glm::vec3(originBlock);

    std::vector<ClipmapVoxel> voxels(static_cast<size_t>(resolution) *
                                     static_cast<size_t>(resolution) *
                                     static_cast<size_t>(resolution));
    for (int z = 0; z < resolution; ++z) {
        for (int y = 0; y < resolution; ++y) {
            for (int x = 0; x < resolution; ++x) {
                const glm::ivec3 blockPos = originBlock + glm::ivec3(
                    static_cast<int>(std::floor(static_cast<float>(x) * voxelSize)),
                    static_cast<int>(std::floor(static_cast<float>(y) * voxelSize)),
                    static_cast<int>(std::floor(static_cast<float>(z) * voxelSize)));
                const size_t index = (static_cast<size_t>(z) * static_cast<size_t>(resolution) +
                                      static_cast<size_t>(y)) * static_cast<size_t>(resolution) +
                                      static_cast<size_t>(x);
                voxels[index] = sampleWorldVoxel(*ctx.worldView, blockPos);
            }
        }
    }

    upload(voxels);
    m_lastActiveChunkRevision = activeRevision;
    m_lastBlockContentRevision = blockRevision;
    m_lastUpdateFrame = ctx.frameIndex;
    m_valid = true;
}

void VoxelGiClipmap::allocateTexture(const int resolution) {
    if (m_texture != 0 && m_resolution == resolution) {
        return;
    }

    if (m_texture != 0) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
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
}

void VoxelGiClipmap::upload(const std::vector<ClipmapVoxel>& voxels) {
    glTextureSubImage3D(m_texture, 0, 0, 0, 0,
                        m_resolution, m_resolution, m_resolution,
                        GL_RGBA, GL_FLOAT, voxels.data());
    glGenerateTextureMipmap(m_texture);
}

glm::ivec3 VoxelGiClipmap::computeOrigin(const glm::vec3& cameraPosition,
                                         const int resolution,
                                         const float voxelSize) const {
    const float span = static_cast<float>(resolution) * voxelSize;
    const glm::vec3 minCorner = cameraPosition - glm::vec3(span * 0.5f);
    return glm::ivec3(
        static_cast<int>(std::floor(minCorner.x / voxelSize) * voxelSize),
        static_cast<int>(std::floor(minCorner.y / voxelSize) * voxelSize),
        static_cast<int>(std::floor(minCorner.z / voxelSize) * voxelSize));
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
