#ifndef MECRAFT_STATIC_MESH_RAY_QUERY_GLSL
#define MECRAFT_STATIC_MESH_RAY_QUERY_GLSL

#include "gpu_scene_contract.glsl"
#include "material_decode.glsl"
#include "static_mesh_ray_tracing_contract.glsl"

layout(std430, set = 1, binding = 8) readonly buffer StaticMeshRayQueryMaterialTable {
    GpuMaterial records[];
} uStaticMeshMaterials;
layout(std430, set = 1, binding = 9) readonly buffer StaticMeshRayQueryGeometryTable {
    GpuSceneGeometry records[];
} uStaticMeshGeometries;
layout(std430, set = 1, binding = 10) readonly buffer StaticMeshRayQueryInstanceTable {
    GpuSceneInstance records[];
} uStaticMeshInstances;

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer StaticMeshRayQueryWordBuffer {
    uint words[];
};

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer StaticMeshRayQueryMetadataBuffer {
    StaticMeshPrimitiveMetadata records[];
};

struct StaticMeshRayQueryVertex {
    vec3 position;
    vec3 normal;
    vec4 tangent;
    vec2 uv;
};

struct StaticMeshRayQueryTriangle {
    StaticMeshRayQueryVertex vertices[3];
    StaticMeshPrimitiveMetadata metadata;
};

struct StaticMeshRayQueryAttributes {
    vec3 position;
    vec3 normal;
    vec4 tangent;
    vec2 uv;
};

struct StaticMeshRayQueryTextureSamples {
    MaterialTextureSamples material;
    vec3 normal;
    vec3 clearcoatNormal;
};

struct StaticMeshRayQuerySurface {
    vec3 albedo;
    vec3 normal;
    vec3 geometricNormal;
    vec3 emission;
    vec3 specularF0;
    float specularF90;
    float perceptualRoughness;
    float metalness;
    float ao;
    uint stableMaterialId;
    uint stableGeometryId;
};

bool staticMeshRayQueryFinite(vec2 value) {
    return !any(isnan(value)) && !any(isinf(value));
}

bool staticMeshRayQueryFinite(vec3 value) {
    return !any(isnan(value)) && !any(isinf(value));
}

bool staticMeshRayQueryFinite(vec4 value) {
    return !any(isnan(value)) && !any(isinf(value));
}

vec3 staticMeshRayQueryTransformVector(GpuSceneAffineTransform transform, vec3 vector) {
    return vec3(dot(transform.row0.xyz, vector), dot(transform.row1.xyz, vector),
                dot(transform.row2.xyz, vector));
}

vec3 staticMeshRayQueryTransformNormal(GpuSceneAffineTransform objectFromWorld, vec3 normal) {
    return mat3(objectFromWorld.row0.xyz, objectFromWorld.row1.xyz, objectFromWorld.row2.xyz) * normal;
}

uint staticMeshRayQueryTextureIndex(GpuMaterial material, uint semantic) {
    return material.textureIndices[semantic / 4u][semantic % 4u];
}

uint staticMeshRayQuerySamplerIndex(GpuMaterial material, uint semantic) {
    return material.samplerIndices[semantic / 4u][semantic % 4u];
}

bool staticMeshRayQueryInstancePresent(uint customIndex, uint sceneInstanceCount) {
    return customIndex < sceneInstanceCount &&
           uStaticMeshInstances.records[customIndex].identityAndVersion.z == GPU_SCENE_CONTRACT_VERSION;
}

