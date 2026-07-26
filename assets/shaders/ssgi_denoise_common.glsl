#include "gbuffer_contract.glsl"
#include "rhi_screen_coordinates.glsl"

layout(location = 0) in vec2 vScreenUv;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uInputTex;
layout(binding = 1) uniform sampler2D uDepthTex;
layout(binding = 2) uniform sampler2D uNormalAoTex;
#ifdef MECRAFT_SSGI_DENOISE_MOMENTS
layout(binding = 3) uniform sampler2D uMomentsTex;
#endif

layout(push_constant) uniform RhiPushConstants {
    vec4 pScreenNearStep;
    vec4 pStrength;
};

#define uScreenSize pScreenNearStep.xy
#define uNear pScreenNearStep.z
#define uStepWidth pScreenNearStep.w
#define uStrength pStrength.x

float linearizeDepth(float depth) {
    return 2.0 * uNear / max(1.0 - depth, 1e-7);
}

vec3 decodeNormal(vec2 uv) {
    return unpackGBufferNormal(texture(uNormalAoTex, uv));
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

#ifdef MECRAFT_SSGI_DENOISE_MOMENTS
float estimateTemporalNoise(vec2 uv, float centerLuminance) {
    vec4 moments = texture(uMomentsTex, uv);
    float variance = max(max(moments.y - moments.x * moments.x, 0.0), moments.w);
    float relativeSigma = sqrt(variance) / max(centerLuminance + 0.035, 0.035);
    float shortHistory = 1.0 - smoothstep(1.0, 16.0, moments.z);
    return clamp(max(relativeSigma, shortHistory), 0.0, 1.0);
}
#endif

void main() {
    vec2 textureUv = rhiScreenUvToTextureUv(vScreenUv);
    float centerDepth = texture(uDepthTex, textureUv).r;
    if (centerDepth >= 0.9999) {
        FragColor = vec4(0.0);
        return;
    }

    vec4 center = texture(uInputTex, textureUv);
    vec3 centerNormal = decodeNormal(textureUv);
    float centerLinearDepth = linearizeDepth(centerDepth);
    float centerLuminance = luminance(max(center.rgb, vec3(0.0)));
    vec2 texelSize = 1.0 / max(uScreenSize, vec2(1.0));
    ivec2 centerTexel = ivec2(gl_FragCoord.xy);
    ivec2 maxTexel = ivec2(uScreenSize) - 1;
    float centerConfidence = smoothstep(0.04, 0.45, center.a);
    float localNoise = estimateLocalNoise(centerTexel, maxTexel, centerLuminance);
#ifdef MECRAFT_SSGI_DENOISE_MOMENTS
    localNoise = max(localNoise, estimateTemporalNoise(textureUv, centerLuminance));
#endif

    const float kernel[5] = float[](0.0625, 0.25, 0.375, 0.25, 0.0625);
    vec3 colorSum = center.rgb * 0.45;
    float alphaSum = center.a * 0.45;
    float weightSum = 0.45;

    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 sampleUv = textureUv + vec2(float(x), float(y)) * texelSize * uStepWidth;
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
