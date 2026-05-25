#version 450 core
// Volumetric fog temporal resolve. Reads velocity to reproject previous frame's
// temporally-resolved volumetric fog, applies 3x3 neighborhood clamp and
// depth-based disocclusion rejection to prevent ghosting.
// Operates on RGBA16F half-resolution data:
//   rgb = volumetric scattering
//   a   = transmittance

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uCurrentTex;
uniform sampler2D uHistoryTex;
uniform sampler2D uVelocityTex;
uniform sampler2D uDepthTex;
uniform sampler2D uHistoryDepthTex;

uniform vec2 uScreenSize; // Half-resolution viewport size
uniform float uHistoryWeight;
uniform float uNearPlane;
uniform float uFarPlane;

float viewDistanceFromDepth(float depth) {
    if (depth >= 0.9999) {
        return 1e6;
    }
    return (uNearPlane * uFarPlane) / (depth * (uNearPlane - uFarPlane) + uFarPlane);
}

void main() {
    ivec2 texel = ivec2(gl_FragCoord.xy);
    vec2 texelSize = 1.0 / max(uScreenSize, vec2(1.0));
    vec2 screenCoord = gl_FragCoord.xy * texelSize;

    vec4 current = texelFetch(uCurrentTex, texel, 0);

    // Reproject using full-resolution velocity texture
    vec2 velocity = texture(uVelocityTex, screenCoord).rg;
    vec2 prevCoord = screenCoord - velocity;

    // Out-of-bounds history: use current frame
    if (prevCoord.x < 0.0 || prevCoord.x > 1.0 ||
        prevCoord.y < 0.0 || prevCoord.y > 1.0) {
        FragColor = current;
        return;
    }

    // Depth disocclusion: compare linearized current depth with linearized depth at prev frame's reprojected position
    float currentDepth = texture(uDepthTex, screenCoord).r;
    float historyDepth = texture(uHistoryDepthTex, prevCoord).r;
    float linCurrent = viewDistanceFromDepth(currentDepth);
    float linHistory = viewDistanceFromDepth(historyDepth);
    float relDepthDiff = abs(linCurrent - linHistory) / max(linCurrent, 0.1);
    float disocclusion = smoothstep(0.05, 0.5, relDepthDiff);

    // 3x3 neighborhood min/max clamp for RGB and Alpha to prevent ghosting
    ivec2 clampedSize = textureSize(uCurrentTex, 0) - 1;
    vec3 minColor = current.rgb;
    vec3 maxColor = current.rgb;
    float minAlpha = current.a;
    float maxAlpha = current.a;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            if (x == 0 && y == 0) continue;
            ivec2 sampleTexel = clamp(texel + ivec2(x, y), clampedSize * 0 + ivec2(0), clampedSize);
            vec4 s = texelFetch(uCurrentTex, sampleTexel, 0);
            minColor = min(minColor, s.rgb);
            maxColor = max(maxColor, s.rgb);
            minAlpha = min(minAlpha, s.a);
            maxAlpha = max(maxAlpha, s.a);
        }
    }

    // Sample and clamp history
    vec2 safeHistoryUv = clamp(prevCoord, texelSize * 0.5, 1.0 - texelSize * 0.5);
    vec4 history = texture(uHistoryTex, safeHistoryUv);
    history.rgb = clamp(history.rgb, minColor, maxColor);
    history.a = clamp(history.a, minAlpha, maxAlpha);

    // Blend weight: base weight reduced by disocclusion
    float blendWeight = uHistoryWeight * (1.0 - disocclusion);

    vec4 result = mix(current, history, blendWeight);
    FragColor = vec4(max(result.rgb, vec3(0.0)), result.a);
}
