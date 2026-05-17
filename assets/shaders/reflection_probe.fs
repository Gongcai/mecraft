#version 450 core
#include "gbuffer_contract.glsl"

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneLightingTex;
uniform sampler2D uDepthTex;
uniform sampler2D uNormalAoTex;
uniform sampler2D uMaterialTex;
uniform sampler2D uMaterialAuxTex;
uniform sampler2D uSkyCaptureTex;
uniform sampler3D uAtmosphereLut;
uniform mat4 uViewProj;
uniform mat4 uInvViewProj;
uniform vec3 uCameraPos;
uniform vec3 uSunDirection;
uniform vec3 uMoonDirection;
uniform float uSkyIntensity;
uniform float uMoonVisibility;
uniform float uWeatherWetness;
uniform float uSurfaceWetness;
uniform float uSkyWetness;
uniform float uFogWetness;
uniform float uCloudWetness;
uniform float uTime;
uniform sampler2D uVoxelLightTex; // GBuffer attachment 2: sky light.r, block light.g
uniform int uReflectionDebugMode; // 0=off, 1=pixelWetness, 2=reflectance, 3=ssrHit, 4=roughness, 5=specularWeight

#include "atmosphere_lut.glsl"

// DerivativeMain Common.inc helpers (mirrored from derivative_shadow.glsl)
float saturate(float x) { return clamp(x, 0.0, 1.0); }
float remap(float e0, float e1, float x) { return saturate((x - e0) / (e1 - e0)); }

const int kSsrSteps = 28;

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    return world.xyz / max(world.w, 0.00001);
}

vec2 projectWorldUv(vec3 worldPos) {
    vec4 clip = uViewProj * vec4(worldPos, 1.0);
    return clip.xy / max(clip.w, 0.00001) * 0.5 + 0.5;
}

float linearDepth01(float depth) {
    float ndc = depth * 2.0 - 1.0;
    float nearPlane = 0.05;
    float farPlane = 512.0;
    float linearDepth = (2.0 * nearPlane * farPlane) / max(farPlane + nearPlane - ndc * (farPlane - nearPlane), 0.0001);
    return clamp(linearDepth / farPlane, 0.0, 1.0);
}

bool traceScreenSpaceReflection(vec3 worldPos,
                                vec3 reflectedDir,
                                float roughness,
                                out vec3 hitColor,
                                out float hitConfidence) {
    hitColor = vec3(0.0);
    hitConfidence = 0.0;

    float maxDistance = mix(44.0, 8.0, clamp(roughness, 0.0, 1.0));
    float stepLength = maxDistance / float(kSsrSteps);
    float thickness = mix(0.006, 0.018, clamp(roughness, 0.0, 1.0));

    for (int i = 1; i <= kSsrSteps; ++i) {
        float t = float(i) * stepLength;
        vec3 sampleWorld = worldPos + reflectedDir * t;
        vec2 uv = projectWorldUv(sampleWorld);
        if (uv.x <= 0.001 || uv.x >= 0.999 || uv.y <= 0.001 || uv.y >= 0.999) {
            break;
        }

        float sceneDepth = texture(uDepthTex, uv).r;
        if (sceneDepth >= 0.9999) {
            continue;
        }

        vec3 sceneWorld = reconstructWorldPosition(uv, sceneDepth);
        vec4 rayClip = uViewProj * vec4(sampleWorld, 1.0);
        float rayDepth = linearDepth01(rayClip.z / max(rayClip.w, 0.00001) * 0.5 + 0.5);
        float hitDepth = linearDepth01(sceneDepth);
        float depthDelta = rayDepth - hitDepth;
        if (depthDelta >= 0.0 && depthDelta < thickness + t * 0.00018) {
            float edgeFade = smoothstep(0.02, 0.14, min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y)));
            float distanceFade = 1.0 - smoothstep(maxDistance * 0.35, maxDistance, t);
            float normalFacing = smoothstep(0.0, 0.35, dot(normalize(sceneWorld - worldPos), reflectedDir));
            hitConfidence = clamp(edgeFade * distanceFade * normalFacing * (1.0 - roughness * 0.65), 0.0, 1.0);
            hitColor = texture(uSceneLightingTex, uv).rgb;
            return hitConfidence > 0.001;
        }
    }

    return false;
}

