#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uCurrentTex;
uniform sampler2D uHistoryTex;
uniform sampler2D uVelocityTex;
uniform sampler2D uDepthTex;
uniform sampler2D uNormalAoTex;
uniform sampler2D uHistoryDepthTex;
uniform vec2 uScreenSize;
uniform float uHistoryWeight;
uniform float uNear;

float linearizeDepth(float depth) {
    return 2.0 * uNear / max(1.0 - depth, 1e-7);
}

vec3 decodeNormal(vec3 packedNormal) {
    return normalize(packedNormal * 2.0 - 1.0);
}

bool invalidVec2(vec2 v) {
    return any(isnan(v)) || any(isinf(v));
}

void main() {
    ivec2 texel = ivec2(gl_FragCoord.xy);
    ivec2 maxTexel = ivec2(uScreenSize) - 1;
    vec2 texelSize = 1.0 / max(uScreenSize, vec2(1.0));
    vec2 screenCoord = gl_FragCoord.xy * texelSize;

    vec4 current = texelFetch(uCurrentTex, texel, 0);
    float depth = texelFetch(uDepthTex, texel, 0).r;
    if (depth >= 0.9999) {
        FragColor = current;
        return;
    }

    vec2 velocity = texelFetch(uVelocityTex, texel, 0).rg;
    vec2 prevCoord = screenCoord - velocity;
    if (invalidVec2(velocity) || invalidVec2(prevCoord) ||
        any(greaterThan(abs(velocity), vec2(1.0))) ||
        prevCoord.x < 0.0 || prevCoord.x > 1.0 ||
        prevCoord.y < 0.0 || prevCoord.y > 1.0) {
        FragColor = current;
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
}
