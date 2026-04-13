#version 330 core
out vec4 FragColor;

in vec2 vUV;
in float vLight;
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

// Ambient Occlusion brightness levels
const float aoLevels[4] = float[](0.4, 0.6, 0.8, 1.0);

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

    // AO: map 0-3 level to brightness multiplier
    float aoFactor = aoLevels[int(vAO + 0.5)];

    // Base light (minimum 0.1 so dark areas aren't fully black)
    float lightFactor = max(vLight, 0.1);

    // Combine texture, lighting, and AO
    vec3 finalColor = texColor.rgb * lightFactor * aoFactor;

    if (uFogEnabled != 0) {
        float fogFactor = computeFogFactor(vFogDist);
        finalColor = mix(uFogColor, finalColor, fogFactor);
    }

    FragColor = vec4(finalColor, texColor.a);
}
