#version 330 core
out vec4 FragColor;

in vec2 vUV;
in float vLayer;
in float vNormal;
in float vFogDist;
flat in float vTintKind;
in vec2 vTintUV;

uniform sampler2DArray texArray;
uniform sampler2D uGrassColormap;
uniform sampler2D uFoliageColormap;
uniform int uForceBaseLod;
uniform int uFogEnabled;
uniform int uFogMode;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogDensity;

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
    // Cross vegetation alpha-cutout mips can darken noticeably at distance.
    // Keep plants on base LOD while leaving terrain blocks on regular mip sampling.
    bool isCrossVegetation = (vNormal > -2.5 && vNormal < -0.5);
    bool forceBaseLod = (uForceBaseLod != 0) || isCrossVegetation;
    vec3 sampleCoord = vec3(vUV, vLayer);
    vec4 texColor = forceBaseLod
        ? textureLod(texArray, sampleCoord, 0.0)
        : texture(texArray, sampleCoord);

    if (texColor.a < 0.1)
        discard;

    if (vTintKind > 0.5 && vTintKind < 1.5) {
        texColor.rgb *= texture(uGrassColormap, vTintUV).rgb;
    } else if (vTintKind > 1.5 && vTintKind < 2.5) {
        texColor.rgb *= texture(uFoliageColormap, vTintUV).rgb;
    }

    if (uFogEnabled == 0) {
        FragColor = texColor;
        return;
    }

    float fogFactor = computeFogFactor(vFogDist);
    vec3 finalColor = mix(uFogColor, texColor.rgb, fogFactor);
    FragColor = vec4(finalColor, texColor.a);
}
