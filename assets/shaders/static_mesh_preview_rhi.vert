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

layout(push_constant) uniform StaticMeshPreviewPushConstants {
    mat4 uViewProj;
    mat4 uModel;
};

void main() {
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);
    mat3 normalMatrix = transpose(inverse(mat3(uModel)));
    vUv = aUv;
    vNormal = normalize(normalMatrix * aNormal);
    vTangent = normalize(mat3(uModel) * aTangent.xyz);
    vTangentSign = aTangent.w;
    vWorldPosition = worldPosition.xyz;
    gl_Position = uViewProj * worldPosition;
}
