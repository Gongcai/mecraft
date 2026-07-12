#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUv;
layout(location = 2) in float aShade;
layout(location = 3) in vec3 aNormal;
layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vVelocity;
layout(push_constant) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uPreviousViewProj;
    mat4 uModel;
    mat4 uPreviousModel;
    vec4 uLight;
};
void main() {
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);
    gl_Position = uViewProj * worldPosition;
    vec4 previousClip = uPreviousViewProj * uPreviousModel * vec4(aPosition, 1.0);
    vec2 currentNdc = gl_Position.xy / max(gl_Position.w, 0.00001);
    vec2 previousNdc = previousClip.xy / max(previousClip.w, 0.00001);
    vUv = aUv;
    vNormal = normalize(mat3(uModel) * aNormal);
    vVelocity = (currentNdc - previousNdc) * 0.5;
}
