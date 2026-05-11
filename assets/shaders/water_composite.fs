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
    return mix(procedural, externalNoise, 0.0);
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
    float facing = clamp(abs(dot(viewDir, vec3(0.0, 1.0, 0.0))), 0.0, 1.0);
    float fresnel = pow(1.0 - facing, 3.0);

    vec3 shallowTint = srgbToLinear(vec3(0.34, 0.66, 0.88));
    vec3 deepTint = max(srgbToLinear(vec3(0.06, 0.24, 0.42)) * max(uWaterAbsorption, vec3(0.001)), vec3(0.0));
    float absorption = clamp(depthGap * 280.0, 0.0, 1.0);
    float distanceAbsorption = smoothstep(12.0, 84.0, vFogDist);
    vec3 waterTint = mix(shallowTint, deepTint, max(absorption, distanceAbsorption * 0.45));

    if (uCompositeInputsEnabled != 0) {
        vec2 refractUv = clamp(screenUv + vec2(wave, nFine - 0.5) * (0.0015 + 0.0040 * fresnel) * topFace,
                               vec2(0.0),
                               vec2(1.0));
        vec3 sceneColor = texture(uSceneColorTex, refractUv).rgb;
        color = mix(color, sceneColor * waterTint, (0.08 + 0.14 * absorption) * topFace);
    }

    color = mix(color, color * waterTint, 0.34 + absorption * 0.26);
    color += shallowTint * (0.038 + 0.024 * wave + 0.055 * shimmer) * topFace;
    color += vec3(1.0) * fresnel * (0.066 + 0.060 * topFace + 0.032 * shimmer);
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
