#version 450 core

layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform CrosshairPushConstants {
    vec4 screenAndOffset;
    vec4 color;
} pc;

void main() {
    FragColor = pc.color;
}
