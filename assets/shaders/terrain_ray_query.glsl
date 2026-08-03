#ifndef MECRAFT_TERRAIN_RAY_QUERY_GLSL
#define MECRAFT_TERRAIN_RAY_QUERY_GLSL

#include "gbuffer_contract.glsl"
#include "pbr_brdf.glsl"
#include "terrain_material_sampling.glsl"
#include "terrain_ray_tracing_contract.glsl"

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer TerrainRayQueryVertexBuffer {
    uint words[];
};

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer TerrainRayQueryMetadataBuffer {
    TerrainPrimitiveMetadata records[];
};

struct TerrainRayQueryVertex {
    vec3 position;
    vec2 uv;
    float sunlight;
    float blockLight;
    float ao;
};

struct TerrainRayQueryTriangle {
    TerrainRayQueryVertex vertices[3];
    TerrainPrimitiveMetadata metadata;
    uint geometryClass;
};

struct TerrainRayQueryAttributes {
    vec3 position;
    vec2 uv;
    float sunlight;
    float blockLight;
    float ao;
};

struct TerrainRayQuerySurface {
    vec3 albedo;
    vec3 normal;
    vec3 geometricNormal;
    vec3 emission;
    vec3 specularF0;
    float specularF90;
    float perceptualRoughness;
    float metalness;
    float ao;
    float skyVisibility;
    float blockLight;
    uint materialKind;
};

bool terrainRayQueryFinite(vec2 value) {
    return !any(isnan(value)) && !any(isinf(value));
}

bool terrainRayQueryFinite(vec3 value) {
    return !any(isnan(value)) && !any(isinf(value));
}

bool terrainRayQueryFinite(vec4 value) {
    return !any(isnan(value)) && !any(isinf(value));
}

void terrainRayQueryDecodeVertex(TerrainRayQueryVertexBuffer vertexBuffer, uint firstWord,
                                 out TerrainRayQueryVertex vertex) {
    uint positionWord = firstWord + TERRAIN_RAY_TRACING_VERTEX_POSITION_OFFSET / 4u;
    uint uvWord = firstWord + TERRAIN_RAY_TRACING_VERTEX_UV_OFFSET / 4u;
    uint packedLight = vertexBuffer.words[firstWord + 5u];
    vertex.position = vec3(uintBitsToFloat(vertexBuffer.words[positionWord + 0u]),
                           uintBitsToFloat(vertexBuffer.words[positionWord + 1u]),
                           uintBitsToFloat(vertexBuffer.words[positionWord + 2u]));
    vertex.uv = vec2(uintBitsToFloat(vertexBuffer.words[uvWord + 0u]),
                     uintBitsToFloat(vertexBuffer.words[uvWord + 1u]));
    vertex.sunlight = float((packedLight >> 8u) & 0xffu) * (1.0 / 255.0);
    vertex.blockLight = float((packedLight >> 16u) & 0xffu) * (1.0 / 255.0);
    vertex.ao = float((packedLight >> 24u) & 0xffu) * (1.0 / 3.0);
}

bool terrainRayQueryLoadTriangle(TerrainRayTracingGpuInstance instanceData, uint geometryIndex, uint primitiveIndex,
                                 out TerrainRayQueryTriangle triangle) {
    for (uint index = 0u; index < 3u; ++index) {
        triangle.vertices[index].position = vec3(0.0);
        triangle.vertices[index].uv = vec2(0.0);
        triangle.vertices[index].sunlight = 0.0;
        triangle.vertices[index].blockLight = 0.0;
        triangle.vertices[index].ao = 0.0;
    }
    triangle.metadata = TerrainPrimitiveMetadata(0u, 0u, 0u, 0u);
    triangle.geometryClass = 0xffffffffu;
    if (!terrainRayTracingGpuInstanceValid(instanceData) || geometryIndex >= instanceData.geometryCount) {
        return false;
    }
    TerrainRayTracingGpuGeometry geometry = instanceData.geometries[geometryIndex];
    if (geometry.geometryClass > TERRAIN_RAY_TRACING_GEOMETRY_CUTOUT ||
        primitiveIndex >= geometry.primitiveCount) {
        return false;
    }

    uint firstVertex = geometry.vertexBase + primitiveIndex * 3u;
    uint firstWord = firstVertex * (TERRAIN_RAY_TRACING_VERTEX_STRIDE / 4u);
    TerrainRayQueryVertexBuffer vertexBuffer = TerrainRayQueryVertexBuffer(instanceData.vertexAddressWords);
    for (uint vertexIndex = 0u; vertexIndex < 3u; ++vertexIndex) {
        terrainRayQueryDecodeVertex(vertexBuffer, firstWord, triangle.vertices[vertexIndex]);
        firstWord += TERRAIN_RAY_TRACING_VERTEX_STRIDE / 4u;
    }

    TerrainRayQueryMetadataBuffer metadataBuffer =
        TerrainRayQueryMetadataBuffer(instanceData.primitiveMetadataAddressWords);
    triangle.metadata = metadataBuffer.records[geometry.primitiveBase + primitiveIndex];
    triangle.geometryClass = geometry.geometryClass;
    return terrainRayQueryFinite(triangle.vertices[0].position) &&
           terrainRayQueryFinite(triangle.vertices[1].position) &&
           terrainRayQueryFinite(triangle.vertices[2].position) &&
           terrainRayQueryFinite(triangle.vertices[0].uv) && terrainRayQueryFinite(triangle.vertices[1].uv) &&
           terrainRayQueryFinite(triangle.vertices[2].uv) &&
           triangle.metadata.textureLayer < TERRAIN_MATERIAL_TEXTURE_LAYER_CAPACITY;
}