bool staticMeshRayQueryResolveGeometry(uint customIndex, uint geometryIndex, uint sceneInstanceCount,
                                       uint materialCount, uint geometryCount, out GpuSceneInstance instanceData,
                                       out GpuSceneGeometry geometry, out GpuMaterial material) {
    if (!staticMeshRayQueryInstancePresent(customIndex, sceneInstanceCount)) {
        return false;
    }

    instanceData = uStaticMeshInstances.records[customIndex];
    uint instanceFlags = instanceData.geometryMaterialAndFlags.w;
    if (instanceData.identityAndVersion.x == 0u || instanceData.identityAndVersion.y != customIndex ||
        instanceData.identityAndVersion.w != 0u ||
        (instanceFlags & GPU_SCENE_INSTANCE_FLAG_ENABLED) == 0u ||
        (instanceFlags & GPU_SCENE_INSTANCE_FLAG_RAY_TRACING_VISIBLE) == 0u ||
        geometryIndex >= instanceData.geometryMaterialAndFlags.y) {
        return false;
    }

    uint geometryBase = instanceData.geometryMaterialAndFlags.x;
    if (geometryBase >= geometryCount || geometryIndex >= geometryCount - geometryBase) {
        return false;
    }
    geometry = uStaticMeshGeometries.records[geometryBase + geometryIndex];
    uint geometryFlags = geometry.vertexLayoutAndFlags.w;
    uint surfaceFlags = geometryFlags &
                        (GPU_SCENE_GEOMETRY_FLAG_OPAQUE | GPU_SCENE_GEOMETRY_FLAG_CUTOUT |
                         GPU_SCENE_GEOMETRY_FLAG_TRANSPARENT);
    if (!any(notEqual(geometry.vertexAddress, uvec2(0u))) ||
        !any(notEqual(geometry.indexAddress, uvec2(0u))) ||
        !any(notEqual(geometry.primitiveMetadataAddress, uvec2(0u))) ||
        geometry.vertexLayoutAndFlags.x != STATIC_MESH_RAY_TRACING_VERTEX_STRIDE ||
        geometry.vertexLayoutAndFlags.y != STATIC_MESH_RAY_TRACING_POSITION_OFFSET ||
        geometry.vertexLayoutAndFlags.z == 0u ||
        (geometryFlags & GPU_SCENE_GEOMETRY_FLAG_RAY_TRACING_VISIBLE) == 0u ||
        (surfaceFlags != GPU_SCENE_GEOMETRY_FLAG_OPAQUE && surfaceFlags != GPU_SCENE_GEOMETRY_FLAG_CUTOUT) ||
        geometry.indexRangeAndType.y == 0u || geometry.indexRangeAndType.y % 3u != 0u ||
        geometry.indexRangeAndType.z != GPU_SCENE_INDEX_TYPE_UINT32 ||
        geometry.indexRangeAndType.w != GPU_SCENE_CONTRACT_VERSION ||
        geometry.materialAndIdentity.x == GPU_SCENE_INVALID_TABLE_INDEX ||
        geometry.materialAndIdentity.y == 0u || geometry.materialAndIdentity.z == 0u ||
        geometry.materialAndIdentity.w != 16u ||
        geometry.primitiveMeshletAndRevision.x != geometry.indexRangeAndType.y / 3u ||
        geometry.primitiveMeshletAndRevision.w != STATIC_MESH_RAY_TRACING_GEOMETRY_REVISION) {
        return false;
    }

    uint materialBase = instanceData.geometryMaterialAndFlags.z;
    uint assetMaterialIndex = geometry.materialAndIdentity.x;
    if (materialBase >= materialCount || assetMaterialIndex >= materialCount - materialBase) {
        return false;
    }
    material = uStaticMeshMaterials.records[materialBase + assetMaterialIndex];
    if (material.modesAndFlags.w != GPU_MATERIAL_CONTRACT_VERSION ||
        (surfaceFlags == GPU_SCENE_GEOMETRY_FLAG_OPAQUE &&
         material.modesAndFlags.x != GPU_MATERIAL_ALPHA_OPAQUE) ||
        (surfaceFlags == GPU_SCENE_GEOMETRY_FLAG_CUTOUT &&
         material.modesAndFlags.x != GPU_MATERIAL_ALPHA_MASK)) {
        return false;
    }
    return true;
}

