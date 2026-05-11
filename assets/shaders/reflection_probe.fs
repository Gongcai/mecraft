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
uniform float uWeatherWetness;
uniform float uTime;

const float kTwoPi = 6.28318530718;
const int kSsrSteps = 28;

vec2 directionToSkyCaptureUv(vec3 dir) {
    dir = normalize(dir);
    float phi = atan(dir.x, -dir.z);
    float u = phi / kTwoPi + 0.5;
    float v = dir.y * 0.5 + 0.5;
    return vec2(fract(u), clamp(v, 0.0, 1.0));
}

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
        vec3 sky = texture(uSkyCaptureTex, vTexCoord).rgb;
        FragColor = vec4(sky, 0.0);
        return;
    }

    vec3 normal = normalize(texture(uNormalAoTex, vTexCoord).rgb * 2.0 - 1.0);
    vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
    vec3 viewDir = normalize(worldPos - uCameraPos);
    vec3 reflectedDir = reflect(viewDir, normal);
    vec3 skyReflection = texture(uSkyCaptureTex, directionToSkyCaptureUv(reflectedDir)).rgb;

    vec3 sceneFallback = texture(uSceneLightingTex, vTexCoord).rgb;
    float smoothness = 1.0 - clamp(material.roughness, 0.0, 1.0);
    float wetBoost = clamp(uWeatherWetness, 0.0, 1.0) * aux.wetnessMask * clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    float reflectance = clamp(material.f0 * 2.4 + smoothness * 0.22 + wetBoost * (0.14 + aux.porosity * 0.12), 0.0, 1.0);
    vec3 ssrColor = vec3(0.0);
    float ssrHit = 0.0;
    traceScreenSpaceReflection(worldPos + normal * 0.025, reflectedDir, material.roughness, ssrColor, ssrHit);

    vec3 fallback = mix(sceneFallback * 0.08, skyReflection, 0.70 + smoothness * 0.22);
    vec3 color = mix(fallback, ssrColor, ssrHit);

    FragColor = vec4(color, reflectance * mix(0.45, 1.0, ssrHit));
}
