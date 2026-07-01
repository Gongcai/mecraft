#ifndef MECRAFT_VOXEL_GI_CLIPMAP_H
#define MECRAFT_VOXEL_GI_CLIPMAP_H

#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

/// Maintains a camera-centered 3D radiance clipmap for stable low-frequency GI.
class VoxelGiClipmap {
public:
    VoxelGiClipmap() = default;
    ~VoxelGiClipmap();

    VoxelGiClipmap(const VoxelGiClipmap&) = delete;
    VoxelGiClipmap& operator=(const VoxelGiClipmap&) = delete;

    void shutdown();
    void update(const FrameContext& ctx, const VoxelGiSettings& settings);

    [[nodiscard]] GLuint texture() const { return m_texture; }
    [[nodiscard]] bool valid() const { return m_valid && m_texture != 0; }
    [[nodiscard]] glm::vec3 origin() const { return m_origin; }
    [[nodiscard]] float voxelSize() const { return m_voxelSize; }
    [[nodiscard]] int resolution() const { return m_resolution; }
    [[nodiscard]] int mipLevels() const { return m_mipLevels; }

private:
    struct ClipmapVoxel {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float occupancy = 0.0f;
    };

    void allocateTexture(int resolution);
    void upload(const std::vector<ClipmapVoxel>& voxels);
    [[nodiscard]] glm::ivec3 computeOrigin(const glm::vec3& cameraPosition, int resolution, float voxelSize) const;
    [[nodiscard]] ClipmapVoxel sampleWorldVoxel(const IWorldView& worldView, const glm::ivec3& blockPos) const;

    GLuint m_texture = 0;
    bool m_valid = false;
    int m_resolution = 0;
    int m_mipLevels = 1;
    float m_voxelSize = 1.0f;
    glm::vec3 m_origin = glm::vec3(0.0f);
    glm::ivec3 m_originBlock = glm::ivec3(0);
    uint64_t m_lastActiveChunkRevision = 0;
    uint64_t m_lastBlockContentRevision = 0;
    uint64_t m_lastUpdateFrame = 0;
};

#endif // MECRAFT_VOXEL_GI_CLIPMAP_H
