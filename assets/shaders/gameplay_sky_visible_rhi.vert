#version 450 core
layout(location = 0) out vec3 vWorldDir;
layout(push_constant) uniform RhiPushConstants {
    mat4 uProjection;
    mat4 uView;
    vec4 uSkyTopHaze;
    vec4 uSkyHorizonGlare;
    vec4 uSunDirectionVisibility;
    vec4 uMoonDirectionVisibility;
    vec4 uSunScatterNight;
    vec4 uMoonLightPhase;
};
void main() {
    vec2 position = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 clip = position * 2.0 - 1.0;
    vec4 viewNear = inverse(uProjection) * vec4(clip, 1.0, 1.0);
    vec3 viewDir = viewNear.xyz / viewNear.w;
    vWorldDir = mat3(transpose(uView)) * viewDir;
    gl_Position = vec4(clip, 0.0, 1.0);
}