bool staticMeshRayQueryLoadMetadata(GpuSceneGeometry geometry, uint primitiveIndex,
                                    out StaticMeshPrimitiveMetadata metadata) {
    metadata = StaticMeshPrimitiveMetadata(0u, 0u, 0u, 0u);
    if (primitiveIndex >= geometry.primitiveMeshletAndRevision.x) {
        return false;
    }
    StaticMeshRayQueryMetadataBuffer metadataBuffer =
        StaticMeshRayQueryMetadataBuffer(geometry.primitiveMetadataAddress);
    metadata = metadataBuffer.records[primitiveIndex];
    return metadata.contractVersion == STATIC_MESH_RAY_TRACING_CONTRACT_VERSION &&
           metadata.materialIndex == geometry.materialAndIdentity.x &&
           metadata.stableMaterialId == geometry.materialAndIdentity.y &&
           metadata.stableGeometryId == geometry.materialAndIdentity.z;
}

bool staticMeshRayQueryDecodeVertex(StaticMeshRayQueryWordBuffer vertexBuffer, uint vertexIndex,
                                    uint vertexCount, out StaticMeshRayQueryVertex vertex) {
    vertex.position = vec3(0.0);
    vertex.normal = vec3(0.0);
    vertex.tangent = vec4(0.0);
    vertex.uv = vec2(0.0);
    const uint wordsPerVertex = STATIC_MESH_RAY_TRACING_VERTEX_STRIDE / 4u;
    if (vertexIndex >= vertexCount || vertexIndex > 0xffffffffu / wordsPerVertex) {
        return false;
    }
    uint firstWord = vertexIndex * wordsPerVertex;
    uint positionWord = firstWord + STATIC_MESH_RAY_TRACING_POSITION_OFFSET / 4u;
    uint normalWord = firstWord + STATIC_MESH_RAY_TRACING_NORMAL_OFFSET / 4u;
    uint tangentWord = firstWord + STATIC_MESH_RAY_TRACING_TANGENT_OFFSET / 4u;
    uint uvWord = firstWord + STATIC_MESH_RAY_TRACING_UV_OFFSET / 4u;
    vertex.position = vec3(uintBitsToFloat(vertexBuffer.words[positionWord + 0u]),
                           uintBitsToFloat(vertexBuffer.words[positionWord + 1u]),
                           uintBitsToFloat(vertexBuffer.words[positionWord + 2u]));
    vertex.normal = vec3(uintBitsToFloat(vertexBuffer.words[normalWord + 0u]),
                         uintBitsToFloat(vertexBuffer.words[normalWord + 1u]),
                         uintBitsToFloat(vertexBuffer.words[normalWord + 2u]));
    vertex.tangent = vec4(uintBitsToFloat(vertexBuffer.words[tangentWord + 0u]),
                          uintBitsToFloat(vertexBuffer.words[tangentWord + 1u]),
                          uintBitsToFloat(vertexBuffer.words[tangentWord + 2u]),
                          uintBitsToFloat(vertexBuffer.words[tangentWord + 3u]));
    vertex.uv = vec2(uintBitsToFloat(vertexBuffer.words[uvWord + 0u]),
                     uintBitsToFloat(vertexBuffer.words[uvWord + 1u]));
    return staticMeshRayQueryFinite(vertex.position) && staticMeshRayQueryFinite(vertex.normal) &&
           staticMeshRayQueryFinite(vertex.tangent) && staticMeshRayQueryFinite(vertex.uv) &&
           dot(vertex.normal, vertex.normal) > 1.0e-12 && dot(vertex.tangent.xyz, vertex.tangent.xyz) > 1.0e-12 &&
           abs(vertex.tangent.w) > 0.5;
}

