#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform sampler2D uDepthTex;
uniform sampler2D uNormalAoTex;
uniform sampler2D uMomentsTex;
uniform vec2 uScreenSize;
uniform float uNear;
uniform float uStepWidth;
uniform float uStrength;
uniform int uMomentsAvailable;

float linearizeDepth(float depth) {
    return 2.0 * uNear / max(1.0 - depth, 1e-7);
}

vec3 decodeNormal(vec2 uv) {
    return normalize(texture(uNormalAoTex, uv).rgb * 2.0 - 1.0);
}

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

float estimateLocalNoise(ivec2 centerTexel, ivec2 maxTexel, float centerLuminance) {
    float luminanceSum = 0.0;
    float luminanceSqSum = 0.0;
    float alphaSum = 0.0;
    float sampleCount = 0.0;

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 sampleTexel = clamp(centerTexel + ivec2(x, y), ivec2(0), maxTexel);
            vec4 sampleValue = texelFetch(uInputTex, sampleTexel, 0);
            float sampleLuminance = luminance(max(sampleValue.rgb, vec3(0.0)));
            luminanceSum += sampleLuminance;
            luminanceSqSum += sampleLuminance * sampleLuminance;
            alphaSum += sampleValue.a;
            sampleCount += 1.0;
        }
    }

    float meanLuminance = luminanceSum / max(sampleCount, 1.0);
    float variance = max(luminanceSqSum / max(sampleCount, 1.0) - meanLuminance * meanLuminance, 0.0);
    float relativeSigma = sqrt(variance) / max(max(centerLuminance, meanLuminance) + 0.035, 0.035);
    float meanConfidence = smoothstep(0.04, 0.45, alphaSum / max(sampleCount, 1.0));
    return clamp(max(relativeSigma, 1.0 - meanConfidence), 0.0, 1.0);
}

float estimateTemporalNoise(vec2 uv, float centerLuminance) {
    if (uMomentsAvailable == 0) {
        return 0.0;
    }

    vec4 moments = texture(uMomentsTex, uv);
    float variance = max(max(moments.y - moments.x * moments.x, 0.0), moments.w);
    float relativeSigma = sqrt(variance) / max(centerLuminance + 0.035, 0.035);
    float shortHistory = 1.0 - smoothstep(1.0, 16.0, moments.z);
    return clamp(max(relativeSigma, shortHistory), 0.0, 1.0);
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
    float centerLuminance = luminance(max(center.rgb, vec3(0.0)));
    vec2 texelSize = 1.0 / max(uScreenSize, vec2(1.0));
    ivec2 centerTexel = ivec2(gl_FragCoord.xy);
    ivec2 maxTexel = ivec2(uScreenSize) - 1;
    float centerConfidence = smoothstep(0.04, 0.45, center.a);
    float localNoise = max(estimateLocalNoise(centerTexel, maxTexel, centerLuminance),
                           estimateTemporalNoise(vTexCoord, centerLuminance));

    const float kernel[5] = float[](0.0625, 0.25, 0.375, 0.25, 0.0625);
    vec3 colorSum = center.rgb * 0.45;
    float alphaSum = center.a * 0.45;
    float weightSum = 0.45;

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
            float sampleLuminance = luminance(max(sampleValue.rgb, vec3(0.0)));
            float luminanceScale = 0.035 + max(centerLuminance, sampleLuminance) *
                                   mix(0.25, 0.70, localNoise);
            float colorWeight = exp2(-abs(sampleLuminance - centerLuminance) /
                                     max(luminanceScale, 1e-4));
            float confidenceWeight = mix(0.18, 1.0, clamp(sampleValue.a * 2.0, 0.0, 1.0));
            float spatialWeight = kernel[x + 2] * kernel[y + 2];
            float weight = spatialWeight * depthWeight * normalWeight * colorWeight * confidenceWeight;

            colorSum += sampleValue.rgb * weight;
            alphaSum += sampleValue.a * weight;
            weightSum += weight;
        }
    }

    vec4 filtered = weightSum > 1e-5
        ? vec4(colorSum / weightSum, alphaSum / weightSum)
        : center;
    float filterAmount = clamp(uStrength, 0.0, 1.0) * mix(0.45, 1.0, max(centerConfidence, localNoise));
    FragColor = mix(center, filtered, filterAmount);
}
