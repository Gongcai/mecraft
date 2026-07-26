#version 450 core

#include "gbuffer_contract.glsl"
#include "rhi_screen_coordinates.glsl"

layout(location = 0) in vec2 vScreenUv;
layout(location = 1) in vec2 vClipUv;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uSceneLightingTex;
layout(binding = 1) uniform sampler2D uAlbedoTex;
layout(binding = 2) uniform sampler2D uNormalAoTex;
layout(binding = 3) uniform sampler2D uMaterialAuxTex;
layout(binding = 4) uniform sampler2D uDepthTex;
layout(binding = 5) uniform sampler2D uNoiseTex;

layout(std140, binding = 6) uniform SsgiBaseParams {
    mat4 pViewProj;
    mat4 pInvViewProj;
    vec4 pCameraPosRadius;
    vec4 pHalfResolutionStrengthMaxDistance;
    vec4 pQuality;
    ivec4 pControls;
};

#define uViewProj pViewProj
#define uInvViewProj pInvViewProj
#define uCameraPos pCameraPosRadius.xyz
#define uRadius pCameraPosRadius.w
#define uHalfResolution pHalfResolutionStrengthMaxDistance.xy
#define uStrength pHalfResolutionStrengthMaxDistance.z
#define uMaxDistance pHalfResolutionStrengthMaxDistance.w
#define uThickness pQuality.x
#define uRadianceFilterStrength pQuality.y
#define uColorBleedStrength pQuality.z
#define uSamples pControls.x
#define uFrameIndex pControls.y

const float PI = 3.14159265359;
const float GOLDEN_ANGLE = 2.39996322973;

