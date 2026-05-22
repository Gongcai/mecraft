// Held item shadow sampling — Mecraft Phase 5.4.
// Lightweight wrapper around mecraft_shadow.glsl for forward held-item shaders.
// Provides sampleHeldItemShadow() which returns a visibility factor [0,1].
//
// IMPORTANT: All uniforms and helper functions required by mecraft_shadow.glsl
// must be declared BEFORE the include, because the shadow functions reference
// them at parse time.

#ifndef MECRAFT_HELD_ITEM_SHADOW_GLSL
#define MECRAFT_HELD_ITEM_SHADOW_GLSL

// ---- Uniforms required by mecraft_shadow.glsl shadow functions ----
// These mirror deferred_lighting.fs declarations and must be bound by C++.
uniform vec3  uCameraPos;
uniform vec3  uSunDirection;
uniform float uShadowDistance;
uniform float uShadowConstantBias;
uniform float uShadowSlopeBias;
uniform float uShadowNormalOffset;
uniform float uShadowSoftness;
uniform float uShadowPcssStrength;
uniform int   uSoftShadowsEnabled;
uniform int   uPcssShadowsEnabled;
uniform int   uShadowsEnabled;

// ---- Helper functions required by mecraft_shadow.glsl ----
// deferred_lighting.fs uses a noise texture; held item shaders use a
// screen-space interleaved gradient noise instead (no extra texture needed).
float shadowDither() {
    // Jimenez 2014 — gradient noise from screen-space coordinates.
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(gl_FragCoord.xy, magic.xy)));
}

// ---- Include the full CSM shadow system ----
#ifndef MECRAFT_SHADOW_ENABLE_STANDARD_SAMPLE
#define MECRAFT_SHADOW_ENABLE_STANDARD_SAMPLE
#endif
#define MECRAFT_SHADOW_OPAQUE_ONLY
#include "mecraft_shadow.glsl"

// ---- Held item shadow API ----
// Sample CSM shadow for a held item vertex.
// modelPos is the camera-relative position from the vertex shader;
// we add uCameraPos to get true world position for shadow projection.
// Returns visibility in [0,1]: 1.0 = fully lit, 0.0 = fully shadowed.
float sampleHeldItemShadow(vec3 modelPos, vec3 normal) {
    if (uShadowsEnabled == 0) {
        return 1.0;
    }
    vec3 worldPos = uCameraPos + modelPos;
    vec3 lightDir = normalize(uSunDirection);
    ShadowSample s = sampleCsmShadow(worldPos, normal, lightDir);
    return s.visibility;
}

#endif
