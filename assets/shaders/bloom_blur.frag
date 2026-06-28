#version 450 core

// DerivativeMain/program/Post/BlurH.glsl and BlurV.glsl:
// 9-tap symmetric binomial Gaussian using integer texel taps.
// Mecraft adaptation: each bloom tile is a separate texture, so edge taps are
// clamped to the texture bounds instead of crossing atlas tile borders.
// Weights are 8th-order binomial coefficients normalized to sum to 1:
//   [70, 56, 28, 8, 1] / 256

in vec2 vTexCoord;
out vec4 FragColor;

layout(binding = 0) uniform sampler2D uImage;
layout(location = 0) uniform vec2 uDirection;  // (1,0) for horizontal, (0,1) for vertical
layout(location = 1) uniform float uWeight;

const float w0 = 70.0 / 256.0;  // center
const float w1 = 56.0 / 256.0;  // +/-1
const float w2 = 28.0 / 256.0;  // +/-2
const float w3 =  8.0 / 256.0;  // +/-3
const float w4 =  1.0 / 256.0;  // +/-4

vec3 FetchTap(ivec2 texel, ivec2 offset) {
    ivec2 maxTexel = textureSize(uImage, 0) - ivec2(1);
    return texelFetch(uImage, clamp(texel + offset, ivec2(0), maxTexel), 0).rgb;
}

void main() {
    ivec2 texel = ivec2(gl_FragCoord.xy);
    ivec2 direction = ivec2(round(uDirection));
    vec3 color = FetchTap(texel, direction * 0) * w0;
    color += (FetchTap(texel, direction * 1) + FetchTap(texel, direction * -1)) * w1;
    color += (FetchTap(texel, direction * 2) + FetchTap(texel, direction * -2)) * w2;
    color += (FetchTap(texel, direction * 3) + FetchTap(texel, direction * -3)) * w3;
    color += (FetchTap(texel, direction * 4) + FetchTap(texel, direction * -4)) * w4;
    FragColor = vec4(color * uWeight, 1.0);
}
