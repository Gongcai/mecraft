#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uCurrentTex;
uniform sampler2D uHistoryTex;
uniform sampler2D uVelocityTex;
uniform sampler2D uDepthTex;
uniform sampler2D uHistoryDepthTex;

uniform mat4 uInvViewProj;
uniform mat4 uPreviousViewProj;
uniform vec2 uScreenSize;
uniform vec2 uJitter;
uniform vec2 uPreviousJitter;
uniform int uFrameIndex;
uniform float uBlendMin;
uniform float uBlendMax;

const float kPi = 3.14159265359;

vec3 rgbToYCoCgR(vec3 c) {
    float y  =  0.25 * c.r + 0.5 * c.g + 0.25 * c.b;
    float co =  0.5 * c.r - 0.5 * c.b;
    float cg = -0.25 * c.r + 0.5 * c.g - 0.25 * c.b;
    return vec3(y, co, cg);
}

vec3 yCoCgRToRgb(vec3 c) {
    float r = c.x + c.y - c.z;
    float g = c.x + c.z;
    float b = c.x - c.y - c.z;
    return vec3(r, g, b);
}

vec3 clipAABB(vec3 aabbMin, vec3 aabbMax, vec3 color) {
    vec3 center = 0.5 * (aabbMax + aabbMin);
    vec3 extents = 0.5 * (aabbMax - aabbMin);
    vec3 offset = color - center;
    vec3 ts = mix(vec3(1e6), extents / max(abs(offset), 1e-6), step(vec3(1e-6), abs(extents)));
    float t = min(min(ts.x, ts.y), ts.z);
    t = clamp(t, 0.0, 1.0);
    return center + offset * t;
}

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    return world.xyz / max(world.w, 0.00001);
}

float reprojectedPreviousDepth(vec2 uv, float depth) {
    vec3 worldPos = reconstructWorldPosition(uv, depth);
    vec4 previousClip = uPreviousViewProj * vec4(worldPos, 1.0);
    vec3 previousNdc = previousClip.xyz / max(previousClip.w, 0.00001);
    return previousNdc.z * 0.5 + 0.5;
}

vec3 sampleCurrentClamped(vec2 uv) {
    vec2 texelSize = 1.0 / max(uScreenSize, vec2(1.0));
    return texture(uCurrentTex, clamp(uv, texelSize * 0.5, 1.0 - texelSize * 0.5)).rgb;
}

void main() {
    vec2 texelSize = 1.0 / max(uScreenSize, vec2(1.0));
    vec2 velocity = texture(uVelocityTex, vTexCoord).rg;
    vec2 historyUv = vTexCoord - velocity;

    float depth = texture(uDepthTex, vTexCoord).r;

    // If sky pixel, just output current frame
    if (depth >= 0.9999) {
        FragColor = texture(uCurrentTex, vTexCoord);
        return;
    }

    vec3 currentColor = texture(uCurrentTex, vTexCoord).rgb;

    // Reject history if out of bounds
    bool validHistory = historyUv.x >= 0.0 && historyUv.x <= 1.0 &&
                        historyUv.y >= 0.0 && historyUv.y <= 1.0;

    if (!validHistory) {
        FragColor = vec4(currentColor, 1.0);
        return;
    }

    float historyDepth = texture(uHistoryDepthTex, historyUv).r;
    float expectedHistoryDepth = reprojectedPreviousDepth(vTexCoord, depth);
    bool depthMatches = historyDepth < 0.9999 &&
                        abs(historyDepth - expectedHistoryDepth) < max(0.0015, depth * 0.0025);

    float velocityPixels = length(velocity * uScreenSize);
    bool saneVelocity = all(lessThan(abs(velocity), vec2(0.35))) && velocityPixels < 160.0;

    if (!depthMatches || !saneVelocity) {
        FragColor = vec4(currentColor, 1.0);
        return;
    }

    vec2 safeHistoryUv = clamp(historyUv, texelSize * 0.5, 1.0 - texelSize * 0.5);
    vec3 historyColor = texture(uHistoryTex, safeHistoryUv).rgb;

    // 3x3 neighborhood variance clipping in YCoCgR
    vec3 neighborMin = currentColor;
    vec3 neighborMax = currentColor;
    vec3 neighborSum = currentColor;
    float neighborCount = 1.0;

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            if (x == 0 && y == 0) continue;
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            vec3 neighbor = sampleCurrentClamped(vTexCoord + offset);
            neighborMin = min(neighborMin, neighbor);
            neighborMax = max(neighborMax, neighbor);
            neighborSum += neighbor;
            neighborCount += 1.0;
        }
    }

    // Convert to YCoCgR for clipping
    vec3 avgYCoCgR = rgbToYCoCgR(neighborSum / neighborCount);
    vec3 minYCoCgR = rgbToYCoCgR(neighborMin);
    vec3 maxYCoCgR = rgbToYCoCgR(neighborMax);

    // Expand AABB slightly
    vec3 boxSize = maxYCoCgR - minYCoCgR;
    minYCoCgR -= boxSize * 0.125;
    maxYCoCgR += boxSize * 0.125;

    // Clip history to AABB
    vec3 historyYCoCgR = rgbToYCoCgR(historyColor);
    historyYCoCgR = clipAABB(minYCoCgR, maxYCoCgR, historyYCoCgR);
    historyColor = yCoCgRToRgb(historyYCoCgR);

    // Blend factor: blend history with current. Moving/reprojected pixels need
    // more current-frame weight; otherwise old shadow silhouettes smear during turns.
    float velocityLength = length(velocity) * max(uScreenSize.x, uScreenSize.y);
    float motionBlendMax = max(uBlendMax, 0.72);
    float blendFactor = mix(uBlendMin, motionBlendMax,
                            smoothstep(0.35, 28.0, velocityLength));

    // Reinhard domain blending for HDR stability
    vec3 currentReinhard = currentColor / (1.0 + currentColor);
    vec3 historyReinhard = historyColor / (1.0 + historyColor);
    float disocclusion = smoothstep(0.0015, 0.012, abs(historyDepth - expectedHistoryDepth));
    blendFactor = max(blendFactor, disocclusion);
    float colorDelta = length(currentReinhard - historyReinhard);
    float reactiveBlend = smoothstep(0.018, 0.16, colorDelta);
    blendFactor = max(blendFactor, reactiveBlend * 0.82);
    vec3 resultReinhard = mix(historyReinhard, currentReinhard, blendFactor);
    vec3 result = resultReinhard / max(1.0 - resultReinhard, 1e-6);

    FragColor = vec4(max(result, vec3(0.0)), 1.0);
}