void main() {
    float depth = texture(uDepthTex, vTexCoord).r;
    vec4 packedMaterial = texture(uMaterialTex, vTexCoord);
    SurfaceMaterial material = unpackGBufferMaterial(packedMaterial);
    SurfaceMaterialAux aux = unpackGBufferMaterialAux(texture(uMaterialAuxTex, vTexCoord));

    if (depth >= 0.9999) {
        vec3 skyPos = reconstructWorldPosition(vTexCoord, 1.0);
        vec3 skyDir = normalize(skyPos - uCameraPos);
        vec3 sky = sampleSkyRadianceCloudy(uSkyCaptureTex, skyDir);
        FragColor = vec4(sky, 0.0);
        return;
    }

    vec3 normal = normalize(texture(uNormalAoTex, vTexCoord).rgb * 2.0 - 1.0);
    vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
    vec3 viewDir = normalize(worldPos - uCameraPos);
    TranslucentMask transMask = decodeTranslucentMask(aux.materialKind);

    // DerivativeMain wet surface — same formula as deferred_lighting.fs lines 458-487.
    // Modifies roughness/F0/normal before SSR trace so reflection sees wet state.
    float roughness = material.roughness;
    float f0Scalar = material.f0;
    float pixelWetness = 0.0;
    {
        float weatherWetness = clamp(uSurfaceWetness, 0.0, 1.0);
        float skyLightRaw01 = clamp(texture(uVoxelLightTex, vTexCoord).r, 0.0, 1.0);
        float outdoorWetMask = saturate(skyLightRaw01 * 10.0 - 9.0);
        float upwardFacing = remap(0.5, 0.9, clamp(normal.y, 0.0, 1.0));
        pixelWetness = weatherWetness * outdoorWetMask * upwardFacing;
        pixelWetness = max(pixelWetness, aux.wetnessMask * weatherWetness * skyLightRaw01);

        if (!transMask.isTranslucent && pixelWetness > 1e-4) {
            normal = mix(normal, vec3(0.0, 1.0, 0.0), pixelWetness * 0.65);
            roughness = mix(roughness, max(0.08, roughness * 0.36), pixelWetness);
            f0Scalar = max(f0Scalar, 0.04 * pixelWetness);
        }
    }

    // Recompute reflected direction with wet-flattened normal.
    vec3 reflectedDir = reflect(viewDir, normal);
    vec3 skyReflection = sampleSkyRadianceCloudy(uSkyCaptureTex, reflectedDir);

    // Early debug: pixelWetness and roughness are available for ALL pixels.
    if (uReflectionDebugMode == 1) {
        FragColor = vec4(vec3(pixelWetness), 0.0);
        return;
    }
    if (uReflectionDebugMode == 4) {
        FragColor = vec4(vec3(roughness), 0.0);
        return;
    }

    vec3 sceneFallback = texture(uSceneLightingTex, vTexCoord).rgb;
    float smoothness = 1.0 - clamp(roughness, 0.0, 1.0);
    bool hasDerivativeReflection = transMask.isTranslucent ||
                                   aux.metalness > 0.5 ||
                                   smoothness > 0.375;
    if (!hasDerivativeReflection) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float fresnel = pow(1.0 - clamp(dot(-viewDir, normal), 0.0, 1.0), 5.0);
    // Wet boost: DerivativeMain rain wet surfaces get stronger Fresnel and base reflectance.
    float wetReflectBoost = pixelWetness * mix(0.04, 0.14, smoothness);
    float reflectance = clamp(f0Scalar * 2.0 + smoothness * 0.18 + fresnel * 0.18 +
                              aux.porosity * 0.16 + wetReflectBoost, 0.0, 1.0);
    vec3 ssrColor = vec3(0.0);
    float ssrHit = 0.0;
    traceScreenSpaceReflection(worldPos + normal * 0.025, reflectedDir, roughness, ssrColor, ssrHit);

    // Late debug: reflectance and ssrHit only valid after SSR trace.
    if (uReflectionDebugMode == 2) {
        FragColor = vec4(vec3(reflectance), 0.0);
        return;
    }
    if (uReflectionDebugMode == 3) {
        FragColor = vec4(vec3(ssrHit), 0.0);
        return;
    }
    if (uReflectionDebugMode == 5) {
        float specularFloor = mix(0.48, 0.72, pixelWetness);
        float specular = reflectance * mix(specularFloor, 1.0, ssrHit);
        FragColor = vec4(vec3(specular), 0.0);
        return;
    }

    // Wet terrain gets stronger sky fallback: more sky-dominant, less scene-dark.
    float fallbackSkyWeight = mix(0.74, 0.95, pixelWetness);
    vec3 roughSky = mix(skyReflection, skyReflection * vec3(0.82, 0.91, 1.04), roughness * 0.45);
    vec3 fallback = mix(sceneFallback * 0.06, roughSky, fallbackSkyWeight + smoothness * 0.20);
    vec3 color = mix(fallback, ssrColor, ssrHit);

    // Premultiplied output (DerivativeMain convention):
    // rgb = reflection * specular, a = 1 - specular (scene pass-through)
    // Wet surfaces get a higher specular floor so reflection contributes more to final scene.
    float specularFloor = mix(0.48, 0.72, pixelWetness);
    float specular = reflectance * mix(specularFloor, 1.0, ssrHit);
    FragColor = vec4(max(color, vec3(0.0)) * specular, 1.0 - specular);
}
