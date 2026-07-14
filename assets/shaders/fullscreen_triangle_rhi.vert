#version 450 core

#include "rhi_screen_coordinates.glsl"

layout(location = 0) out vec2 vScreenUv;
layout(location = 1) out vec2 vClipUv;

void main() {
    const vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    vec2 pos = positions[gl_VertexIndex];
    vClipUv = pos * 0.5 + 0.5;
    vScreenUv = rhiScreenUvToClipUv(vClipUv);
    gl_Position = vec4(pos, 0.0, 1.0);
}
