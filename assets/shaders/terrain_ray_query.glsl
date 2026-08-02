#ifndef MECRAFT_TERRAIN_RAY_QUERY_GLSL
#define MECRAFT_TERRAIN_RAY_QUERY_GLSL

#include "terrain_material_sampling.glsl"
#include "terrain_ray_tracing_contract.glsl"

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer TerrainRayQueryVertexBuffer {
    uint words[];
};

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer TerrainRayQueryMetadataBuffer {
    TerrainPrimitiveMetadata records[];
};

struct TerrainRayQueryTriangle {
    vec3 position0;
    vec3 position1;
    vec3 position2;
    vec2 uv0;
    vec2 uv1;
    vec2 uv2;
    TerrainPrimitiveMetadata metadata;
};

bool terrainRayQueryFinite(vec2 value) {
    return !any(isnan(value)) && !any(isinf(value));
}

bool terrainRayQueryFinite(vec3 value) {
    return !any(isnan(value)) && !any(isinf(value));
}

void terrainRayQueryDecodeVertex(TerrainRayQueryVertexBuffer vertexBuffer, uint firstWord, out vec3 position,
                                 out vec2 uv) {
    uint positionWord = firstWord + TERRAIN_RAY_TRACING_VERTEX_POSITION_OFFSET / 4u;
    uint uvWord = firstWord + TERRAIN_RAY_TRACING_VERTEX_UV_OFFSET / 4u;
    position = vec3(uintBitsToFloat(vertexBuffer.words[positionWord + 0u]),
                    uintBitsToFloat(vertexBuffer.words[positionWord + 1u]),
                    uintBitsToFloat(vertexBuffer.words[positionWord + 2u]));
    uv = vec2(uintBitsToFloat(vertexBuffer.words[uvWord + 0u]),
              uintBitsToFloat(vertexBuffer.words[uvWord + 1u]));
}

bool terrainRayQueryLoadTriangle(TerrainRayTracingGpuInstance instanceData, uint geometryIndex, uint primitiveIndex,
                                 out TerrainRayQueryTriangle triangle) {
    triangle = TerrainRayQueryTriangle(vec3(0.0), vec3(0.0), vec3(0.0), vec2(0.0), vec2(0.0), vec2(0.0),
                                       TerrainPrimitiveMetadata(0u, 0u, 0u, 0u));
    if (!terrainRayTracingGpuInstanceValid(instanceData) || geometryIndex >= instanceData.geometryCount) {
        return false;
    }
    TerrainRayTracingGpuGeometry geometry = instanceData.geometries[geometryIndex];
    if (geometry.geometryClass != TERRAIN_RAY_TRACING_GEOMETRY_CUTOUT || primitiveIndex >= geometry.primitiveCount) {
        return false;
    }

    uint firstVertex = geometry.vertexBase + primitiveIndex * 3u;
    uint firstWord = firstVertex * (TERRAIN_RAY_TRACING_VERTEX_STRIDE / 4u);
    TerrainRayQueryVertexBuffer vertexBuffer = TerrainRayQueryVertexBuffer(instanceData.vertexAddressWords);
    terrainRayQueryDecodeVertex(vertexBuffer, firstWord, triangle.position0, triangle.uv0);
    firstWord += TERRAIN_RAY_TRACING_VERTEX_STRIDE / 4u;
    terrainRayQueryDecodeVertex(vertexBuffer, firstWord, triangle.position1, triangle.uv1);
    firstWord += TERRAIN_RAY_TRACING_VERTEX_STRIDE / 4u;
    terrainRayQueryDecodeVertex(vertexBuffer, firstWord, triangle.position2, triangle.uv2);

    TerrainRayQueryMetadataBuffer metadataBuffer =
        TerrainRayQueryMetadataBuffer(instanceData.primitiveMetadataAddressWords);
    triangle.metadata = metadataBuffer.records[geometry.primitiveBase + primitiveIndex];
    return terrainRayQueryFinite(triangle.position0) && terrainRayQueryFinite(triangle.position1) &&
           terrainRayQueryFinite(triangle.position2) && terrainRayQueryFinite(triangle.uv0) &&
           terrainRayQueryFinite(triangle.uv1) && terrainRayQueryFinite(triangle.uv2);
}

