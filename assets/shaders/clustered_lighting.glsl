#ifndef MECRAFT_CLUSTERED_LIGHTING_GLSL
#define MECRAFT_CLUSTERED_LIGHTING_GLSL

#include "cluster_build_contract.glsl"
#include "clustered_light_evaluation.glsl"
#include "local_shadow_contract.glsl"
#include "rhi_screen_coordinates.glsl"

#ifndef MECRAFT_CLUSTER_BIND_SET
#define MECRAFT_CLUSTER_BIND_SET 1
#endif

layout(set = MECRAFT_CLUSTER_BIND_SET, binding = 0, std430) readonly buffer ClusterLightBuffer {
    GpuLight uClusterLights[];
};
layout(set = MECRAFT_CLUSTER_BIND_SET, binding = 1, std430) readonly buffer ClusterRecordBuffer {
    ClusterRecord uClusterRecords[];
};
layout(set = MECRAFT_CLUSTER_BIND_SET, binding = 2, std430) readonly buffer ClusterIndexBuffer {
    uint uClusterLightIndices[];
};
layout(set = MECRAFT_CLUSTER_BIND_SET, binding = 3, std430) readonly buffer ClusterStatsBuffer {
    uint uClusterStats[];
};
layout(set = MECRAFT_CLUSTER_BIND_SET, binding = 4, std430) readonly buffer LocalShadowMetadataBuffer {
    LocalShadowMetadata uLocalShadowMetadata[];
};
layout(set = MECRAFT_CLUSTER_BIND_SET, binding = 5) uniform sampler2D uLocalShadowSpotAtlas;
layout(set = MECRAFT_CLUSTER_BIND_SET, binding = 6) uniform samplerCubeArray uLocalShadowPointCubeArray;

bool localShadowFinite(float value) {
    return !isnan(value) && !isinf(value);
}

bool localShadowFinite(vec4 value) {
    return all(not(isnan(value))) && all(not(isinf(value)));
}

bool localShadowCommonMetadataValid(LocalShadowMetadata metadata) {
    vec4 parameters = metadata.nearFarDepthBiasNormalOffset;
    return localShadowFinite(parameters) &&
        parameters.x > 0.0 && parameters.y > parameters.x &&
        parameters.z >= 0.0 && parameters.w >= 0.0 &&
        metadata.classification.w == LOCAL_SHADOW_CONTRACT_VERSION;
}

bool localShadowPointProjectedDepth(
    LocalShadowMetadata metadata,
    vec3 direction,
    float receiverDistance,
    out float depth) {
    vec4 nearFar = metadata.nearFarDepthBiasNormalOffset;
    float faceDepth = receiverDistance * max(max(abs(direction.x), abs(direction.y)), abs(direction.z));
    if (!localShadowFinite(vec4(direction, receiverDistance)) ||
        !localShadowFinite(faceDepth) || faceDepth <= 0.0 || nearFar.y <= nearFar.x) {
        return false;
    }
    // The six 90-degree GLM perspective faces share the same radial depth
    // mapping. Avoiding a per-tap matrix multiply preserves the rendered
    // depth value while keeping the point-shadow PCF path arithmetic-only.
    depth = nearFar.y / (nearFar.y - nearFar.x) -
        nearFar.y * nearFar.x / ((nearFar.y - nearFar.x) * faceDepth);
    return localShadowFinite(depth);
}

