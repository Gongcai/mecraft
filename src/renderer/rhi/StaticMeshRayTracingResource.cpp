#include "renderer/rhi/StaticMeshRayTracingResource.h"

#include "renderer/contracts/StaticMeshRayTracingContract.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace renderer::rt {
namespace {

[[nodiscard]] bool sameBuffer(const RhiBufferHandle left, const RhiBufferHandle right) {
    return left.index == right.index && left.generation == right.generation;
}

[[nodiscard]] bool validGeometry(const StaticMeshRayTracingGeometry& geometry,
                                 const std::vector<renderer::contracts::GpuMaterial>& materials,
                                 const std::vector<renderer::contracts::StableMaterialId>& materialIds) {
    using namespace renderer::contracts;
    const GpuSceneGeometry& gpu = geometry.gpu;
    const uint32_t materialIndex = gpu.materialAndIdentity.x;
    const GpuSceneGeometryFlags flags = gpu.vertexLayoutAndFlags.w;
    const GpuSceneGeometryFlags surfaceClass = flags & kGpuSceneGeometrySurfaceClassMask;
    if (materialIndex >= materials.size() || materialIndex >= materialIds.size()) {
        return false;
    }
    const uint32_t alphaMode = materials[materialIndex].modesAndFlags.x;
    const bool materialMatchesSurfaceClass = (surfaceClass == gpuSceneGeometryFlagBit(GpuSceneGeometryFlag::Opaque) &&
                                              alphaMode == static_cast<uint32_t>(GpuMaterialAlphaMode::Opaque)) ||
                                             (surfaceClass == gpuSceneGeometryFlagBit(GpuSceneGeometryFlag::Cutout) &&
                                              alphaMode == static_cast<uint32_t>(GpuMaterialAlphaMode::Mask));
    const bool validPrimitiveAreas =
        std::all_of(geometry.primitiveAreaVectors.begin(), geometry.primitiveAreaVectors.end(),
                    [](const glm::vec3& areaVector) {
                        return std::isfinite(areaVector.x) && std::isfinite(areaVector.y) &&
                               std::isfinite(areaVector.z) && glm::dot(areaVector, areaVector) > 1.0e-16f;
                    });
    return validPrimitiveAreas && geometry.vertexBuffer.isValid() && geometry.indexBuffer.isValid() &&
           geometry.primitiveMetadataBuffer.isValid() && !sameBuffer(geometry.vertexBuffer, geometry.indexBuffer) &&
           !sameBuffer(geometry.vertexBuffer, geometry.primitiveMetadataBuffer) &&
           !sameBuffer(geometry.indexBuffer, geometry.primitiveMetadataBuffer) && gpu.vertexAddress.isValid() &&
           gpu.indexAddress.isValid() && gpu.primitiveMetadataAddress.isValid() &&
           gpu.vertexLayoutAndFlags.x == kStaticMeshRayTracingVertexStride &&
           gpu.vertexLayoutAndFlags.y == kStaticMeshRayTracingPositionOffset && gpu.vertexLayoutAndFlags.z != 0u &&
           (surfaceClass == gpuSceneGeometryFlagBit(GpuSceneGeometryFlag::Opaque) ||
            surfaceClass == gpuSceneGeometryFlagBit(GpuSceneGeometryFlag::Cutout)) &&
           hasGpuSceneGeometryFlag(flags, GpuSceneGeometryFlag::RayTracingVisible) && gpu.indexRangeAndType.y != 0u &&
           gpu.indexRangeAndType.y % 3u == 0u &&
           gpu.indexRangeAndType.z == static_cast<uint32_t>(GpuSceneIndexType::Uint32) &&
           gpu.indexRangeAndType.w == kGpuSceneContractVersion && materialMatchesSurfaceClass &&
           materialIds[materialIndex].isValid() && gpu.materialAndIdentity.y == materialIds[materialIndex].value &&
           gpu.materialAndIdentity.z != 0u && gpu.materialAndIdentity.w == sizeof(StaticMeshPrimitiveMetadata) &&
           gpu.primitiveMeshletAndRevision.x == gpu.indexRangeAndType.y / 3u &&
           geometry.primitiveAreaVectors.size() == gpu.primitiveMeshletAndRevision.x &&
           gpu.primitiveMeshletAndRevision.w == kStaticMeshRayTracingGeometryRevision;
}

} // namespace

std::shared_ptr<StaticMeshRayTracingResource>
StaticMeshRayTracingResource::create(const uint64_t bindlessIdentity,
                                     std::shared_ptr<const renderer::core::GlobalBindlessLifetime> bindlessLifetime,
                                     std::vector<renderer::contracts::GpuMaterial> materials,
                                     std::vector<renderer::contracts::StableMaterialId> materialIds,
                                     std::vector<StaticMeshRayTracingGeometry> geometries) {
    if (bindlessIdentity == 0u || bindlessLifetime == nullptr || materials.empty() ||
        materials.size() != materialIds.size() || geometries.empty() ||
        materials.size() > std::numeric_limits<uint32_t>::max() ||
        geometries.size() > std::numeric_limits<uint32_t>::max()) {
        return nullptr;
    }
    for (std::size_t index = 0u; index < materials.size(); ++index) {
        if (!materialIds[index].isValid() ||
            materials[index].modesAndFlags.w != renderer::contracts::kGpuMaterialContractVersion) {
            return nullptr;
        }
    }

    glm::vec3 boundsMin(geometries.front().gpu.localBoundsMin);
    glm::vec3 boundsMax(geometries.front().gpu.localBoundsMax);
    for (const StaticMeshRayTracingGeometry& geometry : geometries) {
        if (!validGeometry(geometry, materials, materialIds)) {
            return nullptr;
        }
        boundsMin = glm::min(boundsMin, glm::vec3(geometry.gpu.localBoundsMin));
        boundsMax = glm::max(boundsMax, glm::vec3(geometry.gpu.localBoundsMax));
    }
    return std::shared_ptr<StaticMeshRayTracingResource>(
        new StaticMeshRayTracingResource(bindlessIdentity, std::move(bindlessLifetime), std::move(materials),
                                         std::move(materialIds), std::move(geometries), boundsMin, boundsMax));
}

StaticMeshRayTracingResource::StaticMeshRayTracingResource(
    const uint64_t bindlessIdentity, std::shared_ptr<const renderer::core::GlobalBindlessLifetime> bindlessLifetime,
    std::vector<renderer::contracts::GpuMaterial> materials,
    std::vector<renderer::contracts::StableMaterialId> materialIds,
    std::vector<StaticMeshRayTracingGeometry> geometries, const glm::vec3 localBoundsMin,
    const glm::vec3 localBoundsMax)
    : m_bindlessIdentity(bindlessIdentity), m_bindlessLifetime(std::move(bindlessLifetime)),
      m_materials(std::move(materials)), m_materialIds(std::move(materialIds)), m_geometries(std::move(geometries)),
      m_localBoundsMin(localBoundsMin), m_localBoundsMax(localBoundsMax) {}

} // namespace renderer::rt
