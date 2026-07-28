#ifndef MECRAFT_CLUSTERED_LIGHTING_GLSL
#define MECRAFT_CLUSTERED_LIGHTING_GLSL

#include "cluster_build_contract.glsl"
#include "clustered_light_evaluation.glsl"

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
    for (uint listIndex = 0u; listIndex < record.count; ++listIndex) {
        uint lightIndex = uClusterLightIndices[record.offset + listIndex];
        if (lightIndex >= lightCount) {
            buildValid = false;
            return result;
        }
        ClusteredSurfaceLighting contribution = evaluateGpuLight(
            uClusterLights[lightIndex], cameraRelativeSurface,
            normal, viewDirection, specularF0, specularF90,
            perceptualRoughness);
        result.diffuse += contribution.diffuse;
        result.specular += contribution.specular;
    }
    return result;
}

#endif