float sampleLocalSpotShadow(
    LocalShadowMetadata metadata,
    vec3 cameraRelativeSurface,
    vec3 normal,
    out vec3 resourceCoordinate,
    out bool valid) {
    valid = true;
    resourceCoordinate = vec3(0.0);
    vec4 atlas = metadata.atlasScaleBias;
    if (!localShadowCommonMetadataValid(metadata) ||
        metadata.classification.x != LOCAL_SHADOW_TYPE_SPOT ||
        metadata.classification.y >= LOCAL_SHADOW_SPOT_METADATA_COUNT ||
        metadata.classification.z != 1u || !localShadowFinite(atlas) ||
        any(lessThanEqual(atlas.xy, vec2(0.0))) ||
        any(lessThan(atlas.zw, vec2(0.0))) ||
        any(greaterThan(atlas.zw + atlas.xy, vec2(1.0)))) {
        valid = false;
        return 1.0;
    }

    vec3 receiver = cameraRelativeSurface + normal *
        metadata.nearFarDepthBiasNormalOffset.w;
    vec4 clip = metadata.cameraRelativeViewProjection[0] *
        vec4(receiver, 1.0);
    if (!localShadowFinite(clip) || clip.w <= 0.0) {
        valid = false;
        return 1.0;
    }
    vec3 ndc = clip.xyz / clip.w;
    if (!localShadowFinite(vec4(ndc, 0.0))) {
        valid = false;
        return 1.0;
    }
    vec2 localClipUv = ndc.xy * 0.5 + 0.5;
    vec2 localTextureUv = rhiScreenUvToTextureUv(
        rhiScreenUvToClipUv(localClipUv));
    vec2 atlasUv = localTextureUv * atlas.xy + atlas.zw;
    resourceCoordinate = vec3(atlasUv, 0.0);
    float referenceDepth = ndc.z * 0.5 + 0.5 -
        metadata.nearFarDepthBiasNormalOffset.z;
    if (any(lessThan(localClipUv, vec2(0.0))) ||
        any(greaterThan(localClipUv, vec2(1.0))) ||
        referenceDepth < 0.0 || referenceDepth > 1.0) {
        return 1.0;
    }

    ivec2 atlasSize = textureSize(uLocalShadowSpotAtlas, 0);
    if (any(lessThanEqual(atlasSize, ivec2(0)))) {
        valid = false;
        return 1.0;
    }
    vec2 texel = 1.0 / vec2(atlasSize);
    vec2 tileMinimum = atlas.zw + texel * 0.5;
    vec2 tileMaximum = atlas.zw + atlas.xy - texel * 0.5;
    if (any(greaterThan(tileMinimum, tileMaximum))) {
        valid = false;
        return 1.0;
    }

    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 sampleUv = clamp(
                atlasUv + vec2(float(x), float(y)) * texel,
                tileMinimum, tileMaximum);
            float storedDepth = texture(uLocalShadowSpotAtlas, sampleUv).r;
            if (!localShadowFinite(storedDepth)) {
                valid = false;
                return 1.0;
            }
            visibility += referenceDepth <= storedDepth ? 1.0 : 0.0;
        }
    }
    return visibility / 9.0;
}

