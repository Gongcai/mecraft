#version 450 core
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vNormal;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler2D uTexture;
layout(push_constant) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uModel;
    vec4 uLighting;
};
void main() {
    vec4 texel = texture(uTexture, vUv);
    if (texel.a < 0.1) discard;
    float skyLight = uLighting.x * clamp(uLighting.z, 0.0, 1.0);
    float localLight = max(skyLight, uLighting.y);
    float ambient = mix(0.08, 0.55, localLight);
    float diffuse = max(dot(normalize(vNormal), normalize(vec3(0.3, 1.0, 0.5))), 0.0);
    float light = ambient + diffuse * skyLight * (1.0 - ambient);
    vec3 skinColor = mix(texel.rgb, vec3(1.0, 0.22, 0.22),
                         clamp(uLighting.w, 0.0, 1.0) * 0.70);
    fragColor = vec4(skinColor * light, texel.a);
}
