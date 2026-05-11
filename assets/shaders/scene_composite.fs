#version 450 core
#include "gbuffer_contract.glsl"

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneLightingTex;
uniform sampler2D uReflectionTex;
uniform sampler2D uCloudTex;
uniform sampler2D uDepthTex;
uniform sampler2D uNormalAoTex;
uniform sampler2D uMaterialTex;
uniform sampler2D uMaterialAuxTex;
uniform sampler2D uVelocityTex;
uniform sampler2D uHistorySceneTex;
uniform sampler2D uHistoryDepthTex;
uniform sampler2D uHistoryReflectionTex;
uniform sampler2D uHistoryCloudTex;
uniform sampler2D uSkyCaptureTex;

// Albedo texture for refraction sampling (DerivativeMain composite1 equivalent)
uniform sampler2D uAlbedoTex;

// Atmosphere precomputed scattering LUT (256x128x33 RGBA32F)
uniform sampler3D uAtmosphereLut;

// Scene composite resource contract:
// - SceneLighting is opaque HDR lighting before screen-space scene effects.
// - Reflection/Cloud are half-res effect targets sampled in full-res scene UV space.
// - Velocity and history inputs are intentionally bound now so future temporal resolve,
//   SSR rejection, and cloud reprojection can evolve inside this shader without C++ churn.
uniform mat4 uViewProj;
uniform mat4 uInvViewProj;
uniform mat4 uPreviousViewProj;
uniform mat4 uPreviousInvViewProj;
uniform vec2 uJitter;
uniform vec2 uPreviousJitter;
uniform int uFrameIndex;
uniform float uTime;
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
