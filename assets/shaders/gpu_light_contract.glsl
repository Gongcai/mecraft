#ifndef MECRAFT_GPU_LIGHT_CONTRACT_GLSL
#define MECRAFT_GPU_LIGHT_CONTRACT_GLSL

const uint GPU_LIGHT_CONTRACT_VERSION = 3u;
const uint GPU_LIGHT_INVALID_RESOURCE_INDEX = 0xffffffffu;

const uint GPU_LIGHT_TYPE_DIRECTIONAL = 0u;
const uint GPU_LIGHT_TYPE_POINT = 1u;
const uint GPU_LIGHT_TYPE_SPOT = 2u;
const uint GPU_LIGHT_TYPE_RECT = 3u;

const uint GPU_LIGHT_SHADOW_NONE = 0u;
const uint GPU_LIGHT_SHADOW_RASTER_DYNAMIC = 1u;
const uint GPU_LIGHT_SHADOW_RASTER_CACHED = 2u;
const uint GPU_LIGHT_SHADOW_RAY_QUERY = 3u;

const uint GPU_LIGHT_CONTRIBUTION_DIFFUSE = 1u << 0u;
const uint GPU_LIGHT_CONTRIBUTION_SPECULAR = 1u << 1u;
const uint GPU_LIGHT_CONTRIBUTION_VOLUMETRIC = 1u << 2u;

struct GpuLight {
    vec4 positionAndRange;
    vec4 direction;
    vec4 colorAndIntensity;
    vec4 spotCosinesAndRectSize;
    uvec4 classificationAndIdentity;
    uvec4 resourcesAndFlags;
};

uint gpuLightType(GpuLight light) {
    return light.classificationAndIdentity.x;
}

uint gpuLightStableId(GpuLight light) {
    return light.classificationAndIdentity.y;
}

uint gpuLightShadowPolicy(GpuLight light) {
    return light.classificationAndIdentity.z;
}

uint gpuLightShadowIndex(GpuLight light) {
    return light.classificationAndIdentity.w;
}

uint gpuLightContributionFlags(GpuLight light) {
    return light.resourcesAndFlags.z;
}

bool gpuLightContributes(GpuLight light, uint contributionFlag) {
    return (gpuLightContributionFlags(light) & contributionFlag) != 0u;
}

float gpuLightInverseRangeSquared(GpuLight light) {
    return light.direction.w;
}

#endif