float sampleLocalPointShadow(
    GpuLight light,
    LocalShadowMetadata metadata,
    vec3 cameraRelativeSurface,
    vec3 normal,
    out vec3 resourceCoordinate,
    out bool valid) {
    valid = true;
    resourceCoordinate = vec3(0.0);
    if (!localShadowCommonMetadataValid(metadata) ||
        metadata.classification.x != LOCAL_SHADOW_TYPE_POINT ||
        metadata.classification.y >= LOCAL_SHADOW_POINT_METADATA_COUNT ||
        metadata.classification.z != 6u ||
        any(notEqual(metadata.atlasScaleBias, vec4(0.0)))) {
        valid = false;
        return 1.0;
    }

    vec3 receiver = cameraRelativeSurface + normal *
        metadata.nearFarDepthBiasNormalOffset.w;
    vec3 receiverVector = receiver - light.positionAndRange.xyz;
    float receiverDistance = length(receiverVector);
    if (!localShadowFinite(receiverDistance)) {
        valid = false;
        return 1.0;
    }
    if (receiverDistance <= metadata.nearFarDepthBiasNormalOffset.x ||
        receiverDistance >= metadata.nearFarDepthBiasNormalOffset.y) {
        return 1.0;
    }

    vec3 baseDirection = receiverVector / receiverDistance;
    resourceCoordinate = baseDirection * 0.5 + 0.5;
    vec3 referenceAxis = abs(baseDirection.y) < 0.99
        ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(referenceAxis, baseDirection));
    vec3 bitangent = cross(baseDirection, tangent);
    int faceResolution = textureSize(uLocalShadowPointCubeArray, 0).x;
    if (faceResolution <= 0) {
        valid = false;
        return 1.0;
    }
    float angularTexel = 2.0 / float(faceResolution);

    // Four diagonal taps preserve a soft point shadow while scaling to many voxel lights.
    const vec2 pcfOffsets[4] = vec2[4](
        vec2(-0.75, -0.75), vec2(0.75, -0.75),
        vec2(-0.75, 0.75), vec2(0.75, 0.75));
    float visibility = 0.0;
    for (uint sampleIndex = 0u; sampleIndex < 4u; ++sampleIndex) {
        vec2 offset = pcfOffsets[sampleIndex];
        vec3 sampleDirection = normalize(
            baseDirection + (tangent * offset.x + bitangent * offset.y) * angularTexel);
        float referenceDepth;
        if (!localShadowPointProjectedDepth(metadata, sampleDirection,
                                            receiverDistance, referenceDepth)) {
            valid = false;
            return 1.0;
        }
        referenceDepth -= metadata.nearFarDepthBiasNormalOffset.z;
        float storedDepth = texture(
            uLocalShadowPointCubeArray,
            vec4(sampleDirection,
                 float(metadata.classification.y))).r;
        if (!localShadowFinite(storedDepth)) {
            valid = false;
            return 1.0;
        }
        visibility += referenceDepth <= storedDepth ? 1.0 : 0.0;
    }
    return visibility / 4.0;
}

float localShadowVisibility(
    GpuLight light,
    vec3 cameraRelativeSurface,
    vec3 normal,
    out vec3 resourceCoordinate,
    out bool valid) {
    resourceCoordinate = vec3(0.0);
    valid = true;
    uint policy = gpuLightShadowPolicy(light);
    uint metadataIndex = gpuLightShadowIndex(light);
    if (policy == GPU_LIGHT_SHADOW_NONE) {
        valid = metadataIndex == GPU_LIGHT_INVALID_RESOURCE_INDEX;
        return 1.0;
    }
    if ((policy != GPU_LIGHT_SHADOW_RASTER_DYNAMIC &&
         policy != GPU_LIGHT_SHADOW_RASTER_CACHED) ||
        metadataIndex >= LOCAL_SHADOW_METADATA_COUNT) {
        valid = false;
        return 1.0;
    }

    LocalShadowMetadata metadata = uLocalShadowMetadata[metadataIndex];
    uint type = gpuLightType(light);
    if (type == GPU_LIGHT_TYPE_SPOT) {
        if (metadataIndex >= LOCAL_SHADOW_SPOT_METADATA_COUNT ||
            metadata.classification.y != metadataIndex) {
            valid = false;
            return 1.0;
        }
        return sampleLocalSpotShadow(
            metadata, cameraRelativeSurface, normal,
            resourceCoordinate, valid);
    }
    if (type == GPU_LIGHT_TYPE_POINT) {
        if (metadataIndex < LOCAL_SHADOW_POINT_METADATA_BASE ||
            metadata.classification.y !=
                metadataIndex - LOCAL_SHADOW_POINT_METADATA_BASE) {
            valid = false;
            return 1.0;
        }
        return sampleLocalPointShadow(
            light, metadata, cameraRelativeSurface, normal,
            resourceCoordinate, valid);
    }
    valid = false;
    return 1.0;
}

uint clusteredSurfaceClusterIndex(
    vec2 clipUv,
    float viewDepth,
    uvec4 grid,
    uvec4 renderExtent,
    vec4 depthParameters) {
    uvec2 pixel = uvec2(clamp(clipUv, vec2(0.0), vec2(0.999999)) *
        vec2(renderExtent.xy));
    uvec2 tile = min(pixel / grid.ww, grid.xy - uvec2(1u));
    float clampedDepth = clamp(
        viewDepth, depthParameters.x, depthParameters.y);
    uint slice = min(uint(max(floor(log(clampedDepth) *
        depthParameters.z + depthParameters.w), 0.0)), grid.z - 1u);
    return (slice * grid.y + tile.y) * grid.x + tile.x;
}

