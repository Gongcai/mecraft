#include "renderer/contracts/GpuSceneContract.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/matrix.hpp>
#include <glm/vector_relational.hpp>

#include <cmath>
#include <cstdlib>

namespace renderer::contracts {
namespace {

constexpr float kAffineEpsilon = 1.0e-5f;
constexpr float kDeterminantEpsilon = 1.0e-8f;

[[nodiscard]] bool finiteMatrix(const glm::mat4& matrix) {
    for (uint32_t column = 0u; column < 4u; ++column) {
        for (uint32_t row = 0u; row < 4u; ++row) {
            if (!std::isfinite(matrix[column][row])) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool finiteVector(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool finiteVector(const glm::vec4& value) {
    return finiteVector(glm::vec3(value)) && std::isfinite(value.w);
}

[[nodiscard]] bool affineMatrix(const glm::mat4& matrix) {
    return std::abs(matrix[0][3]) <= kAffineEpsilon && std::abs(matrix[1][3]) <= kAffineEpsilon &&
           std::abs(matrix[2][3]) <= kAffineEpsilon && std::abs(matrix[3][3] - 1.0f) <= kAffineEpsilon;
}

[[nodiscard]] bool invertibleAffineMatrix(const glm::mat4& matrix) {
    const float determinant = glm::determinant(glm::mat3(matrix));
    return std::isfinite(determinant) && std::abs(determinant) > kDeterminantEpsilon;
}

[[nodiscard]] GpuSceneAffineTransform packAffineTransform(const glm::mat4& matrix) {
    return {{matrix[0][0], matrix[1][0], matrix[2][0], matrix[3][0]},
            {matrix[0][1], matrix[1][1], matrix[2][1], matrix[3][1]},
            {matrix[0][2], matrix[1][2], matrix[2][2], matrix[3][2]}};
}

[[nodiscard]] bool addressAligned(const uint64_t address, const uint64_t alignment) {
    return address != 0u && alignment != 0u && address % alignment == 0u;
}

[[nodiscard]] bool rangeFits(const uint32_t first, const uint32_t count) {
    return count <= std::numeric_limits<uint32_t>::max() - first;
}

[[nodiscard]] bool addressRangeFits(const uint64_t address, const uint64_t firstElement, const uint64_t elementCount,
                                    const uint64_t elementSize) {
    if (address == 0u || elementCount == 0u || elementSize == 0u ||
        firstElement > std::numeric_limits<uint64_t>::max() / elementSize) {
        return false;
    }
    const uint64_t firstByteOffset = firstElement * elementSize;
    if (address > std::numeric_limits<uint64_t>::max() - firstByteOffset) {
        return false;
    }
    const uint64_t firstByteAddress = address + firstByteOffset;
    return elementCount <= (std::numeric_limits<uint64_t>::max() - firstByteAddress) / elementSize;
}

[[nodiscard]] bool oneGeometrySurfaceClass(const GpuSceneGeometryFlags flags) {
    const GpuSceneGeometryFlags surfaceClass = flags & kGpuSceneGeometrySurfaceClassMask;
    return surfaceClass != 0u && (surfaceClass & (surfaceClass - 1u)) == 0u;
}

[[nodiscard]] GpuSceneInstanceNormalizationResult instanceFailure(const GpuSceneNormalizationError error,
                                                                  const GpuSceneField field) {
    return {{}, error, field};
}

[[nodiscard]] GpuSceneGeometryNormalizationResult geometryFailure(const GpuSceneNormalizationError error,
                                                                  const GpuSceneField field) {
    return {{}, error, field};
}

} // namespace

bool GpuSceneInstanceNormalizationResult::succeeded() const {
    return error == GpuSceneNormalizationError::None;
}

bool GpuSceneGeometryNormalizationResult::succeeded() const {
    return error == GpuSceneNormalizationError::None;
}

GpuSceneInstanceNormalizationResult normalizeGpuSceneInstance(const GpuSceneInstanceNormalizationInput& input) {
    if (!finiteMatrix(input.worldFromObject)) {
        return instanceFailure(GpuSceneNormalizationError::NonFiniteValue, GpuSceneField::WorldFromObject);
    }
    if (!finiteMatrix(input.previousWorldFromObject)) {
        return instanceFailure(GpuSceneNormalizationError::NonFiniteValue, GpuSceneField::PreviousWorldFromObject);
    }
    if (!affineMatrix(input.worldFromObject)) {
        return instanceFailure(GpuSceneNormalizationError::NonAffineTransform, GpuSceneField::WorldFromObject);
    }
    if (!affineMatrix(input.previousWorldFromObject)) {
        return instanceFailure(GpuSceneNormalizationError::NonAffineTransform, GpuSceneField::PreviousWorldFromObject);
    }
    if (!invertibleAffineMatrix(input.worldFromObject)) {
        return instanceFailure(GpuSceneNormalizationError::SingularTransform, GpuSceneField::WorldFromObject);
    }
    if (!finiteVector(input.worldBoundsCenterAndRadius)) {
        return instanceFailure(GpuSceneNormalizationError::NonFiniteValue, GpuSceneField::WorldBounds);
    }
    if (input.worldBoundsCenterAndRadius.w <= 0.0f) {
        return instanceFailure(GpuSceneNormalizationError::InvalidBounds, GpuSceneField::WorldBounds);
    }
    if (input.geometryCount == 0u || !rangeFits(input.geometryBase, input.geometryCount)) {
        return instanceFailure(GpuSceneNormalizationError::InvalidGeometryRange, GpuSceneField::GeometryRange);
    }
    if (input.materialBase == kGpuSceneInvalidTableIndex) {
        return instanceFailure(GpuSceneNormalizationError::ValueOutOfRange, GpuSceneField::MaterialBase);
    }
    if ((input.flags & ~kGpuSceneKnownInstanceFlags) != 0u) {
        return instanceFailure(GpuSceneNormalizationError::UnknownFlags, GpuSceneField::InstanceFlags);
    }
    if (!input.stableObjectId.isValid()) {
        return instanceFailure(GpuSceneNormalizationError::InvalidStableId, GpuSceneField::StableObjectId);
    }
    const bool rayTracingVisible = hasGpuSceneInstanceFlag(input.flags, GpuSceneInstanceFlag::RayTracingVisible);
    if (rayTracingVisible && input.rayTracingInstanceId > kGpuSceneMaxRayTracingInstanceId) {
        return instanceFailure(GpuSceneNormalizationError::InvalidRayTracingInstanceId,
                               GpuSceneField::RayTracingInstanceId);
    }
    if (!rayTracingVisible && input.rayTracingInstanceId != kGpuSceneInvalidRayTracingInstanceId) {
        return instanceFailure(GpuSceneNormalizationError::RayTracingStateConflict,
                               GpuSceneField::RayTracingInstanceId);
    }

    const glm::mat4 objectFromWorld = glm::inverse(input.worldFromObject);
    if (!finiteMatrix(objectFromWorld)) {
        return instanceFailure(GpuSceneNormalizationError::SingularTransform, GpuSceneField::WorldFromObject);
    }

    GpuSceneInstance instance;
    instance.worldFromObject = packAffineTransform(input.worldFromObject);
    instance.previousWorldFromObject = packAffineTransform(input.previousWorldFromObject);
    instance.objectFromWorld = packAffineTransform(objectFromWorld);
    instance.worldBoundsCenterAndRadius = input.worldBoundsCenterAndRadius;
    instance.geometryMaterialAndFlags = {input.geometryBase, input.geometryCount, input.materialBase, input.flags};
    instance.identityAndVersion = {input.stableObjectId.value, input.rayTracingInstanceId, kGpuSceneContractVersion,
                                   0u};
    return {instance, GpuSceneNormalizationError::None, GpuSceneField::None};
}

GpuSceneGeometryNormalizationResult normalizeGpuSceneGeometry(const GpuSceneGeometryNormalizationInput& input) {
    if (!addressAligned(input.vertexAddress, 4u)) {
        return geometryFailure(GpuSceneNormalizationError::InvalidDeviceAddress, GpuSceneField::VertexAddress);
    }
    uint32_t indexElementSize = 0u;
    switch (input.indexType) {
    case GpuSceneIndexType::Uint16: indexElementSize = 2u; break;
    case GpuSceneIndexType::Uint32: indexElementSize = 4u; break;
    }
    if (indexElementSize == 0u) {
        return geometryFailure(GpuSceneNormalizationError::ValueOutOfRange, GpuSceneField::IndexType);
    }
    if (!addressAligned(input.indexAddress, indexElementSize)) {
        return geometryFailure(GpuSceneNormalizationError::InvalidDeviceAddress, GpuSceneField::IndexAddress);
    }
    if (!addressAligned(input.primitiveMetadataAddress, 4u)) {
        return geometryFailure(GpuSceneNormalizationError::InvalidDeviceAddress,
                               GpuSceneField::PrimitiveMetadataAddress);
    }
    if (input.vertexStride < sizeof(float) * 3u || input.vertexStride % 4u != 0u ||
        input.positionByteOffset % 4u != 0u || input.positionByteOffset > input.vertexStride - sizeof(float) * 3u ||
        input.vertexCount == 0u) {
        return geometryFailure(GpuSceneNormalizationError::InvalidVertexLayout, GpuSceneField::VertexLayout);
    }
    if (!addressRangeFits(input.vertexAddress, 0u, input.vertexCount, input.vertexStride)) {
        return geometryFailure(GpuSceneNormalizationError::ValueOutOfRange, GpuSceneField::VertexLayout);
    }
    if (input.indexCount == 0u || input.indexCount % 3u != 0u || !rangeFits(input.firstIndex, input.indexCount)) {
        return geometryFailure(GpuSceneNormalizationError::InvalidIndexRange, GpuSceneField::IndexRange);
    }
    if (!addressRangeFits(input.indexAddress, input.firstIndex, input.indexCount, indexElementSize)) {
        return geometryFailure(GpuSceneNormalizationError::ValueOutOfRange, GpuSceneField::IndexRange);
    }
    if (input.materialIndex == kGpuSceneInvalidTableIndex) {
        return geometryFailure(GpuSceneNormalizationError::InvalidMaterialIndex, GpuSceneField::MaterialIndex);
    }
    if (!input.stableMaterialId.isValid()) {
        return geometryFailure(GpuSceneNormalizationError::InvalidStableId, GpuSceneField::StableMaterialId);
    }
    if (!input.stableGeometryId.isValid()) {
        return geometryFailure(GpuSceneNormalizationError::InvalidStableId, GpuSceneField::StableGeometryId);
    }
    if (input.primitiveMetadataStride < sizeof(uint32_t) || input.primitiveMetadataStride % 4u != 0u) {
        return geometryFailure(GpuSceneNormalizationError::InvalidPrimitiveMetadata,
                               GpuSceneField::PrimitiveMetadataStride);
    }
    if (!addressRangeFits(input.primitiveMetadataAddress, 0u, input.indexCount / 3u, input.primitiveMetadataStride)) {
        return geometryFailure(GpuSceneNormalizationError::ValueOutOfRange, GpuSceneField::PrimitiveMetadataStride);
    }
    if (input.meshletCount == 0u && (input.meshletAddress != 0u || input.firstMeshlet != 0u)) {
        return geometryFailure(GpuSceneNormalizationError::InvalidMeshletRange, GpuSceneField::MeshletRange);
    }
    if (input.meshletCount != 0u && !addressAligned(input.meshletAddress, 4u)) {
        return geometryFailure(GpuSceneNormalizationError::InvalidDeviceAddress, GpuSceneField::MeshletAddress);
    }
    if (input.meshletCount != 0u && !rangeFits(input.firstMeshlet, input.meshletCount)) {
        return geometryFailure(GpuSceneNormalizationError::InvalidMeshletRange, GpuSceneField::MeshletRange);
    }
    if (input.geometryRevision == 0u) {
        return geometryFailure(GpuSceneNormalizationError::ValueOutOfRange, GpuSceneField::GeometryRevision);
    }
    if ((input.flags & ~kGpuSceneKnownGeometryFlags) != 0u || !oneGeometrySurfaceClass(input.flags)) {
        return geometryFailure(GpuSceneNormalizationError::UnknownFlags, GpuSceneField::GeometryFlags);
    }
    if (!finiteVector(input.localBoundsMin) || !finiteVector(input.localBoundsMax)) {
        return geometryFailure(GpuSceneNormalizationError::NonFiniteValue, GpuSceneField::LocalBounds);
    }
    if (glm::any(glm::greaterThan(input.localBoundsMin, input.localBoundsMax))) {
        return geometryFailure(GpuSceneNormalizationError::InvalidBounds, GpuSceneField::LocalBounds);
    }

    GpuSceneGeometry geometry;
    geometry.vertexAddress = packGpuSceneDeviceAddress(input.vertexAddress);
    geometry.indexAddress = packGpuSceneDeviceAddress(input.indexAddress);
    geometry.primitiveMetadataAddress = packGpuSceneDeviceAddress(input.primitiveMetadataAddress);
    geometry.meshletAddress = packGpuSceneDeviceAddress(input.meshletAddress);
    geometry.vertexLayoutAndFlags = {input.vertexStride, input.positionByteOffset, input.vertexCount, input.flags};
    geometry.indexRangeAndType = {input.firstIndex, input.indexCount, static_cast<uint32_t>(input.indexType),
                                  kGpuSceneContractVersion};
    geometry.materialAndIdentity = {input.materialIndex, input.stableMaterialId.value, input.stableGeometryId.value,
                                    input.primitiveMetadataStride};
    geometry.primitiveMeshletAndRevision = {input.indexCount / 3u, input.firstMeshlet, input.meshletCount,
                                            input.geometryRevision};
    geometry.localBoundsMin = glm::vec4(input.localBoundsMin, 0.0f);
    geometry.localBoundsMax = glm::vec4(input.localBoundsMax, 0.0f);
    return {geometry, GpuSceneNormalizationError::None, GpuSceneField::None};
}

glm::vec3 transformGpuScenePoint(const GpuSceneAffineTransform& transform, const glm::vec3& point) {
    const glm::vec4 homogeneous(point, 1.0f);
    return {glm::dot(transform.row0, homogeneous), glm::dot(transform.row1, homogeneous),
            glm::dot(transform.row2, homogeneous)};
}

const char* gpuSceneNormalizationErrorStableId(const GpuSceneNormalizationError error) {
    switch (error) {
    case GpuSceneNormalizationError::None: return "None";
    case GpuSceneNormalizationError::NonFiniteValue: return "NonFiniteValue";
    case GpuSceneNormalizationError::NonAffineTransform: return "NonAffineTransform";
    case GpuSceneNormalizationError::SingularTransform: return "SingularTransform";
    case GpuSceneNormalizationError::ValueOutOfRange: return "ValueOutOfRange";
    case GpuSceneNormalizationError::InvalidStableId: return "InvalidStableId";
    case GpuSceneNormalizationError::UnknownFlags: return "UnknownFlags";
    case GpuSceneNormalizationError::InvalidGeometryRange: return "InvalidGeometryRange";
    case GpuSceneNormalizationError::InvalidBounds: return "InvalidBounds";
    case GpuSceneNormalizationError::InvalidRayTracingInstanceId: return "InvalidRayTracingInstanceId";
    case GpuSceneNormalizationError::RayTracingStateConflict: return "RayTracingStateConflict";
    case GpuSceneNormalizationError::InvalidDeviceAddress: return "InvalidDeviceAddress";
    case GpuSceneNormalizationError::InvalidVertexLayout: return "InvalidVertexLayout";
    case GpuSceneNormalizationError::InvalidIndexRange: return "InvalidIndexRange";
    case GpuSceneNormalizationError::InvalidMaterialIndex: return "InvalidMaterialIndex";
    case GpuSceneNormalizationError::InvalidPrimitiveMetadata: return "InvalidPrimitiveMetadata";
    case GpuSceneNormalizationError::InvalidMeshletRange: return "InvalidMeshletRange";
    }
    std::abort();
}

const char* gpuSceneFieldStableId(const GpuSceneField field) {
    switch (field) {
    case GpuSceneField::None: return "None";
    case GpuSceneField::WorldFromObject: return "WorldFromObject";
    case GpuSceneField::PreviousWorldFromObject: return "PreviousWorldFromObject";
    case GpuSceneField::WorldBounds: return "WorldBounds";
    case GpuSceneField::GeometryRange: return "GeometryRange";
    case GpuSceneField::MaterialBase: return "MaterialBase";
    case GpuSceneField::InstanceFlags: return "InstanceFlags";
    case GpuSceneField::StableObjectId: return "StableObjectId";
    case GpuSceneField::RayTracingInstanceId: return "RayTracingInstanceId";
    case GpuSceneField::VertexAddress: return "VertexAddress";
    case GpuSceneField::IndexAddress: return "IndexAddress";
    case GpuSceneField::PrimitiveMetadataAddress: return "PrimitiveMetadataAddress";
    case GpuSceneField::MeshletAddress: return "MeshletAddress";
    case GpuSceneField::VertexLayout: return "VertexLayout";
    case GpuSceneField::IndexRange: return "IndexRange";
    case GpuSceneField::IndexType: return "IndexType";
    case GpuSceneField::MaterialIndex: return "MaterialIndex";
    case GpuSceneField::StableMaterialId: return "StableMaterialId";
    case GpuSceneField::StableGeometryId: return "StableGeometryId";
    case GpuSceneField::PrimitiveMetadataStride: return "PrimitiveMetadataStride";
    case GpuSceneField::MeshletRange: return "MeshletRange";
    case GpuSceneField::GeometryRevision: return "GeometryRevision";
    case GpuSceneField::GeometryFlags: return "GeometryFlags";
    case GpuSceneField::LocalBounds: return "LocalBounds";
    }
    std::abort();
}

} // namespace renderer::contracts
