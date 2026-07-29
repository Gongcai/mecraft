#ifndef MECRAFT_LOCAL_SHADOW_CONTRACT_GLSL
#define MECRAFT_LOCAL_SHADOW_CONTRACT_GLSL

const uint LOCAL_SHADOW_CONTRACT_VERSION = 1u;
const uint LOCAL_SHADOW_TYPE_SPOT = 0u;
const uint LOCAL_SHADOW_TYPE_POINT = 1u;
const uint LOCAL_SHADOW_SPOT_METADATA_COUNT = 64u;
const uint LOCAL_SHADOW_POINT_METADATA_COUNT = 64u;
const uint LOCAL_SHADOW_POINT_METADATA_BASE = 64u;
const uint LOCAL_SHADOW_METADATA_COUNT = 128u;

struct LocalShadowMetadata {
    mat4 cameraRelativeViewProjection[6];
    vec4 atlasScaleBias;
    vec4 nearFarDepthBiasNormalOffset;
    uvec4 classification;
};

#endif // MECRAFT_LOCAL_SHADOW_CONTRACT_GLSL
