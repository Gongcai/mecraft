#ifndef MECRAFT_VOXEL_LIGHT_REGISTRY_H
#define MECRAFT_VOXEL_LIGHT_REGISTRY_H

#include "renderer/contracts/GpuLightContract.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

class IWorldView;

namespace renderer::lighting {

/// Maintains stable analytic-light proxies for the active voxel chunk set.
/// Chunk contents are rescanned only after their local block revision changes;
/// camera-relative GPU records are rebuilt every frame from cached sources.
class VoxelLightRegistry final {
public:
    VoxelLightRegistry();
    ~VoxelLightRegistry();

    VoxelLightRegistry(const VoxelLightRegistry&) = delete;
    VoxelLightRegistry& operator=(const VoxelLightRegistry&) = delete;

    /// Builds the complete camera-relative light snapshot for one world view.
    /// @param worldView Active loaded chunks and monotonic world revisions.
    /// @param cameraPositionMeters World-space camera position used as floating origin.
    /// @param lights Destination replaced only after every source validates.
    /// @return True when synchronization and GPU normalization both succeed.
    [[nodiscard]] bool buildSceneLights(const IWorldView& worldView, const glm::vec3& cameraPositionMeters,
                                        std::vector<renderer::contracts::SceneLight>& lights);

    /// Retires every cached source and detaches the current world owner.
    void reset();

    /// Returns the source-set revision, excluding camera-relative movement.
    [[nodiscard]] uint64_t lightRevision() const;

    /// Returns the number of active cached voxel light sources.
    [[nodiscard]] std::size_t sourceCount() const;

    /// Returns the precise synchronization or normalization failure.
    [[nodiscard]] const std::string& lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace renderer::lighting

#endif // MECRAFT_VOXEL_LIGHT_REGISTRY_H
