// Dedicated vertex shader for deferred lighting.
// Extends the fullscreen triangle with per-frame Sky SH computation.
// SkySH is built once (3 vertex invocations) and passed via flat interpolation,
// avoiding 25 sky texture samples per pixel in the fragment shader.

#version 450 core
#include "sky_sh.glsl"
#include "rhi_screen_coordinates.glsl"

layout(binding = 9) uniform sampler2D uSkyCaptureTex;

layout(location = 0) out vec2 vScreenUv;
layout(location = 1) out vec2 vClipUv;
layout(location = 2) flat out vec4 vSkySH_R;
layout(location = 3) flat out vec4 vSkySH_G;
layout(location = 4) flat out vec4 vSkySH_B;

void main() {
    const vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    vec2 pos = positions[gl_VertexIndex];
    vClipUv = pos * 0.5 + 0.5;
    vScreenUv = rhiScreenUvToClipUv(vClipUv);
    gl_Position = vec4(pos, 0.0, 1.0);

    // Build sky SH coefficients once per frame (3 vertex invocations total).
    // flat interpolation guarantees all fragments receive the provoking vertex's value.
    SkySH skySH = buildSkySH(uSkyCaptureTex);
    vSkySH_R = skySH.R;
    vSkySH_G = skySH.G;
    vSkySH_B = skySH.B;
}
