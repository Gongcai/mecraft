#ifndef MECRAFT_TERRAIN_RAY_TRACING_CONTRACT_H
#define MECRAFT_TERRAIN_RAY_TRACING_CONTRACT_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace renderer::contracts {

inline constexpr uint32_t kTerrainRayTracingContractVersion = 1u;
inline constexpr uint32_t kTerrainRayTracingGeometryCapacity = 2u;
inline constexpr uint32_t kTerrainRayTracingVertexStride = 32u;
inline constexpr uint32_t kTerrainRayTracingVertexPositionOffset = 0u;
inline constexpr uint32_t kTerrainRayTracingVertexUvOffset = 12u;
inline constexpr int32_t kTerrainPrimitiveFaceCrossFlower = -2;
inline constexpr int32_t kTerrainPrimitiveFaceCrossBiomeTint = -1;
inline constexpr int32_t kTerrainPrimitiveFaceMinimum = kTerrainPrimitiveFaceCrossFlower;
inline constexpr int32_t kTerrainPrimitiveFaceMaximum = 5;

inline constexpr uint32_t kTerrainPrimitiveAnimationFrameCountMask = 0x3fu;
inline constexpr uint32_t kTerrainPrimitiveAnimationFramesPerSecondShift = 6u;
inline constexpr uint32_t kTerrainPrimitiveAnimationFramesPerSecondMask = 0x3fu;
inline constexpr uint32_t kTerrainPrimitiveAnimationAnimatedShift = 12u;
inline constexpr uint32_t kTerrainPrimitiveAnimationAnimatedMask = 1u;
inline constexpr uint32_t kTerrainPrimitiveAnimationKnownMask =
    kTerrainPrimitiveAnimationFrameCountMask |
    (kTerrainPrimitiveAnimationFramesPerSecondMask << kTerrainPrimitiveAnimationFramesPerSecondShift) |
    (kTerrainPrimitiveAnimationAnimatedMask << kTerrainPrimitiveAnimationAnimatedShift);
inline constexpr uint32_t kTerrainPrimitiveMaterialTintKnownMask = 0xffffu;
inline constexpr uint32_t kTerrainPrimitiveFaceKnownMask = 0xffu;

/// Stores immutable material data for one non-indexed terrain triangle.
/// The metadata order exactly follows consecutive three-vertex primitives in the terrain BLAS vertex buffer.
struct alignas(16) TerrainPrimitiveMetadata final {
    uint32_t textureLayer = 0u;
    uint32_t animationAndFlags = 0u;
    uint32_t materialAndTint = 0u;
    uint32_t faceAndFlags = 0u;

    [[nodiscard]] bool operator==(const TerrainPrimitiveMetadata& other) const {
        return textureLayer == other.textureLayer && animationAndFlags == other.animationAndFlags &&
               materialAndTint == other.materialAndTint && faceAndFlags == other.faceAndFlags;
    }
};

/// Supplies unpacked terrain material fields before they are encoded into the fixed 16-byte record.
struct TerrainPrimitiveMetadataInput final {
    uint32_t textureLayer = 0u;
    uint32_t animationFrameCount = 1u;
    uint32_t animationFramesPerSecond = 0u;
    bool animated = false;
    uint16_t materialAndTint = 0u;
    int32_t face = 0;
};

/// Encodes one triangle's material fields after validating texture-array, animation, tint, and face constraints.
/// @param input Unpacked fields copied from the triangle's three matching terrain vertices.
/// @return The fixed GPU record, or no value when any field violates the terrain material contract.
[[nodiscard]] std::optional<TerrainPrimitiveMetadata>
encodeTerrainPrimitiveMetadata(const TerrainPrimitiveMetadataInput& input);

/// Validates reserved bits and every decoded field of an already packed primitive record.
/// @param metadata Candidate 16-byte record.
/// @return True only when the record can be reproduced by the canonical encoder.
[[nodiscard]] bool validTerrainPrimitiveMetadata(const TerrainPrimitiveMetadata& metadata);

/// Extracts the validated animation-frame count stored in bits 0 through 5.
/// @param metadata Canonically encoded primitive record.
/// @return Texture-array frame count in the inclusive range [1, 63].
[[nodiscard]] constexpr uint32_t terrainPrimitiveAnimationFrameCount(const TerrainPrimitiveMetadata& metadata) {
    return metadata.animationAndFlags & kTerrainPrimitiveAnimationFrameCountMask;
}