ClusteredSurfaceLighting evaluateClusteredSurfaceLighting(
    vec2 clipUv,
    float viewDepth,
    uvec4 grid,
    uvec4 renderExtent,
    vec4 depthParameters,
    vec3 cameraRelativeSurface,
    vec3 normal,
    vec3 viewDirection,
    vec3 specularF0,
    float specularF90,
    float perceptualRoughness,
    out bool buildValid) {
    ClusteredSurfaceLighting result;
    result.diffuse = vec3(0.0);
    result.specular = vec3(0.0);
    result.shadowLightId = GPU_LIGHT_INVALID_RESOURCE_INDEX;
    result.shadowMetadataIndex = GPU_LIGHT_INVALID_RESOURCE_INDEX;
    result.shadowResourceCoordinate = vec3(0.0);
    result.shadowVisibility = 1.0;
    result.shadowWeight = 0.0;
    buildValid = uClusterStats[CLUSTER_STATS_BUILD_ERROR] == 0u &&
        uClusterStats[CLUSTER_STATS_CONTRACT_VERSION] ==
            GPU_LIGHT_CONTRACT_VERSION;
    if (!buildValid) {
        return result;
    }

    uint clusterIndex = clusteredSurfaceClusterIndex(
        clipUv, viewDepth, grid, renderExtent, depthParameters);
    ClusterRecord record = uClusterRecords[clusterIndex];
    uint capacity = uClusterStats[CLUSTER_STATS_INDEX_CAPACITY];
    uint lightCount = uClusterStats[CLUSTER_STATS_LIGHT_COUNT];
    if (record.offset > capacity ||
        record.count > capacity - min(record.offset, capacity)) {
        buildValid = false;
        return result;
    }
    if (record.count == 0u) {
        return result;
    }
    float nDotV = max(dot(normal, viewDirection), 0.0);
    float alphaSquared =
        pbrPerceptualRoughnessToAlphaSquared(perceptualRoughness);
    for (uint listIndex = 0u; listIndex < record.count; ++listIndex) {
        uint lightIndex = uClusterLightIndices[record.offset + listIndex];
        if (lightIndex >= lightCount) {
            buildValid = false;
            return result;
        }
        GpuLight light = uClusterLights[lightIndex];
        GpuLightSurfaceContribution contribution = evaluateGpuLight(
            light, cameraRelativeSurface,
            normal, viewDirection, specularF0, specularF90,
            nDotV, alphaSquared);
        vec3 combinedContribution =
            contribution.diffuse + contribution.specular;
        if (all(lessThanEqual(combinedContribution, vec3(0.0)))) {
            continue;
        }

        float shadowVisibilityValue = 1.0;
        if (gpuLightShadowPolicy(light) != GPU_LIGHT_SHADOW_NONE) {
            float shadowWeight = dot(
                combinedContribution, vec3(0.2126, 0.7152, 0.0722));
            vec3 shadowResourceCoordinate;
            bool shadowValid;
            shadowVisibilityValue = localShadowVisibility(
                light, cameraRelativeSurface, normal,
                shadowResourceCoordinate, shadowValid);
            if (!shadowValid) {
                buildValid = false;
                return result;
            }
            if (shadowWeight > result.shadowWeight) {
                result.shadowLightId = gpuLightStableId(light);
                result.shadowMetadataIndex = gpuLightShadowIndex(light);
                result.shadowResourceCoordinate = shadowResourceCoordinate;
                result.shadowVisibility = shadowVisibilityValue;
                result.shadowWeight = shadowWeight;
            }
        }
        result.diffuse += contribution.diffuse * shadowVisibilityValue;
        result.specular += contribution.specular * shadowVisibilityValue;
    }
    return result;
}

#endif