bool staticMeshRayQueryLoadTriangle(GpuSceneGeometry geometry, uint primitiveIndex,
                                    out StaticMeshRayQueryTriangle triangle) {
    for (uint index = 0u; index < 3u; ++index) {
        triangle.vertices[index].position = vec3(0.0);
        triangle.vertices[index].normal = vec3(0.0);
        triangle.vertices[index].tangent = vec4(0.0);
        triangle.vertices[index].uv = vec2(0.0);
    }
    triangle.metadata = StaticMeshPrimitiveMetadata(0u, 0u, 0u, 0u);
    if (!staticMeshRayQueryLoadMetadata(geometry, primitiveIndex, triangle.metadata) ||
        primitiveIndex > 0xffffffffu / 3u) {
        return false;
    }

    uint firstIndex = geometry.indexRangeAndType.x;
    uint primitiveFirstIndex = primitiveIndex * 3u;
    if (primitiveFirstIndex > 0xffffffffu - firstIndex ||
        primitiveFirstIndex + 3u > geometry.indexRangeAndType.y) {
        return false;
    }
    StaticMeshRayQueryWordBuffer indexBuffer = StaticMeshRayQueryWordBuffer(geometry.indexAddress);
    StaticMeshRayQueryWordBuffer vertexBuffer = StaticMeshRayQueryWordBuffer(geometry.vertexAddress);
    for (uint vertex = 0u; vertex < 3u; ++vertex) {
        uint vertexIndex = indexBuffer.words[firstIndex + primitiveFirstIndex + vertex];
        if (!staticMeshRayQueryDecodeVertex(vertexBuffer, vertexIndex, geometry.vertexLayoutAndFlags.z,
                                            triangle.vertices[vertex])) {
            return false;
        }
    }
    return true;
}

bool staticMeshRayQueryInterpolateAttributes(StaticMeshRayQueryTriangle triangle, vec2 barycentrics,
                                             out StaticMeshRayQueryAttributes attributes) {
    attributes.position = vec3(0.0);
    attributes.normal = vec3(0.0);
    attributes.tangent = vec4(0.0);
    attributes.uv = vec2(0.0);
    if (!staticMeshRayQueryFinite(barycentrics)) {
        return false;
    }
    vec3 weights = vec3(1.0 - barycentrics.x - barycentrics.y, barycentrics);
    attributes.position = triangle.vertices[0].position * weights.x + triangle.vertices[1].position * weights.y +
                          triangle.vertices[2].position * weights.z;
    attributes.normal = triangle.vertices[0].normal * weights.x + triangle.vertices[1].normal * weights.y +
                        triangle.vertices[2].normal * weights.z;
    vec3 tangent = triangle.vertices[0].tangent.xyz * weights.x + triangle.vertices[1].tangent.xyz * weights.y +
                   triangle.vertices[2].tangent.xyz * weights.z;
    float tangentSign = triangle.vertices[0].tangent.w * weights.x + triangle.vertices[1].tangent.w * weights.y +
                        triangle.vertices[2].tangent.w * weights.z;
    attributes.uv = triangle.vertices[0].uv * weights.x + triangle.vertices[1].uv * weights.y +
                    triangle.vertices[2].uv * weights.z;
    float normalLengthSquared = dot(attributes.normal, attributes.normal);
    float tangentLengthSquared = dot(tangent, tangent);
    if (!staticMeshRayQueryFinite(attributes.position) || !staticMeshRayQueryFinite(attributes.normal) ||
        !staticMeshRayQueryFinite(tangent) || !staticMeshRayQueryFinite(attributes.uv) ||
        isnan(tangentSign) || isinf(tangentSign) || normalLengthSquared <= 1.0e-12 ||
        tangentLengthSquared <= 1.0e-12 || abs(tangentSign) <= 0.5) {
        return false;
    }
    attributes.normal *= inversesqrt(normalLengthSquared);
    attributes.tangent = vec4(tangent * inversesqrt(tangentLengthSquared), tangentSign < 0.0 ? -1.0 : 1.0);
    return true;
}

