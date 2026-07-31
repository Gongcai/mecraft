#version 450 core
#include "rhi_screen_coordinates.glsl"
#include "sky_ibl_common.glsl"

layout(location = 0) in vec2 vScreenUv;
layout(location = 0) out vec2 fragDfg;

const uint SKY_IBL_DFG_SAMPLE_COUNT = 256u;

vec2 integrateDfg(float nDotV, float roughness) {
    vec3 viewDirection = vec3(sqrt(max(1.0 - nDotV * nDotV, 0.0)),
                              0.0, nDotV);
    float alpha = max(roughness * roughness, 0.001);
    float scale = 0.0;
    float bias = 0.0;
    for (uint index = 0u; index < SKY_IBL_DFG_SAMPLE_COUNT; ++index) {
        vec2 samplePoint = skyIblHammersley(index, SKY_IBL_DFG_SAMPLE_COUNT);
        vec3 halfway = skyIblImportanceSampleGgx(
            samplePoint, vec3(0.0, 0.0, 1.0), alpha);
        vec3 lightDirection = normalize(2.0 * dot(viewDirection, halfway) *
                                        halfway - viewDirection);
        float nDotL = max(lightDirection.z, 0.0);
        float nDotH = max(halfway.z, 0.0);
        float vDotH = max(dot(viewDirection, halfway), 0.0);
        if (nDotL > 0.0) {
            float visibility = skyIblGeometrySmith(
                nDotV, nDotL, roughness) * vDotH /
                max(nDotH * nDotV, 1e-5);
            float fresnel = pow(1.0 - vDotH, 5.0);
            scale += (1.0 - fresnel) * visibility;
            bias += fresnel * visibility;
        }
    }
    return vec2(scale, bias) / float(SKY_IBL_DFG_SAMPLE_COUNT);
}

void main() {
    vec2 uv = clamp(rhiScreenUvToTextureUv(vScreenUv), vec2(0.0), vec2(1.0));
    fragDfg = integrateDfg(max(uv.x, 1e-4), max(uv.y, 1e-4));
}
