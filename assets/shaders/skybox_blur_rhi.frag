#version 450 core
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler2D uTexture;
layout(push_constant) uniform RhiPushConstants {
    vec4 uDirection;
};
void main() {
    const float weights[7] = float[](0.1964, 0.1742, 0.1222, 0.0678,
                                     0.0298, 0.0104, 0.0029);
    vec3 result = texture(uTexture, vTexCoord).rgb * weights[0];
    for (int i = 1; i < 7; ++i) {
        vec2 offset = uDirection.xy * float(i) * 2.0;
        result += texture(uTexture, vTexCoord + offset).rgb * weights[i];
        result += texture(uTexture, vTexCoord - offset).rgb * weights[i];
    }
    fragColor = vec4(result, 1.0);
}
