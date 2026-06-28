// Instanced block entity shadow fragment shader.
// Writes the same shadow attachments as entity_shadow.frag.

#version 450 core

in vec2 vUV;
in vec3 vWorldPos;
in vec3 vNormal;
in float vEntitySunlight;

uniform sampler2D uTexture;

layout(location = 0) out vec4 ShadowColor;
layout(location = 1) out vec4 ShadowNormal;

vec2 encodeNormal(vec3 n) {
    n = normalize(n);
    n /= (abs(n.x) + abs(n.y) + abs(n.z) + 1e-6);
    vec2 enc = n.xy;
    if (n.z < 0.0) {
        enc = (vec2(1.0) - abs(enc.yx)) * vec2(enc.x >= 0.0 ? 1.0 : -1.0,
                                               enc.y >= 0.0 ? 1.0 : -1.0);
    }
    return enc * 0.5 + 0.5;
}

void main() {
    vec4 texColor = texture(uTexture, vUV);
    if (texColor.a < 0.1) {
        discard;
    }

    ShadowColor = vec4(texColor.rgb, 1.0);
    ShadowNormal = vec4(encodeNormal(normalize(vNormal)), vEntitySunlight, 1.0);
}
