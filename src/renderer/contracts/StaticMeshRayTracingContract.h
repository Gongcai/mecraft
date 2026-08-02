#ifndef MECRAFT_STATIC_MESH_RAY_TRACING_CONTRACT_H
#define MECRAFT_STATIC_MESH_RAY_TRACING_CONTRACT_H

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace renderer::contracts {

inline constexpr uint32_t kStaticMeshRayTracingContractVersion = 1u;
inline constexpr uint32_t kStaticMeshRayTracingGeometryRevision = 1u;
inline constexpr uint32_t kStaticMeshRayTracingVertexStride = 48u;
inline constexpr uint32_t kStaticMeshRayTracingPositionOffset = 0u;
inline constexpr uint32_t kStaticMeshRayTracingNormalOffset = 12u;
inline constexpr uint32_t kStaticMeshRayTracingTangentOffset = 24u;
inline constexpr uint32_t kStaticMeshRayTracingUvOffset = 40u;

/// Stores immutable identity data for one indexed static-mesh triangle.
struct alignas(16) StaticMeshPrimitiveMetadata final {
    uint32_t materialIndex = 0u;
    uint32_t stableMaterialId = 0u;
    uint32_t stableGeometryId = 0u;
    uint32_t contractVersion = kStaticMeshRayTracingContractVersion;

    [[nodiscard]] bool operator==(const StaticMeshPrimitiveMetadata& other) const {
        return materialIndex == other.materialIndex && stableMaterialId == other.stableMaterialId &&
               stableGeometryId == other.stableGeometryId && contractVersion == other.contractVersion;
    }
};

static_assert(sizeof(StaticMeshPrimitiveMetadata) == 16u);
static_assert(alignof(StaticMeshPrimitiveMetadata) == 16u);
static_assert(std::is_standard_layout_v<StaticMeshPrimitiveMetadata>);
static_assert(std::is_trivially_copyable_v<StaticMeshPrimitiveMetadata>);

} // namespace renderer::contracts

#endif // MECRAFT_STATIC_MESH_RAY_TRACING_CONTRACT_H
