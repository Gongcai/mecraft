// DerivativeMain/lib/Atmosphere/Fogs.glsl — CommonFog port
// Handles special-medium fog: blindness, darkness, lava, powder snow.
// Mecraft adaptation: uses uIsEyeInWater integer (0=none, 1=water, 2=lava, 3=powder_snow)
// instead of DerivativeMain's isEyeInWater. Blindness/darkness uniforms default to 0
// until Mecraft status effect system is implemented.

#ifndef FOGS_GLSL
#define FOGS_GLSL

// Common.inc helpers — safe to coexist with derivative_shadow.glsl via #ifndef guards.
#ifndef rLOG2
#define rLOG2 1.442695040889  // 1.0 / log(2.0)
#endif

#ifndef fastExp
#define fastExp(x) exp2((x) * rLOG2)  // DerivativeMain Common.inc:30
#endif

#ifndef saturate
#define saturate(x) clamp(x, 0.0, 1.0)  // DerivativeMain Common.inc:32
#endif

// CommonFog: final fog pass for special rendering media.
// DerivativeMain/lib/Atmosphere/Fogs.glsl — adapted for Mecraft integer eye-in-water.
//
// Parameters:
//   color            — scene HDR color (modified in place)
//   dist             — fog distance in world units
//   eyeMedium        — 0=none, 1=water, 2=lava, 3=powder_snow
//   blind            — blindness effect strength [0,1]
//   dark             — darkness effect strength [0,1]
//   wetness          — weather wetness [0,1] (DerivativeMain wetnessCustom)
//   skyIlluminance   — hemisphere sky irradiance from SkyCapture metadata
//   directIlluminance — sun+moon irradiance from SkyCapture metadata
void CommonFog(inout vec3 color, in float dist, in int eyeMedium,
               in float blind, in float dark, in float wetness,
               in vec3 skyIlluminance, in vec3 directIlluminance) {
    // Blindness / darkness: exponential darkening with distance
    if (blind + dark > 0.0) {
        color *= fastExp(-dist * max(blind, dark));
    }

    // Lava: saturate toward orange (DerivativeMain vec3(3.96, 0.68, 0.02))
    if (eyeMedium == 2) {
        color = mix(color, vec3(3.96, 0.68, 0.02), saturate(dist));
    }

    // Powder snow: illuminance-based fog with exponential falloff
    // DerivativeMain uses skyIlluminance + directIlluminance, wetness blend, eyeSkylightFix.
    if (eyeMedium == 3) {
        vec3 fogColor = skyIlluminance + directIlluminance;
        fogColor = 6.0 * mix(fogColor, directIlluminance * 0.1, wetness * 0.8);
        // eyeSkylightFix: in DerivativeMain this compensates for skylight sampling artifacts.
        // Mecraft uses 1.0 (no fix needed) since skylight comes from SH, not blocklight.
        const float eyeSkylightFix = 1.0;
        color = mix(fogColor * eyeSkylightFix, color, exp2(-dist * 2.0));
    }
}

#endif // FOGS_GLSL
