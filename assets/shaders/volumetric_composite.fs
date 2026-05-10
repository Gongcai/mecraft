#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneTex;
uniform sampler2D uVolumetricTex;
uniform sampler2D uDepthTex;
uniform vec2 uInvFullResolution;

vec4 sampleDepthAwareVolumetric(vec2 uv) {
    float centerDepth = texture(uDepthTex, uv).r;
    vec2 halfResStep = uInvFullResolution * 2.0;
    vec2 offsets[5] = vec2[](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(-1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(0.0, -1.0)
    );
    float spatialWeights[5] = float[](1.0, 0.55, 0.55, 0.55, 0.55);

    vec4 sum = vec4(0.0);
    float weightSum = 0.0;
    for (int i = 0; i < 5; ++i) {
        vec2 sampleUv = clamp(uv + offsets[i] * halfResStep, vec2(0.0), vec2(1.0));
        float sampleDepth = texture(uDepthTex, sampleUv).r;
        float depthDelta = abs(sampleDepth - centerDepth);
        float depthWeight = exp(-depthDelta * 320.0);
        if (centerDepth >= 0.9999 && sampleDepth < 0.9999) {
            depthWeight *= 0.08;
        }
        float weight = spatialWeights[i] * max(depthWeight, 0.025);
        sum += texture(uVolumetricTex, sampleUv) * weight;
        weightSum += weight;
    }
    return sum / max(weightSum, 0.0001);
}

void main() {
    vec3 scene = texture(uSceneTex, vTexCoord).rgb;
    vec4 volumetric = sampleDepthAwareVolumetric(vTexCoord);
    FragColor = vec4(scene * volumetric.a + volumetric.rgb, 1.0);
}
