#version 330 core
out vec4 FragColor;

in vec2 vUV;
in vec3 vWorldPos;

uniform sampler2D texAtlas;
uniform int uForceBaseLod;
uniform int uFogEnabled;
uniform int uFogMode;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogDensity;
uniform vec3 uCameraPos;

float computeFogFactor(float distanceToCamera) {
    if (uFogMode == 1) {
        return clamp(exp(-uFogDensity * distanceToCamera), 0.0, 1.0);
    }

    if (uFogMode == 2) {
        float d = uFogDensity * distanceToCamera;
        return clamp(exp(-(d * d)), 0.0, 1.0);
    }

    float linearRange = max(uFogEnd - uFogStart, 0.0001);
    return clamp((uFogEnd - distanceToCamera) / linearRange, 0.0, 1.0);
}

void main() {
    vec4 texColor = (uForceBaseLod != 0)
        ? textureLod(texAtlas, vUV, 0.0)
        : texture(texAtlas, vUV);

    if (texColor.a < 0.1)
        discard;

    if (uFogEnabled == 0) {
        FragColor = texColor;
        return;
    }

    float distanceToCamera = length(vWorldPos - uCameraPos);
    float fogFactor = computeFogFactor(distanceToCamera);
    vec3 finalColor = mix(uFogColor, texColor.rgb, fogFactor);
    FragColor = vec4(finalColor, texColor.a);
}
