// Shared DerivativeMain shadow distortion and bias functions.
// Ported verbatim from DerivativeMain/lib/Head/Common.inc and
// DerivativeMain/lib/Lighting/ShadowDistortion.glsl and
// DerivativeMain/lib/Lighting/SunLighting.glsl.
//
// IMPORTANT: Do NOT approximate or "simplify" any formula here.
// DerivativeMain is the authoritative source.  See section 0 of the
// DerivativeMain porting analysis report for the rationale.
//
// Every function is annotated with its DerivativeMain origin.

#ifndef MECRAFT_DERIVATIVE_SHADOW_GLSL
#define MECRAFT_DERIVATIVE_SHADOW_GLSL

//----------------------------------------------------------------------------//
// Base math helpers — DerivativeMain/lib/Head/Common.inc
//----------------------------------------------------------------------------//

// DerivativeMain Common.inc:22-26
#ifndef MECRAFT_DERIV_PI
#define MECRAFT_DERIV_PI
const float PI  = radians(180.0);   // DerivativeMain Common.inc:22
const float rPI = 1.0 / PI;          // DerivativeMain Common.inc:23
const float TAU = radians(360.0);    // DerivativeMain Common.inc:24
const float rTAU = 1.0 / TAU;       // DerivativeMain Common.inc:25
const float rLOG2 = 1.0 / log(2.0);  // DerivativeMain Common.inc:26
#endif

// DerivativeMain Common.inc:28
#define rcp(x) (1.0 / (x))

// DerivativeMain Common.inc:29-33
#ifndef oneMinus
#define oneMinus(x) (1.0 - (x))      // DerivativeMain Common.inc:29
#endif
#ifndef saturate
#define saturate(x) clamp(x, 0.0, 1.0)  // DerivativeMain Common.inc:32
#endif
#ifndef max0
#define max0(x) max(x, 0.0)          // DerivativeMain Common.inc:31
#endif
#ifndef fastExp
#define fastExp(x) exp2((x) * rLOG2) // DerivativeMain Common.inc:30
#endif
#ifndef clamp16F
#define clamp16F(x) clamp(x, 0.0, 65535.0)  // DerivativeMain Common.inc:33
#endif

// DerivativeMain Common.inc:50
float sqr(float x) { return x * x; }
vec2  sqr(vec2 x) { return x * x; }
vec3  sqr(vec3 x) { return x * x; }

// DerivativeMain Common.inc:55
float cube(float x) { return x * x * x; }
vec2  cube(vec2 x) { return x * x * x; }
vec3  cube(vec3 x) { return x * x * x; }

// DerivativeMain Common.inc:59
float pow4(float x) { return cube(x) * x; }
vec3  pow4(vec3 x) { return cube(x) * x; }

// DerivativeMain Common.inc:62
float pow5(float x) { return pow4(x) * x; }
vec3  pow5(vec3 x) { return pow4(x) * x; }

// DerivativeMain Common.inc:67
// sqrt2(x) = sqrt(sqrt(x)) = x^0.25  — the FOURTH root, NOT the square root.
// A previous project mistakenly rewrote this as sqrt(x), causing the
// Derivative shadow warp read/write mismatch bug (drifting shadow blob).
float sqrt2(float c) { return sqrt(sqrt(c)); }
vec3  sqrt2(vec3 c) { return sqrt(sqrt(c)); }

// DerivativeMain Common.inc:65
float pow16(float x) { return sqr(pow4(x)); }

// DerivativeMain Common.inc:70-72
float curve(float x) { return sqr(x) * (3.0 - 2.0 * x); }

// DerivativeMain Common.inc:74-75
float dotSelf(vec2 x) { return dot(x, x); }
float dotSelf(vec3 x) { return dot(x, x); }

// DerivativeMain Common.inc:77-78
vec2 sincos(float x) { return vec2(sin(x), cos(x)); }
vec2 cossin(float x) { return vec2(cos(x), sin(x)); }

// DerivativeMain Common.inc:80
float remap(float e0, float e1, float x) { return saturate((x - e0) / (e1 - e0)); }

