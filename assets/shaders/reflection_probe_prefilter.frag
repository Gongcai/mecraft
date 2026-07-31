#version 450 core
#include "rhi_screen_coordinates.glsl"
#include "sky_ibl_common.glsl"

layout(location = 0) in vec2 vScreenUv;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform samplerCube uProbeRadiance;
layout(push_constant) uniform ReflectionProbePrefilterPushConstants {
    uint uFace;
    float uRoughness;
    uint uSourceResolution;
    uint uSampleCount;
};

void main() {
    vec2 uv = rhiScreenUvToTextureUv(vScreenUv);
    vec3 normal = skyIblFaceDirection(uFace, uv);
    vec3 viewDirection = normal;
    float alpha = max(uRoughness * uRoughness, 0.001);
    vec3 radiance = vec3(0.0);
    float weight = 0.0;
    for (uint index = 0u; index < uSampleCount; ++index) {
        vec2 samplePoint = skyIblHammersley(index, uSampleCount);
        vec3 halfway = skyIblImportanceSampleGgx(
            samplePoint, normal, alpha);
        vec3 lightDirection = normalize(
            2.0 * dot(viewDirection, halfway) * halfway - viewDirection);
        float nDotL = max(dot(normal, lightDirection), 0.0);
        if (nDotL > 0.0) {
            radiance += textureLod(
                uProbeRadiance, lightDirection, 0.0).rgb * nDotL;
            weight += nDotL;
        }
    }
    fragColor = vec4(
        max(radiance / max(weight, 1e-5), vec3(0.0)), 1.0);
}
