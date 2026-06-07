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
uniform float uSunVisibility;
uniform float uSkyIntensity;
uniform float uMoonVisibility;
uniform float uMoonPhaseAngle;
uniform float uSkyWetness;
uniform float uCloudCompositeStrength;
uniform float uReflectionCompositeStrength;
uniform int uReflectionDebugMode; // >0: bypass composite, output raw reflection. 6=composite delta, 26=reflection/scene ratio
uniform int uIsEyeInWater;
uniform vec3 uWaterAbsorption;

#include "lighting_environment.glsl"
#include "atmosphere_lut.glsl"
#include "fogs.glsl"
#include "procedural_celestials.glsl"

// DerivativeMain Fogs.glsl: uniform float blindness / darknessFactor
// Mecraft: default 0 until status effect system is implemented.
uniform float uBlindness;
uniform float uDarknessFactor;

vec3 tonemapDebugSafe(vec3 color) {
    return max(color, vec3(0.0));
}

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    return world.xyz / max(world.w, 0.00001);
}

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

void applyUnderwaterFog(inout vec3 color, float fogDistance, LightingEnvironment env) {
    // DerivativeMain/lib/Water/WaterFog.glsl UnderwaterFog(), adapted for
    // Mecraft's fullscreen scene composite where the camera is already underwater.
    const float waterSkylight = 0.75;
    float fogDensity = (0.1 + 0.05 * uSkyWetness * waterSkylight) * max(fogDistance, 0.0);

    vec3 skyFogBase = mix(env.skyHorizonAvg, env.skyZenith, 0.3);
    vec3 fogColor = mix(skyFogBase * 0.4,
                        vec3(luminance(skyFogBase) * 0.1),
                        0.8 * uSkyWetness * waterSkylight) * (1.0 / 3.14159265359);

    vec3 absorption = uWaterAbsorption * 8.0 + 0.03;
    vec3 transmittance = exp(-absorption * max(fogDensity, 2.0) + 0.4);

    color *= transmittance;
    color += fogColor * clamp(waterSkylight + 0.2, 0.0, 1.0) * (1.0 - transmittance);
}

