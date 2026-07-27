#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aUv;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vTangent;
layout(location = 3) out float vTangentSign;
layout(location = 4) out vec3 vWorldPosition;

layout(push_constant) uniform StaticMeshTransparentPushConstants {
    mat4 uModel;
};
layout(std140, binding = 6) uniform StaticMeshFrameParams {
    vec4 uVoxelLight;
    mat4 uViewProj;
    mat4 uPreviousViewProj;
    vec4 uCameraPosition;
    vec4 uSunDirection;
    vec4 uSunColor;
    vec4 uAmbientColor;
    vec4 uFogColor;
    vec4 uFogParams;
};

void main() {
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);
    mat3 modelLinear = mat3(uModel);
    vUv = aUv;
    vNormal = normalize(transpose(inverse(modelLinear)) * aNormal);
    vTangent = normalize(modelLinear * aTangent.xyz);
    vTangentSign = aTangent.w;
    vWorldPosition = worldPosition.xyz;
    gl_Position = uViewProj * worldPosition;
}
