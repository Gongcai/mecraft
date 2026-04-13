#version 330 core
out vec4 FragColor;

in vec2 vUV;
in float vLight;
in float vSunlight;
in float vBlockLight;
in float vAO;
in float vNormal;
in float vLayer;
in float vFogDist;

uniform sampler2DArray texArray;
uniform int uForceBaseLod;
uniform vec3 uGrassTintColor;
uniform int uFogEnabled;
uniform int uFogMode;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogDensity;
uniform int uDebugLightMode; // 0=off, 1=sky light heatmap, 2=block light heatmap, 3=combined heatmap
uniform float uSkyIntensity; // 0.0-1.0, day/night cycle (default 1.0)

// Ambient Occlusion brightness levels
// Level 0 (fully occluded corner) = 0.72, level 3 (open) = 1.0
// Keep the range narrow so corners are darkened but never go jet-black.
const float aoLevels[4] = float[](0.72, 0.82, 0.91, 1.0);

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
    bool forceBaseLod = (uForceBaseLod != 0) || (vNormal < -0.5);
    vec3 sampleCoord = vec3(vUV, vLayer);
    vec4 texColor = forceBaseLod
        ? textureLod(texArray, sampleCoord, 0.0)
        : texture(texArray, sampleCoord);

    if (texColor.a < 0.1)
        discard;

    // Grass tint for cross vegetation (aNormal == -1.0 for grass, -2.0 for flowers)
    if (abs(vNormal + 1.0) < 0.001) {
        texColor.rgb *= uGrassTintColor;
    }

    // AO: bilinear interpolate through the discrete AO levels
    // GPU smoothly interpolates vAO between vertex values (e.g., 2.3),
    // so we must NOT discretize with int() - that destroys the gradient.
    float aoIdx = clamp(vAO, 0.0, 3.0);
    int aoLow = int(aoIdx);
    int aoHigh = min(aoLow + 1, 3);
    float aoFactor = mix(aoLevels[aoLow], aoLevels[aoHigh], fract(aoIdx));

    // Brightness was pre-computed on the CPU via the exponential decay curve
    // pow(0.8, 15-L) and averaged in brightness space for physically-correct
    // smooth lighting.  The GPU interpolates brightness values directly.
    float sun   = vSunlight;
    float block = vBlockLight;

    // Base light: use skyIntensity to scale sun contribution (for day/night cycle)
    float skyContribution = sun * max(uSkyIntensity, 0.0);
    // Ambient floor of 0.08 ensures even fully-shadowed spots are visible
    float lightFactor = max(max(block, skyContribution), 0.08);

    // Combine texture, lighting, and AO
    vec3 finalColor = texColor.rgb * lightFactor * aoFactor;

    if (uFogEnabled != 0) {
        float fogFactor = computeFogFactor(vFogDist);
        finalColor = mix(uFogColor, finalColor, fogFactor);
    }

    FragColor = vec4(finalColor, texColor.a);
}