bool terrainRayQueryInterpolateAttributes(TerrainRayQueryTriangle triangle, vec2 barycentrics,
                                          out TerrainRayQueryAttributes attributes) {
    attributes.position = vec3(0.0);
    attributes.uv = vec2(0.0);
    attributes.sunlight = 0.0;
    attributes.blockLight = 0.0;
    attributes.ao = 0.0;
    if (!terrainRayQueryFinite(barycentrics)) {
        return false;
    }
    vec3 weights = vec3(1.0 - barycentrics.x - barycentrics.y, barycentrics);
    attributes.position = triangle.vertices[0].position * weights.x +
                          triangle.vertices[1].position * weights.y +
                          triangle.vertices[2].position * weights.z;
    attributes.uv = triangle.vertices[0].uv * weights.x + triangle.vertices[1].uv * weights.y +
                    triangle.vertices[2].uv * weights.z;
    attributes.sunlight = triangle.vertices[0].sunlight * weights.x +
                          triangle.vertices[1].sunlight * weights.y +
                          triangle.vertices[2].sunlight * weights.z;
    attributes.blockLight = triangle.vertices[0].blockLight * weights.x +
                            triangle.vertices[1].blockLight * weights.y +
                            triangle.vertices[2].blockLight * weights.z;
    attributes.ao = triangle.vertices[0].ao * weights.x + triangle.vertices[1].ao * weights.y +
                    triangle.vertices[2].ao * weights.z;
    return terrainRayQueryFinite(attributes.position) && terrainRayQueryFinite(attributes.uv) &&
           !isnan(attributes.sunlight) && !isinf(attributes.sunlight) &&
           !isnan(attributes.blockLight) && !isinf(attributes.blockLight) && !isnan(attributes.ao) &&
           !isinf(attributes.ao);
}

