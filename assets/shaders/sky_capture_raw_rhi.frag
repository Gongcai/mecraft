#version 450 core
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler3D uAtmosphereLut;
layout(push_constant) uniform RhiPushConstants {
    vec4 uSunDirectionAltitude;
    vec4 uWeatherMoonFlux;
};
#define MECRAFT_ATMOSPHERE_EXTERNAL_UNIFORMS 1
#define uMoonPhaseFlux uWeatherMoonFlux.y
#include "atmosphere_lut.glsl"
const float kPi = 3.14159265359;
const float kTwoPi = 6.28318530718;
void main() {
    vec2 uv = clamp(vTexCoord, vec2(0.0), vec2(1.0));
#ifdef RHI_VULKAN
    uv.y = 1.0 - uv.y;
#endif
    float u = fract((uv.x - 2.0 / float(skyCaptureRes.x)) /
                    (1.0 - 4.0 / float(skyCaptureRes.x)));
    float phi = u * kTwoPi;
    float theta = uv.y * kPi;
    float sinTheta = sin(theta);
    vec3 direction = normalize(vec3(sin(phi) * sinTheta, cos(theta), cos(phi) * sinTheta));
    vec3 transmittance;
    vec3 sky = atmGetSkyRadiance(max(uSunDirectionAltitude.w, 0.0), direction,
                                 normalize(uSunDirectionAltitude.xyz), transmittance);
    float weatherOcclusion = clamp(uWeatherMoonFlux.x, 0.0, 1.0);
    if (weatherOcclusion > 0.001) {
        float luminance = dot(sky, vec3(0.2126, 0.7152, 0.0722));
        vec3 wetnessGrey = luminance * vec3(1.026186824, 0.9881671071, 1.015787125);
        sky = mix(sky, wetnessGrey, weatherOcclusion * 0.7);
        sky *= 1.0 - weatherOcclusion * 0.6;
    }
    fragColor = vec4(max(sky, vec3(0.0)), 1.0);
}
