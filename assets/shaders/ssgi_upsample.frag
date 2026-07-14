#version 450 core

#include "rhi_screen_coordinates.glsl"

layout(location = 0) in vec2 vScreenUv;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uSsgiHalfResTex;
layout(binding = 1) uniform sampler2D uDepthTex;

layout(push_constant) uniform RhiPushConstants {
    vec2 uHalfResSize;
    float uNear;
    float uPadding0;
};

float linearizeDepth(float depth) {
    return 2.0 * uNear / max(1.0 - depth, 1e-7);
}

void main() {
    ivec2 fullTexel = ivec2(gl_FragCoord.xy);
    float centerDepth = texelFetch(uDepthTex, fullTexel, 0).r;
    if (centerDepth >= 0.9999) {
        FragColor = vec4(0.0);
        return;
    }

    float linCenter = linearizeDepth(centerDepth);
    vec2 halfCoord = vScreenUv * uHalfResSize - 0.5;
    ivec2 halfBase = ivec2(floor(halfCoord));
    vec2 f = fract(halfCoord);
    ivec2 maxHalf = ivec2(uHalfResSize) - 1;
    vec2 halfTexelSize = 1.0 / max(uHalfResSize, vec2(1.0));

    ivec2 tc00 = clamp(halfBase + ivec2(0, 0), ivec2(0), maxHalf);
    ivec2 tc10 = clamp(halfBase + ivec2(1, 0), ivec2(0), maxHalf);
    ivec2 tc01 = clamp(halfBase + ivec2(0, 1), ivec2(0), maxHalf);
    ivec2 tc11 = clamp(halfBase + ivec2(1, 1), ivec2(0), maxHalf);

    ivec2 halfExtent = ivec2(uHalfResSize);
    ivec2 nt00 = rhiScreenUvToNativeTexel((vec2(tc00) + 0.5) * halfTexelSize, halfExtent);
    ivec2 nt10 = rhiScreenUvToNativeTexel((vec2(tc10) + 0.5) * halfTexelSize, halfExtent);
    ivec2 nt01 = rhiScreenUvToNativeTexel((vec2(tc01) + 0.5) * halfTexelSize, halfExtent);
    ivec2 nt11 = rhiScreenUvToNativeTexel((vec2(tc11) + 0.5) * halfTexelSize, halfExtent);
    vec4 gi00 = texelFetch(uSsgiHalfResTex, nt00, 0);
    vec4 gi10 = texelFetch(uSsgiHalfResTex, nt10, 0);
    vec4 gi01 = texelFetch(uSsgiHalfResTex, nt01, 0);
    vec4 gi11 = texelFetch(uSsgiHalfResTex, nt11, 0);

    vec2 uv00 = (vec2(tc00) + 0.5) * halfTexelSize;
    vec2 uv10 = (vec2(tc10) + 0.5) * halfTexelSize;
    vec2 uv01 = (vec2(tc01) + 0.5) * halfTexelSize;
    vec2 uv11 = (vec2(tc11) + 0.5) * halfTexelSize;

    float d00 = linearizeDepth(texture(uDepthTex, rhiScreenUvToTextureUv(uv00)).r);
    float d10 = linearizeDepth(texture(uDepthTex, rhiScreenUvToTextureUv(uv10)).r);
    float d01 = linearizeDepth(texture(uDepthTex, rhiScreenUvToTextureUv(uv01)).r);
    float d11 = linearizeDepth(texture(uDepthTex, rhiScreenUvToTextureUv(uv11)).r);

    float w00 = (1.0 - f.x) * (1.0 - f.y) * exp(-abs(d00 - linCenter) / max(linCenter, 0.1) * 8.0);
    float w10 = f.x * (1.0 - f.y) * exp(-abs(d10 - linCenter) / max(linCenter, 0.1) * 8.0);
    float w01 = (1.0 - f.x) * f.y * exp(-abs(d01 - linCenter) / max(linCenter, 0.1) * 8.0);
    float w11 = f.x * f.y * exp(-abs(d11 - linCenter) / max(linCenter, 0.1) * 8.0);

    float totalWeight = max(w00 + w10 + w01 + w11, 1e-6);
    FragColor = (gi00 * w00 + gi10 * w10 + gi01 * w01 + gi11 * w11) / totalWeight;
}
