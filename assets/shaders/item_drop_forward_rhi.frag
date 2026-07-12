#version 450 core
layout(location = 0) in vec2 vUv;
layout(location = 1) in float vShade;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler2D uAtlas;
layout(push_constant) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uModel;
    vec4 uLighting;
};
void main() {
    vec4 texel = texture(uAtlas, vUv);
    if (texel.a < 0.1) discard;
    float skyLight = uLighting.x * clamp(uLighting.z, 0.0, 1.0);
    float light = mix(0.08, 1.0, max(skyLight, uLighting.y));
    fragColor = vec4(texel.rgb * vShade * light, texel.a);
}
