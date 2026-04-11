#version 330 core
out vec4 FragColor;

in vec2 vUV;
in float vNormal;
in float vFogDist;

uniform sampler2D texAtlas;
uniform int uForceBaseLod;
uniform vec3 uGrassTintColor;
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
    vec4 texColor = (uForceBaseLod != 0)
        ? textureLod(texAtlas, vUV, 0.0)
        : texture(texAtlas, vUV);

    if (texColor.a < 0.1)
        discard;

    if (vNormal < 0.0) {
        texColor.rgb *= uGrassTintColor;
    }

    if (uFogEnabled == 0) {
        FragColor = texColor;
        return;
    }

    float fogFactor = computeFogFactor(vFogDist);
    vec3 finalColor = mix(uFogColor, texColor.rgb, fogFactor);
    FragColor = vec4(finalColor, texColor.a);
}