void main() {
    vec4 scene = texture(uSceneLightingTex, vTexCoord);
    float depth = texture(uDepthTex, vTexCoord).r;
    vec3 color = scene.rgb;
    LightingEnvironment env = getLightingEnvironment(uSkyCaptureTex);

    // DerivativeMain deferred5.fsh:168-176: clouds composited ONLY on sky pixels.
    // Geometry pixels never get cloud blending (clouds are always behind geometry).
    vec4 cloud = texture(uCloudTex, vTexCoord);
    if (depth >= 0.9999) {
        vec3 skyPos = reconstructWorldPosition(vTexCoord, 1.0);
        vec3 skyDir = normalize(skyPos - uCameraPos);
        // DerivativeMain composites live cloud data over raw colortex5 sky.
        // Using the cloudy atlas half here double-applies sky/cloud attenuation.
        vec3 sky = sampleSkyRadiance(uSkyCaptureTex, skyDir);
        float solarLobeMask = proceduralSunAngularMask(skyDir, 0.018, 0.180);
        float solarCoreMask = proceduralSunAngularMask(skyDir, 0.000, 0.070);
        sky *= (1.0 - solarLobeMask * 0.94) * (1.0 - solarCoreMask * 0.92);
        sky *= min(1.0, mix(18.0, 1.5, solarCoreMask) / max(luminance(sky), 1e-5));
        sky += renderProceduralMoonDisk(skyDir);
        sky += renderProceduralSunDisk(skyDir);
        float cloudSolarMask = proceduralSunAngularMask(skyDir, 0.018, 0.155);
        float cloudSolarCoreMask = proceduralSunAngularMask(skyDir, 0.000, 0.085);
        cloud.rgb *= (1.0 - cloudSolarMask * 0.985) * (1.0 - cloudSolarCoreMask * 0.98);
        cloud.rgb *= min(1.0, mix(10.0, 0.25, cloudSolarCoreMask) / max(luminance(cloud.rgb), 1e-5));
        cloud.a = mix(cloud.a, 1.0, cloudSolarMask * 0.96);
        // Premultiplied alpha: sceneData = sceneData * cloudData.a + cloudData.rgb
        // Strength blends between unmodified sky and full premultiplied result,
        // so reducing strength doesn't break sky transmittance energy.
        vec3 premul = sky * cloud.a + cloud.rgb;
        color = mix(sky, premul, clamp(uCloudCompositeStrength, 0.0, 1.0));
    }

    // DerivativeMain composite1 convention:
    // - translucent surfaces use reflection.a as scene pass-through
    // - opaque reflective surfaces add reflection.rgb directly
    vec4 reflection = texture(uReflectionTex, vTexCoord);

    // Reflection debug: bypass composite, output raw reflection data.
    if (uReflectionDebugMode > 0) {
        if (uReflectionDebugMode == 6) {
            // Composite delta: actual contribution of reflection to final scene.
            float compositeStr = clamp(uReflectionCompositeStrength, 0.0, 1.0);
            SurfaceMaterialAux aux = unpackGBufferMaterialAux(texture(uMaterialAuxTex, vTexCoord));
            TranslucentMask transMask = decodeTranslucentMask(aux.materialKind);
            vec3 delta = transMask.isTranslucent
                ? compositeStr * (reflection.rgb - color * (1.0 - reflection.a))
                : compositeStr * reflection.rgb;
            FragColor = vec4(abs(delta), 1.0);
        } else if (uReflectionDebugMode == 26) {
            float sceneLum = max(luminance(color), 1e-5);
            float reflLum = luminance(reflection.rgb) * clamp(uReflectionCompositeStrength, 0.0, 1.0);
            FragColor = vec4(vec3(clamp(reflLum / sceneLum, 0.0, 1.0)), 1.0);
        } else if (uReflectionDebugMode == 27) {
            FragColor = vec4(vec3(luminance(color)), 1.0);
        } else if (uReflectionDebugMode == 28) {
            float reflLum = luminance(reflection.rgb) * clamp(uReflectionCompositeStrength, 0.0, 1.0);
            FragColor = vec4(vec3(reflLum * 64.0), 1.0);
        } else {
            FragColor = vec4(reflection.rgb, 1.0);
        }
        return;
    }

    SurfaceMaterial material = unpackGBufferMaterial(texture(uMaterialTex, vTexCoord));
    SurfaceMaterialAux aux = unpackGBufferMaterialAux(texture(uMaterialAuxTex, vTexCoord));
    TranslucentMask transMask = decodeTranslucentMask(aux.materialKind);
    float compositeStrength = clamp(uReflectionCompositeStrength, 0.0, 1.0);
    if (transMask.isTranslucent) {
        // DerivativeMain composite1.fsh: sceneData = sceneData * reflectionData.a + reflectionData.rgb
        float sceneWeight = mix(1.0, reflection.a, compositeStrength);
        color = color * sceneWeight + reflection.rgb * compositeStrength;
    } else {
        // DerivativeMain opaque path: sceneData += reflectionData.rgb * mix(1, albedo, metal).
        // Mecraft has already applied the metal tint in reflection_probe.fs.
        color += reflection.rgb * compositeStrength;
    }

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

    // DerivativeMain composite1.fsh: underwater fog + CommonFog (final fog pass).
    // Water uses detailed Beer-Lambert + sky scatter; lava/powder_snow use CommonFog only.
    // Blindness/darkness CommonFog applies regardless of eye medium.
    {
        vec3 fogWorldPos = reconstructWorldPosition(vTexCoord, depth < 0.9999 ? depth : 0.9999);
        float fogDistance = length(fogWorldPos - uCameraPos);
        if (uIsEyeInWater == 1) {
            applyUnderwaterFog(color, fogDistance, env);
        }
        CommonFog(color, fogDistance, uIsEyeInWater,
                  uBlindness, uDarknessFactor, uWeatherWetness,
                  env.skyIlluminance, env.directIlluminance);
    }

    FragColor = vec4(tonemapDebugSafe(color), scene.a);
}
