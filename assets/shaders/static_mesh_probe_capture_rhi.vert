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

layout(push_constant) uniform StaticMeshProbeCapturePushConstants {
    mat4 uModel;
};
layout(set = 1, binding = 0, std140) uniform ProbeCaptureFrameParams {
    mat4 uProbeViewProjection;
    vec4 uProbePosition;
    vec4 uSunDirection;
    vec4 uSunColor;
    vec4 uAmbientColor;
    uvec4 uLightCount;
};

void main() {
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);
    mat3 modelLinear = mat3(uModel);
    vUv = aUv;
    vNormal = normalize(transpose(inverse(modelLinear)) * aNormal);
    vTangent = normalize(modelLinear * aTangent.xyz);
    vTangentSign = aTangent.w;
    vWorldPosition = worldPosition.xyz;
    gl_Position = uProbeViewProjection * worldPosition;
}
