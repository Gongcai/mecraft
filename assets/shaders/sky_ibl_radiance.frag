#version 450 core
#include "lighting_environment.glsl"
#include "rhi_screen_coordinates.glsl"
#include "sky_ibl_common.glsl"

layout(location = 0) in vec2 vScreenUv;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler2D uSkyCapture;
layout(push_constant) uniform SkyIblPushConstants {
    uint uFace;
    float uRoughness;
    uint uSourceResolution;
    uint uSampleCount;
};

void main() {
    vec2 uv = rhiScreenUvToTextureUv(vScreenUv);
    vec3 direction = skyIblFaceDirection(uFace, uv);
    fragColor = vec4(max(sampleEnvironmentCloudySky(uSkyCapture, direction),
                         vec3(0.0)), 1.0);
}