bool staticMeshRayQueryConeTextureLod(StaticMeshRayQueryTriangle triangle, GpuSceneInstance instanceData,
                                      float coneWidth, uint textureIndex, uint samplerIndex, out float lod) {
    lod = 0.0;
    if (isnan(coneWidth) || isinf(coneWidth) || coneWidth <= 0.0) {
        return false;
    }
    ivec2 textureExtent = textureSize(
        sampler2D(globalBindlessTextures2D[nonuniformEXT(textureIndex)],
                  globalBindlessSamplers[nonuniformEXT(samplerIndex)]),
        0);
    int textureMipLevels = textureQueryLevels(
        sampler2D(globalBindlessTextures2D[nonuniformEXT(textureIndex)],
                  globalBindlessSamplers[nonuniformEXT(samplerIndex)]));
    if (textureExtent.x <= 0 || textureExtent.y <= 0 || textureMipLevels <= 0) {
        return false;
    }

    vec3 position0 = gpuSceneTransformPoint(instanceData.worldFromObject, triangle.vertices[0].position);
    vec3 position1 = gpuSceneTransformPoint(instanceData.worldFromObject, triangle.vertices[1].position);
    vec3 position2 = gpuSceneTransformPoint(instanceData.worldFromObject, triangle.vertices[2].position);
    vec3 edge1 = position1 - position0;
    vec3 edge2 = position2 - position0;
    vec2 uvEdge1 = triangle.vertices[1].uv - triangle.vertices[0].uv;
    vec2 uvEdge2 = triangle.vertices[2].uv - triangle.vertices[0].uv;
    float edge11 = dot(edge1, edge1);
    float edge12 = dot(edge1, edge2);
    float edge22 = dot(edge2, edge2);
    float determinant = edge11 * edge22 - edge12 * edge12;
    if (!staticMeshRayQueryFinite(position0) || !staticMeshRayQueryFinite(position1) ||
        !staticMeshRayQueryFinite(position2) || isnan(determinant) || isinf(determinant) ||
        determinant <= 1.0e-12) {
        return false;
    }

    vec3 gradientU = ((uvEdge1.x * edge22 - uvEdge2.x * edge12) * edge1 +
                      (uvEdge2.x * edge11 - uvEdge1.x * edge12) * edge2) /
                     determinant;
    vec3 gradientV = ((uvEdge1.y * edge22 - uvEdge2.y * edge12) * edge1 +
                      (uvEdge2.y * edge11 - uvEdge1.y * edge12) * edge2) /
                     determinant;
    float texelFootprint = coneWidth *
                           max(length(gradientU) * float(textureExtent.x),
                               length(gradientV) * float(textureExtent.y));
    if (isnan(texelFootprint) || isinf(texelFootprint)) {
        return false;
    }
    lod = clamp(log2(max(texelFootprint, 1.0)), 0.0, float(textureMipLevels - 1));
    return true;
}

bool staticMeshRayQuerySampleSemantic(StaticMeshRayQueryTriangle triangle, GpuSceneInstance instanceData,
                                      GpuMaterial material, uint semantic, vec2 uv, float coneWidth,
                                      out vec4 sampleValue, out float lod) {
    uint textureIndex = staticMeshRayQueryTextureIndex(material, semantic);
    uint samplerIndex = staticMeshRayQuerySamplerIndex(material, semantic);
    sampleValue = vec4(0.0);
    if (!staticMeshRayQueryConeTextureLod(triangle, instanceData, coneWidth, textureIndex, samplerIndex, lod)) {
        return false;
    }
    sampleValue = textureLod(sampler2D(globalBindlessTextures2D[nonuniformEXT(textureIndex)],
                                       globalBindlessSamplers[nonuniformEXT(samplerIndex)]),
                             uv, lod);
    return staticMeshRayQueryFinite(sampleValue);
}

