#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform sampler2D uDepthTex;
uniform sampler2D uNormalAoTex;
uniform vec2 uScreenSize;
uniform float uNear;
uniform float uStepWidth;
uniform float uStrength;

float linearizeDepth(float depth) {
    return 2.0 * uNear / max(1.0 - depth, 1e-7);
}

vec3 decodeNormal(vec2 uv) {
    return normalize(texture(uNormalAoTex, uv).rgb * 2.0 - 1.0);
}

void main() {
    float centerDepth = texture(uDepthTex, vTexCoord).r;
    if (centerDepth >= 0.9999) {
        FragColor = vec4(0.0);
        return;
    }

    vec4 center = texture(uInputTex, vTexCoord);
    vec3 centerNormal = decodeNormal(vTexCoord);
    float centerLinearDepth = linearizeDepth(centerDepth);
    vec2 texelSize = 1.0 / max(uScreenSize, vec2(1.0));

    const float kernel[5] = float[](0.0625, 0.25, 0.375, 0.25, 0.0625);
    vec3 colorSum = vec3(0.0);
    float alphaSum = 0.0;
    float weightSum = 0.0;

    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 sampleUv = vTexCoord + vec2(float(x), float(y)) * texelSize * uStepWidth;
            if (sampleUv.x <= 0.0 || sampleUv.y <= 0.0 || sampleUv.x >= 1.0 || sampleUv.y >= 1.0) {
                continue;
            }

            float sampleDepth = texture(uDepthTex, sampleUv).r;
            if (sampleDepth >= 0.9999) {
                continue;
            }

            vec4 sampleValue = texture(uInputTex, sampleUv);
            float sampleLinearDepth = linearizeDepth(sampleDepth);
            float relDepthDiff = abs(sampleLinearDepth - centerLinearDepth) / max(centerLinearDepth, 0.1);
            float depthWeight = exp2(-relDepthDiff * 10.0);

            vec3 sampleNormal = decodeNormal(sampleUv);
            float normalWeight = pow(max(dot(sampleNormal, centerNormal), 0.0), 24.0);
            float confidenceWeight = mix(0.18, 1.0, clamp(sampleValue.a * 2.0, 0.0, 1.0));
            float spatialWeight = kernel[x + 2] * kernel[y + 2];
            float weight = spatialWeight * depthWeight * normalWeight * confidenceWeight;

            colorSum += sampleValue.rgb * weight;
            alphaSum += sampleValue.a * weight;
            weightSum += weight;
        }
    }

    vec4 filtered = weightSum > 1e-5
        ? vec4(colorSum / weightSum, alphaSum / weightSum)
        : center;
    FragColor = mix(center, filtered, clamp(uStrength, 0.0, 1.0));
}
