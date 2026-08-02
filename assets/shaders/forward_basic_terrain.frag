#version 450 core
// Forward vanilla terrain fragment shader - pure Minecraft-style rendering.
// No directional light, no Hammon diffuse, no sRGB linearize, no water composite,
// no skyCapture, atmosphereLut, shadow maps, SSAO, SSR, volumetric.
// Contract: texture array + lightmap day/night + AO levels + biome tint + fog.

#include "terrain_material_sampling.glsl"

layout(location = 0) out vec4 FragColor;

layout(location = 0) in vec2 vUV;
layout(location = 1) in float vLight;
layout(location = 2) in float vSunlight;
layout(location = 3) in float vBlockLight;
layout(location = 4) in float vAO;
layout(location = 5) in float vNormal;
layout(location = 6) in float vLayer;
layout(location = 7) in float vAnimationFrameCount;
layout(location = 8) in float vAnimationFps;
layout(location = 9) in float vAnimated;
layout(location = 10) in float vFogDist;
layout(location = 11) in vec3 vWorldPos;
layout(location = 12) flat in float vTintKind;
layout(location = 13) flat in float vMaterialKind;
layout(location = 14) in vec2 vTintUV;

// Textures
#ifdef RHI_TERRAIN_FORWARD_MDI
layout(set = 1, binding = 0) uniform sampler2DArray texArray;
layout(set = 1, binding = 1) uniform sampler2D uLightmapDay;
layout(set = 1, binding = 2) uniform sampler2D uLightmapNight;
layout(set = 1, binding = 3) uniform sampler2D uGrassColormap;
layout(set = 1, binding = 4) uniform sampler2D uFoliageColormap;
#include "terrain_forward_params.glsl"
#else
uniform sampler2DArray texArray;
uniform sampler2D uLightmapDay;
uniform sampler2D uLightmapNight;
uniform sampler2D uGrassColormap;
uniform sampler2D uFoliageColormap;

// Control
uniform int uForceBaseLod;
uniform float uSkyIntensity; // 0.0-1.0, day/night interpolation factor
uniform float uAnimationTime;

// Fog
uniform int uFogEnabled;
uniform int uFogMode;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogDensity;

// Debug
uniform int uDebugLightMode; // 0=off, 1=sky light heatmap, 2=block light heatmap, 3=combined
#endif

vec3 redstoneTintSrgb(vec2 tintUV) {
    float power = clamp(floor(tintUV.x * 16.0), 0.0, 15.0) / 15.0;
    int tint = int(clamp(floor(tintUV.y * 16.0), 0.0, 15.0));
    const vec3 lowPalette[16] = vec3[16](
        vec3(0.30, 0.00, 0.00), vec3(0.00, 0.05, 0.30), vec3(0.00, 0.22, 0.03), vec3(0.22, 0.18, 0.00),
        vec3(0.18, 0.00, 0.28), vec3(0.00, 0.20, 0.24), vec3(0.28, 0.09, 0.00), vec3(0.22, 0.22, 0.22),
        vec3(0.35, 0.02, 0.12), vec3(0.10, 0.20, 0.36), vec3(0.04, 0.28, 0.17), vec3(0.32, 0.24, 0.04),
        vec3(0.25, 0.07, 0.34), vec3(0.02, 0.30, 0.30), vec3(0.32, 0.16, 0.08), vec3(0.36, 0.36, 0.36)
    );
    const vec3 highPalette[16] = vec3[16](
        vec3(1.00, 0.10, 0.02), vec3(0.08, 0.35, 1.00), vec3(0.08, 0.95, 0.18), vec3(1.00, 0.86, 0.08),
        vec3(0.78, 0.18, 1.00), vec3(0.05, 0.92, 1.00), vec3(1.00, 0.38, 0.05), vec3(0.82, 0.82, 0.82),
        vec3(1.00, 0.18, 0.42), vec3(0.35, 0.62, 1.00), vec3(0.18, 1.00, 0.62), vec3(1.00, 0.74, 0.20),
        vec3(0.82, 0.40, 1.00), vec3(0.25, 1.00, 0.92), vec3(1.00, 0.56, 0.25), vec3(1.00, 1.00, 1.00)
    );
    return mix(lowPalette[tint], highPalette[tint], power);
}

