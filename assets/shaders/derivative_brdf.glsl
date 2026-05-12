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

//----------------------------------------------------------------------------//
// Fresnel — DerivativeMain/lib/Surface/BRDF.glsl:2-36
//----------------------------------------------------------------------------//

// DerivativeMain BRDF.glsl:5-8
// Air-to-dielectric Schlick Fresnel approximation.
float FresnelSchlick(in float cosTheta, in float f0) {
    float f = pow5(1.0 - cosTheta);
    return saturate(f + oneMinus(f) * f0);
}

// DerivativeMain BRDF.glsl:11-24
// Exact dielectric Fresnel (air-to-dielectric, parameterized by F0 reflectance).
// Converts f0 to IOR internally, then applies the exact Fresnel equations.
float FresnelDielectric(in float cosTheta, in float f0) {
    f0 = min(sqrt(f0), 0.99999);
    f0 = (1.0 + f0) * rcp(1.0 - f0);

    float cosR = 1.0 - sqr(sqrt(1.0 - sqr(cosTheta)) * rcp(max(f0, 1e-16)));
    if (cosR < 0.0) return 1.0;

    cosR = sqrt(cosR);
    float a = f0 * cosTheta;
    float b = f0 * cosR;
    float r1 = (a - cosR) / (a + cosR);
    float r2 = (b - cosTheta) / (b + cosTheta);
    return saturate(0.5 * (r1 * r1 + r2 * r2));
}

// DerivativeMain BRDF.glsl:26-36
// Exact dielectric Fresnel parameterized by refractive index (IOR) directly.
float FresnelDielectricN(in float cosTheta, in float n) {
    float cosR = sqr(n) + sqr(cosTheta) - 1.0;
    if (cosR < 0.0) return 1.0;

    cosR = sqrt(cosR);
    float a = n * cosTheta;
    float b = n * cosR;
    float r1 = (a - cosR) / (a + cosR);
    float r2 = (b - cosTheta) / (b + cosTheta);
    return saturate(0.5 * (r1 * r1 + r2 * r2));
}

//----------------------------------------------------------------------------//
// Smith GGX — DerivativeMain/lib/Surface/BRDF.glsl:38-48
//----------------------------------------------------------------------------//

// DerivativeMain BRDF.glsl:40-42
float V1SmithGGXInverse(in float cosTheta, in float alpha2) {
    return cosTheta + sqrt((cosTheta - alpha2 * cosTheta) * cosTheta + alpha2);
}

// DerivativeMain BRDF.glsl:44-48
// Height-correlated Smith GGX visibility function (G2).
float V2SmithGGX(in float NdotV, in float NdotL, in float alpha2) {
    float ggxl = NdotL * sqrt(alpha2 + (NdotV - NdotV * alpha2) * NdotV);
    float ggxv = NdotV * sqrt(alpha2 + (NdotL - NdotL * alpha2) * NdotL);
    return 0.5 / (ggxl + ggxv);
}

//----------------------------------------------------------------------------//
// GGX NDF — DerivativeMain/lib/Surface/BRDF.glsl:50-52
//----------------------------------------------------------------------------//

// DerivativeMain BRDF.glsl:50-52
float DistributionGGX(in float NdotH, in float alpha2) {
    return alpha2 * rPI / sqr(1.0 + (NdotH * alpha2 - NdotH) * NdotH);
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
    float singleRough = facing * (0.45 - 0.2 * facing) * (rcp(NdotH) + 2.0);

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
    if (NdotL < 1e-5) return 0.0;
    float F = FresnelSchlick(LdotH, f0);

    float D = DistributionGGX(NdotH, alpha2);
    float V = V2SmithGGX(max(NdotV, 1e-2), max(NdotL, 1e-2), alpha2);

    return min(NdotL * D * V * F, 4.0);
}

#endif
