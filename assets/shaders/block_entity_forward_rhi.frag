#version 450 core
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vLight;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler2D uTexture;
layout(std140, binding = 15) uniform RhiPushConstants {
    mat4 uViewProj;
    vec4 uLighting;
};
void main() {
    vec4 texel = texture(uTexture, vUv);
    if (texel.a < 0.1) discard;
    vec3 sunDirection = normalize(vec3(0.3, 1.0, 0.5));
    float diffuse = max(dot(normalize(vNormal), sunDirection), 0.0);
    float skyLight = vLight.x * clamp(uLighting.x, 0.0, 1.0);
    float localLight = max(skyLight, vLight.y);
    float ambient = mix(0.08, 0.55, localLight);
    float light = ambient + diffuse * skyLight * (1.0 - ambient);
    fragColor = vec4(texel.rgb * light, texel.a);
}
