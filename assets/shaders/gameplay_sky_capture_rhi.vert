#version 450 core
#include "rhi_screen_coordinates.glsl"
layout(location = 0) out vec3 vWorldDir;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec4 vColor;
void main() {
    vec2 position = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 screenUv = rhiScreenUvToClipUv(position);
    vUV = rhiScreenUvToTextureUv(screenUv);
    vWorldDir = vec3(0.0, 1.0, 0.0);
    vColor = vec4(1.0);
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
