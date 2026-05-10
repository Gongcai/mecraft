#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneTex;
uniform float uThreshold;

void main() {
    vec3 color = texture(uSceneTex, vTexCoord).rgb;
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float peak = max(max(color.r, color.g), color.b);
    float brightness = max(luma, peak * 0.72);
    float softKnee = max(uThreshold * 0.55, 0.001);
    float excess = max(brightness - uThreshold + softKnee, 0.0);
    float soft = (excess * excess) / max(4.0 * softKnee, 0.001);
    float contribution = max(brightness - uThreshold, soft);
    float mask = clamp(contribution / max(brightness, 0.001), 0.0, 1.0);
    FragColor = vec4(color * mask, 1.0);
}
