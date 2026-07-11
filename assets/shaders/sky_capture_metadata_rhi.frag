#version 450 core
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler3D uAtmosphereLut;
layout(std140, binding = 15) uniform RhiPushConstants {
    vec4 uSunDirectionAltitude;
    vec4 uCloudDynamicWeatherMoonFlux;
};
#define MECRAFT_ATMOSPHERE_EXTERNAL_UNIFORMS 1
#define uMoonPhaseFlux uCloudDynamicWeatherMoonFlux.w
#include "atmosphere_lut.glsl"
void main() {
    int row = int(gl_FragCoord.y);
    vec3 camera = vec3(0.0, atmPlanetRadius + max(uSunDirectionAltitude.w, 0.0), 0.0);
    vec3 sunIrradiance;
    vec3 moonIrradiance;
    vec3 skyIrradiance = atmGetSunAndSkyIrradiance(camera, normalize(uSunDirectionAltitude.xyz),
                                                    sunIrradiance, moonIrradiance);
    vec3 value;
    if (row == 0) value = sunIrradiance + moonIrradiance;
    else if (row == 1) value = skyIrradiance;
    else if (row == 2) value = sunIrradiance;
    else if (row == 3) value = moonIrradiance;
    else if (row == 5) value = uCloudDynamicWeatherMoonFlux.xyz;
    else discard;
    fragColor = vec4(value, 1.0);
}