bool staticMeshRayQuerySampleMaterial(StaticMeshRayQueryTriangle triangle, GpuSceneInstance instanceData,
                                      GpuMaterial material, vec2 uv, float coneWidth,
                                      out StaticMeshRayQueryTextureSamples samples, out float baseColorLod) {
    samples.material = defaultMaterialTextureSamples();
    samples.normal = vec3(0.0);
    samples.clearcoatNormal = vec3(0.0);
    baseColorLod = 0.0;
    vec4 semanticSamples[12];
    float semanticLods[12];
    for (uint semantic = 0u; semantic < 12u; ++semantic) {
        if (!staticMeshRayQuerySampleSemantic(triangle, instanceData, material, semantic, uv, coneWidth,
                                              semanticSamples[semantic], semanticLods[semantic])) {
            return false;
        }
    }
    baseColorLod = semanticLods[GPU_MATERIAL_TEXTURE_BASE_COLOR];
    samples.material.baseColor = semanticSamples[GPU_MATERIAL_TEXTURE_BASE_COLOR];
    samples.material.properties = semanticSamples[GPU_MATERIAL_TEXTURE_METALLIC_ROUGHNESS_OR_SPECULAR_GLOSSINESS];
    samples.normal = semanticSamples[GPU_MATERIAL_TEXTURE_NORMAL].xyz;
    samples.material.occlusion = semanticSamples[GPU_MATERIAL_TEXTURE_OCCLUSION].r;
    samples.material.emissive = semanticSamples[GPU_MATERIAL_TEXTURE_EMISSIVE].rgb;
    samples.material.specularWeight = semanticSamples[GPU_MATERIAL_TEXTURE_SPECULAR_WEIGHT].a;
    samples.material.specularColor = semanticSamples[GPU_MATERIAL_TEXTURE_SPECULAR_COLOR].rgb;
    samples.material.clearcoat = semanticSamples[GPU_MATERIAL_TEXTURE_CLEARCOAT].r;
    samples.material.clearcoatRoughness = semanticSamples[GPU_MATERIAL_TEXTURE_CLEARCOAT_ROUGHNESS].g;
    samples.clearcoatNormal = semanticSamples[GPU_MATERIAL_TEXTURE_CLEARCOAT_NORMAL].xyz;
    samples.material.transmission = semanticSamples[GPU_MATERIAL_TEXTURE_TRANSMISSION].r;
    samples.material.thickness = semanticSamples[GPU_MATERIAL_TEXTURE_THICKNESS].g;
    return staticMeshRayQueryFinite(samples.normal) && staticMeshRayQueryFinite(samples.clearcoatNormal);
}

bool staticMeshRayQuerySampleBaseColor(StaticMeshRayQueryTriangle triangle, GpuSceneInstance instanceData,
                                       GpuMaterial material, vec2 uv, float coneWidth,
                                       out vec4 baseColor, out float lod) {
    return staticMeshRayQuerySampleSemantic(triangle, instanceData, material,
                                            GPU_MATERIAL_TEXTURE_BASE_COLOR, uv, coneWidth,
                                            baseColor, lod);
}

bool staticMeshRayQueryTangentFrame(GpuSceneInstance instanceData, StaticMeshRayQueryAttributes attributes,
                                    out mat3 frame) {
    frame = mat3(1.0);
    vec3 objectNormal = normalize(attributes.normal);
    vec3 objectTangent = attributes.tangent.xyz - objectNormal * dot(objectNormal, attributes.tangent.xyz);
    float objectTangentLengthSquared = dot(objectTangent, objectTangent);
    if (!staticMeshRayQueryFinite(objectNormal) || !staticMeshRayQueryFinite(objectTangent) ||
        objectTangentLengthSquared <= 1.0e-12) {
        return false;
    }
    objectTangent *= inversesqrt(objectTangentLengthSquared);
    vec3 objectBitangent = normalize(cross(objectNormal, objectTangent)) * attributes.tangent.w;

    vec3 worldNormal = staticMeshRayQueryTransformNormal(instanceData.objectFromWorld, objectNormal);
    vec3 worldTangent = staticMeshRayQueryTransformVector(instanceData.worldFromObject, objectTangent);
    vec3 transformedBitangent = staticMeshRayQueryTransformVector(instanceData.worldFromObject, objectBitangent);
    float worldNormalLengthSquared = dot(worldNormal, worldNormal);
    if (!staticMeshRayQueryFinite(worldNormal) || !staticMeshRayQueryFinite(worldTangent) ||
        !staticMeshRayQueryFinite(transformedBitangent) || worldNormalLengthSquared <= 1.0e-12) {
        return false;
    }
    worldNormal *= inversesqrt(worldNormalLengthSquared);
    worldTangent -= worldNormal * dot(worldNormal, worldTangent);
    float worldTangentLengthSquared = dot(worldTangent, worldTangent);
    if (worldTangentLengthSquared <= 1.0e-12 || dot(transformedBitangent, transformedBitangent) <= 1.0e-12) {
        return false;
    }
    worldTangent *= inversesqrt(worldTangentLengthSquared);
    float handedness = dot(cross(worldNormal, worldTangent), transformedBitangent) < 0.0 ? -1.0 : 1.0;
    vec3 worldBitangent = normalize(cross(worldNormal, worldTangent)) * handedness;
    if (!staticMeshRayQueryFinite(worldBitangent)) {
        return false;
    }
    frame = mat3(worldTangent, worldBitangent, worldNormal);
    return true;
}

