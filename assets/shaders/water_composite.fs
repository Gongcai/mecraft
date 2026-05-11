#version 450 core
#include "gbuffer_contract.glsl"

// Varyings from chunk_lit.vs
in vec2 vUV;
in float vLight;
in float vSunlight;
in float vBlockLight;
in float vAO;
in float vNormal;
in float vLayer;
in float vAnimationFrameCount;
in float vAnimationFps;
in float vAnimated;
in float vFogDist;
in vec3 vWorldPos;
flat in float vTintKind;
flat in float vMaterialKind;
in vec2 vTintUV;

uniform sampler2DArray texArray;
uniform sampler2D uOpaqueDepthTex;
uniform sampler2D uSkyCaptureTex;
uniform sampler2D uSceneColorTex;
uniform sampler2D uWaterNoiseTex;
uniform sampler2D uNormalAoTex;
uniform sampler2D uMaterialTex;
uniform sampler2D uMaterialAuxTex;
uniform sampler2D uReflectionTex;
uniform sampler2D uShadowMap;
uniform int uSkyCaptureEnabled;
uniform int uCompositeInputsEnabled;
uniform int uWaterCompositeEnabled;
uniform int uDepthSofteningEnabled;
uniform float uAnimationTime;
uniform float uWindTime;
uniform float uWaterStillFirstLayer;
uniform float uWaterStillLayerCount;
uniform float uWaterFlowFirstLayer;
uniform float uWaterFlowLayerCount;
uniform vec3 uWaterAbsorption;
uniform vec3 uCameraPos;
uniform vec3 uSunDirection;
uniform vec3 uSunLightColor;
uniform float uWeatherWetness;

out vec4 FragColor;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

vec2 directionToSkyCaptureUv(vec3 dir) {
    dir = normalize(dir);
    float phi = atan(dir.x, -dir.z);
    float u = phi / 6.28318530718 + 0.5;
    float v = dir.y * 0.5 + 0.5;
    return vec2(fract(u), clamp(v, 0.0, 1.0));
}