// DerivativeMain Common.inc:131-133
float GetLuminance(in vec3 color) { return dot(color, vec3(0.2722, 0.6741, 0.0537)); }

// DerivativeMain Common.inc — maxOf / minOf helpers
float maxOf(vec3 v) { return max(max(v.x, v.y), v.z); }
float minOf(vec3 v) { return min(min(v.x, v.y), v.z); }
float maxOf(vec2 v) { return max(v.x, v.y); }
float minOf(vec2 v) { return min(v.x, v.y); }

//----------------------------------------------------------------------------//
// Block light falloff — DerivativeMain/lib/Head/Functions.inc:4-7
//----------------------------------------------------------------------------//

// DerivativeMain Functions.inc:4-7
// Nonlinear remap of block light channel: inverse-square falloff + mild linear.
// Without this, torch-lit areas have incorrect brightness curves.
void GetBlocklightFalloff(inout float blocklight) {
    blocklight = rcp(sqr(16.0 - 15.0 * blocklight)) + sqr(blocklight) * 0.05;
    blocklight = remap(rcp(sqr(16.0)), 1.0, blocklight);
}

//----------------------------------------------------------------------------//
// Color space conversions — DerivativeMain/lib/Head/Common.inc
//----------------------------------------------------------------------------//

// DerivativeMain Common.inc — LinearToSRGB / SRGBtoLinear
vec3 LinearToSRGB(in vec3 color) { return pow(max(color, vec3(0.0)), vec3(1.0 / 2.2)); }
vec3 SRGBtoLinear(in vec3 color) { return pow(max(color, vec3(0.0)), vec3(2.2)); }

//----------------------------------------------------------------------------//
// Shadow distortion — DerivativeMain/lib/Lighting/ShadowDistortion.glsl
//----------------------------------------------------------------------------//

// DerivativeMain ShadowDistortion.glsl:2-5
float cubeLength(in vec2 v) {
    vec2 t = abs(cube(v));
    return pow(t.x + t.y, 1.0 / 3.0);
}

// DerivativeMain ShadowDistortion.glsl:7-9
float quarticLength(in vec2 v) {
    return sqrt2(pow4(v.x) + pow4(v.y));
}

// DerivativeMain ShadowDistortion.glsl:11-17
// SHADOW_MAP_BIAS is 0.9 by default (DerivativeMain Settings.glsl:74).
// Mecraft exposes this as a runtime uniform; the function below accepts it
// as a parameter so callers can pass either the constant or the uniform.
float DistortionFactor(in vec2 shadowClipPos, in float shadowMapBias) {
    // DISTANT_HORIZONS / DH_SHADOW is a non-target branch; Mecraft always
    // takes the else path, equivalent to DerivativeMain without DH.
    return quarticLength(shadowClipPos * 1.165) * shadowMapBias + 1.0 - shadowMapBias;
}

// Overload with compile-time constant — matches DerivativeMain default.
float DistortionFactor(in vec2 shadowClipPos) {
    const float SHADOW_MAP_BIAS = 0.9; // DerivativeMain Settings.glsl:74
    return DistortionFactor(shadowClipPos, SHADOW_MAP_BIAS);
}

// DerivativeMain ShadowDistortion.glsl:25-31
vec3 DistortShadowSpace(in vec3 shadowClipPos, in float distortionFactor) {
    // Non-DH path: xy *= rcp(distortionFactor), z *= 0.2
    return shadowClipPos * vec3(vec2(rcp(distortionFactor)), 0.2);
}

// DerivativeMain ShadowDistortion.glsl:33-40
vec3 DistortShadowSpace(in vec3 shadowClipPos) {
    float df = DistortionFactor(shadowClipPos.xy);
    return DistortShadowSpace(shadowClipPos, df);
}

//----------------------------------------------------------------------------//
// World-to-shadow projection — DerivativeMain/lib/Lighting/SunLighting.glsl:18-24
//----------------------------------------------------------------------------//