bool terrainRayQueryConeTextureLod(TerrainRayQueryTriangle triangle, float coneWidth, ivec2 textureExtent,
                                   int textureMipLevels, out float lod) {
    lod = 0.0;
    if (isnan(coneWidth) || isinf(coneWidth) || coneWidth <= 0.0 || textureExtent.x <= 0 || textureExtent.y <= 0 ||
        textureMipLevels <= 0) {
        return false;
    }

    vec3 edge1 = triangle.vertices[1].position - triangle.vertices[0].position;
    vec3 edge2 = triangle.vertices[2].position - triangle.vertices[0].position;
    vec2 uvEdge1 = triangle.vertices[1].uv - triangle.vertices[0].uv;
    vec2 uvEdge2 = triangle.vertices[2].uv - triangle.vertices[0].uv;
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

float terrainRayQuerySampledLayer(TerrainPrimitiveMetadata metadata, float animationTimeSeconds) {
    return terrainAnimatedTextureLayer(float(metadata.textureLayer),
                                       float(terrainPrimitiveAnimationFrameCount(metadata)),
                                       float(terrainPrimitiveAnimationFramesPerSecond(metadata)),
                                       terrainPrimitiveAnimated(metadata) ? 1.0 : 0.0, animationTimeSeconds);
}

vec3 terrainRayQueryFaceNormal(int face) {
    if (face == TERRAIN_PRIMITIVE_FACE_CROSS_FLOWER || face == TERRAIN_PRIMITIVE_FACE_CROSS_BIOME_TINT) {
        return vec3(0.0, 1.0, 0.0);
    }
    if (face == 0) return vec3(0.0, 1.0, 0.0);
    if (face == 1) return vec3(0.0, -1.0, 0.0);
    if (face == 2) return vec3(0.0, 0.0, 1.0);
    if (face == 3) return vec3(0.0, 0.0, -1.0);
    if (face == 4) return vec3(-1.0, 0.0, 0.0);
    return vec3(1.0, 0.0, 0.0);
}

bool terrainRayQueryTangentFrame(TerrainRayQueryTriangle triangle, vec3 normal, out mat3 frame) {
    vec3 edge1 = triangle.vertices[1].position - triangle.vertices[0].position;
    vec3 edge2 = triangle.vertices[2].position - triangle.vertices[0].position;
    vec2 uvEdge1 = triangle.vertices[1].uv - triangle.vertices[0].uv;
    vec2 uvEdge2 = triangle.vertices[2].uv - triangle.vertices[0].uv;
    float determinant = uvEdge1.x * uvEdge2.y - uvEdge1.y * uvEdge2.x;
    if (isnan(determinant) || isinf(determinant) || abs(determinant) <= 1.0e-8) {
        return false;
    }
    vec3 tangent = (edge1 * uvEdge2.y - edge2 * uvEdge1.y) / determinant;
    tangent -= normal * dot(normal, tangent);
    float tangentLengthSquared = dot(tangent, tangent);
    if (!terrainRayQueryFinite(tangent) || tangentLengthSquared <= 1.0e-12) {
        return false;
    }
    tangent *= inversesqrt(tangentLengthSquared);
    vec3 bitangent = normalize(cross(normal, tangent)) * (determinant < 0.0 ? -1.0 : 1.0);
    frame = mat3(tangent, bitangent, normal);
    return terrainRayQueryFinite(bitangent);
}

vec3 terrainRayQuerySrgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

vec3 terrainRayQueryRedstoneTintSrgb(vec2 tintUv) {
    float power = clamp(floor(tintUv.x * 16.0), 0.0, 15.0) / 15.0;
    int tint = int(clamp(floor(tintUv.y * 16.0), 0.0, 15.0));
    const vec3 lowPalette[16] = vec3[16](
        vec3(0.30, 0.00, 0.00), vec3(0.00, 0.05, 0.30), vec3(0.00, 0.22, 0.03), vec3(0.22, 0.18, 0.00),
        vec3(0.18, 0.00, 0.28), vec3(0.00, 0.20, 0.24), vec3(0.28, 0.09, 0.00), vec3(0.22, 0.22, 0.22),
        vec3(0.35, 0.02, 0.12), vec3(0.10, 0.20, 0.36), vec3(0.04, 0.28, 0.17), vec3(0.32, 0.24, 0.04),
        vec3(0.25, 0.07, 0.34), vec3(0.02, 0.30, 0.30), vec3(0.32, 0.16, 0.08), vec3(0.36, 0.36, 0.36));
    const vec3 highPalette[16] = vec3[16](
        vec3(1.00, 0.10, 0.02), vec3(0.08, 0.35, 1.00), vec3(0.08, 0.95, 0.18), vec3(1.00, 0.86, 0.08),
        vec3(0.78, 0.18, 1.00), vec3(0.05, 0.92, 1.00), vec3(1.00, 0.38, 0.05), vec3(0.82, 0.82, 0.82),
        vec3(1.00, 0.18, 0.42), vec3(0.35, 0.62, 1.00), vec3(0.18, 1.00, 0.62), vec3(1.00, 0.74, 0.20),
        vec3(0.82, 0.40, 1.00), vec3(0.25, 1.00, 0.92), vec3(1.00, 0.56, 0.25), vec3(1.00, 1.00, 1.00));
    return mix(lowPalette[tint], highPalette[tint], power);
}

bool terrainRayQueryCommittedSurface(TerrainRayTracingGpuInstance instanceData, uint geometryIndex,
                                     uint primitiveIndex, vec2 barycentrics, float coneWidth,
                                     float animationTimeSeconds, vec3 incomingRayDirection,
                                     bool normalMapsEnabled, bool specularMapsEnabled,
                                     float emissionScale, sampler2DArray terrainAlbedo,
                                     sampler2DArray terrainNormal, sampler2DArray terrainSpecular,
                                     sampler2D grassColormap, sampler2D foliageColormap,
                                     out TerrainRayQuerySurface surface) {
    surface.albedo = vec3(0.0);
    surface.normal = vec3(0.0, 1.0, 0.0);
    surface.geometricNormal = vec3(0.0, 1.0, 0.0);
    surface.emission = vec3(0.0);
    surface.specularF0 = vec3(0.04);
    surface.specularF90 = 1.0;
    surface.perceptualRoughness = 1.0;
    surface.metalness = 0.0;
    surface.ao = 0.0;
    surface.skyVisibility = 0.0;
    surface.blockLight = 0.0;
    surface.materialKind = 0u;

    TerrainRayQueryTriangle triangle;
    TerrainRayQueryAttributes attributes;
    if (!terrainRayQueryLoadTriangle(instanceData, geometryIndex, primitiveIndex, triangle) ||
        !terrainRayQueryInterpolateAttributes(triangle, barycentrics, attributes)) {
        return false;
    }
    ivec3 albedoExtent = textureSize(terrainAlbedo, 0);
    ivec3 normalExtent = textureSize(terrainNormal, 0);
    ivec3 specularExtent = textureSize(terrainSpecular, 0);
    int albedoMipLevels = textureQueryLevels(terrainAlbedo);
    float lod = 0.0;
    if (albedoExtent.x <= 0 || albedoExtent.y <= 0 || albedoExtent.z <= 0 ||
        any(notEqual(normalExtent, albedoExtent)) || any(notEqual(specularExtent, albedoExtent)) ||
        textureQueryLevels(terrainNormal) != albedoMipLevels ||
        textureQueryLevels(terrainSpecular) != albedoMipLevels ||
        !terrainRayQueryConeTextureLod(triangle, coneWidth, albedoExtent.xy, albedoMipLevels, lod)) {
        return false;
    }
    float sampledLayer = terrainRayQuerySampledLayer(triangle.metadata, animationTimeSeconds);
    if (isnan(sampledLayer) || isinf(sampledLayer) || sampledLayer < 0.0 || sampledLayer >= float(albedoExtent.z)) {
        return false;
    }
    vec4 albedoTexel = textureLod(terrainAlbedo, vec3(attributes.uv, sampledLayer), lod);
    if (!terrainRayQueryFinite(albedoTexel) || !terrainAlphaTestPasses(albedoTexel.a)) {
        return false;
    }

    uint packedMaterial = terrainPrimitiveMaterialAndTint(triangle.metadata);
    uint tintKind = (packedMaterial >> 14u) & 3u;
    uint materialKind = (packedMaterial >> 8u) & 63u;
    vec2 tintUv = (vec2(float((packedMaterial >> 4u) & 15u), float(packedMaterial & 15u)) + 0.5) / 16.0;
    vec3 albedo = terrainRayQuerySrgbToLinear(albedoTexel.rgb);
    if (tintKind == 1u) {
        albedo *= terrainRayQuerySrgbToLinear(textureLod(grassColormap, tintUv, 0.0).rgb);
    } else if (tintKind == 2u) {
        albedo *= terrainRayQuerySrgbToLinear(textureLod(foliageColormap, tintUv, 0.0).rgb);
    } else if (tintKind == 3u) {
        albedo *= terrainRayQuerySrgbToLinear(terrainRayQueryRedstoneTintSrgb(tintUv));
    }
    if (!terrainRayQueryFinite(albedo)) {
        return false;
    }

    float emissiveLuma = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
    float emissivePeak = max(max(albedo.r, albedo.g), albedo.b);
    bool emissiveMaterial = isDerivativeEmissiveMaterialId(int(materialKind)) ||
                            int(materialKind) == MATERIAL_ORE || int(materialKind) == MATERIAL_NETHER_ORE;
    float emissiveMask = smoothstep(0.34, 0.72, max(emissiveLuma, emissivePeak * 0.72));
    float emissiveHint = emissiveMaterial ? emissiveMask * clamp(attributes.blockLight * 1.25, 0.0, 1.0) : 0.0;
    SurfaceMaterial material = surfaceMaterialForKind(float(materialKind), emissiveHint);
    SurfaceMaterialAux materialAux = surfaceMaterialAuxForKind(float(materialKind));
    vec3 geometricNormal = normalize(terrainRayQueryFaceNormal(terrainPrimitiveFace(triangle.metadata)));
    vec3 normal = geometricNormal;
    float ao = clamp(attributes.ao, 0.0, 1.0);
    bool crossedVegetation = terrainPrimitiveFace(triangle.metadata) == TERRAIN_PRIMITIVE_FACE_CROSS_FLOWER ||
                             terrainPrimitiveFace(triangle.metadata) == TERRAIN_PRIMITIVE_FACE_CROSS_BIOME_TINT;
    if (normalMapsEnabled && !crossedVegetation) {
        vec4 normalTexel = textureLod(terrainNormal, vec3(attributes.uv, sampledLayer), lod);
        if (!terrainRayQueryFinite(normalTexel)) {
            return false;
        }
        LabPbrNormalSample normalSample = decodeLabPbrNormal(normalTexel);
        mat3 tangentFrame;
        if (!terrainRayQueryTangentFrame(triangle, normal, tangentFrame)) {
            return false;
        }
        normal = normalize(tangentFrame * normalSample.tangentNormal);
        ao *= normalSample.materialAo;
    }
    if (specularMapsEnabled) {
        vec4 specularTexel = textureLod(terrainSpecular, vec3(attributes.uv, sampledLayer), lod);
        if (!terrainRayQueryFinite(specularTexel)) {
            return false;
        }
        if (!isLabPbrInternalNeutralSpecular(specularTexel)) {
            LabPbrSpecularSample decoded = decodeLabPbrSpecular(specularTexel, albedo);
            material.perceptualRoughness = decoded.perceptualRoughness;
            material.encodedF0OrMetalId = clamp(specularTexel.g, 0.0, 1.0);
            if (decoded.emissionProvided) {
                material.emission = decoded.emission;
            }
            materialAux.porosity = decoded.porosity;
            materialAux.metalness = decoded.metalness;
        }
    }
    geometricNormal = faceforward(geometricNormal, incomingRayDirection, geometricNormal);
    normal = faceforward(normalize(normal), incomingRayDirection, geometricNormal);
    if (dot(normal, geometricNormal) < 0.0) {
        normal = -normal;
    }
    surface.albedo = albedo;
    surface.normal = normal;
    surface.geometricNormal = geometricNormal;
    surface.emission = albedo * material.emission * emissionScale;
    surface.specularF0 = decodeLabPbrF0(material.encodedF0OrMetalId, albedo);
    surface.specularF90 = pbrMaterialSpecularF90(material.specularF90, materialAux.metalness);
    surface.perceptualRoughness = material.perceptualRoughness;
    surface.metalness = materialAux.metalness;
    surface.ao = clamp(ao, 0.0, 1.0);
    surface.skyVisibility = clamp(attributes.sunlight, 0.0, 1.0);
    surface.blockLight = clamp(attributes.blockLight, 0.0, 1.0);
    surface.materialKind = materialKind;
    return terrainRayQueryFinite(surface.albedo) && terrainRayQueryFinite(surface.normal) &&
           terrainRayQueryFinite(surface.emission) && terrainRayQueryFinite(surface.specularF0) &&
           !isnan(surface.specularF90) && !isinf(surface.specularF90) &&
           !isnan(surface.perceptualRoughness) && !isinf(surface.perceptualRoughness) &&
           !isnan(surface.metalness) && !isinf(surface.metalness) && !isnan(surface.ao) && !isinf(surface.ao) &&
           !isnan(surface.skyVisibility) && !isinf(surface.skyVisibility) &&
           !isnan(surface.blockLight) && !isinf(surface.blockLight);
}

bool terrainRayQueryCandidateAlphaPasses(TerrainRayTracingGpuInstance instanceData, uint geometryIndex,
                                         uint primitiveIndex, vec2 barycentrics, float coneWidth,
                                         float animationTimeSeconds, sampler2DArray terrainAlbedo,
                                         out float sampledOpacity, out float sampledLod) {
    sampledOpacity = 0.0;
    sampledLod = 0.0;
    TerrainRayQueryTriangle triangle;
    TerrainRayQueryAttributes attributes;
    if (!terrainRayQueryLoadTriangle(instanceData, geometryIndex, primitiveIndex, triangle) ||
        triangle.geometryClass != TERRAIN_RAY_TRACING_GEOMETRY_CUTOUT ||
        !terrainRayQueryInterpolateAttributes(triangle, barycentrics, attributes)) {
        return false;
    }
    ivec3 textureExtent = textureSize(terrainAlbedo, 0);
    int textureMipLevels = textureQueryLevels(terrainAlbedo);
    if (!terrainRayQueryConeTextureLod(triangle, coneWidth, textureExtent.xy, textureMipLevels, sampledLod)) {
        return false;
    }
    float sampledLayer = terrainRayQuerySampledLayer(triangle.metadata, animationTimeSeconds);
    if (isnan(sampledLayer) || isinf(sampledLayer) || sampledLayer < 0.0 || sampledLayer >= float(textureExtent.z)) {
        return false;
    }
    sampledOpacity = textureLod(terrainAlbedo, vec3(attributes.uv, sampledLayer), sampledLod).a;
    return terrainAlphaTestPasses(sampledOpacity);
}

#endif // MECRAFT_TERRAIN_RAY_QUERY_GLSL
