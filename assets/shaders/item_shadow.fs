// Item drop shadow depth fragment shader — Mecraft Phase 5.3 extension.
// Writes dropped non-block items into the CSM shadow map.
// Outputs hard shadow (alpha = 1.0) with white albedo (items have no meaningful
// colored shadow contribution).

#version 450 core

in vec2 vUV;

uniform sampler2D uAtlas;

layout(location = 0) out vec4 ShadowColor;
layout(location = 1) out vec4 ShadowNormal;

void main() {
    vec4 texColor = texture(uAtlas, vUV);
    if (texColor.a < 0.1) {
        discard;
    }

    // Opaque caster: white shadow color, upward normal, full skylight.
    ShadowColor = vec4(texColor.rgb, 1.0);
    ShadowNormal = vec4(0.5, 0.5, 1.0, 1.0);
}
