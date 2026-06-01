#version 450 core
#extension GL_ARB_shader_draw_parameters : require
layout (location = 0) in vec3 aLegacyPos;
layout (location = 1) in vec2 aLegacyUV;
layout (location = 2) in float aLegacyNormal;
layout (location = 3) in float aLegacySunlight;
layout (location = 4) in float aLegacyBlockLight;
layout (location = 5) in float aLegacyAO;
layout (location = 6) in float aLegacyLayer;
layout (location = 7) in float aLegacyAnimationFrameCount;
layout (location = 8) in float aLegacyAnimationFps;
layout (location = 9) in float aLegacyAnimated;
layout (location = 10) in uint aLegacyTintPacked;
layout (location = 11) in uint aPackedPos;
layout (location = 12) in uint aPackedUV;
layout (location = 13) in uint aPackedLightAoLayer;
layout (location = 14) in uint aPackedTintAnim;
#include "terrain_vertex_decode.glsl"

uniform mat4 view;
uniform mat4 viewProj;
uniform mat4 model;
uniform int uUseModel;
uniform int uVertexFormat;

out vec2 vUV;
out float vLight;
out float vSunlight;
out float vBlockLight;
out float vAO;
out float vNormal;
out float vLayer;
out float vAnimationFrameCount;
out float vAnimationFps;
out float vAnimated;
out float vFogDist;
out vec3 vWorldPos;
flat out float vTintKind;
flat out float vMaterialKind;
out vec2 vTintUV;

void main() {
    TerrainVertexDecoded vertex = (uVertexFormat == 1)
        ? decodeTerrainPackedVertex(aPackedPos, aPackedUV, aPackedLightAoLayer, aPackedTintAnim, gl_BaseInstanceARB)
        : decodeLegacyBlockVertex(aLegacyPos, aLegacyUV, aLegacyNormal, aLegacySunlight, aLegacyBlockLight,
                                  aLegacyAO, aLegacyLayer, aLegacyAnimationFrameCount, aLegacyAnimationFps,
                                  aLegacyAnimated, aLegacyTintPacked);
    vec4 localPos = vec4(vertex.pos, 1.0);
    vec4 worldPos = (uUseModel != 0) ? model * localPos : localPos;
    vec4 viewPos = view * worldPos;
    gl_Position = viewProj * worldPos;

    vUV = vertex.uv;

    // Raw light levels normalized to [0,1] are passed through directly.
    // The fragment shader will use them as lightmap UV coordinates.
    vSunlight = vertex.sunlight;
    vBlockLight = vertex.blockLight;
    vLight = max(vertex.sunlight, vertex.blockLight);

    vAO = vertex.ao;
    vNormal = vertex.normal;
    vLayer = vertex.layer;
    vAnimationFrameCount = vertex.animationFrameCount;
    vAnimationFps = vertex.animationFps;
    vAnimated = vertex.animated;
    vFogDist = max(0.0, -viewPos.z);
    vWorldPos = worldPos.xyz;

    uint tintKind = (vertex.tintPacked >> 14u) & 3u;
    uint materialKind = (vertex.tintPacked >> 8u) & 63u;
    uint tintU = (vertex.tintPacked >> 4u) & 15u;
    uint tintV = vertex.tintPacked & 15u;
    vTintKind = float(tintKind);
    vMaterialKind = float(materialKind);
    vTintUV = (vec2(float(tintU), float(tintV)) + vec2(0.5)) / 16.0;
}