/// Extracts the validated integer animation playback rate stored in bits 6 through 11.
/// @param metadata Canonically encoded primitive record.
/// @return Animation frames per second in the inclusive range [0, 63].
[[nodiscard]] constexpr uint32_t terrainPrimitiveAnimationFramesPerSecond(const TerrainPrimitiveMetadata& metadata) {
    return (metadata.animationAndFlags >> kTerrainPrimitiveAnimationFramesPerSecondShift) &
           kTerrainPrimitiveAnimationFramesPerSecondMask;
}

/// Tests the animation flag stored in bit 12.
/// @param metadata Canonically encoded primitive record.
/// @return True when texture-array frame selection advances with render time.
[[nodiscard]] constexpr bool terrainPrimitiveAnimated(const TerrainPrimitiveMetadata& metadata) {
    return ((metadata.animationAndFlags >> kTerrainPrimitiveAnimationAnimatedShift) &
            kTerrainPrimitiveAnimationAnimatedMask) != 0u;
}

/// Extracts the original 16-bit derivative-material and tint payload from one primitive.
/// @param metadata Canonically encoded primitive record.
/// @return Exact BlockVertex tint payload shared by all three triangle vertices.
[[nodiscard]] constexpr uint16_t terrainPrimitiveMaterialAndTint(const TerrainPrimitiveMetadata& metadata) {
    return static_cast<uint16_t>(metadata.materialAndTint & kTerrainPrimitiveMaterialTintKnownMask);
}

/// Decodes the signed raster face or crossed-quad marker from the low byte.
/// @param metadata Canonically encoded primitive record.
/// @return Face value in the inclusive range [-2, 5].
[[nodiscard]] constexpr int32_t terrainPrimitiveFace(const TerrainPrimitiveMetadata& metadata) {
    const uint32_t encodedFace = metadata.faceAndFlags & kTerrainPrimitiveFaceKnownMask;
    return encodedFace <= 0x7fu ? static_cast<int32_t>(encodedFace) : static_cast<int32_t>(encodedFace) - 0x100;
}

/// Classifies the two canonical terrain BLAS geometry ranges.
enum class TerrainRayTracingGeometryClass : uint32_t { Opaque = 0u, Cutout = 1u };

/// Maps one BLAS Geometry Index to contiguous vertex and primitive-metadata ranges.
struct TerrainRayTracingGeometryRange final {
    uint32_t geometryIndex = 0u;
    TerrainRayTracingGeometryClass geometryClass = TerrainRayTracingGeometryClass::Opaque;
    uint32_t vertexBase = 0u;
    uint32_t vertexCount = 0u;
    uint32_t primitiveBase = 0u;
    uint32_t primitiveCount = 0u;

    [[nodiscard]] bool operator==(const TerrainRayTracingGeometryRange& other) const {
        return geometryIndex == other.geometryIndex && geometryClass == other.geometryClass &&
               vertexBase == other.vertexBase && vertexCount == other.vertexCount &&
               primitiveBase == other.primitiveBase && primitiveCount == other.primitiveCount;
    }
};

/// Captures the exact terrain buffers and Geometry Index mapping referenced by one TLAS instance generation.
struct TerrainRayTracingHitData final {
    uint64_t revision = 0u;
    uint64_t vertexAddress = 0u;
    uint64_t primitiveMetadataAddress = 0u;
    uint32_t vertexStride = kTerrainRayTracingVertexStride;
    uint32_t primitiveMetadataStride = sizeof(TerrainPrimitiveMetadata);
    uint32_t geometryCount = 0u;
    std::array<TerrainRayTracingGeometryRange, kTerrainRayTracingGeometryCapacity> geometries{};

    [[nodiscard]] bool operator==(const TerrainRayTracingHitData& other) const {
        return revision == other.revision && vertexAddress == other.vertexAddress &&
               primitiveMetadataAddress == other.primitiveMetadataAddress && vertexStride == other.vertexStride &&
               primitiveMetadataStride == other.primitiveMetadataStride && geometryCount == other.geometryCount &&
               geometries == other.geometries;
    }
};

/// Stores one Geometry Index range in the shader-visible TLAS hit-data table.
struct alignas(16) TerrainRayTracingGpuGeometry final {
    uint32_t vertexBase = 0u;
    uint32_t primitiveBase = 0u;
    uint32_t primitiveCount = 0u;
    uint32_t geometryClass = 0u;

    [[nodiscard]] bool operator==(const TerrainRayTracingGpuGeometry& other) const {
        return vertexBase == other.vertexBase && primitiveBase == other.primitiveBase &&
               primitiveCount == other.primitiveCount && geometryClass == other.geometryClass;
    }
};