vec3 decodeFaceNormal(float face) {
    if (face > -2.5 && face < -0.5) {
        return vec3(0.0, 1.0, 0.0);
    }
    int idx = int(round(face));
    if (idx == 0) return vec3(0.0, 1.0, 0.0);
    if (idx == 1) return vec3(0.0, -1.0, 0.0);
    if (idx == 2) return vec3(0.0, 0.0, 1.0);
    if (idx == 3) return vec3(0.0, 0.0, -1.0);
    if (idx == 4) return vec3(-1.0, 0.0, 0.0);
    return vec3(1.0, 0.0, 0.0);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float waterNoise(vec2 p, float time) {
    float n = valueNoise(p * 0.72 + vec2(time * 0.035, -time * 0.021));
    n += valueNoise(p * 1.93 + vec2(-time * 0.052, time * 0.044)) * 0.52;
    n += valueNoise(p * 4.17 + vec2(time * 0.090, time * 0.027)) * 0.24;
    return n / 1.76;
}

bool layerInRange(float layer, float firstLayer, float layerCount) {
    return layerCount > 0.5 && layer >= firstLayer - 0.5 && layer < firstLayer + layerCount - 0.5;
}

bool isWaterLayer(float layer) {
    return layerInRange(layer, uWaterStillFirstLayer, uWaterStillLayerCount) ||
           layerInRange(layer, uWaterFlowFirstLayer, uWaterFlowLayerCount);
}

float sampleWaterCompositeNoise(vec2 proceduralP, vec2 textureUv, vec2 wind, float timeScale) {
    float procedural = waterNoise(proceduralP, uAnimationTime * timeScale);
    if (uWaterCompositeEnabled == 0) {
        return procedural;
    }
    float externalNoise = texture(uWaterNoiseTex, textureUv + wind).r;
    return mix(procedural, externalNoise, 0.55);
}

vec3 proceduralWaterNormal(vec2 p) {
    float t = uAnimationTime * 0.08;
    float h0 = waterNoise(p * 0.028, t);
    float hx1 = waterNoise((p + vec2(2.0, 0.0)) * 0.028, t);
    float hx2 = waterNoise((p - vec2(2.0, 0.0)) * 0.028, t);
    float hz1 = waterNoise((p + vec2(0.0, 2.0)) * 0.028, t);
    float hz2 = waterNoise((p - vec2(0.0, 2.0)) * 0.028, t);
    vec2 slope = vec2(hx1 - hx2, hz1 - hz2);
    slope += vec2(h0 - 0.5, 0.5 - h0) * 0.16;
    return normalize(vec3(slope.x * 2.4, 1.0, slope.y * 2.4));
}

vec3 sampleSkyReflection(vec3 dir) {
    return texture(uSkyCaptureTex, directionToSkyCaptureUv(dir)).rgb;
}

vec3 applyWaterComposite(vec3 color, float alpha, float faceNormal, float depthGap, vec2 screenUv) {
    float topFace = step(-0.5, faceNormal) * step(faceNormal, 0.5);
    vec2 p = vWorldPos.xz;
    float n = sampleWaterCompositeNoise(p,
                                        p * 0.018,
                                        vec2(uAnimationTime * 0.006, -uAnimationTime * 0.004),
                                        1.0);
    float nFine = sampleWaterCompositeNoise(p * 2.35 + vec2(17.2, -9.4),
                                            (p + vec2(17.2, -9.4)) * 0.046,
                                            vec2(-uAnimationTime * 0.010, uAnimationTime * 0.008),
                                            1.37);
    float wave = (n - 0.5) * 2.0;
    float shimmer = smoothstep(0.62, 0.90, nFine) *
                    (1.0 - smoothstep(96.0, 180.0, vFogDist));

    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    vec3 normal = proceduralWaterNormal(p + vec2(uAnimationTime * 8.0, -uAnimationTime * 5.0));
    normal = normalize(mix(normal, decodeFaceNormal(faceNormal), 0.18));
    float facing = clamp(dot(viewDir, normal), 0.0, 1.0);
    float fresnel = pow(1.0 - facing, 4.5);
    float fresnelBoost = mix(0.72, 1.22, smoothstep(0.35, 0.95, nFine));

    vec3 shallowTint = srgbToLinear(vec3(0.31, 0.61, 0.83));
    vec3 deepTint = srgbToLinear(vec3(0.04, 0.17, 0.28));
    vec3 absorption = exp(-max(uWaterAbsorption, vec3(0.001)) * (0.42 + depthGap * 6.0 + vFogDist * 0.01));
    float absorptionMix = clamp(depthGap * 280.0, 0.0, 1.0);
    float distanceAbsorption = smoothstep(10.0, 92.0, vFogDist);
    vec3 waterTint = mix(shallowTint, deepTint, max(absorptionMix, distanceAbsorption * 0.42));

    if (uCompositeInputsEnabled != 0) {
        vec2 refractUv = clamp(screenUv + vec2(wave, nFine - 0.5) * (0.0015 + 0.0040 * fresnel) * topFace,
                               vec2(0.0),
                               vec2(1.0));
        vec3 sceneColor = texture(uSceneColorTex, refractUv).rgb;
        vec2 reflUv = clamp(screenUv + vec2(wave, nFine - 0.5) * 0.008 * topFace, vec2(0.0), vec2(1.0));
        vec3 reflectionColor = texture(uReflectionTex, reflUv).rgb;
        vec3 skyReflection = sampleSkyReflection(reflect(-viewDir, normal));
        vec3 waterReflection = mix(skyReflection, reflectionColor, 0.75);
        color = mix(color, sceneColor * waterTint * absorption, (0.14 + 0.24 * absorptionMix) * topFace);
        color = mix(color, waterReflection, clamp(fresnel * fresnelBoost, 0.0, 1.0) * topFace);
    }

    float foamMask = smoothstep(0.005, 0.085, depthGap) * smoothstep(0.60, 0.95, nFine);
    float crest = smoothstep(0.42, 0.84, n);
    vec3 foam = mix(vec3(0.82, 0.90, 0.95), vec3(1.0), crest) * foamMask * topFace;

    color = mix(color, color * waterTint * absorption, 0.30 + absorptionMix * 0.30);
    color += shallowTint * (0.036 + 0.022 * wave + 0.052 * shimmer) * topFace;
    color += vec3(1.0) * fresnel * (0.045 + 0.090 * topFace + 0.026 * shimmer);
    color += foam * (0.16 + 0.12 * fresnel);
    color += waterTint * (0.015 + 0.025 * topFace) * max(uWeatherWetness, 0.0);
    return max(color, vec3(0.0));
}

void main() {
    if (!isWaterLayer(vLayer)) {
        discard;
    }

    vec4 texColor = texture(texArray, vec3(vUV, vLayer));
    float alpha = texColor.a;
    if (alpha < 0.01) {
        discard;
    }

    vec3 color = texColor.rgb;

    // Depth-aware alpha softening
    float depthGap = 0.0;
    if (uDepthSofteningEnabled != 0) {
        float sceneDepth = texelFetch(uOpaqueDepthTex, ivec2(gl_FragCoord.xy), 0).r;
        float fragDepth = gl_FragCoord.z;
        float near = 0.05;
        float far = 512.0;
        float linearFrag = (2.0 * near) / (far + near - (fragDepth * 2.0 - 1.0) * (far - near));
        float linearScene = (2.0 * near) / (far + near - (sceneDepth * 2.0 - 1.0) * (far - near));
        depthGap = max(0.0, linearScene - linearFrag);
        float softAlpha = smoothstep(0.0, 0.15, depthGap);
        alpha *= 0.5 + 0.5 * softAlpha;
    }

    vec2 screenUv = gl_FragCoord.xy / vec2(textureSize(uSceneColorTex, 0));
    color = applyWaterComposite(color, alpha, vNormal, depthGap, screenUv);

    FragColor = vec4(color, alpha);
}
