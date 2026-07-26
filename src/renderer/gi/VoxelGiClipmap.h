#ifndef MECRAFT_VOXEL_GI_CLIPMAP_H
#define MECRAFT_VOXEL_GI_CLIPMAP_H

#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiHandles.h"
#include "../rhi/RhiRenderGraph.h"
#include "../rhi/RhiTypes.h"
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
class RhiCommandList;
class RhiDevice;

/// Maintains a camera-centered 3D radiance clipmap for stable low-frequency GI.
class VoxelGiClipmap {
public:
    VoxelGiClipmap() = default;
    ~VoxelGiClipmap();

    VoxelGiClipmap(const VoxelGiClipmap&) = delete;
    VoxelGiClipmap& operator=(const VoxelGiClipmap&) = delete;

    void shutdown();

    /// Prepares the CPU clipmap update and adds its GPU copy stages to the graph.
    /// @param graph Graph receiving clipmap transfer passes and staging resources.
    /// @param ctx Frame state used to sample the world and lighting.
    /// @param settings Clipmap resolution, cadence, and radiance settings.
    /// @param textureColors Provider used to sample block material colors.
    /// @param rhiDevice Device owning the persistent clipmap and staging buffers.
    /// @param dependency Pass that must complete before the clipmap update starts.
    /// @return The final clipmap pass, the unchanged dependency when no update is
    /// required, or an invalid handle when preparation fails.
    [[nodiscard]] RgPassHandle addGraphPasses(
        RenderGraph& graph,
        const FrameContext& ctx,
        const VoxelGiSettings& settings,
        const IBlockTextureColorProvider& textureColors,
        RhiDevice& rhiDevice,
        RgPassHandle dependency);

    /// Commits or rejects the prepared CPU state after graph submission.
    /// @param succeeded True when the complete graph recorded and submitted.
    void finishGraphExecution(bool succeeded);

    /// Reports whether a GPU clipmap update is awaiting graph completion.
    /// @return True between addGraphPasses and finishGraphExecution.
    [[nodiscard]] bool graphFramePrepared() const {
        return m_graphFramePrepared;
    }

    [[nodiscard]] RhiTextureHandle textureHandle() const { return m_texture; }
    [[nodiscard]] bool valid() const { return m_valid && m_texture.isValid(); }
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

    struct PendingUpload {
        RhiBufferHandle stagingBuffer;
        RhiBufferDesc stagingDesc;
        RhiBufferTextureCopy copy;
    };

    [[nodiscard]] bool allocateTexture(int resolution, RhiDevice& rhiDevice);
    [[nodiscard]] bool prepareFullVolumeUpload(RhiDevice& rhiDevice);
    [[nodiscard]] int prepareShiftedVolumeUpload(const glm::ivec3& deltaVoxels,
                                                 RhiDevice& rhiDevice);
    [[nodiscard]] bool prepareSubVolumeUpload(int x, int y, int z,
                                              int width, int height, int depth,
                                              RhiDevice& rhiDevice);
    void prepareShiftCopies(const glm::ivec3& deltaVoxels);
    [[nodiscard]] bool recordFullVolumeUpload(RhiCommandList& commandList) const;
    [[nodiscard]] bool recordShiftToScratch(RhiCommandList& commandList) const;
    [[nodiscard]] bool recordShiftedVolumeUpload(RhiCommandList& commandList) const;
    void releasePendingUploads();
    void resetPendingGraphState();
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

    RhiDevice* m_rhiDevice = nullptr;
    RhiTextureHandle m_texture;
    RhiTextureHandle m_shiftScratchTexture;
    RhiResourceState m_textureState = RhiResourceState::Undefined;
    RhiResourceState m_shiftScratchTextureState = RhiResourceState::Undefined;
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

    bool m_graphFramePrepared = false;
    VoxelGiClipmapUpdateMode m_pendingMode = VoxelGiClipmapUpdateMode::Disabled;
    std::vector<PendingUpload> m_pendingUploads;
    RhiTextureCopy m_pendingShiftToScratch;
    RhiTextureCopy m_pendingShiftFromScratch;
    VoxelGiClipmapStats m_pendingStats;
    LightingSampleParams m_pendingLighting;
    uint64_t m_pendingActiveChunkRevision = 0;
    uint64_t m_pendingBlockContentRevision = 0;
    uint64_t m_pendingUpdateFrame = 0;
};

#endif // MECRAFT_VOXEL_GI_CLIPMAP_H
