#version 450 core
// Forward vanilla terrain fragment shader - pure Minecraft-style rendering.
// No directional light, no Hammon diffuse, no sRGB linearize, no water composite,
// no skyCapture, atmosphereLut, shadow maps, SSAO, SSR, volumetric.
// Contract: texture array + lightmap day/night + AO levels + biome tint + fog.

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
in vec3 vWorldPos;
flat in float vTintKind;
flat in float vMaterialKind;
in vec2 vTintUV;

// Textures
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
    float sampledLayer = vLayer;
    if (vAnimated > 0.5 && vAnimationFrameCount > 1.0 && vAnimationFps > 0.0) {
        float frame = mod(floor(uAnimationTime * vAnimationFps), vAnimationFrameCount);
        sampledLayer += frame;
    }

    vec3 sampleCoord = vec3(vUV, sampledLayer);
    vec4 texColor = forceBaseLodFlag
        ? textureLod(texArray, sampleCoord, 0.0)
        : texture(texArray, sampleCoord);

    if (texColor.a < 0.1)
        discard;

    // Biome tinting: grass (kind=1) and foliage (kind=2)
    if (vTintKind > 0.5 && vTintKind < 1.5) {
        texColor.rgb *= texture(uGrassColormap, vTintUV).rgb;
    } else if (vTintKind > 1.5 && vTintKind < 2.5) {
        texColor.rgb *= texture(uFoliageColormap, vTintUV).rgb;
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
