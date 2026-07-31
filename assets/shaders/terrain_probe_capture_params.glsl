#ifndef MECRAFT_TERRAIN_PROBE_CAPTURE_PARAMS_GLSL
#define MECRAFT_TERRAIN_PROBE_CAPTURE_PARAMS_GLSL

layout(std140, set = 1, binding = 13) uniform TerrainProbeCaptureMaterialParams {
    vec4 uProbeMaterialTiming;
    ivec4 uProbeMaterialFlags;
    ivec4 uProbeWeatherFlags;
};

layout(std140, set = 2, binding = 0) uniform TerrainProbeCaptureFrameParams {
    mat4 uProbeViewProjection;
    vec4 uProbePosition;
    vec4 uProbeSunDirection;
    vec4 uProbeSunColor;
    vec4 uProbeAmbientColor;
    uvec4 uProbeLightCount;
};

#endif
