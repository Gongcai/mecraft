#version 450 core

layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform UiColorPushConstants {
    vec4 screenRect;
    vec4 rectRadius;
    vec4 color;
} pc;

void main() {
    FragColor = pc.color;
}
