#version 330 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uTexture;
uniform vec2 uDirection; // (1/w, 0) for horizontal, (0, 1/h) for vertical

void main() {
    // 13-tap Gaussian kernel, sigma ≈ 5.0
    float weights[7] = float[](0.1964, 0.1742, 0.1222, 0.0678, 0.0298, 0.0104, 0.0029);

    vec3 result = texture(uTexture, vTexCoord).rgb * weights[0];
    for (int i = 1; i < 7; ++i) {
        vec2 offset = uDirection * float(i) * 2.0;
        result += texture(uTexture, vTexCoord + offset).rgb * weights[i];
        result += texture(uTexture, vTexCoord - offset).rgb * weights[i];
    }
    FragColor = vec4(result, 1.0);
}
