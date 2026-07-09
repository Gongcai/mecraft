#version 450 core
// SSAO temporal reprojection. Reads velocity to reproject previous frame's AO,
// applies 3x3 neighborhood clamp and depth-based disocclusion rejection to
// prevent ghosting. Single-channel (R8) — no YCoCg needed.

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uCurrentTex;
layout(binding = 1) uniform sampler2D uHistoryTex;
layout(binding = 2) uniform sampler2D uVelocityTex;
layout(binding = 3) uniform sampler2D uDepthTex;

layout(std140, binding = 15) uniform RhiPushConstants {
    vec2 uScreenSize;
    float uHistoryWeight;
    float uNear;
};

void main() {
    ivec2 texel = ivec2(gl_FragCoord.xy);
    vec2 texelSize = 1.0 / max(uScreenSize, vec2(1.0));
    vec2 screenCoord = gl_FragCoord.xy * texelSize;

    float currentAo = texelFetch(uCurrentTex, texel, 0).r;

    // Reproject using velocity buffer
    vec2 velocity = texelFetch(uVelocityTex, texel, 0).rg;
    vec2 prevCoord = screenCoord - velocity;

    // Out-of-bounds history: use current frame
    if (prevCoord.x < 0.0 || prevCoord.x > 1.0 ||
        prevCoord.y < 0.0 || prevCoord.y > 1.0) {
        FragColor = vec4(currentAo, 0.0, 0.0, 1.0);
        return;
    }

    // Depth disocclusion: compare linearized current depth with linearized depth at
    // reprojected position. Linearization ensures perspective-correct thresholds that
    // don't drift with projection parameters.
    // linearZ = 2*near / (1 - ndc), same formula used in ssao_filter.fs.
    float currentDepth = texelFetch(uDepthTex, texel, 0).r;
    float historyDepth = texture(uDepthTex, prevCoord).r;
    float linCurrent = 2.0 * uNear / max(1.0 - currentDepth, 1e-7);
    float linHistory = 2.0 * uNear / max(1.0 - historyDepth, 1e-7);
    float relDepthDiff = abs(linCurrent - linHistory) / max(linCurrent, 0.1);
    float disocclusion = smoothstep(0.05, 0.5, relDepthDiff);

    // 3x3 neighborhood min/max clamp for scalar AO.
    // Prevents history from exceeding the local AO range → kills ghosting.
    ivec2 clampedSize = ivec2(uScreenSize) - 1;
    float minAo = currentAo;
    float maxAo = currentAo;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            if (x == 0 && y == 0) continue;
            ivec2 sampleTexel = clamp(texel + ivec2(x, y), ivec2(0), clampedSize);
            float sampleAo = texelFetch(uCurrentTex, sampleTexel, 0).r;
            minAo = min(minAo, sampleAo);
            maxAo = max(maxAo, sampleAo);
        }
    }

    // Sample and clamp history
    vec2 safeHistoryUv = clamp(prevCoord, texelSize * 0.5, 1.0 - texelSize * 0.5);
    float historyAo = texture(uHistoryTex, safeHistoryUv).r;
    historyAo = clamp(historyAo, minAo, maxAo);

    // Blend weight: base weight reduced by disocclusion and pixel motion magnitude.
    // Stationary pixels get full history weight; fast-moving or disoccluded pixels get less.
    float pixelMotion = length(velocity * uScreenSize);
    float motionFactor = exp(-pixelMotion * 0.25);
    float blendWeight = uHistoryWeight * (1.0 - disocclusion) * motionFactor;

    float result = mix(currentAo, historyAo, blendWeight);
    FragColor = vec4(result, 0.0, 0.0, 1.0);
}
