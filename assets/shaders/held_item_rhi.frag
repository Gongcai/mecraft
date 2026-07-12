#version 450 core
layout(location = 0) in vec2 vUv;
layout(location = 1) in float vShade;
layout(location = 2) in vec3 vNormal;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler2D uAtlas;
layout(push_constant) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uModel;
    vec4 uLighting;
};
void main() {
    vec4 texel = texture(uAtlas, vUv);
    if (texel.a < 0.1) {
        discard;
    }
    float skyLight = uLighting.x * clamp(uLighting.z, 0.0, 1.0);
    float localLight = max(skyLight, uLighting.y);
    float ambient = mix(0.08, 0.55, localLight);
    float diffuse = max(dot(normalize(vNormal), normalize(vec3(0.3, 1.0, 0.5))), 0.0);
    float light = ambient + diffuse * skyLight * (1.0 - ambient);
    fragColor = vec4(texel.rgb * clamp(vShade, 0.0, 1.0) * light * uLighting.w, texel.a);
}
