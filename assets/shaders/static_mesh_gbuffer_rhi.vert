#version 450 core

#include "rhi_screen_coordinates.glsl"

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aUv;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vTangent;
layout(location = 3) out float vTangentSign;
layout(location = 4) out vec2 vVelocity;

layout(push_constant) uniform StaticMeshPushConstants {
    mat4 uModelViewProj;
    mat4 uPreviousModelViewProj;
};

void main() {
    gl_Position = uModelViewProj * vec4(aPosition, 1.0);
    vec4 previousClip = uPreviousModelViewProj * vec4(aPosition, 1.0);
    vec2 currentNdc = gl_Position.xy / max(gl_Position.w, 0.00001);
    vec2 previousNdc = previousClip.xy / max(previousClip.w, 0.00001);
    vec2 currentClipUv = currentNdc * 0.5 + 0.5;
    vec2 previousClipUv = previousNdc * 0.5 + 0.5;

    vUv = aUv;
    vNormal = normalize(aNormal);
    vTangent = normalize(aTangent.xyz);
    vTangentSign = aTangent.w;
    vVelocity = rhiScreenUvToTextureUv(rhiScreenUvToClipUv(currentClipUv)) -
                rhiScreenUvToTextureUv(rhiScreenUvToClipUv(previousClipUv));
}
