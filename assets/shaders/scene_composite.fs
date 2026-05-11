#version 450 core
#include "gbuffer_contract.glsl"

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneLightingTex;
uniform sampler2D uReflectionTex;
uniform sampler2D uCloudTex;
uniform sampler2D uDepthTex;
uniform sampler2D uMaterialAuxTex;
uniform sampler2D uSkyCaptureTex;
uniform float uWeatherWetness;
uniform float uCloudCompositeStrength;
uniform float uReflectionCompositeStrength;

vec3 tonemapDebugSafe(vec3 color) {
    return max(color, vec3(0.0));
}

void main() {
    vec4 scene = texture(uSceneLightingTex, vTexCoord);
    float depth = texture(uDepthTex, vTexCoord).r;
    vec3 color = scene.rgb;

    vec4 cloud = texture(uCloudTex, vTexCoord);
    if (depth >= 0.9999) {
        vec3 sky = texture(uSkyCaptureTex, vTexCoord).rgb;
        color = mix(color, mix(sky, cloud.rgb, clamp(cloud.a, 0.0, 1.0)),
                    clamp(uCloudCompositeStrength, 0.0, 1.0));
    }

    vec4 reflection = texture(uReflectionTex, vTexCoord);
    SurfaceMaterialAux aux = unpackGBufferMaterialAux(texture(uMaterialAuxTex, vTexCoord));
    float wetSurface = clamp(uWeatherWetness * aux.wetnessMask * (0.35 + aux.porosity * 0.65), 0.0, 1.0);
    float reflectionWeight = reflection.a * wetSurface * clamp(uReflectionCompositeStrength, 0.0, 1.0);
    color = mix(color, reflection.rgb, reflectionWeight);

    FragColor = vec4(tonemapDebugSafe(color), scene.a);
}
