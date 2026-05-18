#version 450 core

in vec2 vTexCoord;
out vec2 FragVelocity;

uniform sampler2D uDepthTex;
uniform mat4 uInvViewProj;
uniform mat4 uPreviousViewProj;
uniform vec2 uScreenSize;

// DerivativeMain: 3x3 neighborhood offsets (excluding center)
const ivec2 offset3x3N[8] = ivec2[8](
    ivec2(-1, -1), ivec2(0, -1), ivec2(1, -1),
    ivec2(-1,  0),                ivec2(1,  0),
    ivec2(-1,  1), ivec2(0,  1), ivec2(1,  1)
);

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    return world.xyz / max(world.w, 0.00001);
}

void main() {
    ivec2 texel = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(uDepthTex, texel, 0).r;

    // DerivativeMain-style closest fragment: search 3x3 neighborhood for the
    // nearest depth texel. This stabilizes velocity at depth discontinuities.
    vec3 closestFragment = vec3(texel, depth);
    for (int i = 0; i < 8; ++i) {
        ivec2 sampleTexel = offset3x3N[i] + texel;
        float sampleDepth = texelFetch(uDepthTex, clamp(sampleTexel, ivec2(0), ivec2(uScreenSize) - 1), 0).r;
        if (sampleDepth < closestFragment.z) {
            closestFragment = vec3(sampleTexel, sampleDepth);
        }
    }

    // DerivativeMain: no sky early return. depth=1 (sky/far-plane) gets
    // a valid far-plane reprojection velocity. This is critical for TAA
    // to properly accumulate VFog dither; sky pixels must track camera
    // rotation, not stay pinned to screen space.
    // DerivativeMain/program/Post/Temporal.frag::GetClosestFragment returns
    // closestFragment.xy *= screenPixelSize, without a half-texel offset.
    vec2 closestUv = closestFragment.xy / uScreenSize;
    vec3 worldPos = reconstructWorldPosition(closestUv, closestFragment.z);
    vec4 previousClip = uPreviousViewProj * vec4(worldPos, 1.0);
    vec2 previousUv = previousClip.xy / max(previousClip.w, 0.00001) * 0.5 + 0.5;

    // DerivativeMain raw reprojection: Reproject() uses raw projection
    // matrices and does not manually subtract current/previous jitter.
    FragVelocity = closestUv - previousUv;
}
