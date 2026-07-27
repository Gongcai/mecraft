#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 3) in vec2 aUv;
layout(location = 0) out vec2 vUv;

layout(push_constant) uniform StaticMeshPushConstants {
    mat4 uModelViewProj;
};

void main() {
    gl_Position = uModelViewProj * vec4(aPosition, 1.0);
    vUv = aUv;
}
