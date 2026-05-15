// Sky Spherical Harmonics (L1) for skylight evaluation.
//
// Ported from DerivativeMain lib/Atmosphere/Atmosphere.glsl (ToSH/FromSH)
// and world0/deferred5.vsh (25-direction SH accumulation).
//
// Usage:
//   SkySH sh = buildSkySH(skyCapture);
//   vec3 skylight = evaluateSkySH(sh, worldNormal);

#ifndef MECRAFT_SKY_SH_GLSL
#define MECRAFT_SKY_SH_GLSL

#include "render_contract.glsl"

// DerivativeMain lib/Atmosphere/Atmosphere.glsl:12-15
// Encodes a scalar radiance value + direction into L1 SH coefficients (per-channel).
vec4 ToSH(float value, vec3 dir) {
    // DerivativeMain: const vec2 foo = vec2(0.5 * PI * sqrt(rPI), 0.3849 * PI * sqrt(0.75 * rPI));
    const float shPI = 3.14159265359;
    const float shRPI = 0.31830988618;
    const vec2 foo = vec2(0.5 * shPI * sqrt(shRPI), 0.3849 * shPI * sqrt(0.75 * shRPI));
    return vec4(foo.x, foo.y * dir.yzx) * value;
}

// DerivativeMain lib/Atmosphere/Atmosphere.glsl:17-22
// Reconstructs radiance from L1 SH coefficients evaluated along lightDir.
vec3 FromSH(vec4 cR, vec4 cG, vec4 cB, vec3 lightDir) {
    // DerivativeMain: const vec2 foo = vec2(0.5 * sqrt(rPI), sqrt(0.75 * rPI));
    const vec2 foo = vec2(0.28209479177, 0.48860251190); // precomputed from DerivativeMain
    vec4 sh = vec4(foo.x, foo.y * lightDir.yzx);
    return vec3(dot(sh, cR), dot(sh, cG), dot(sh, cB));
}

// Per-channel L1 SH coefficients packed into a struct.
struct SkySH {
    vec4 R;
    vec4 G;
    vec4 B;
};

// Build sky SH by sampling 25 directions from the raw sky capture.
// Matches DerivativeMain world0/deferred5.vsh:46-69.
SkySH buildSkySH(sampler2D skyCapture) {
    SkySH sh;
    sh.R = vec4(0.0);
    sh.G = vec4(0.0);
    sh.B = vec4(0.0);

    for (uint i = 0u; i < 5u; ++i) {
        float latitude = float(i) * 0.62831853;
        float cosLat = cos(latitude), sinLat = sin(latitude);
        for (uint j = 0u; j < 5u; ++j) {
            float longitude = float(j) * 1.25663706;
            vec3 rayDir = vec3(cosLat * cos(longitude), sinLat, cosLat * sin(longitude));

            vec3 skyCol = sampleSkyRadiance(skyCapture, rayDir);

            sh.R += ToSH(skyCol.r, rayDir);
            sh.G += ToSH(skyCol.g, rayDir);
            sh.B += ToSH(skyCol.b, rayDir);
        }
    }

    sh.R /= 25.0;
    sh.G /= 25.0;
    sh.B /= 25.0;
    return sh;
}

// Evaluate sky SH for a given direction (surface normal).
vec3 evaluateSkySH(SkySH sh, vec3 dir) {
    return FromSH(sh.R, sh.G, sh.B, dir);
}

#endif
