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
// DerivativeMain terrain starts from smoothness in specTex.r and converts to
// roughness as sqr(1.0 - specTex.r). Mecraft stores roughness directly, so the
// equivalent wet smoothness mix becomes roughness * sqr(1.0 - wetness).
float ApplyWetRoughness(float roughness, float pixelWetness) {
    float wet = clamp(pixelWetness, 0.0, 1.0);
    return max(roughness * (1.0 - wet) * (1.0 - wet), 0.02);
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

// DerivativeMain RainEffect.glsl: wetnessDistribution = texture(noisetex, worldPos.xz * 0.01).r
// Returns a smooth low-frequency scalar in [0, 1] without the previous stripe-like placeholder noise.
float ComputeDerivativeRainWetness(sampler2D noiseTex, vec2 worldXZ, float surfaceWetness, float time) {
    vec2 p = (worldXZ - time * vec2(0.01, 0.006)) * 0.01;
    float n = texture(noiseTex, p).z;
    n += texture(noiseTex, p * 0.6).x * 2.0;
    n += texture(noiseTex, p * 0.2).y * 3.0;
    return saturate(n * 0.18) * clamp(surfaceWetness, 0.0, 1.0);
}

// DerivativeMain RainEffect.glsl: GetRainWetness(), including outdoor and
// upward-facing gates from Terrain.frag before each wetFact threshold is applied.
float ComputeRainSurfaceWetnessNoise(sampler2D noiseTex,
                                     vec3 worldPos,
                                     float surfaceWetness,
                                     float skyLightRaw01,
                                     float normalY,
                                     float time) {
    float wetness = clamp(surfaceWetness, 0.0, 1.0);
    float outdoorWetMask = saturate(skyLightRaw01 * 10.0 - 9.0);
    float upwardFacing = remap(0.5, 0.9, clamp(normalY, 0.0, 1.0));
    if (wetness <= 0.0 || outdoorWetMask <= 0.0 || upwardFacing <= 0.0) {
        return 0.0;
    }

    float rainWetness = ComputeDerivativeRainWetness(noiseTex, worldPos.xz - worldPos.y, wetness, time);
    rainWetness *= outdoorWetMask * upwardFacing;
    return rainWetness;
}

// DerivativeMain Terrain.frag: wetFact = smoothstep(0.54, 0.62, noise)
// This narrow mask drives the RippleNormal splash normal only.
float ComputeRainSplashMask(sampler2D noiseTex,
                            vec3 worldPos,
                            float surfaceWetness,
                            float skyLightRaw01,
                            float normalY,
                            float time) {
    float rainWetness = ComputeRainSurfaceWetnessNoise(noiseTex, worldPos, surfaceWetness, skyLightRaw01, normalY, time);
    return smoothstep(0.54, 0.62, rainWetness);
}

// DerivativeMain RainEffect.glsl: GetRainWetness + Terrain.frag wet spec thresholds.
// Produces patchy puddle coverage instead of a full wet-film response.
float ComputeRainPuddleMask(sampler2D noiseTex,
                            vec3 worldPos,
                            float surfaceWetness,
                            float skyLightRaw01,
                            float normalY,
                            float porosity,
                            float time) {
    float rainWetness = ComputeRainSurfaceWetnessNoise(noiseTex, worldPos, surfaceWetness, skyLightRaw01, normalY, time);

    float puddleMask = remap(0.35, 0.57, rainWetness);
    puddleMask = puddleMask * puddleMask;
    puddleMask *= 1.0 - clamp(porosity, 0.0, 1.0) * 0.20;
    return saturate(puddleMask);
}

// DerivativeMain RainEffect.glsl / RippleNormal.png.
// Sample the 64-frame horizontal ripple atlas without the previous thin-strip placeholder noise.
vec2 SampleRainRippleNormal(sampler2D rippleTex,
                            vec3 worldPos,
                            float wetness,
                            float time,
                            float worldScale,
                            float rippleSpeed) {
    float wet = saturate(wetness);
    if (wet <= 0.0) {
        return vec2(0.0);
    }

    vec2 rippleUv = worldPos.xz * worldScale;
    rippleUv.x = (rippleUv.x + floor(fract(time * rippleSpeed) * 64.0)) * (1.0 / 64.0);

    vec2 ripple = texture(rippleTex, rippleUv).rg * 2.0 - 1.0;
    float lod = dot(abs(fwidth(worldPos)), vec3(5.0));
    ripple /= 1.0 + lod;
    return ripple * 0.75 * wet;
}

#endif // WEATHER_SURFACE_GLSL
