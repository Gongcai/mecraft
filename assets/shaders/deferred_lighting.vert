// Dedicated vertex shader for deferred lighting.
// Extends the fullscreen triangle with per-frame Sky SH computation.
// SkySH is built once (3 vertex invocations) and passed via flat interpolation,
// avoiding 25 sky texture samples per pixel in the fragment shader.

#version 450 core
#include "sky_sh.glsl"

uniform sampler2D uSkyCaptureTex;

out vec2 vTexCoord;
flat out vec4 vSkySH_R;
flat out vec4 vSkySH_G;
flat out vec4 vSkySH_B;

void main() {
    const vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    vec2 pos = positions[gl_VertexID];
    vTexCoord = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);

    // Build sky SH coefficients once per frame (3 vertex invocations total).
    // flat interpolation guarantees all fragments receive the provoking vertex's value.
    SkySH skySH = buildSkySH(uSkyCaptureTex);
    vSkySH_R = skySH.R;
    vSkySH_G = skySH.G;
    vSkySH_B = skySH.B;
}
