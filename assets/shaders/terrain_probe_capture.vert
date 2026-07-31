#version 450 core
#extension GL_ARB_shader_draw_parameters : require

layout(location = 11) in uint aPackedPos;
layout(location = 12) in uint aPackedUV;
layout(location = 13) in uint aPackedLightAoLayer;
layout(location = 14) in uint aPackedTintAnim;

#include "terrain_vertex_decode.glsl"
#include "terrain_probe_capture_params.glsl"

layout(location = 0) out vec2 vUV;
layout(location = 1) out float vSunlight;
layout(location = 2) out float vBlockLight;
layout(location = 3) out float vAO;
layout(location = 4) out float vNormal;
layout(location = 5) out float vLayer;
layout(location = 6) out float vAnimationFrameCount;
layout(location = 7) out float vAnimationFps;
layout(location = 8) out float vAnimated;
layout(location = 9) flat out float vTintKind;
layout(location = 10) flat out float vMaterialKind;
layout(location = 11) out vec2 vTintUV;
layout(location = 12) out vec3 vWorldPos;

void main() {
    TerrainVertexDecoded vertex = decodeTerrainPackedVertex(
        aPackedPos,
        aPackedUV,
        aPackedLightAoLayer,
        aPackedTintAnim,
        gl_BaseInstanceARB);
    vec4 worldPosition = vec4(vertex.pos, 1.0);
    gl_Position = uProbeViewProjection * worldPosition;

    vUV = vertex.uv;
    vSunlight = vertex.sunlight;
    vBlockLight = vertex.blockLight;
    vAO = vertex.ao;
    vNormal = vertex.normal;
    vLayer = vertex.layer;
    vAnimationFrameCount = vertex.animationFrameCount;
    vAnimationFps = vertex.animationFps;
    vAnimated = vertex.animated;
    vWorldPos = worldPosition.xyz;

    uint tintKind = (vertex.tintPacked >> 14u) & 3u;
    uint materialKind = (vertex.tintPacked >> 8u) & 63u;
    uint tintU = (vertex.tintPacked >> 4u) & 15u;
    uint tintV = vertex.tintPacked & 15u;
    vTintKind = float(tintKind);
    vMaterialKind = float(materialKind);
    vTintUV = (vec2(float(tintU), float(tintV)) + vec2(0.5)) / 16.0;
}
