// Entity shadow depth fragment shader — Mecraft Phase 5.7 enhanced.
// Writes humanoid/mob depth and color into CSM shadow map.
// Phase 5.7: skylight from CPU world light query replaces hardcoded 0.0.

#version 450 core

in vec2 vUV;
in vec3 vWorldPos;
in vec3 vNormal;

uniform sampler2D uTexture;
uniform float uEntitySunlight;

// Shadow color outputs (matching terrain shadow_depth.fs layout):
// layout 0 = shadowcolor0: RGB = albedo, A = 1.0 (opaque caster)
// layout 1 = shadowcolor1: RG = encoded normal, B = skylight, A = 1.0
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

    // Opaque caster: alpha = 1.0 marks hard shadow (not transparent)
    ShadowColor = vec4(texColor.rgb, 1.0);
    ShadowNormal = vec4(encodeNormal(normalize(vNormal)), uEntitySunlight, 1.0);
}
