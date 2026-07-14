#version 450 core
// Depth-aware bilinear upsample from half-resolution SSAO to full resolution.
// Weights each of the 4 half-res texels by depth similarity to the full-res center pixel,
// preserving edges at depth discontinuities.

#include "rhi_screen_coordinates.glsl"

layout(location = 0) in vec2 vScreenUv;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uSsaoHalfResTex;
layout(binding = 1) uniform sampler2D uDepthTex;

layout(push_constant) uniform RhiPushConstants {
    vec2 uHalfResSize;
    float uNear;
    float uPadding0;
};

void main() {
    ivec2 fullTexel = ivec2(gl_FragCoord.xy);
    float centerDepth = texelFetch(uDepthTex, fullTexel, 0).r;
    float linCenter = 2.0 * uNear / max(1.0 - centerDepth, 1e-7);

    // Map full-res UV to half-res texel space
    vec2 halfCoord = vScreenUv * uHalfResSize - 0.5;
    ivec2 halfBase = ivec2(floor(halfCoord));
    vec2 f = fract(halfCoord);

    // Bilinear weights
    vec2 w0 = (1.0 - f);
    vec2 w1 = f;

    // Sample 4 half-res texels and their depths (via full-res depth at corresponding positions)
    vec2 halfTexelSize = 1.0 / uHalfResSize;

    ivec2 halfExtent = ivec2(uHalfResSize);
    ivec2 halfMaxTexel = halfExtent - 1;
    ivec2 screenTexel00 = clamp(halfBase + ivec2(0, 0), ivec2(0), halfMaxTexel);
    ivec2 screenTexel10 = clamp(halfBase + ivec2(1, 0), ivec2(0), halfMaxTexel);
    ivec2 screenTexel01 = clamp(halfBase + ivec2(0, 1), ivec2(0), halfMaxTexel);
    ivec2 screenTexel11 = clamp(halfBase + ivec2(1, 1), ivec2(0), halfMaxTexel);
    ivec2 nativeTexel00 = rhiScreenUvToNativeTexel((vec2(screenTexel00) + 0.5) * halfTexelSize, halfExtent);
    ivec2 nativeTexel10 = rhiScreenUvToNativeTexel((vec2(screenTexel10) + 0.5) * halfTexelSize, halfExtent);
    ivec2 nativeTexel01 = rhiScreenUvToNativeTexel((vec2(screenTexel01) + 0.5) * halfTexelSize, halfExtent);
    ivec2 nativeTexel11 = rhiScreenUvToNativeTexel((vec2(screenTexel11) + 0.5) * halfTexelSize, halfExtent);
    float ao00 = texelFetch(uSsaoHalfResTex, nativeTexel00, 0).r;
    float ao10 = texelFetch(uSsaoHalfResTex, nativeTexel10, 0).r;
    float ao01 = texelFetch(uSsaoHalfResTex, nativeTexel01, 0).r;
    float ao11 = texelFetch(uSsaoHalfResTex, nativeTexel11, 0).r;

    // Depth at each half-res sample position (using full-res depth texture)
    vec2 uv00 = (vec2(screenTexel00) + 0.5) * halfTexelSize;
    vec2 uv10 = (vec2(screenTexel10) + 0.5) * halfTexelSize;
    vec2 uv01 = (vec2(screenTexel01) + 0.5) * halfTexelSize;
    vec2 uv11 = (vec2(screenTexel11) + 0.5) * halfTexelSize;

    float d00 = 2.0 * uNear / max(1.0 - texture(uDepthTex, rhiScreenUvToTextureUv(uv00)).r, 1e-7);
    float d10 = 2.0 * uNear / max(1.0 - texture(uDepthTex, rhiScreenUvToTextureUv(uv10)).r, 1e-7);
    float d01 = 2.0 * uNear / max(1.0 - texture(uDepthTex, rhiScreenUvToTextureUv(uv01)).r, 1e-7);
    float d11 = 2.0 * uNear / max(1.0 - texture(uDepthTex, rhiScreenUvToTextureUv(uv11)).r, 1e-7);

    // Depth-aware weights: reject texels at significantly different depths
    float dw00 = exp(-abs(d00 - linCenter) / max(linCenter, 0.1) * 8.0);
    float dw10 = exp(-abs(d10 - linCenter) / max(linCenter, 0.1) * 8.0);
    float dw01 = exp(-abs(d01 - linCenter) / max(linCenter, 0.1) * 8.0);
    float dw11 = exp(-abs(d11 - linCenter) / max(linCenter, 0.1) * 8.0);

    // Combine bilinear + depth weights
    float w00 = w0.x * w0.y * dw00;
    float w10 = w1.x * w0.y * dw10;
    float w01 = w0.x * w1.y * dw01;
    float w11 = w1.x * w1.y * dw11;

    float totalWeight = w00 + w10 + w01 + w11;
    float ao = (ao00 * w00 + ao10 * w10 + ao01 * w01 + ao11 * w11) / max(totalWeight, 1e-6);

    FragColor = vec4(ao, 0.0, 0.0, 1.0);
}
