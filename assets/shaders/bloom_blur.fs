#version 450 core

// DerivativeMain BlurH/BlurV: 9-tap symmetric binomial Gaussian.
// Weights are 8th-order binomial coefficients normalized to sum to 1:
//   [70, 56, 28, 8, 1] / 256

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uImage;
uniform vec2 uDirection;  // (1,0) for horizontal, (0,1) for vertical
uniform float uWeight;

const float w0 = 70.0 / 256.0;  // center
const float w1 = 56.0 / 256.0;  // +/-1
const float w2 = 28.0 / 256.0;  // +/-2
const float w3 =  8.0 / 256.0;  // +/-3
const float w4 =  1.0 / 256.0;  // +/-4

void main() {
    vec2 texel = uDirection / vec2(textureSize(uImage, 0));
    vec3 color = texture(uImage, vTexCoord).rgb * w0;
    color += (texture(uImage, vTexCoord + texel * 1.0).rgb +
              texture(uImage, vTexCoord - texel * 1.0).rgb) * w1;
    color += (texture(uImage, vTexCoord + texel * 2.0).rgb +
              texture(uImage, vTexCoord - texel * 2.0).rgb) * w2;
    color += (texture(uImage, vTexCoord + texel * 3.0).rgb +
              texture(uImage, vTexCoord - texel * 3.0).rgb) * w3;
    color += (texture(uImage, vTexCoord + texel * 4.0).rgb +
              texture(uImage, vTexCoord - texel * 4.0).rgb) * w4;
    FragColor = vec4(color * uWeight, 1.0);
}
