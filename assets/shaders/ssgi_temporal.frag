#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uCurrentTex;
uniform sampler2D uHistoryTex;
uniform sampler2D uVelocityTex;
uniform sampler2D uDepthTex;
uniform vec2 uScreenSize;
uniform float uHistoryWeight;
uniform float uNear;

float linearizeDepth(float depth) {
    return 2.0 * uNear / max(1.0 - depth, 1e-7);
}

void main() {
    ivec2 texel = ivec2(gl_FragCoord.xy);
    ivec2 maxTexel = ivec2(uScreenSize) - 1;
    vec2 texelSize = 1.0 / max(uScreenSize, vec2(1.0));
    vec2 screenCoord = gl_FragCoord.xy * texelSize;

    vec4 current = texelFetch(uCurrentTex, texel, 0);
    float depth = texelFetch(uDepthTex, texel, 0).r;
    if (depth >= 0.9999 || current.a <= 1e-4) {
        FragColor = current;
        return;
    }

    vec2 velocity = texelFetch(uVelocityTex, texel, 0).rg;
    vec2 prevCoord = screenCoord - velocity;
    if (prevCoord.x < 0.0 || prevCoord.x > 1.0 ||
        prevCoord.y < 0.0 || prevCoord.y > 1.0) {
        FragColor = current;
        return;
    }

    vec3 minColor = current.rgb;
    vec3 maxColor = current.rgb;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 sampleTexel = clamp(texel + ivec2(x, y), ivec2(0), maxTexel);
            vec3 c = texelFetch(uCurrentTex, sampleTexel, 0).rgb;
            minColor = min(minColor, c);
            maxColor = max(maxColor, c);
        }
    }

    vec2 safeHistoryUv = clamp(prevCoord, texelSize * 0.5, 1.0 - texelSize * 0.5);
    vec4 history = texture(uHistoryTex, safeHistoryUv);
    history.rgb = clamp(history.rgb, minColor, maxColor);

    float linCurrent = linearizeDepth(depth);
    float linHistory = linearizeDepth(texture(uDepthTex, prevCoord).r);
    float relDepthDiff = abs(linCurrent - linHistory) / max(linCurrent, 0.1);
    float disocclusion = smoothstep(0.05, 0.5, relDepthDiff);
    float pixelMotion = length(velocity * uScreenSize);
    float motionFactor = exp(-pixelMotion * 0.25);
    float confidence = clamp(min(current.a, history.a), 0.0, 1.0);
    float blendWeight = clamp(uHistoryWeight, 0.0, 0.98) * (1.0 - disocclusion) * motionFactor * confidence;

    vec3 color = mix(current.rgb, history.rgb, blendWeight);
    FragColor = vec4(max(color, vec3(0.0)), current.a);
}
