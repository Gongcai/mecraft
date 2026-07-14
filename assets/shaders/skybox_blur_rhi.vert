#version 450 core

#include "rhi_screen_coordinates.glsl"

layout(location = 0) out vec2 vScreenUv;
layout(location = 1) out vec2 vClipUv;
void main() {
    float x = -1.0 + float((gl_VertexIndex & 1) << 2);
    float y = -1.0 + float((gl_VertexIndex & 2) << 1);
    vClipUv = vec2(x * 0.5 + 0.5, y * 0.5 + 0.5);
    vScreenUv = rhiScreenUvToClipUv(vClipUv);
    gl_Position = vec4(x, y, 0.0, 1.0);
}
