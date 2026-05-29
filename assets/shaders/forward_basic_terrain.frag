#version 450 core
// Forward basic terrain fragment shader — vanilla-style fallback renderer.
// No skyCapture, atmosphereLut, shadow maps, SSAO, SSR, volumetric, or deferred resources.
// Reads only: block texture array, lightmap, biome colormap, fog, simple sky lighting.

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

// Lighting
uniform vec3 uSunDirection;
uniform vec3 uSunLightColor;
uniform vec3 uMoonLightColor;
uniform vec3 uSkyAmbientColor;
uniform float uSkyIntensity;
uniform float uMoonVisibility;
uniform vec3 uCameraPos;

// Fog
uniform int uFogEnabled;
uniform int uFogMode;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogDensity;

// Water effects (minimal)
uniform int uWaterEffectsEnabled;
uniform float uWaterStillFirstLayer;
uniform float uWaterStillLayerCount;
uniform float uWaterFlowFirstLayer;
uniform float uWaterFlowLayerCount;
uniform int uForceBaseLod;
uniform float uAnimationTime;

const float kPi = 3.14159265359;

bool layerInRange(float layer, float firstLayer, float layerCount) {
    return layerCount > 0.5 && layer >= firstLayer - 0.5 && layer < firstLayer + layerCount - 0.5;
}

bool isWaterLayer(float layer) {
    return layerInRange(layer, uWaterStillFirstLayer, uWaterStillLayerCount) ||
           layerInRange(layer, uWaterFlowFirstLayer, uWaterFlowLayerCount);
}

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

float hammonDiffuseApprox(float ndotl, float roughness) {
    float lit = max(ndotl, 0.0);
    return pow(lit, mix(1.18, 0.78, roughness));
}

void main() {
    // Sample block texture
    vec2 uv = vUV;
    int lod = uForceBaseLod;
    vec4 texColor = textureLod(texArray, vec3(uv, vLayer), float(lod));
    if (texColor.a < 0.01) discard;

    // Biome tinting: grass (kind=1) and foliage (kind=2)
    vec3 albedo = srgbToLinear(texColor.rgb);
    float tintKind = vTintKind;
    if (tintKind > 0.5 && tintKind < 1.5) {
        vec3 tint = srgbToLinear(texture(uGrassColormap, vTintUV).rgb);
        albedo *= tint;
    } else if (tintKind > 1.5 && tintKind < 2.5) {
        vec3 tint = srgbToLinear(texture(uFoliageColormap, vTintUV).rgb);
        albedo *= tint;
    }

    // Normal from face index (axis-aligned faces)
    vec3 normal = vec3(0.0, 1.0, 0.0); // default up
    float faceIdx = vNormal;
    if (faceIdx < 0.5) normal = vec3(0.0, 1.0, 0.0);       // top
    else if (faceIdx < 1.5) normal = vec3(0.0, -1.0, 0.0);  // bottom
    else if (faceIdx < 2.5) normal = vec3(1.0, 0.0, 0.0);   // east
    else if (faceIdx < 3.5) normal = vec3(-1.0, 0.0, 0.0);  // west
    else if (faceIdx < 4.5) normal = vec3(0.0, 0.0, 1.0);   // south
    else if (faceIdx < 5.5) normal = vec3(0.0, 0.0, -1.0);  // north

    // Lightmap sampling
    float skyLight = vSunlight;
    float blockLightVal = vBlockLight;
    vec2 lmUV = vec2(blockLightVal, skyLight);
    vec3 lightmapDay = texture(uLightmapDay, lmUV).rgb;
    vec3 lightmapNight = texture(uLightmapNight, lmUV).rgb;
    vec3 vanillaLight = mix(lightmapNight, lightmapDay, uSkyIntensity);

    // Simple directional sun/moon lighting
    float ndotSun = max(dot(normal, uSunDirection), 0.0);
    float ndotMoon = max(dot(normal, -uSunDirection), 0.0);
    float diffuse = hammonDiffuseApprox(ndotSun, 0.86);

    // Sky ambient: brighter on top faces, dimmer on bottom
    float upward = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 skyAmbient = uSkyAmbientColor * (0.3 + 0.5 * upward) * 0.5;

    // Combine lighting
    vec3 sunContribution = uSunLightColor * diffuse * 0.8;
    vec3 moonContribution = uMoonLightColor * ndotMoon * uMoonVisibility * 0.15;
    vec3 lightColor = sunContribution + moonContribution + skyAmbient + vanillaLight * 0.5;
    lightColor = max(lightColor, vec3(0.05)); // minimum ambient

    // AO
    float aoFactor = mix(0.4, 1.0, vAO);

    // Final color
    vec3 finalColor = albedo * lightColor * aoFactor;

    // Water tint for water layers
    bool waterLayer = (uWaterEffectsEnabled != 0) && isWaterLayer(vLayer);
    if (waterLayer) {
        vec3 waterTint = srgbToLinear(vec3(0.28, 0.58, 0.78));
        finalColor = mix(finalColor, waterTint * lightColor, 0.35);
    }

    // Fog
    float alpha = texColor.a;
    if (uFogEnabled != 0) {
        float fogFactor = 0.0;
        if (uFogMode == 0) {
            // Linear fog
            fogFactor = clamp((uFogEnd - vFogDist) / max(uFogEnd - uFogStart, 0.001), 0.0, 1.0);
        } else if (uFogMode == 1) {
            // Exponential fog
            fogFactor = exp(-uFogDensity * vFogDist);
        } else {
            // Exponential squared fog
            fogFactor = exp(-uFogDensity * uFogDensity * vFogDist * vFogDist);
        }
        finalColor = mix(uFogColor, finalColor, fogFactor);
    }

    FragColor = vec4(finalColor, alpha);
}
