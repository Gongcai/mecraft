#version 450 core

layout(set = 0, binding = 0) uniform sampler2D uBackdrop;

layout(push_constant) uniform UiGlassPushConstants {
    vec4 screenRect;
    vec4 backdropSizeOpacity;
    vec4 tint;
    vec4 appearance;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = gl_FragCoord.xy / max(pc.appearance.xy, vec2(1.0));
    vec3 backdrop = texture(uBackdrop, uv).rgb;
    float luma = dot(backdrop, vec3(0.299, 0.587, 0.114));
    vec3 softened = mix(vec3(luma), backdrop, pc.appearance.z) * pc.appearance.w;
    vec3 tinted = mix(softened, pc.tint.rgb, clamp(pc.tint.a, 0.0, 1.0));
    outColor = vec4(tinted, clamp(pc.backdropSizeOpacity.w, 0.0, 1.0));
}
