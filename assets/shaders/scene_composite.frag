#version 450 core
#include "gbuffer_contract.glsl"
#include "render_contract.glsl"
#include "rhi_screen_coordinates.glsl"

layout(location = 0) in vec2 vScreenUv;
layout(location = 1) in vec2 vClipUv;
layout(location = 0) out vec4 FragColor;
layout(location = 1) out float FragReactiveMask;
layout(location = 2) out float FragTransparencyMask;

layout(binding = 0) uniform sampler2D uSceneLightingTex;
layout(binding = 1) uniform sampler2D uReflectionTex;
layout(binding = 2) uniform sampler2D uCloudTex;
layout(binding = 3) uniform sampler2D uDepthTex;
layout(binding = 4) uniform sampler2D uNormalAoTex;
layout(binding = 5) uniform sampler2D uMaterialTex;
layout(binding = 6) uniform sampler2D uMaterialAuxTex;
layout(binding = 7) uniform sampler2D uSkyCaptureTex;
layout(binding = 8) uniform sampler2D uAlbedoTex;
layout(binding = 9) uniform sampler2D uSsgiTex;

layout(std140, binding = 10) uniform SceneCompositeParams {
    mat4 pInvViewProj;
    vec4 pCameraPosSkyIntensity;
    vec4 pSunDirectionVisibility;
    vec4 pMoonDirectionVisibility;
    vec4 pAtmosphereComposite;
    vec4 pReflectionWater;
    vec4 pStatus;
    ivec4 pFlags0;
    ivec4 pFlags1;
};

#define uInvViewProj pInvViewProj
#define uCameraPos pCameraPosSkyIntensity.xyz
#define uSkyIntensity pCameraPosSkyIntensity.w
#define uSunDirection pSunDirectionVisibility.xyz
#define uSunVisibility pSunDirectionVisibility.w
#define uMoonDirection pMoonDirectionVisibility.xyz
#define uMoonVisibility pMoonDirectionVisibility.w
#define uMoonPhaseAngle pAtmosphereComposite.x
#define uSkyWetness pAtmosphereComposite.y
#define uWeatherWetness pAtmosphereComposite.z
#define uCloudCompositeStrength pAtmosphereComposite.w
#define uReflectionCompositeStrength pReflectionWater.x
#define uWaterAbsorption pReflectionWater.yzw
#define uBlindness pStatus.x
#define uDarknessFactor pStatus.y
#define uSsgiEnabled pFlags0.x
#define uReflectionDebugMode pFlags1.x
#define uIsEyeInWater pFlags1.y

#include "lighting_environment.glsl"
#include "fogs.glsl"
#include "procedural_celestials.glsl"

vec3 tonemapDebugSafe(vec3 color) {
    return max(color, vec3(0.0));
}

vec3 reconstructWorldPosition(vec2 clipUv, float depth) {
    vec4 clip = vec4(clipUv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
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
    vec2 textureUv = rhiScreenUvToTextureUv(vScreenUv);
    FragReactiveMask = 0.0;
    FragTransparencyMask = 0.0;
    SurfaceMaterialAux baseAux = unpackGBufferMaterialAux(
        texture(uMaterialAuxTex, textureUv));
    vec4 scene = texture(uSceneLightingTex, textureUv);
    float depth = texture(uDepthTex, textureUv).r;
    vec3 color = scene.rgb;
    LightingEnvironment env = getLightingEnvironment(uSkyCaptureTex);

    // DerivativeMain deferred5.fsh:168-176: clouds composited ONLY on sky pixels.
    // Geometry pixels never get cloud blending (clouds are always behind geometry).
    vec4 cloud = texture(uCloudTex, textureUv);
    if (depth >= 0.9999) {
        vec3 skyPos = reconstructWorldPosition(vClipUv, 1.0);
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
    vec4 reflection = texture(uReflectionTex, textureUv);

    // Reflection debug: bypass composite, output raw reflection data.
    if (uReflectionDebugMode > 0) {
        if (uReflectionDebugMode == 6) {
            // Composite delta: actual contribution of reflection to final scene.
            float compositeStr = clamp(uReflectionCompositeStrength, 0.0, 1.0);
            TranslucentMask transMask = decodeTranslucentMask(baseAux.materialKind);
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

    SurfaceMaterial material = unpackGBufferMaterial(texture(uMaterialTex, textureUv));
    SurfaceMaterialAux aux = baseAux;
    TranslucentMask transMask = decodeTranslucentMask(aux.materialKind);
    if (uSsgiEnabled != 0 && depth < 0.9999 && !transMask.isTranslucent) {
        vec4 ssgi = texture(uSsgiTex, textureUv);
        float confidence = smoothstep(0.06, 0.45, ssgi.a);
        float lowLightWeight = 1.0 - smoothstep(0.08, 0.85, luminance(color));
        float surfaceWeight = mix(0.28, 1.0, lowLightWeight);
        color += max(ssgi.rgb, vec3(0.0)) * confidence * surfaceWeight;
    }

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
        vec3 transAlbedo = texture(uAlbedoTex, textureUv).rgb;
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
        vec3 fogWorldPos = reconstructWorldPosition(
            vClipUv, depth < 0.9999 ? depth : 0.9999);
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