vec3 reconstructWorldPosition(vec2 clipUv, float depth) {
    vec4 clip = vec4(clipUv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    return world.xyz / max(world.w, 0.00001);
}

vec2 spiralSample(int index, int sampleCount, float jitter) {
    float fi = float(index) + jitter;
    float r = sqrt((float(index) + 0.5) / max(float(sampleCount), 1.0));
    float a = fi * GOLDEN_ANGLE + jitter * 2.0 * PI;
    return vec2(cos(a), sin(a)) * r;
}

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 filteredRadiance(vec2 screenUv, float depth, vec3 worldPos, vec3 normal) {
    vec2 textureUv = rhiScreenUvToTextureUv(screenUv);
    vec3 centerRadiance = max(texture(uSceneLightingTex, textureUv).rgb, vec3(0.0));
    float filterStrength = clamp(uRadianceFilterStrength, 0.0, 1.0);
    if (filterStrength <= 1e-4) {
        return centerRadiance;
    }

    vec2 texelSize = 1.0 / vec2(textureSize(uSceneLightingTex, 0));
    vec3 radianceSum = centerRadiance;
    float weightSum = 1.0;
    const vec2 offsets[4] = vec2[](
        vec2(1.0, 0.0),
        vec2(-1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(0.0, -1.0)
    );

    for (int i = 0; i < 4; ++i) {
        vec2 sampleScreenUv = screenUv + offsets[i] * texelSize;
        if (sampleScreenUv.x <= 0.0 || sampleScreenUv.y <= 0.0 ||
            sampleScreenUv.x >= 1.0 || sampleScreenUv.y >= 1.0) {
            continue;
        }

        vec2 sampleTextureUv = rhiScreenUvToTextureUv(sampleScreenUv);
        float sampleDepth = texture(uDepthTex, sampleTextureUv).r;
        if (sampleDepth >= 0.9999) {
            continue;
        }

        vec3 sampleWorld = reconstructWorldPosition(
            rhiScreenUvToClipUv(sampleScreenUv), sampleDepth);
        vec3 sampleNormal = unpackGBufferNormal(texture(uNormalAoTex, sampleTextureUv));
        float depthWeight = exp2(-length(sampleWorld - worldPos) * 1.75);
        float normalWeight = pow(max(dot(sampleNormal, normal), 0.0), 24.0);
        float weight = depthWeight * normalWeight * 0.5;
        radianceSum += max(texture(uSceneLightingTex, sampleTextureUv).rgb, vec3(0.0)) * weight;
        weightSum += weight;
    }

    vec3 filtered = radianceSum / max(weightSum, 1e-4);
    return mix(centerRadiance, filtered, filterStrength);
}

void main() {
    vec2 textureUv = rhiScreenUvToTextureUv(vScreenUv);
    float centerDepth = texture(uDepthTex, textureUv).r;
    if (centerDepth >= 0.9999) {
        FragColor = vec4(0.0);
        return;
    }

    SurfaceMaterialAux centerAux = unpackGBufferMaterialAux(texture(uMaterialAuxTex, textureUv));
    if (decodeTranslucentMask(centerAux.materialKind).isTranslucent) {
        FragColor = vec4(0.0);
        return;
    }

    vec3 centerWorld = reconstructWorldPosition(vClipUv, centerDepth);
    vec3 centerNormal = unpackGBufferNormal(texture(uNormalAoTex, textureUv));
    vec3 centerAlbedo = texture(uAlbedoTex, textureUv).rgb;
    float viewDistance = max(length(centerWorld - uCameraPos), 0.5);

    ivec2 noiseSize = textureSize(uNoiseTex, 0);
    vec2 noiseUv = (gl_FragCoord.xy + vec2(float((uFrameIndex * 17) & 255), float((uFrameIndex * 43) & 255))) /
                   vec2(max(noiseSize, ivec2(1)));
    float jitter = texture(uNoiseTex, noiseUv).r;

    vec2 aspectScale = vec2(uHalfResolution.y / max(uHalfResolution.x, 1.0), 1.0);
    float projectedRadius = clamp(uRadius * uViewProj[1][1] / viewDistance, 0.008, 0.22);

    vec3 indirect = vec3(0.0);
    float confidence = 0.0;
    int sampleCount = clamp(uSamples, 1, 32);

    for (int i = 0; i < sampleCount; ++i) {
        vec2 offset = spiralSample(i, sampleCount, jitter) * projectedRadius * aspectScale;
        vec2 sampleScreenUv = vScreenUv + offset;
        if (sampleScreenUv.x <= 0.0 || sampleScreenUv.y <= 0.0 ||
            sampleScreenUv.x >= 1.0 || sampleScreenUv.y >= 1.0) {
            continue;
        }

        vec2 sampleTextureUv = rhiScreenUvToTextureUv(sampleScreenUv);
        float sampleDepth = texture(uDepthTex, sampleTextureUv).r;
        if (sampleDepth >= 0.9999) {
            continue;
        }

        SurfaceMaterialAux sampleAux = unpackGBufferMaterialAux(
            texture(uMaterialAuxTex, sampleTextureUv));
        if (decodeTranslucentMask(sampleAux.materialKind).isTranslucent) {
            continue;
        }

        vec3 sampleWorld = reconstructWorldPosition(
            rhiScreenUvToClipUv(sampleScreenUv), sampleDepth);
        vec3 toSample = sampleWorld - centerWorld;
        float distSq = dot(toSample, toSample);
        if (distSq <= 1e-5) {
            continue;
        }

        float dist = sqrt(distSq);
        if (dist > uMaxDistance) {
            continue;
        }

        vec3 dir = toSample / dist;
        vec3 sampleNormal = unpackGBufferNormal(texture(uNormalAoTex, sampleTextureUv));
        float receiverTerm = max(dot(centerNormal, dir), 0.0);
        float emitterTerm = max(dot(sampleNormal, -dir), 0.0);
        if (receiverTerm <= 0.018 || emitterTerm <= 0.018) {
            continue;
        }

        float distanceFade = 1.0 - smoothstep(uMaxDistance * 0.35, uMaxDistance, dist);
        float contactThickness = 1.0 - smoothstep(uThickness, uThickness * 4.0, dist);
        float facing = pow(receiverTerm, 0.75) * pow(emitterTerm, 0.65);
        float attenuation = distanceFade / (1.0 + distSq * 0.18);
        float weight = facing * attenuation * mix(0.74, 1.0, contactThickness);

        vec3 sampleAlbedo = texture(uAlbedoTex, sampleTextureUv).rgb;
        vec3 radiance = filteredRadiance(
            sampleScreenUv, sampleDepth, sampleWorld, sampleNormal);
        radiance *= min(1.0, 10.0 / max(luminance(radiance), 1e-4));
        vec3 albedoChroma = min(sampleAlbedo / max(luminance(sampleAlbedo), 0.18), vec3(3.0));
        radiance = mix(radiance, albedoChroma * luminance(radiance), clamp(uColorBleedStrength, 0.0, 1.0));
        indirect += radiance * weight;
        confidence += weight;
    }

    float normalization = uStrength / max(float(sampleCount) * 0.10, 1.0);
    vec3 gi = centerAlbedo * indirect * normalization;
    gi = min(gi, vec3(8.0));
    float alpha = clamp(confidence / max(float(sampleCount) * 0.08, 1.0), 0.0, 1.0);
    FragColor = vec4(gi, alpha);
}