bool staticMeshRayQueryCommittedSurface(uint customIndex, uint geometryIndex, uint primitiveIndex,
                                        vec2 barycentrics, float coneWidth, vec3 incomingRayDirection,
                                        float emissionScale, uint sceneInstanceCount, uint materialCount,
                                        uint geometryCount, out StaticMeshRayQuerySurface surface) {
    surface.albedo = vec3(0.0);
    surface.normal = vec3(0.0, 1.0, 0.0);
    surface.geometricNormal = vec3(0.0, 1.0, 0.0);
    surface.emission = vec3(0.0);
    surface.specularF0 = vec3(0.04);
    surface.specularF90 = 1.0;
    surface.perceptualRoughness = 1.0;
    surface.metalness = 0.0;
    surface.ao = 0.0;
    surface.stableMaterialId = 0u;
    surface.stableGeometryId = 0u;
    if (!staticMeshRayQueryFinite(incomingRayDirection) || dot(incomingRayDirection, incomingRayDirection) <= 1.0e-12 ||
        isnan(emissionScale) || isinf(emissionScale) || emissionScale < 0.0) {
        return false;
    }

    GpuSceneInstance instanceData;
    GpuSceneGeometry geometry;
    GpuMaterial material;
    StaticMeshRayQueryTriangle triangle;
    StaticMeshRayQueryAttributes attributes;
    StaticMeshRayQueryTextureSamples textureSamples;
    float baseColorLod = 0.0;
    if (!staticMeshRayQueryResolveGeometry(customIndex, geometryIndex, sceneInstanceCount, materialCount,
                                           geometryCount, instanceData, geometry, material) ||
        !staticMeshRayQueryLoadTriangle(geometry, primitiveIndex, triangle) ||
        !staticMeshRayQueryInterpolateAttributes(triangle, barycentrics, attributes) ||
        !staticMeshRayQuerySampleMaterial(triangle, instanceData, material, attributes.uv, coneWidth,
                                          textureSamples, baseColorLod)) {
        return false;
    }

    MaterialSample sampledMaterial = decodeGltfMaterial(material, textureSamples.material);
    if (!staticMeshRayQueryFinite(sampledMaterial.baseColor) ||
        !materialPassesAlphaTest(material, sampledMaterial.baseColor.a) ||
        !staticMeshRayQueryFinite(sampledMaterial.emissive) ||
        !staticMeshRayQueryFinite(sampledMaterial.dielectricF0) || isnan(sampledMaterial.specularF90) ||
        isinf(sampledMaterial.specularF90) || isnan(sampledMaterial.perceptualRoughness) ||
        isinf(sampledMaterial.perceptualRoughness) || isnan(sampledMaterial.metalness) ||
        isinf(sampledMaterial.metalness) || isnan(sampledMaterial.occlusion) ||
        isinf(sampledMaterial.occlusion)) {
        return false;
    }

    mat3 tangentFrame;
    if (!staticMeshRayQueryTangentFrame(instanceData, attributes, tangentFrame)) {
        return false;
    }
    vec3 geometricNormal = normalize(tangentFrame[2]);
    vec3 tangentNormal = decodeMaterialTangentNormal(textureSamples.normal, material.materialFactors.z);
    vec3 worldNormal = normalize(tangentFrame * tangentNormal);
    if (!staticMeshRayQueryFinite(tangentNormal) || !staticMeshRayQueryFinite(geometricNormal) ||
        !staticMeshRayQueryFinite(worldNormal)) {
        return false;
    }
    geometricNormal = faceforward(geometricNormal, incomingRayDirection, geometricNormal);
    worldNormal = faceforward(worldNormal, incomingRayDirection, geometricNormal);
    if (dot(worldNormal, geometricNormal) < 0.0) {
        worldNormal = -worldNormal;
    }

    surface.albedo = sampledMaterial.baseColor.rgb;
    surface.normal = worldNormal;
    surface.geometricNormal = geometricNormal;
    surface.emission = evaluateMaterialEmission(sampledMaterial) * emissionScale;
    surface.specularF0 = pbrMaterialSpecularF0(sampledMaterial.dielectricF0, surface.albedo,
                                               sampledMaterial.metalness);
    surface.specularF90 = pbrMaterialSpecularF90(sampledMaterial.specularF90, sampledMaterial.metalness);
    surface.perceptualRoughness = sampledMaterial.perceptualRoughness;
    surface.metalness = sampledMaterial.metalness;
    surface.ao = sampledMaterial.occlusion;
    surface.stableMaterialId = triangle.metadata.stableMaterialId;
    surface.stableGeometryId = triangle.metadata.stableGeometryId;
    return staticMeshRayQueryFinite(surface.albedo) && staticMeshRayQueryFinite(surface.normal) &&
           staticMeshRayQueryFinite(surface.emission) && staticMeshRayQueryFinite(surface.specularF0) &&
           !isnan(surface.specularF90) && !isinf(surface.specularF90) &&
           !isnan(surface.perceptualRoughness) && !isinf(surface.perceptualRoughness) &&
           !isnan(surface.metalness) && !isinf(surface.metalness) && !isnan(surface.ao) && !isinf(surface.ao);
}

