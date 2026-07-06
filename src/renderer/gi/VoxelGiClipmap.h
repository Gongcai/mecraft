#ifndef MECRAFT_VOXEL_GI_CLIPMAP_H
#define MECRAFT_VOXEL_GI_CLIPMAP_H

#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../../world/block/Block.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

enum class VoxelGiClipmapUpdateMode : uint8_t {
    Disabled = 0,
    Idle = 1,
    Full = 2,
    Shifted = 3,
};

struct VoxelGiClipmapStats {
    VoxelGiClipmapUpdateMode mode = VoxelGiClipmapUpdateMode::Disabled;
    bool valid = false;
    int resolution = 0;
    int mipLevels = 0;
    glm::ivec3 originBlock = glm::ivec3(0);
    glm::ivec3 deltaVoxels = glm::ivec3(0);
    uint64_t sampledVoxels = 0;
    uint64_t reusedVoxels = 0;
    uint64_t uploadedVoxels = 0;
    uint64_t copiedVoxels = 0;
    int uploadedBoxes = 0;
    float skyRadianceScale = 0.0f;
    float sunRadianceScale = 0.0f;
};

class IBlockTextureColorProvider;

/// Maintains a camera-centered 3D radiance clipmap for stable low-frequency GI.
class VoxelGiClipmap {
public:
    VoxelGiClipmap() = default;
    ~VoxelGiClipmap();

    VoxelGiClipmap(const VoxelGiClipmap&) = delete;
    VoxelGiClipmap& operator=(const VoxelGiClipmap&) = delete;

    void shutdown();
    void update(const FrameContext& ctx,
                const VoxelGiSettings& settings,
                const IBlockTextureColorProvider& textureColors);

    [[nodiscard]] uint32_t texture() const { return m_texture; }
    [[nodiscard]] bool valid() const { return m_valid && m_texture != 0; }
    [[nodiscard]] glm::vec3 origin() const { return m_origin; }
    [[nodiscard]] float voxelSize() const { return m_voxelSize; }
    [[nodiscard]] int resolution() const { return m_resolution; }
    [[nodiscard]] int mipLevels() const { return m_mipLevels; }
    [[nodiscard]] const VoxelGiClipmapStats& stats() const { return m_stats; }

private:
    struct ClipmapVoxel {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float occupancy = 0.0f;
    };

    struct LightingSampleParams {
        glm::vec3 sunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 sunTint = glm::vec3(1.0f);
        glm::vec3 skyTint = glm::vec3(1.0f);
        float skyRadianceScale = 0.0f;
        float sunRadianceScale = 0.0f;
        float skyBounceStrength = 1.0f;
        float sunBounceStrength = 1.0f;
    };

    void allocateTexture(int resolution);
    void uploadFullVolume();
    [[nodiscard]] int uploadShiftedVolume(const glm::ivec3& deltaVoxels);
    [[nodiscard]] bool uploadSubVolume(int x, int y, int z, int width, int height, int depth);
    void copyOverlapThroughScratch(const glm::ivec3& deltaVoxels);
    void rebuildVolume(const IWorldView& worldView,
                       const IBlockTextureColorProvider& textureColors,
                       const LightingSampleParams& lighting,
                       const glm::ivec3& originBlock);
    [[nodiscard]] bool shiftCachedVolume(const IWorldView& worldView,
                                         const IBlockTextureColorProvider& textureColors,
                                         const LightingSampleParams& lighting,
                                         const glm::ivec3& originBlock,
                                         const glm::ivec3& deltaVoxels);
    [[nodiscard]] bool computeVoxelDelta(const glm::ivec3& originBlock, glm::ivec3& outDeltaVoxels) const;
    [[nodiscard]] uint64_t overlapVoxelCount(const glm::ivec3& deltaVoxels) const;
    void updateStatsBase(VoxelGiClipmapUpdateMode mode, const glm::ivec3& originBlock);
    [[nodiscard]] size_t voxelIndex(int x, int y, int z) const;
    [[nodiscard]] glm::ivec3 voxelWorldPosition(const glm::ivec3& originBlock, int x, int y, int z) const;
    [[nodiscard]] glm::ivec3 computeOrigin(const glm::vec3& cameraPosition,
                                           int resolution,
                                           float voxelSize,
                                           int originSnap) const;
    [[nodiscard]] const glm::vec3& cachedMaterialAlbedo(BlockID blockId,
                                                        const BlockDef& def,
                                                        const IBlockTextureColorProvider& textureColors);
    [[nodiscard]] ClipmapVoxel sampleWorldVoxel(const IWorldView& worldView,
                                                const IBlockTextureColorProvider& textureColors,
                                                const LightingSampleParams& lighting,
                                                const glm::ivec3& blockPos);

    uint32_t m_texture = 0;
    uint32_t m_shiftScratchTexture = 0;
    bool m_valid = false;
    int m_resolution = 0;
    int m_mipLevels = 1;
    float m_voxelSize = 1.0f;
    glm::vec3 m_origin = glm::vec3(0.0f);
    glm::ivec3 m_originBlock = glm::ivec3(0);
    std::vector<ClipmapVoxel> m_voxels;
    std::vector<glm::vec3> m_materialAlbedoCache;
    std::vector<uint8_t> m_materialAlbedoCacheValid;
    VoxelGiClipmapStats m_stats;
    uint64_t m_lastActiveChunkRevision = 0;
    uint64_t m_lastBlockContentRevision = 0;
    float m_lastSkyRadianceScale = -1.0f;
    float m_lastSunRadianceScale = -1.0f;
    float m_lastSkyBounceStrength = -1.0f;
    float m_lastSunBounceStrength = -1.0f;
    glm::vec3 m_lastSunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
    uint64_t m_lastUpdateFrame = 0;
};

#endif // MECRAFT_VOXEL_GI_CLIPMAP_H