// DerivativeMain SunLighting.glsl:18-24  WorldPosToShadowProjPosBias
// Mecraft adaptation: uses uShadowModelView / uShadowProjection uniforms
// instead of DerivativeMain's shadowModelView / shadowProjection builtins.
// transMAD and projMAD are from DerivativeMain Common.inc:35,39.
//
// #define transMAD(m, v)  (mat3(m) * (v) + (m)[3].xyz)
// #define projMAD(m, v)   (diagonal3(m) * (v) + (m)[3].xyz)
//   where diagonal3(m) = vec3(m[0].x, m[1].y, m[2].z)

vec3 WorldPosToShadowProjPosBias(in vec3 worldOffsetPos,
                                  in mat4 shadowModelView,
                                  in mat4 shadowProjection,
                                  in float shadowMapBias,
                                  out float distortFactor) {
    // transMAD(shadowModelView, worldOffsetPos)
    vec3 shadowClipPos = mat3(shadowModelView) * worldOffsetPos + shadowModelView[3].xyz;
    // projMAD(shadowProjection, shadowClipPos)
    shadowClipPos = vec3(shadowProjection[0].x, shadowProjection[1].y, shadowProjection[2].z) * shadowClipPos + shadowProjection[3].xyz;

    distortFactor = DistortionFactor(shadowClipPos.xy, shadowMapBias);
    return DistortShadowSpace(shadowClipPos, distortFactor) * 0.5 + 0.5;
}

//----------------------------------------------------------------------------//
// Shadow bias helpers — consolidated from deferred_lighting.fs / volumetric_fog.fs / deferred_debug.fs
// Unified to match DerivativeMain's SunLighting.glsl conventions.
//----------------------------------------------------------------------------//

// DerivativeMain-inspired shadow projection edge fade.
// Prevents shadow artifacts at the shadow map boundary.
float shadowProjectionFade(in vec3 proj, in sampler2D shadowMap) {
    vec2 edgeDistance = min(proj.xy, vec2(1.0) - proj.xy);
    float texelUv = 1.0 / max(float(textureSize(shadowMap, 0).x), 1.0);
    float edgeFade = smoothstep(texelUv * 8.0, texelUv * 36.0, min(edgeDistance.x, edgeDistance.y));
    float nearFade = smoothstep(0.002, 0.016, proj.z);
    float farFade = 1.0 - smoothstep(0.965, 0.998, proj.z);
    return clamp(edgeFade * nearFade * farFade, 0.0, 1.0);
}

// Scale factor converting shadow depth differences to world-space units.
// The z *= 0.2 in DistortShadowSpace compresses depth; we undo it here.
float shadowDepthWorldScale(in mat4 shadowProjectionInverse, in int shadowWarpMode) {
    float scale = max(abs(shadowProjectionInverse[2][2]) * 2.0, 1.0);
    return (shadowWarpMode != 2) ? scale / 0.2 : scale;
}

// Convert a world-space bias amount to shadow-map depth units.
float shadowDepthBiasFromWorld(in float worldUnits, in mat4 shadowProjectionInverse, in int shadowWarpMode) {
    return worldUnits / shadowDepthWorldScale(shadowProjectionInverse, shadowWarpMode);
}

// DerivativeMain SunLighting.glsl:57  — constant minimum bias after PCF z-offset.
// DerivativeMain uses 1e-4 for Derivative warp mode.
float derivativeMinimumShadowBias(in int shadowWarpMode) {
    return (shadowWarpMode != 2) ? 1.0e-4 : 6.0e-5;
}

// World-space shadow bias combining constant and slope-based components.
// DerivativeMain SunLighting.glsl:28-54  BlockerSearch / PCF bias logic.
// receiverScale follows DerivativeMain's distance-adaptive receiver plane bias.
float shadowWorldBias(in float ndotl, in float viewDistance,
                      in float shadowTexelWorldSize, in float shadowDistance,
                      in float shadowConstantBias, in float shadowSlopeBias) {
    float texelWorld = max(shadowTexelWorldSize, 0.0001);
    float slope = 1.0 - clamp(ndotl, 0.0, 1.0);
    // DerivativeMain uses receiverScale = 1.0 + 0.35 * (dist / shadowDistance)
    float receiverScale = 1.0 + 0.35 * clamp(viewDistance / max(shadowDistance, 1.0), 0.0, 1.0);
    return texelWorld * receiverScale * (0.35 + shadowConstantBias * 18.0 + shadowSlopeBias * 18.0 * slope);
}