/// Stores one fixed 64-byte Custom Index record consumed through physical storage-buffer addresses.
struct alignas(16) TerrainRayTracingGpuInstance final {
    std::array<uint32_t, 2u> vertexAddressWords{};
    std::array<uint32_t, 2u> primitiveMetadataAddressWords{};
    std::array<TerrainRayTracingGpuGeometry, kTerrainRayTracingGeometryCapacity> geometries{};
    uint32_t geometryCount = 0u;
    uint32_t revisionLow = 0u;
    uint32_t revisionHigh = 0u;
    uint32_t contractVersion = 0u;

    [[nodiscard]] bool operator==(const TerrainRayTracingGpuInstance& other) const {
        return vertexAddressWords == other.vertexAddressWords &&
               primitiveMetadataAddressWords == other.primitiveMetadataAddressWords && geometries == other.geometries &&
               geometryCount == other.geometryCount && revisionLow == other.revisionLow &&
               revisionHigh == other.revisionHigh && contractVersion == other.contractVersion;
    }
};

/// Validates a complete terrain hit-data snapshot and its canonical Opaque/Cutout ordering.
/// @param hitData Candidate addresses, strides, and Geometry Index ranges.
/// @return True only when every Geometry Index maps to one contiguous non-empty primitive range.
[[nodiscard]] bool validTerrainRayTracingHitData(const TerrainRayTracingHitData& hitData);

/// Encodes one validated CPU hit-data snapshot into the fixed shader-visible Custom Index record.
/// @param hitData Exact Terrain BLAS generation referenced by the matching TLAS instance.
/// @return The 64-byte GPU record, or no value when the CPU snapshot violates the canonical contract.
[[nodiscard]] std::optional<TerrainRayTracingGpuInstance>
encodeTerrainRayTracingGpuInstance(const TerrainRayTracingHitData& hitData);

static_assert(alignof(TerrainPrimitiveMetadata) == 16u);
static_assert(sizeof(TerrainPrimitiveMetadata) == 16u);
static_assert(offsetof(TerrainPrimitiveMetadata, textureLayer) == 0u);
static_assert(offsetof(TerrainPrimitiveMetadata, animationAndFlags) == 4u);
static_assert(offsetof(TerrainPrimitiveMetadata, materialAndTint) == 8u);
static_assert(offsetof(TerrainPrimitiveMetadata, faceAndFlags) == 12u);
static_assert(std::is_standard_layout_v<TerrainPrimitiveMetadata>);
static_assert(std::is_trivially_copyable_v<TerrainPrimitiveMetadata>);
static_assert(alignof(TerrainRayTracingGpuGeometry) == 16u);
static_assert(sizeof(TerrainRayTracingGpuGeometry) == 16u);
static_assert(offsetof(TerrainRayTracingGpuGeometry, vertexBase) == 0u);
static_assert(offsetof(TerrainRayTracingGpuGeometry, primitiveBase) == 4u);
static_assert(offsetof(TerrainRayTracingGpuGeometry, primitiveCount) == 8u);
static_assert(offsetof(TerrainRayTracingGpuGeometry, geometryClass) == 12u);
static_assert(std::is_standard_layout_v<TerrainRayTracingGpuGeometry>);
static_assert(std::is_trivially_copyable_v<TerrainRayTracingGpuGeometry>);
static_assert(alignof(TerrainRayTracingGpuInstance) == 16u);
static_assert(sizeof(TerrainRayTracingGpuInstance) == 64u);
static_assert(offsetof(TerrainRayTracingGpuInstance, vertexAddressWords) == 0u);
static_assert(offsetof(TerrainRayTracingGpuInstance, primitiveMetadataAddressWords) == 8u);
static_assert(offsetof(TerrainRayTracingGpuInstance, geometries) == 16u);
static_assert(offsetof(TerrainRayTracingGpuInstance, geometryCount) == 48u);
static_assert(offsetof(TerrainRayTracingGpuInstance, revisionLow) == 52u);
static_assert(offsetof(TerrainRayTracingGpuInstance, revisionHigh) == 56u);
static_assert(offsetof(TerrainRayTracingGpuInstance, contractVersion) == 60u);
static_assert(std::is_standard_layout_v<TerrainRayTracingGpuInstance>);
static_assert(std::is_trivially_copyable_v<TerrainRayTracingGpuInstance>);

} // namespace renderer::contracts

#endif // MECRAFT_TERRAIN_RAY_TRACING_CONTRACT_H