// Ambient Occlusion brightness levels
// Level 0 (fully occluded corner) = 0.62, level 3 (open) = 1.0.
const float aoLevels[4] = float[](0.62, 0.75, 0.87, 1.0);

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
    // Debug light visualization modes
    if (uDebugLightMode != 0) {
        float val;
        if (uDebugLightMode == 1) {
            val = vSunlight;
        } else if (uDebugLightMode == 2) {
            val = vBlockLight;
        } else {
            val = vLight;
        }
        // Heatmap: black -> blue -> cyan -> green -> yellow -> red -> white
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

    // Cross vegetation alpha-cutout mips can darken noticeably at distance.
    bool isCrossVegetation = (vNormal > -2.5 && vNormal < -0.5);
    bool forceBaseLodFlag = (uForceBaseLod != 0) || isCrossVegetation;
    float sampledLayer = terrainAnimatedTextureLayer(vLayer, vAnimationFrameCount, vAnimationFps, vAnimated,
                                                      uAnimationTime);

    vec3 sampleCoord = vec3(vUV, sampledLayer);
    vec4 texColor = forceBaseLodFlag
        ? textureLod(texArray, sampleCoord, 0.0)
        : texture(texArray, sampleCoord);

    if (!terrainAlphaTestPasses(texColor.a))
        discard;

    // Biome tinting: grass (kind=1) and foliage (kind=2)
    if (vTintKind > 0.5 && vTintKind < 1.5) {
        texColor.rgb *= texture(uGrassColormap, vTintUV).rgb;
    } else if (vTintKind > 1.5 && vTintKind < 2.5) {
        texColor.rgb *= texture(uFoliageColormap, vTintUV).rgb;
    } else if (vTintKind > 2.5 && vTintKind < 3.5) {
        texColor.rgb *= redstoneTintSrgb(vTintUV);
    }

    // AO: bilinear interpolate through the discrete AO levels
    // GPU smoothly interpolates vAO between vertex values (e.g., 2.3),
    // so we must NOT discretize with int() - that destroys the gradient.
    float aoIdx = clamp(vAO, 0.0, 3.0);
    int aoLow = int(aoIdx);
    int aoHigh = min(aoLow + 1, 3);
    float aoFactor = mix(aoLevels[aoLow], aoLevels[aoHigh], fract(aoIdx));

    // Lightmap lookup:
    // vBlockLight and vSunlight are raw light levels normalized to [0,1] range (level/15).
    // The lightmap image layout:
    //   X axis (left to right) = block light 0 -> 15
    //   Y axis (top to bottom) = sky light 15 -> 0 (inverted)
    // OpenGL V=0 is the top of the image (sky=15, brightest), V=1 is bottom (sky=0, darkest).
    // So we invert vSunlight: high sky level -> low V -> top of texture -> bright.
    vec2 lightmapUV = vec2(vBlockLight, 1.0 - vSunlight);
    vec3 dayLight = texture(uLightmapDay, lightmapUV).rgb;
    vec3 nightLight = texture(uLightmapNight, lightmapUV).rgb;
    vec3 lightColor = mix(nightLight, dayLight, clamp(uSkyIntensity, 0.0, 1.0));

    // Combine texture, lightmap color, and AO
    vec3 finalColor = texColor.rgb * lightColor * aoFactor;

    // Fog
    if (uFogEnabled != 0) {
        float fogFactor = computeFogFactor(vFogDist);
        finalColor = mix(uFogColor, finalColor, fogFactor);
    }

    FragColor = vec4(finalColor, texColor.a);
}