// DerivativeMain-style normal offset for shadow acne reduction.
// Two components: texel-based offset and DerivativeMain distance-squared offset.
float shadowNormalOffsetWorld(in float ndotl, in float viewDistance,
                               in float shadowTexelWorldSize, in float shadowDistance,
                               in float shadowNormalOffset) {
    float texelWorld = max(shadowTexelWorldSize, 0.0001);
    float grazing = 1.0 - clamp(ndotl, 0.0, 1.0);
    float distanceScale = 1.0 + 0.35 * clamp(viewDistance / max(shadowDistance, 1.0), 0.0, 1.0);
    float requestedTexels = max(shadowNormalOffset, 0.0) / 0.09375;
    float texelOffset = texelWorld * requestedTexels * distanceScale * (1.0 + 0.85 * grazing);

    // DerivativeMain offsets receivers by normal * (dist^2 * 8e-5 + 3e-2) * (2 - NdotL)
    float derivativeScale = max(shadowNormalOffset, 0.0) / 0.035;
    float derivativeOffset = (viewDistance * viewDistance * 8e-5 + 3e-2) *
                             (2.0 - clamp(ndotl, 0.0, 1.0)) *
                             derivativeScale;
    return max(texelOffset, derivativeOffset);
}

//----------------------------------------------------------------------------//
// Convenience: calculateShadowWarp — Mecraft's uShadowWarpMode dispatch.
// Mode 0: radial (length-based), Mode 1: Derivative quartic, Mode 2: no warp.
// This replaces the duplicated calculateShadowWarp / calculateShadowDistortion
// functions that were previously inlined in 4 separate shader files.
//----------------------------------------------------------------------------//

float calculateShadowWarp(in vec2 coord, in int shadowWarpMode, in float shadowMapBias) {
    if (shadowWarpMode == 2) {
        return 1.0;
    }
    if (shadowWarpMode == 1) {
        return DistortionFactor(coord, shadowMapBias);
    }
    // Mode 0: radial warp (Mecraft extension, not in DerivativeMain)
    return length(coord * 1.169) * shadowMapBias + 1.0 - shadowMapBias;
}

// Overload with default SHADOW_MAP_BIAS = 0.9
float calculateShadowWarp(in vec2 coord, in int shadowWarpMode) {
    const float SHADOW_MAP_BIAS = 0.9;
    return calculateShadowWarp(coord, shadowWarpMode, SHADOW_MAP_BIAS);
}

//----------------------------------------------------------------------------//
// Convenience: worldToShadowProj — full world-to-shadow-UV pipeline.
// Combines modelView + projection + distortion + bias into one call.
// Returns shadow UV in [0,1] and optionally outputs the warp density factor.
//----------------------------------------------------------------------------//

vec3 worldToShadowProj(in vec3 worldPos,
                        in mat4 shadowModelView,
                        in mat4 shadowProjection,
                        in int shadowWarpMode,
                        in float shadowMapBias,
                        out float warpDensity) {
    vec4 lightView = shadowModelView * vec4(worldPos, 1.0);
    vec4 lightClip = shadowProjection * lightView;
    vec3 proj = lightClip.xyz / max(lightClip.w, 0.00001);
    if (shadowWarpMode != 2) {
        warpDensity = calculateShadowWarp(proj.xy, shadowWarpMode, shadowMapBias);
        proj.xy /= warpDensity;
        proj.z *= 0.2;
    } else {
        warpDensity = 1.0;
    }
    return proj * 0.5 + 0.5;
}

vec3 worldToShadowProj(in vec3 worldPos,
                        in mat4 shadowModelView,
                        in mat4 shadowProjection,
                        in int shadowWarpMode,
                        in float shadowMapBias) {
    float unused;
    return worldToShadowProj(worldPos, shadowModelView, shadowProjection, shadowWarpMode, shadowMapBias, unused);
}

#endif