vec2 terrainRayQueryBarycentricUv(TerrainRayQueryTriangle triangle, vec2 barycentrics) {
    vec3 weights = vec3(1.0 - barycentrics.x - barycentrics.y, barycentrics);
    return triangle.uv0 * weights.x + triangle.uv1 * weights.y + triangle.uv2 * weights.z;
}

bool terrainRayQueryConeTextureLod(TerrainRayQueryTriangle triangle, float coneWidth, ivec2 textureExtent,
                                   int textureMipLevels, out float lod) {
    lod = 0.0;
    if (isnan(coneWidth) || isinf(coneWidth) || coneWidth <= 0.0 || textureExtent.x <= 0 || textureExtent.y <= 0 ||
        textureMipLevels <= 0) {
        return false;
    }

    vec3 edge1 = triangle.position1 - triangle.position0;
    vec3 edge2 = triangle.position2 - triangle.position0;
    vec2 uvEdge1 = triangle.uv1 - triangle.uv0;
    vec2 uvEdge2 = triangle.uv2 - triangle.uv0;
    float edge11 = dot(edge1, edge1);
    float edge12 = dot(edge1, edge2);
    float edge22 = dot(edge2, edge2);
    float determinant = edge11 * edge22 - edge12 * edge12;
    if (isnan(determinant) || isinf(determinant) || determinant <= 1.0e-12) {
        return false;
    }

    vec3 gradientU = ((uvEdge1.x * edge22 - uvEdge2.x * edge12) * edge1 +
                      (uvEdge2.x * edge11 - uvEdge1.x * edge12) * edge2) /
                     determinant;
    vec3 gradientV = ((uvEdge1.y * edge22 - uvEdge2.y * edge12) * edge1 +
                      (uvEdge2.y * edge11 - uvEdge1.y * edge12) * edge2) /
                     determinant;
    float texelFootprint =
        coneWidth * max(length(gradientU) * float(textureExtent.x), length(gradientV) * float(textureExtent.y));
    if (isnan(texelFootprint) || isinf(texelFootprint)) {
        return false;
    }
    lod = clamp(log2(max(texelFootprint, 1.0)), 0.0, float(textureMipLevels - 1));
    return true;
}

bool terrainRayQueryCandidateAlphaPasses(TerrainRayTracingGpuInstance instanceData, uint geometryIndex,
                                         uint primitiveIndex, vec2 barycentrics, float coneWidth,
                                         float animationTimeSeconds, sampler2DArray terrainAlbedo,
                                         out float sampledOpacity, out float sampledLod) {
    sampledOpacity = 0.0;
    sampledLod = 0.0;
    if (!terrainRayQueryFinite(barycentrics)) {
        return false;
    }

    TerrainRayQueryTriangle triangle;
    if (!terrainRayQueryLoadTriangle(instanceData, geometryIndex, primitiveIndex, triangle)) {
        return false;
    }
    ivec3 textureExtent = textureSize(terrainAlbedo, 0);
    int textureMipLevels = textureQueryLevels(terrainAlbedo);
    if (!terrainRayQueryConeTextureLod(triangle, coneWidth, textureExtent.xy, textureMipLevels, sampledLod)) {
        return false;
    }

    float sampledLayer = terrainAnimatedTextureLayer(
        float(triangle.metadata.textureLayer), float(terrainPrimitiveAnimationFrameCount(triangle.metadata)),
        float(terrainPrimitiveAnimationFramesPerSecond(triangle.metadata)),
        terrainPrimitiveAnimated(triangle.metadata) ? 1.0 : 0.0, animationTimeSeconds);
    if (isnan(sampledLayer) || isinf(sampledLayer) || sampledLayer < 0.0 || sampledLayer >= float(textureExtent.z)) {
        return false;
    }
    vec2 uv = terrainRayQueryBarycentricUv(triangle, barycentrics);
    if (!terrainRayQueryFinite(uv)) {
        return false;
    }
    sampledOpacity = textureLod(terrainAlbedo, vec3(uv, sampledLayer), sampledLod).a;
    return terrainAlphaTestPasses(sampledOpacity);
}

#endif // MECRAFT_TERRAIN_RAY_QUERY_GLSL
