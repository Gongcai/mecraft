// Shared DerivativeMain sunlight / SSS / phase function library.
// Ported verbatim from DerivativeMain/lib/Lighting/SunLighting.glsl
// and DerivativeMain/lib/Atmosphere/Atmosphere.glsl.
//
// Scope: pure math functions that have NO sampler/uniform dependencies.
// Sampler-dependent functions (BlockerSearch, PercentageCloserFilter,
// ScreenSpaceShadow) remain as local implementations in consumer files
// due to fundamental differences between OptiFine's sampler2DShadow and
// Mecraft's sampler2D architecture. Consumer files must still match
// DerivativeMain's algorithm exactly — they just adapt the sampler calls.
//
// IMPORTANT: Do NOT approximate or "simplify" any formula here.
// DerivativeMain is the authoritative source.

#ifndef MECRAFT_DERIVATIVE_SUNLIGHT_GLSL
#define MECRAFT_DERIVATIVE_SUNLIGHT_GLSL

#include "derivative_brdf.glsl"

//----------------------------------------------------------------------------//
// Phase function — DerivativeMain/lib/Atmosphere/Atmosphere.glsl:29-32
//----------------------------------------------------------------------------//

// DerivativeMain Atmosphere.glsl:29-32
// Standard Henyey-Greenstein phase function (normalized).
float HenyeyGreensteinPhase(in float cosTheta, in float g) {
    float gg = sqr(g);
    float phase = 1.0 + gg - 2.0 * g * cosTheta;
    return oneMinus(gg) / (4.0 * PI * phase * sqrt(phase));
}

//----------------------------------------------------------------------------//
// Fake bounced light — DerivativeMain/lib/Lighting/SunLighting.glsl:168-174
//----------------------------------------------------------------------------//

// DerivativeMain SunLighting.glsl:168-174
// Simplified single-bounce sky light for surfaces facing away from the sun.
// worldLightVector: normalized sun direction in world space.
float CalculateFakeBouncedLight(in vec3 normal, in vec3 worldLightVector) {
    normal.y = -normal.y;
    vec3 bounceVector = normalize(worldLightVector + vec3(0.0, 1.0, 0.0));
    float bounce = saturate(dot(normal, bounceVector) * 0.4 + 0.6);

    return bounce * (2.0 - bounce) * 3e-2;
}

//----------------------------------------------------------------------------//
// Subsurface scattering — DerivativeMain/lib/Lighting/SunLighting.glsl:176-188
//----------------------------------------------------------------------------//

// DerivativeMain SunLighting.glsl:176-188
// Two-lobe HG subsurface scattering with absorption-based depth decay.
//   albedo:    surface diffuse color (linear)
//   sssAmount: SSS intensity (0..1+, typically from material SSS channel)
//   sssDepth:  depth difference in shadow map (from BlockerSearch.y)
//   LdotV:     dot(sunDir, viewDir), NOT clamped — can be negative for backscatter
vec3 CalculateSubsurfaceScattering(in vec3 albedo, in float sssAmount, in float sssDepth, in float LdotV) {
    vec3 coeff = albedo * inversesqrt(GetLuminance(albedo) + 1e-6);
    coeff = oneMinus(0.75 * saturate(coeff)) * (28.0 / sssAmount);

    vec3 subsurfaceScattering =  fastExp(0.375 * coeff * sssDepth) * HenyeyGreensteinPhase(-LdotV, 0.6);
         subsurfaceScattering += fastExp(0.125 * coeff * sssDepth) * (0.33 * HenyeyGreensteinPhase(-LdotV, 0.35) + 0.17 * rPI);

    return subsurfaceScattering * sssAmount * PI;
}

#endif
