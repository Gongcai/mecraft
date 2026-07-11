#version 450 core

layout(location = 0) in vec2 vLocal;
layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform UiCapsulePushConstants {
    vec4 screenRect;
    vec4 rectRadius;
    vec4 color;
} pc;

void main() {
    vec2 halfSize = pc.rectRadius.xy * 0.5;
    float radius = pc.rectRadius.z;
    vec2 q = abs(vLocal - halfSize) - (halfSize - vec2(radius));
    float distanceToEdge = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - radius;
    float coverage = 1.0 - smoothstep(-0.75, 0.75, distanceToEdge);
    if (coverage <= 0.0) discard;
    FragColor = vec4(pc.color.rgb, pc.color.a * coverage);
}
