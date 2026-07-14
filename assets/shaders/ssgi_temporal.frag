#version 450 core

#include "rhi_screen_coordinates.glsl"

layout(location = 0) in vec2 vScreenUv;
layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 MomentsColor;

layout(binding = 0) uniform sampler2D uCurrentTex;
layout(binding = 1) uniform sampler2D uHistoryTex;
layout(binding = 2) uniform sampler2D uVelocityTex;
layout(binding = 3) uniform sampler2D uDepthTex;
layout(binding = 4) uniform sampler2D uNormalAoTex;
layout(binding = 5) uniform sampler2D uHistoryDepthTex;
layout(binding = 6) uniform sampler2D uHistoryMomentsTex;

layout(push_constant) uniform RhiPushConstants {
    vec2 uScreenSize;
    float uHistoryWeight;
    float uNear;
};

float linearizeDepth(float depth) {
    return 2.0 * uNear / max(1.0 - depth, 1e-7);
}

vec3 decodeNormal(vec3 packedNormal) {
    return normalize(packedNormal * 2.0 - 1.0);
}

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

bool invalidVec2(vec2 v) {
    return any(isnan(v)) || any(isinf(v));
}

void writeCurrent(vec4 current) {
    float currentLuminance = luminance(max(current.rgb, vec3(0.0)));
    FragColor = current;
    MomentsColor = vec4(currentLuminance, currentLuminance * currentLuminance, 1.0, 0.0);
}

void main() {
    ivec2 texel = ivec2(gl_FragCoord.xy);
    ivec2 maxTexel = ivec2(uScreenSize) - 1;
    vec2 texelSize = 1.0 / max(uScreenSize, vec2(1.0));
    vec2 textureUv = rhiScreenUvToTextureUv(vScreenUv);

    vec4 current = texelFetch(uCurrentTex, texel, 0);
    float depth = texelFetch(uDepthTex, texel, 0).r;
    if (depth >= 0.9999) {
        FragColor = current;
        MomentsColor = vec4(0.0);
        return;
    }

    vec2 velocity = texelFetch(uVelocityTex, texel, 0).rg;
    vec2 prevCoord = textureUv - velocity;
    if (invalidVec2(velocity) || invalidVec2(prevCoord) ||
        any(greaterThan(abs(velocity), vec2(1.0))) ||
        prevCoord.x < 0.0 || prevCoord.x > 1.0 ||
        prevCoord.y < 0.0 || prevCoord.y > 1.0) {
        writeCurrent(current);
        return;
    }

    vec3 minColor = current.rgb;
    vec3 maxColor = current.rgb;
    vec3 sumColor = vec3(0.0);
    vec3 sumColorSq = vec3(0.0);
    float sampleCount = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 sampleTexel = clamp(texel + ivec2(x, y), ivec2(0), maxTexel);
            vec3 c = texelFetch(uCurrentTex, sampleTexel, 0).rgb;
            minColor = min(minColor, c);
            maxColor = max(maxColor, c);
            sumColor += c;
            sumColorSq += c * c;
            sampleCount += 1.0;
        }
    }

    vec2 safeHistoryUv = clamp(prevCoord, texelSize * 0.5, 1.0 - texelSize * 0.5);
    vec4 history = texture(uHistoryTex, safeHistoryUv);
    vec4 historyMoments = texture(uHistoryMomentsTex, safeHistoryUv);
    vec3 meanColor = sumColor / max(sampleCount, 1.0);
    vec3 variance = max(sumColorSq / max(sampleCount, 1.0) - meanColor * meanColor, vec3(0.0));
    vec3 sigma = sqrt(variance);
    float currentConfidence = smoothstep(0.02, 0.20, current.a);
    if (currentConfidence > 0.05) {
        float sigmaScale = mix(3.0, 1.8, currentConfidence);
        vec3 clipMin = max(minColor, meanColor - sigma * sigmaScale);
        vec3 clipMax = min(maxColor, meanColor + sigma * sigmaScale);
        history.rgb = clamp(history.rgb, clipMin, clipMax);
    }

    float linCurrent = linearizeDepth(depth);
    float linHistory = linearizeDepth(texture(uHistoryDepthTex, prevCoord).r);
    float relDepthDiff = abs(linCurrent - linHistory) / max(linCurrent, 0.1);
    float depthDisocclusion = smoothstep(0.035, 0.28, relDepthDiff);
    vec3 currentNormal = decodeNormal(texelFetch(uNormalAoTex, texel, 0).rgb);
    vec3 historyNormal = decodeNormal(texture(uNormalAoTex, safeHistoryUv).rgb);
    float normalDisocclusion = 1.0 - smoothstep(0.55, 0.95, max(dot(currentNormal, historyNormal), 0.0));
    float disocclusion = max(depthDisocclusion, normalDisocclusion);
    float pixelMotion = length(velocity * uScreenSize);
    float motionFactor = exp(-pixelMotion * 0.25);
    float confidence = mix(0.65, 1.0, max(currentConfidence, smoothstep(0.02, 0.20, history.a)));
    float blendWeight = clamp(uHistoryWeight, 0.0, 0.98) * (1.0 - disocclusion) * motionFactor * confidence;

    vec3 color = mix(current.rgb, history.rgb, blendWeight);
    float alpha = mix(current.a, history.a, blendWeight);
    FragColor = vec4(max(color, vec3(0.0)), alpha);

    float currentLuminance = luminance(max(current.rgb, vec3(0.0)));
    float currentSecondMoment = currentLuminance * currentLuminance;
    float momentWeight = clamp(blendWeight, 0.0, 0.98);
    float firstMoment = mix(currentLuminance, historyMoments.x, momentWeight);
    float secondMoment = mix(currentSecondMoment, historyMoments.y, momentWeight);
    float historyFrames = min(historyMoments.z + 1.0, 32.0);
    float historyContribution = smoothstep(0.02, 0.45, blendWeight);
    float accumulatedFrames = mix(1.0, historyFrames, historyContribution);
    float momentVariance = max(secondMoment - firstMoment * firstMoment, 0.0);
    MomentsColor = vec4(firstMoment, secondMoment, accumulatedFrames, momentVariance);
}
