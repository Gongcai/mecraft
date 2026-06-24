// Block/item forward lit fragment shader — Mecraft Phase 5.4 enhanced.
// Used for held blocks in first-person view.
// The lightmap provides stable first-person lighting, while CSM adds a soft
// deferred accent without crushing the view model to black.

#version 450 core
#include "held_item_shadow.glsl"

out vec4 FragColor;

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
flat in float vTintKind;
in vec2 vTintUV;
in vec3 vWorldPos;

uniform sampler2DArray texArray;
uniform sampler2D uLightmapDay;
uniform sampler2D uLightmapNight;
uniform sampler2D uGrassColormap;
uniform sampler2D uFoliageColormap;
uniform int uForceBaseLod;
uniform int uFogEnabled;
uniform int uFogMode;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogDensity;
uniform int uDebugLightMode;
uniform float uSkyIntensity;
uniform float uAnimationTime;
uniform float uAmbientStrength;
uniform float uHeldSunlight;
uniform float uHeldBlockLight;
uniform float uHeldSceneHdrScale;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

vec3 redstoneTintSrgb(vec2 tintUV) {
    float power = clamp(floor(tintUV.x * 16.0), 0.0, 15.0) / 15.0;
    return mix(vec3(0.30, 0.0, 0.0), vec3(1.0, 0.10, 0.02), power);
}

const float aoLevels[4] = float[](0.72, 0.82, 0.91, 1.0);

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

float computeFogFactor(float fogDistance) {
    if (uFogMode == 1) {
        return clamp(exp(-uFogDensity * fogDistance), 0.0, 1.0);
    }
    if (uFogMode == 2) {
        float d = uFogDensity * fogDistance;
        return clamp(exp(-(d * d)), 0.0, 1.0);
    }
    float linearRange = max(uFogEnd - uFogStart, 0.0001);
    return clamp((uFogEnd - fogDistance) / linearRange, 0.0, 1.0);
}

void main() {
    if (uDebugLightMode != 0) {
        float val = vLight;
        if (uDebugLightMode == 1) {
            val = vSunlight;
        } else if (uDebugLightMode == 2) {
            val = vBlockLight;
        }

        vec3 heatmap;
        if (val < 0.25) {
            heatmap = mix(vec3(0.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), val * 4.0);
        } else if (val < 0.5) {
            heatmap = mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), (val - 0.25) * 4.0);
        } else if (val < 0.75) {
            heatmap = mix(vec3(0.0, 1.0, 1.0), vec3(1.0, 1.0, 0.0), (val - 0.5) * 4.0);
        } else {
            heatmap = mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (val - 0.75) * 4.0);
        }
        FragColor = vec4(heatmap, 1.0);
        return;
    }

    bool isCrossVegetation = (vNormal > -2.5 && vNormal < -0.5);
    bool forceBaseLod = (uForceBaseLod != 0) || isCrossVegetation;
    float sampledLayer = vLayer;
    if (vAnimated > 0.5 && vAnimationFrameCount > 1.0 && vAnimationFps > 0.0) {
        float frame = mod(floor(uAnimationTime * vAnimationFps), vAnimationFrameCount);
        sampledLayer += frame;
    }

    vec4 texColor = forceBaseLod
        ? textureLod(texArray, vec3(vUV, sampledLayer), 0.0)
        : texture(texArray, vec3(vUV, sampledLayer));
    if (texColor.a < 0.1) {
        discard;
    }

    vec3 albedo = srgbToLinear(texColor.rgb);
    if (vTintKind > 0.5 && vTintKind < 1.5) {
        albedo *= srgbToLinear(texture(uGrassColormap, vTintUV).rgb);
    } else if (vTintKind > 1.5 && vTintKind < 2.5) {
        albedo *= srgbToLinear(texture(uFoliageColormap, vTintUV).rgb);
    } else if (vTintKind > 2.5 && vTintKind < 3.5) {
        albedo *= srgbToLinear(redstoneTintSrgb(vTintUV));
    }

    float aoIdx = clamp(vAO, 0.0, 3.0);
    int aoLow = int(aoIdx);
    int aoHigh = min(aoLow + 1, 3);
    float aoFactor = mix(aoLevels[aoLow], aoLevels[aoHigh], fract(aoIdx));

    vec2 lightmapUV = vec2(uHeldBlockLight, 1.0 - uHeldSunlight);
    vec3 dayLight = srgbToLinear(texture(uLightmapDay, lightmapUV).rgb);
    vec3 nightLight = srgbToLinear(texture(uLightmapNight, lightmapUV).rgb);
    vec3 lightColor = mix(nightLight, dayLight, clamp(uSkyIntensity, 0.0, 1.0));

    vec3 faceNormal = decodeFaceNormal(vNormal);
    vec3 lightDir = normalize(uSunDirection);
    float ndotl = max(dot(faceNormal, lightDir), 0.0);
    float shadow = sampleHeldItemShadow(vWorldPos, faceNormal);
    float shadowVisibility = mix(0.72, 1.0, shadow);
    float directionalShape = mix(0.90, 1.14, ndotl) * shadowVisibility;
    float sunAmount = clamp(uHeldSunlight * uSkyIntensity, 0.0, 1.0);
    float deferredAccent = mix(1.0, directionalShape, 0.65 * sunAmount);

    vec3 finalColor = albedo * lightColor * aoFactor * deferredAccent * uHeldSceneHdrScale;
    if (uFogEnabled != 0) {
        float fogFactor = computeFogFactor(vFogDist);
        finalColor = mix(srgbToLinear(uFogColor), finalColor, fogFactor);
    }

    FragColor = vec4(finalColor, texColor.a);
}