bool staticMeshRayQueryCandidateAlphaPasses(uint customIndex, uint geometryIndex, uint primitiveIndex,
                                            vec2 barycentrics, float coneWidth, uint sceneInstanceCount,
                                            uint materialCount, uint geometryCount, out float sampledOpacity,
                                            out float sampledLod, out uint stableMaterialId,
                                            out uint stableGeometryId) {
    sampledOpacity = 0.0;
    sampledLod = 0.0;
    stableMaterialId = 0u;
    stableGeometryId = 0u;
    GpuSceneInstance instanceData;
    GpuSceneGeometry geometry;
    GpuMaterial material;
    if (!staticMeshRayQueryResolveGeometry(customIndex, geometryIndex, sceneInstanceCount, materialCount,
                                           geometryCount, instanceData, geometry, material) ||
        (geometry.vertexLayoutAndFlags.w & GPU_SCENE_GEOMETRY_FLAG_CUTOUT) == 0u) {
        return false;
    }
    StaticMeshRayQueryTriangle triangle;
    StaticMeshRayQueryAttributes attributes;
    if (!staticMeshRayQueryLoadTriangle(geometry, primitiveIndex, triangle) ||
        !staticMeshRayQueryInterpolateAttributes(triangle, barycentrics, attributes)) {
        return false;
    }
    vec4 baseColorSample;
    if (!staticMeshRayQuerySampleBaseColor(triangle, instanceData, material, attributes.uv, coneWidth,
                                           baseColorSample, sampledLod)) {
        return false;
    }
    sampledOpacity = baseColorSample.a * material.baseColorFactor.a;
    stableMaterialId = triangle.metadata.stableMaterialId;
    stableGeometryId = triangle.metadata.stableGeometryId;
    return !isnan(sampledOpacity) && !isinf(sampledOpacity) &&
           materialPassesAlphaTest(material, sampledOpacity);
}

bool staticMeshRayQueryCommittedIdentity(uint customIndex, uint geometryIndex, uint primitiveIndex,
                                         uint sceneInstanceCount, uint materialCount, uint geometryCount,
                                         out uint stableMaterialId, out uint stableGeometryId) {
    stableMaterialId = 0u;
    stableGeometryId = 0u;
    GpuSceneInstance instanceData;
    GpuSceneGeometry geometry;
    GpuMaterial material;
    StaticMeshPrimitiveMetadata metadata;
    if (!staticMeshRayQueryResolveGeometry(customIndex, geometryIndex, sceneInstanceCount, materialCount,
                                           geometryCount, instanceData, geometry, material) ||
        !staticMeshRayQueryLoadMetadata(geometry, primitiveIndex, metadata)) {
        return false;
    }
    stableMaterialId = metadata.stableMaterialId;
    stableGeometryId = metadata.stableGeometryId;
    return true;
}

#endif // MECRAFT_STATIC_MESH_RAY_QUERY_GLSL
