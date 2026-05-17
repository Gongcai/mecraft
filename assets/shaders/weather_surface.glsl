// weather_surface.glsl — Shared wet surface computation for Mecraft weather system.
// DerivativeMain reference: RainEffect.glsl + Terrain.frag + deferred5.fsh
// Single source of truth for pixelWetness and wet material modifications.
// Consumed by: deferred_lighting.fs, reflection_probe.fs
//
// Prerequisite: consumer must provide saturate() and remap() before including this file.
//   - deferred_lighting.fs gets them via derivative_shadow.glsl include chain
//   - reflection_probe.fs defines them locally before this include

#ifndef WEATHER_SURFACE_GLSL
#define WEATHER_SURFACE_GLSL

// Compute per-pixel wetness from weather state and surface properties.
// surfaceWetness: global wet intensity [0,1] (uSurfaceWetness)
// skyLightRaw01: raw sky light from voxel lightmap [0,1]
// wetnessMask: material-inherent wetness for water/ice/glass [0,1]
// normalY: surface normal.y component for upward-facing test
// Returns: pixel wetness [0,1]
float ComputePixelWetness(float surfaceWetness, float skyLightRaw01, float wetnessMask, float normalY) {
    float weatherWetness = clamp(surfaceWetness, 0.0, 1.0);
    float outdoorWetMask = saturate(skyLightRaw01 * 10.0 - 9.0);
    float upwardFacing = remap(0.5, 0.9, clamp(normalY, 0.0, 1.0));
    float pw = weatherWetness * outdoorWetMask * upwardFacing;
    pw = max(pw, wetnessMask * weatherWetness * skyLightRaw01);
    return pw;
}

// Wet normal flattening: push toward upward vector (water film effect).
// DerivativeMain Terrain.frag:213 — normalData = mix(normalData, vec3(0,0,1), wetFact)
vec3 ApplyWetNormal(vec3 normal, float pixelWetness) {
    return mix(normal, vec3(0.0, 1.0, 0.0), pixelWetness * 0.65);
}

// Wet roughness reduction: wet surfaces are smoother / more reflective.
// DerivativeMain deferred5.fsh:210 — roughness = sqr(oneMinus(roughness) * oneMinus(wetness*0.3))
float ApplyWetRoughness(float roughness, float pixelWetness) {
    return mix(roughness, max(0.08, roughness * 0.36), pixelWetness);
}

// Wet F0 boost: wet surfaces have stronger Fresnel (water IOR ~0.04).
// DerivativeMain Terrain.frag:220 — specularData.g = max(specularData.g, 0.04 * wetFact)
float ApplyWetF0(float f0, float pixelWetness) {
    return max(f0, 0.04 * pixelWetness);
}

// Wet albedo: desaturate 25%, darken 15%, with porosity correction.
// DerivativeMain Terrain.frag:225-230
vec3 ApplyWetAlbedo(vec3 albedo, float porosity, float pixelWetness) {
    float luma = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
    vec3 wetAlbedo = mix(vec3(luma), albedo, 0.75) * 0.85;
    float p = clamp(porosity, 0.0, 1.0);
    vec3 porosityFactor = (1.0 - p) / max(vec3(1.0) - p * wetAlbedo, vec3(0.01));
    wetAlbedo *= porosityFactor;
    return mix(albedo, wetAlbedo, pixelWetness);
}

#endif // WEATHER_SURFACE_GLSL
