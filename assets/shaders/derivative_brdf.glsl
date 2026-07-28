// Shared DerivativeMain BRDF functions.
// Ported verbatim from DerivativeMain/lib/Surface/BRDF.glsl.
//
// IMPORTANT: Do NOT approximate or "simplify" any formula here.
// DerivativeMain is the authoritative source.
//
// Every function is annotated with its DerivativeMain origin.

#ifndef MECRAFT_DERIVATIVE_BRDF_GLSL
#define MECRAFT_DERIVATIVE_BRDF_GLSL

#include "derivative_shadow.glsl"
#include "pbr_brdf.glsl"

//----------------------------------------------------------------------------//
// Fresnel — DerivativeMain/lib/Surface/BRDF.glsl:2-36
//----------------------------------------------------------------------------//

// DerivativeMain BRDF.glsl:5-8
// Air-to-dielectric Schlick Fresnel approximation.
float FresnelSchlick(in float cosTheta, in float f0) {
    return pbrFresnelSchlick(cosTheta, vec3(f0), 1.0).x;
}

// DerivativeMain BRDF.glsl:11-24
// Exact dielectric Fresnel (air-to-dielectric, parameterized by F0 reflectance).
// Converts f0 to IOR internally, then applies the exact Fresnel equations.
float FresnelDielectric(in float cosTheta, in float f0) {
    return pbrFresnelDielectric(cosTheta, f0);
}

// DerivativeMain BRDF.glsl:26-36
// Exact dielectric Fresnel parameterized by refractive index (IOR) directly.
float FresnelDielectricN(in float cosTheta, in float n) {
    return pbrFresnelDielectricFromIor(cosTheta, n);
}

//----------------------------------------------------------------------------//
// Smith GGX — DerivativeMain/lib/Surface/BRDF.glsl:38-48
//----------------------------------------------------------------------------//

// DerivativeMain BRDF.glsl:40-42
float V1SmithGGXInverse(in float cosTheta, in float alpha2) {
    return pbrSmithGgxV1Inverse(cosTheta, alpha2);
}

// DerivativeMain BRDF.glsl:44-48
// Height-correlated Smith GGX visibility function (G2).
float V2SmithGGX(in float NdotV, in float NdotL, in float alpha2) {
    return pbrSmithGgxCorrelatedVisibility(NdotV, NdotL, alpha2);
}

//----------------------------------------------------------------------------//
// GGX NDF — DerivativeMain/lib/Surface/BRDF.glsl:50-52
//----------------------------------------------------------------------------//

// DerivativeMain BRDF.glsl:50-52
float DistributionGGX(in float NdotH, in float alpha2) {
    return pbrDistributionGgx(NdotH, alpha2);
}

//----------------------------------------------------------------------------//
// Diffuse BRDF — DerivativeMain/lib/Surface/BRDF.glsl:54-67
//----------------------------------------------------------------------------//

// DerivativeMain BRDF.glsl:54-67
// Hammon diffuse BRDF with smooth-to-rough transition and multi-scattering term.
vec3 DiffuseHammon(in float LdotV, in float NdotV, in float NdotL, in float NdotH, in float roughness, in vec3 albedo) {
    if (NdotL < 1e-6) return vec3(0.0);
    float facing = max0(LdotV) * 0.5 + 0.5;

    float singleSmooth = 1.05 * oneMinus(pow5(1.0 - max(NdotL, 1e-2))) * oneMinus(pow5(1.0 - max(NdotV, 1e-2)));
    // Mecraft adaptation: alpha-tested foliage exposes Hammon's rough diffuse
    // half-vector singularity as a sun-aligned halo. DerivativeMain runs this
    // after its full material/shadow contract; clamp the reciprocal floor here
    // so near-opposite light/view vectors cannot create HDR rings.
    float safeNdotH = max(NdotH, 0.08);
    float singleRough = facing * (0.45 - 0.2 * facing) * (rcp(safeNdotH) + 2.0);

    float single = mix(singleSmooth, singleRough, roughness) * rPI;
    float multi = 0.1159 * roughness;

    return (multi * albedo + single) * NdotL;
}

//----------------------------------------------------------------------------//
// Specular BRDF — DerivativeMain/lib/Surface/BRDF.glsl:69-78
//----------------------------------------------------------------------------//

// DerivativeMain BRDF.glsl:69-78
// Full PBR specular: Schlick Fresnel + GGX NDF + height-correlated Smith G2.
float SpecularBRDF(in float LdotH, in float NdotV, in float NdotL, in float NdotH, in float alpha2, in float f0) {
    return pbrEvaluateDirectSpecular(
        LdotH, NdotV, NdotL, NdotH, alpha2, vec3(f0), 1.0).x;
}

//----------------------------------------------------------------------------//
// GGX VNDF Importance Sampling — DerivativeMain/lib/Surface/ScreenSpaceReflections.glsl:163-185
//----------------------------------------------------------------------------//

// https://ggx-research.github.io/publication/2023/06/09/publication-ggx.html
vec3 SampleGGXVNDF(in vec3 viewDir, in float roughness, in vec2 xy) {
    #define SPECULAR_TAIL_CLAMP

    #ifdef SPECULAR_TAIL_CLAMP
        xy.y = clamp(xy.y * 0.25, 1e-3, 0.25);
    #endif
    // Transform viewer direction to the hemisphere configuration
    viewDir = normalize(vec3(roughness * viewDir.xy, viewDir.z));

    // Sample a reflection direction off the hemisphere
    float phi = TAU * xy.x;
    float cosTheta = oneMinus(xy.y) * (1.0 + viewDir.z) - viewDir.z;
    float sinTheta = sqrt(saturate(1.0 - cosTheta * cosTheta));
    vec3 reflected = vec3(cossin(phi) * sinTheta, cosTheta);

    // Evaluate halfway direction
    // This gives the normal on the hemisphere
    vec3 halfway = reflected + viewDir;

    // Transform the halfway direction back to hemiellispoid configuation
    // This gives the final sampled normal
    return normalize(vec3(roughness * halfway.xy, halfway.z));
}

#endif
