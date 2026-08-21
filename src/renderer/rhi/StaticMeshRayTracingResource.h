#ifndef MECRAFT_STATIC_MESH_RAY_TRACING_RESOURCE_H
#define MECRAFT_STATIC_MESH_RAY_TRACING_RESOURCE_H

#include "renderer/contracts/GpuMaterialContract.h"
#include "renderer/contracts/GpuSceneContract.h"
#include "renderer/contracts/SceneIdentityContract.h"
#include "renderer/core/GlobalBindlessSet.h"
#include "renderer/rhi/RhiHandles.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace renderer::rt {

/// Binds one canonical GPU Scene geometry record to the exact BLAS-retained buffers supplying its addresses.
struct StaticMeshRayTracingGeometry final {
    renderer::contracts::GpuSceneGeometry gpu;
    RhiBufferHandle vertexBuffer;
    RhiBufferHandle indexBuffer;
    RhiBufferHandle primitiveMetadataBuffer;
    std::vector<glm::vec3> primitiveAreaVectors;
};

/// Stores immutable asset-level Geometry/Material records consumed by every TLAS instance of one static mesh.
class StaticMeshRayTracingResource final {
public:
    /// Creates one validated immutable asset snapshot.
    /// @param bindlessIdentity Global Bindless Set generation containing every material texture and sampler.
    /// @param bindlessLifetime Shared owner that keeps the published RHI resources alive.
    /// @param materials Asset-relative material table in importer order.
    /// @param materialIds Stable IDs parallel to materials.
    /// @param geometries BLAS geometry-order records and their retained source buffers.
    /// @return Shared immutable resource, or nullptr when any table relationship or surface/alpha class is invalid.
    [[nodiscard]] static std::shared_ptr<StaticMeshRayTracingResource>
    create(uint64_t bindlessIdentity, std::shared_ptr<const renderer::core::GlobalBindlessLifetime> bindlessLifetime,
           std::vector<renderer::contracts::GpuMaterial> materials,
           std::vector<renderer::contracts::StableMaterialId> materialIds,
           std::vector<StaticMeshRayTracingGeometry> geometries);

    [[nodiscard]] uint64_t bindlessIdentity() const { return m_bindlessIdentity; }
    [[nodiscard]] const std::vector<renderer::contracts::GpuMaterial>& materials() const { return m_materials; }
    [[nodiscard]] const std::vector<renderer::contracts::StableMaterialId>& materialIds() const {
        return m_materialIds;
    }
    [[nodiscard]] const std::vector<StaticMeshRayTracingGeometry>& geometries() const { return m_geometries; }
    [[nodiscard]] const glm::vec3& localBoundsMin() const { return m_localBoundsMin; }
    [[nodiscard]] const glm::vec3& localBoundsMax() const { return m_localBoundsMax; }

private:
    StaticMeshRayTracingResource(uint64_t bindlessIdentity,
                                 std::shared_ptr<const renderer::core::GlobalBindlessLifetime> bindlessLifetime,
                                 std::vector<renderer::contracts::GpuMaterial> materials,
                                 std::vector<renderer::contracts::StableMaterialId> materialIds,
                                 std::vector<StaticMeshRayTracingGeometry> geometries, glm::vec3 localBoundsMin,
                                 glm::vec3 localBoundsMax);

    uint64_t m_bindlessIdentity = 0u;
    std::shared_ptr<const renderer::core::GlobalBindlessLifetime> m_bindlessLifetime;
    std::vector<renderer::contracts::GpuMaterial> m_materials;
    std::vector<renderer::contracts::StableMaterialId> m_materialIds;
    std::vector<StaticMeshRayTracingGeometry> m_geometries;
    glm::vec3 m_localBoundsMin{0.0f};
    glm::vec3 m_localBoundsMax{0.0f};
};

using StaticMeshRayTracingResourcePtr = std::shared_ptr<const StaticMeshRayTracingResource>;

} // namespace renderer::rt

#endif // MECRAFT_STATIC_MESH_RAY_TRACING_RESOURCE_H
