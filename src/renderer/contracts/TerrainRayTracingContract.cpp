#include "renderer/contracts/TerrainRayTracingContract.h"

#include "renderer/contracts/TerrainMaterialSamplingContract.h"

#include <limits>

namespace renderer::contracts {

std::optional<TerrainPrimitiveMetadata> encodeTerrainPrimitiveMetadata(const TerrainPrimitiveMetadataInput& input) {
    const TerrainTextureAnimation animation{input.textureLayer, input.animationFrameCount,
                                            input.animationFramesPerSecond, input.animated};
    if (!terrainAnimatedTextureLayer(animation, 0.0f).has_value() || input.face < kTerrainPrimitiveFaceMinimum ||
        input.face > kTerrainPrimitiveFaceMaximum) {
        return std::nullopt;
    }

    TerrainPrimitiveMetadata metadata;
    metadata.textureLayer = input.textureLayer;
    metadata.animationAndFlags = input.animationFrameCount |
                                 (input.animationFramesPerSecond << kTerrainPrimitiveAnimationFramesPerSecondShift) |
                                 (static_cast<uint32_t>(input.animated) << kTerrainPrimitiveAnimationAnimatedShift);
    metadata.materialAndTint = input.materialAndTint;
    metadata.faceAndFlags = static_cast<uint8_t>(input.face) |
                            (input.analyticLightOwnsEmission ? kTerrainPrimitiveAnalyticLightOwnsEmissionBit : 0u);
    return metadata;
}

bool validTerrainPrimitiveMetadata(const TerrainPrimitiveMetadata& metadata) {
    if ((metadata.animationAndFlags & ~kTerrainPrimitiveAnimationKnownMask) != 0u ||
        (metadata.materialAndTint & ~kTerrainPrimitiveMaterialTintKnownMask) != 0u ||
        (metadata.faceAndFlags & ~kTerrainPrimitiveFaceKnownMask) != 0u) {
        return false;
    }
    const TerrainPrimitiveMetadataInput input{metadata.textureLayer,
                                              terrainPrimitiveAnimationFrameCount(metadata),
                                              terrainPrimitiveAnimationFramesPerSecond(metadata),
                                              terrainPrimitiveAnimated(metadata),
                                              terrainPrimitiveMaterialAndTint(metadata),
                                              terrainPrimitiveFace(metadata),
                                              terrainPrimitiveAnalyticLightOwnsEmission(metadata)};
    const std::optional<TerrainPrimitiveMetadata> encoded = encodeTerrainPrimitiveMetadata(input);
    return encoded.has_value() && *encoded == metadata;
}

bool validTerrainRayTracingHitData(const TerrainRayTracingHitData& hitData) {
    if (hitData.revision == 0u || hitData.vertexAddress == 0u || hitData.primitiveMetadataAddress == 0u ||
        hitData.vertexAddress % alignof(float) != 0u ||
        hitData.primitiveMetadataAddress % alignof(TerrainPrimitiveMetadata) != 0u ||
        hitData.vertexStride != kTerrainRayTracingVertexStride ||
        hitData.primitiveMetadataStride != sizeof(TerrainPrimitiveMetadata) || hitData.geometryCount == 0u ||
        hitData.geometryCount > kTerrainRayTracingGeometryCapacity) {
        return false;
    }

    uint64_t expectedVertexBase = 0u;
    uint64_t expectedPrimitiveBase = 0u;
    for (uint32_t index = 0u; index < hitData.geometryCount; ++index) {
        const TerrainRayTracingGeometryRange& geometry = hitData.geometries[index];
        const bool knownClass = geometry.geometryClass == TerrainRayTracingGeometryClass::Opaque ||
                                geometry.geometryClass == TerrainRayTracingGeometryClass::Cutout;
        const uint64_t expectedVertexCount = static_cast<uint64_t>(geometry.primitiveCount) * 3u;
        if (!knownClass || geometry.geometryIndex != index || geometry.vertexCount == 0u ||
            geometry.primitiveCount == 0u || geometry.vertexBase != expectedVertexBase ||
            geometry.primitiveBase != expectedPrimitiveBase || geometry.vertexCount != expectedVertexCount) {
            return false;
        }
        if (hitData.geometryCount == 2u &&
            ((index == 0u && geometry.geometryClass != TerrainRayTracingGeometryClass::Opaque) ||
             (index == 1u && geometry.geometryClass != TerrainRayTracingGeometryClass::Cutout))) {
            return false;
        }
        expectedVertexBase += geometry.vertexCount;
        expectedPrimitiveBase += geometry.primitiveCount;
        if (expectedVertexBase > std::numeric_limits<uint32_t>::max() ||
            expectedPrimitiveBase > std::numeric_limits<uint32_t>::max()) {
            return false;
        }
    }

    for (uint32_t index = hitData.geometryCount; index < kTerrainRayTracingGeometryCapacity; ++index) {
        if (!(hitData.geometries[index] == TerrainRayTracingGeometryRange{})) {
            return false;
        }
    }
    return true;
}

std::optional<TerrainRayTracingGpuInstance>
encodeTerrainRayTracingGpuInstance(const TerrainRayTracingHitData& hitData) {
    if (!validTerrainRayTracingHitData(hitData)) {
        return std::nullopt;
    }

    TerrainRayTracingGpuInstance encoded;
    encoded.vertexAddressWords = {static_cast<uint32_t>(hitData.vertexAddress),
                                  static_cast<uint32_t>(hitData.vertexAddress >> 32u)};
    encoded.primitiveMetadataAddressWords = {static_cast<uint32_t>(hitData.primitiveMetadataAddress),
                                             static_cast<uint32_t>(hitData.primitiveMetadataAddress >> 32u)};
    for (uint32_t index = 0u; index < hitData.geometryCount; ++index) {
        const TerrainRayTracingGeometryRange& geometry = hitData.geometries[index];
        encoded.geometries[index] = {geometry.vertexBase, geometry.primitiveBase, geometry.primitiveCount,
                                     static_cast<uint32_t>(geometry.geometryClass)};
    }
    encoded.geometryCount = hitData.geometryCount;
    encoded.revisionLow = static_cast<uint32_t>(hitData.revision);
    encoded.revisionHigh = static_cast<uint32_t>(hitData.revision >> 32u);
    encoded.contractVersion = kTerrainRayTracingContractVersion;
    return encoded;
}

} // namespace renderer::contracts
