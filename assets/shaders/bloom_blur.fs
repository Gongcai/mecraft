#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uImage;
uniform vec2 uDirection;
uniform float uWeight;

void main() {
    vec2 texel = 1.0 / vec2(textureSize(uImage, 0));
    vec3 color = texture(uImage, vTexCoord).rgb * 0.227027;
    color += texture(uImage, vTexCoord + uDirection * texel * 1.384615).rgb * 0.316216;
    color += texture(uImage, vTexCoord - uDirection * texel * 1.384615).rgb * 0.316216;
    color += texture(uImage, vTexCoord + uDirection * texel * 3.230769).rgb * 0.070270;
    color += texture(uImage, vTexCoord - uDirection * texel * 3.230769).rgb * 0.070270;
    FragColor = vec4(color * uWeight, 1.0);
}
