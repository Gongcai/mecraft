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

void main() {
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

    // Catmull-Rom resampling of history
    vec2 historySize = uScreenSize;
    vec2 texelSize = 1.0 / historySize;
    vec2 texelHistoryUv = historyUv * historySize - 0.5;
    vec2 p0 = floor(texelHistoryUv);
    vec2 f = texelHistoryUv - p0;

    // Catmull-Rom weights
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 w0w3 = w0 + w3;

    vec2 uv0 = (p0 - 1.0 + 0.5 * w12 / w0w3) * texelSize;
    vec2 uv3 = (p0 + 2.0 + 0.5 * w12 / w0w3) * texelSize;
    vec2 uv12 = (p0 + 0.5 + w3 / w12) * texelSize;

    float weights[4];
    weights[0] = w0w3.x * w0.y;
    weights[1] = w0w3.x * w3.y;
    weights[2] = w12.x  * w0.y;
    weights[3] = w12.x  * w3.y;

    vec3 historyColor = vec3(0.0);
    historyColor += texture(uHistoryTex, vec2(uv0.x,  uv0.y)).rgb  * weights[0];
    historyColor += texture(uHistoryTex, vec2(uv0.x,  uv3.y)).rgb  * weights[1];
    historyColor += texture(uHistoryTex, vec2(uv12.x, uv0.y)).rgb  * weights[2];
    historyColor += texture(uHistoryTex, vec2(uv12.x, uv3.y)).rgb  * weights[3];

    // 3x3 neighborhood variance clipping in YCoCgR
    vec3 neighborMin = currentColor;
    vec3 neighborMax = currentColor;
    vec3 neighborSum = currentColor;
    float neighborCount = 1.0;

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            if (x == 0 && y == 0) continue;
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            vec3 neighbor = texture(uCurrentTex, vTexCoord + offset).rgb;
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

    // Blend factor: blend history with current
    float velocityLength = length(velocity) * max(uScreenSize.x, uScreenSize.y);
    float blendFactor = mix(uBlendMax, uBlendMin,
                            smoothstep(0.0, 16.0, velocityLength));

    // Reinhard domain blending for HDR stability
    vec3 currentReinhard = currentColor / (1.0 + currentColor);
    vec3 historyReinhard = historyColor / (1.0 + historyColor);
    vec3 resultReinhard = mix(historyReinhard, currentReinhard, blendFactor);
    vec3 result = resultReinhard / max(1.0 - resultReinhard, 1e-6);

    FragColor = vec4(max(result, vec3(0.0)), 1.0);
}
