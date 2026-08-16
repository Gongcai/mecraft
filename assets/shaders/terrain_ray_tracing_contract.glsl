#ifndef MECRAFT_TERRAIN_RAY_TRACING_CONTRACT_GLSL
#define MECRAFT_TERRAIN_RAY_TRACING_CONTRACT_GLSL

const uint TERRAIN_RAY_TRACING_CONTRACT_VERSION = 2u;
const uint TERRAIN_RAY_TRACING_VERTEX_STRIDE = 32u;
const uint TERRAIN_RAY_TRACING_VERTEX_POSITION_OFFSET = 0u;
const uint TERRAIN_RAY_TRACING_VERTEX_UV_OFFSET = 12u;
const uint TERRAIN_PRIMITIVE_ANIMATION_FRAME_COUNT_MASK = 0x3fu;
const uint TERRAIN_PRIMITIVE_ANIMATION_FPS_SHIFT = 6u;
const uint TERRAIN_PRIMITIVE_ANIMATION_FPS_MASK = 0x3fu;
const uint TERRAIN_PRIMITIVE_ANIMATION_ANIMATED_SHIFT = 12u;
const uint TERRAIN_PRIMITIVE_MATERIAL_TINT_MASK = 0xffffu;
const uint TERRAIN_PRIMITIVE_FACE_MASK = 0xffu;
const uint TERRAIN_PRIMITIVE_ANALYTIC_LIGHT_OWNS_EMISSION_BIT = 1u << 8u;
const int TERRAIN_PRIMITIVE_FACE_CROSS_FLOWER = -2;
const int TERRAIN_PRIMITIVE_FACE_CROSS_BIOME_TINT = -1;
const uint TERRAIN_RAY_TRACING_GEOMETRY_OPAQUE = 0u;
const uint TERRAIN_RAY_TRACING_GEOMETRY_CUTOUT = 1u;

// The four scalar words mirror the C++ alignas(16) record and produce a 16-byte std430 array stride.
struct TerrainPrimitiveMetadata {
    uint textureLayer;
    uint animationAndFlags;
    uint materialAndTint;
    uint faceAndFlags;
};

struct TerrainRayTracingGpuGeometry {
    uint vertexBase;
    uint primitiveBase;
    uint primitiveCount;
    uint geometryClass;
};

// The address words and aligned Geometry records mirror the fixed 64-byte C++ Custom Index record.
struct TerrainRayTracingGpuInstance {
    uvec2 vertexAddressWords;
    uvec2 primitiveMetadataAddressWords;
    TerrainRayTracingGpuGeometry geometries[2];
    uint geometryCount;
    uint revisionLow;
    uint revisionHigh;
    uint contractVersion;
};

uint terrainPrimitiveAnimationFrameCount(TerrainPrimitiveMetadata metadata) {
    return metadata.animationAndFlags & TERRAIN_PRIMITIVE_ANIMATION_FRAME_COUNT_MASK;
}

uint terrainPrimitiveAnimationFramesPerSecond(TerrainPrimitiveMetadata metadata) {
    return (metadata.animationAndFlags >> TERRAIN_PRIMITIVE_ANIMATION_FPS_SHIFT) &
           TERRAIN_PRIMITIVE_ANIMATION_FPS_MASK;
}

bool terrainPrimitiveAnimated(TerrainPrimitiveMetadata metadata) {
    return ((metadata.animationAndFlags >> TERRAIN_PRIMITIVE_ANIMATION_ANIMATED_SHIFT) & 1u) != 0u;
}

uint terrainPrimitiveMaterialAndTint(TerrainPrimitiveMetadata metadata) {
    return metadata.materialAndTint & TERRAIN_PRIMITIVE_MATERIAL_TINT_MASK;
}

uint terrainPrimitiveTintKind(TerrainPrimitiveMetadata metadata) {
    return (terrainPrimitiveMaterialAndTint(metadata) >> 14u) & 0x3u;
}

uint terrainPrimitiveDerivativeMaterialId(TerrainPrimitiveMetadata metadata) {
    return (terrainPrimitiveMaterialAndTint(metadata) >> 8u) & 0x3fu;
}

uvec2 terrainPrimitiveTintCoordinates(TerrainPrimitiveMetadata metadata) {
    uint packed = terrainPrimitiveMaterialAndTint(metadata);
    return uvec2((packed >> 4u) & 0xfu, packed & 0xfu);
}

int terrainPrimitiveFace(TerrainPrimitiveMetadata metadata) {
    uint encodedFace = metadata.faceAndFlags & TERRAIN_PRIMITIVE_FACE_MASK;
    return encodedFace <= 0x7fu ? int(encodedFace) : int(encodedFace) - 0x100;
}

bool terrainPrimitiveAnalyticLightOwnsEmission(TerrainPrimitiveMetadata metadata) {
    return (metadata.faceAndFlags & TERRAIN_PRIMITIVE_ANALYTIC_LIGHT_OWNS_EMISSION_BIT) != 0u;
}

bool terrainRayTracingGpuInstanceValid(TerrainRayTracingGpuInstance instanceData) {
    return instanceData.contractVersion == TERRAIN_RAY_TRACING_CONTRACT_VERSION &&
           instanceData.geometryCount > 0u && instanceData.geometryCount <= 2u &&
           (instanceData.revisionLow != 0u || instanceData.revisionHigh != 0u) &&
           any(notEqual(instanceData.vertexAddressWords, uvec2(0u))) &&
           any(notEqual(instanceData.primitiveMetadataAddressWords, uvec2(0u)));
}

#endif // MECRAFT_TERRAIN_RAY_TRACING_CONTRACT_GLSL
