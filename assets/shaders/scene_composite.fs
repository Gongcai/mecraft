#version 450 core
#include "gbuffer_contract.glsl"
#include "render_contract.glsl"

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
uniform vec3 uCameraPos;
uniform vec3 uSunDirection;
uniform vec3 uMoonDirection;
uniform float uSkyIntensity;
uniform float uMoonVisibility;
uniform float uCloudCompositeStrength;
uniform float uReflectionCompositeStrength;

#include "atmosphere_lut.glsl"

vec3 tonemapDebugSafe(vec3 color) {
    return max(color, vec3(0.0));
}

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    return world.xyz / max(world.w, 0.00001);
}

void main() {
    vec4 scene = texture(uSceneLightingTex, vTexCoord);
    float depth = texture(uDepthTex, vTexCoord).r;
    vec3 color = scene.rgb;

    // DerivativeMain deferred5.fsh:168-176: clouds composited ONLY on sky pixels.
    // Geometry pixels never get cloud blending (clouds are always behind geometry).
    vec4 cloud = texture(uCloudTex, vTexCoord);
    if (depth >= 0.9999) {
        vec3 skyPos = reconstructWorldPosition(vTexCoord, 1.0);
        vec3 skyDir = normalize(skyPos - uCameraPos);
        vec3 sky = sampleSkyRadianceCloudy(uSkyCaptureTex, skyDir);
        // Premultiplied alpha: sceneData = sceneData * cloudData.a + cloudData.rgb
        color = sky * cloud.a + cloud.rgb * clamp(uCloudCompositeStrength, 0.0, 1.0);
    }

    // Premultiplied reflection data (DerivativeMain composite1 convention):
    // reflection.rgb = reflection * specular weight, reflection.a = 1 - specular
    // DerivativeMain: sceneData = sceneData * reflectionData.a + reflectionData.rgb
    vec4 reflection = texture(uReflectionTex, vTexCoord);
    SurfaceMaterial material = unpackGBufferMaterial(texture(uMaterialTex, vTexCoord));
    SurfaceMaterialAux aux = unpackGBufferMaterialAux(texture(uMaterialAuxTex, vTexCoord));
    TranslucentMask transMask = decodeTranslucentMask(aux.materialKind);
    float compositeStrength = clamp(uReflectionCompositeStrength, 0.0, 1.0);
    // Blend between full scene pass-through (1.0) and premultiplied reflection
    float sceneWeight = mix(1.0, reflection.a, compositeStrength);
    vec3 reflContrib = reflection.rgb * compositeStrength;
    color = color * sceneWeight + reflContrib;

    // Translucent tinting (DerivativeMain composite1 equivalent).
    // For stained glass: absorb light through colored medium.
    // For ice: tint by squared albedo (approximates volumetric absorption).
    if (scene.a > 0.5 && depth < 0.9999) {
        vec3 transAlbedo = texture(uAlbedoTex, vTexCoord).rgb;
        if (transMask.isStainedGlass) {
            // Beer-Lambert absorption: darker albedo = more absorption.
            vec3 absorbColor = mix(vec3(1.0), transAlbedo, 0.72);
            color *= absorbColor;
        } else if (transMask.isIce) {
            color *= transAlbedo * transAlbedo;
        }
    }

    FragColor = vec4(tonemapDebugSafe(color), scene.a);
}
