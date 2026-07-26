#version 450 core

#include "rhi_screen_coordinates.glsl"

layout(location = 0) in vec2 vScreenUv;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uSceneTex;
layout(binding = 1) uniform sampler2D uBloomTex;
layout(binding = 1) uniform sampler2D uBloomMip0;
layout(binding = 2) uniform sampler2D uBloomMip1;
layout(binding = 3) uniform sampler2D uBloomMip2;
layout(binding = 4) uniform sampler2D uBloomMip3;
layout(binding = 5) uniform sampler2D uBloomMip4;
layout(binding = 6) uniform sampler2D uBloomMip5;
layout(binding = 7) uniform sampler2D uBloomMip6;
layout(binding = 8) uniform sampler2D uNoiseTex;

layout(binding = 10) uniform sampler2D uExposureStateTex;
layout(binding = 9) uniform sampler2D uDepthTex;        // GBuffer depth for sky pixel detection
layout(binding = 11) uniform sampler2D uSceneDepthTex;  // Final scene depth, including forward first-person items

layout(std140, binding = 12) uniform PostProcessParams {
    ivec4 uPostFlags0;
    ivec4 uPostModes0;
    ivec4 uPostFlags1;
    vec4 uPostBloomExposure;
    vec4 uPostSunParams;
    vec4 uPostColorParams0;
    vec4 uPostColorParams1;
    vec4 uPostUnderwaterParams;
    vec4 uPostWeatherParams0;
    vec4 uPostWeatherParams1;
    vec4 uPostWeatherParams2;
};

#define uBloomEnabled (uPostFlags0.x != 0)
#define uAutoExposureEnabled (uPostFlags0.y != 0)
#define uSunRaysEnabled (uPostFlags0.z != 0)
#define uShaderpackGradingEnabled (uPostFlags0.w != 0)
#define uBloomMipCount uPostModes0.x
#define uTonemapMode uPostModes0.y
#define uPostprocessDebugMode uPostModes0.z
#define uUnderwaterEnabled (uPostFlags1.x != 0)
#define uPurkinjeShiftEnabled (uPostFlags1.y != 0)
#define uBloomyFogEnabled (uPostFlags1.z != 0)
#define uBloomStrength uPostBloomExposure.x
#define uExposure uPostBloomExposure.y
#define uGamma uPostBloomExposure.z
#define uScreenRollRadians uPostBloomExposure.w
#define uSunScreenPos uPostSunParams.xy
#define uSunVisibility uPostSunParams.z
#define uSunRayStrength uPostSunParams.w
#define uColorTemperature uPostColorParams0.x
#define uVibrance uPostColorParams0.y
#define uSplitToneStrength uPostColorParams0.z
#define uVignetteStrength uPostColorParams0.w
#define uNoiseDitherStrength uPostColorParams1.x
#define uSharpenStrength uPostColorParams1.y
#define uSaturation uPostColorParams1.z
#define uContrast uPostColorParams1.w
#define uUnderwaterTint uPostUnderwaterParams.xyz
#define uUnderwaterStrength uPostUnderwaterParams.w
#define uWeatherWetness uPostWeatherParams0.x
#define uWeatherStorm uPostWeatherParams0.y
#define uSnowStrength uPostWeatherParams0.z
#define uSkyWetness uPostWeatherParams0.w
#define uFogWetness uPostWeatherParams1.x
#define uCloudWetness uPostWeatherParams1.y
#define uCameraRainVisibility uPostWeatherParams1.z
#define uWeatherExposureBias uPostWeatherParams1.w
#define uWeatherPostRainFog uPostWeatherParams2.x
#define uTime uPostWeatherParams2.y

float g_resolvedExposure = 1.0;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

vec3 tonemapReinhard(vec3 color) {
    return color / (color + vec3(1.0));
}

vec3 tonemapAces(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

vec3 tonemapFilmic(vec3 color) {
    color = max(vec3(0.0), color - vec3(0.004));
    return clamp((color * (6.2 * color + 0.5)) / (color * (6.2 * color + 1.7) + 0.06), 0.0, 1.0);
}

vec3 agxInset(vec3 color) {
    return vec3(
        dot(color, vec3(0.856627153, 0.137318972, 0.111898212)),
        dot(color, vec3(0.095121240, 0.761241990, 0.076799418)),
        dot(color, vec3(0.048251607, 0.101439038, 0.811302370))
    );
}

vec3 agxOutset(vec3 color) {
    return vec3(
        dot(color, vec3(1.127100581, -0.141329763, -0.141329763)),
        dot(color, vec3(-0.110606643, 1.157823702, -0.110606643)),
        dot(color, vec3(-0.016493938, -0.016493938, 1.251936406))
    );
}

vec3 agxContrastApprox(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

// Legacy Mecraft AgX (Blender-style parameters, 6th-order polynomial).
// Kept for tonemapPreserveLuma fallback on Mecraft-extra modes (0/2).
vec3 tonemapAgxLegacy(vec3 color) {
    color = agxInset(max(color, vec3(0.0)));
    color = clamp(log2(max(color, vec3(1e-6))), -12.47393, 4.026069);
    color = (color + 12.47393) / 16.5;
    color = agxContrastApprox(color);
    color = agxOutset(color);
    color = pow(max(color, vec3(0.0)), vec3(2.2));
    return clamp(color, 0.0, 1.0);
}

// DerivativeMain/lib/Post/AgX.glsl:101 — AgX_Minimal
// From https://www.shadertoy.com/view/mdcSDH
// 7th-order polynomial sigmoid, sRGB gamut, -8 to +6 EV range.
vec3 AgX_Minimal(in vec3 val) {
    const mat3 agx_mat = mat3(
        0.842479062253094, 0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772, 0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 agx_mat_inv = mat3(
        1.19687900512017, -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float min_ev = -8.0;
    const float max_ev = 6.0;
    // Input transform
    val = agx_mat * max(val, vec3(0.0)) * 8.0;
    // Log2 space encoding
    val = clamp(log2(max(val, vec3(1e-10))), min_ev, max_ev);
    val = (val - min_ev) / (max_ev - min_ev);
    // Apply 7th-order sigmoid approximation
    vec3 x2 = val * val;
    vec3 x4 = x2 * x2;
    val = -17.86 * x4 * x2 * val
          + 78.01 * x4 * x2
          - 126.7 * x4 * val
          + 92.06 * x4
          - 28.72 * x2 * val
          + 4.361 * x2
          - 0.1718 * val
          + 0.002857;
    // Undo input transform
    return agx_mat_inv * val;
}

// Helper functions needed by AcademyFull (must precede its definition)
float luma709(vec3 color) { return dot(color, vec3(0.2126, 0.7152, 0.0722)); }
float maxOf(vec3 v) { return max(v.x, max(v.y, v.z)); }
float minOf(vec3 v) { return min(v.x, min(v.y, v.z)); }
float sqr(float x) { return x * x; }
float cube(float x) { return x * x * x; }
float curve(float x) { return sqr(x) * (3.0 - 2.0 * x); }
float oneMinus(float x) { return 1.0 - x; }
float GetLuminance(vec3 color) { return luma709(color); }
float saturate(float v) { return clamp(v, 0.0, 1.0); }
vec3 saturate(vec3 v) { return clamp(v, 0.0, 1.0); }
vec3 clamp16F(vec3 color) { return clamp(color, vec3(0.0), vec3(65535.0)); }

vec3 LinearToSRGB(vec3 color) {
    color = max(color, vec3(0.0));
    vec3 lo = color * 12.92;
    vec3 hi = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), color));
}

float rgbToSaturation(vec3 rgb) {
    return (max(maxOf(rgb), 1e-10) - max(minOf(rgb), 1e-10)) / max(maxOf(rgb), 1e-2);
}
float rgbToHue(vec3 rgb) {
    if (rgb.r == rgb.g && rgb.g == rgb.b) return 0.0;
    const float TAU_val = 6.283185307179586;
    float hue = (360.0 / TAU_val) * atan(2.0 * rgb.r - rgb.g - rgb.b, sqrt(3.0) * (rgb.g - rgb.b));
    if (hue < 0.0) hue += 360.0;
    return hue;
}
float rgbToYc(vec3 rgb) {
    const float yc_radius_weight = 1.75;
    float chroma = sqrt(rgb.b * (rgb.b - rgb.g) + rgb.g * (rgb.g - rgb.r) + rgb.r * (rgb.r - rgb.b));
    return (rgb.r + rgb.g + rgb.b + yc_radius_weight * chroma) / 3.0;
}
float GlowFwd(float ycIn, float glowGainIn, float glowMid) {
    if (ycIn <= 2.0 / 3.0 * glowMid) return glowGainIn;
    if (ycIn >= 2.0 * glowMid) return 0.0;
    return glowGainIn * (glowMid / ycIn - 0.5);
}
float SigmoidShaper(float x) {
    float t = max(1.0 - abs(0.5 * x), 0.0);
    float y = 1.0 + sign(x) * oneMinus(t * t);
    return 0.5 * y;
}
float CubicBasisShaperFit(float x, float width) {
    float radius = 0.5 * width;
    return abs(x) < radius ? sqr(curve(1.0 - abs(x) / radius)) : 0.0;
}
float CenterHue(float hue, float centerH) {
    float hueCentered = hue - centerH;
    if (hueCentered < -180.0) hueCentered += 360.0;
    else if (hueCentered > 180.0) hueCentered -= 360.0;
    return hueCentered;
}
float max0(float x) { return max(x, 0.0); }
vec3 max0(vec3 v) { return max(v, vec3(0.0)); }
float fastExp(float x) {
    x = 1.0 + x / 256.0;
    x *= x; x *= x; x *= x; x *= x;
    x *= x; x *= x; x *= x; x *= x;
    return x;
}
const mat3 rgbToXyz = mat3(
    0.4124564, 0.3575761, 0.1804375,
    0.2126729, 0.7151522, 0.0721750,
    0.0193339, 0.1191920, 0.9503041
);
const mat3 xyzToRgb = mat3(
     3.2404542, -1.5371385, -0.4985314,
    -0.9692660,  1.8760108,  0.0415560,
     0.0556434, -0.2040259,  1.0572252
);

// DerivativeMain/lib/Post/ACES.glsl — AcademyFull: full ACES RRT+ODT pipeline.
// Includes segmented spline c5 (RRT), segmented spline c9 (ODT), surround
// adaptation, and D60-to-D65 chromatic adaptation.
const mat3 sRGBtoACES = mat3(
    0.43963298, 0.38298870, 0.17737832,
    0.08977644, 0.81343943, 0.09678413,
    0.01754117, 0.11154655, 0.87091228
);
const mat3 s_ap0ToAp1 = mat3(
     1.4514393161, -0.2365107465, -0.2149285696,
    -0.0765537734,  1.1762296998, -0.0996759264,
     0.0083161484, -0.0060324498,  0.9977163014
);
const mat3 s_ap1ToAp0 = mat3(
     0.6954522414, 0.1406786965, 0.1638690622,
     0.0447945634, 0.8596711185, 0.0955343182,
    -0.0055258826, 0.0040252103, 1.0015006723
);
const mat3 s_ap1ToXyz = mat3(
     0.6624541811, 0.1340042065, 0.1561876870,
     0.2722287168, 0.6740817658, 0.0536895174,
    -0.0055746495, 0.0040607335, 1.0103391003
);
const mat3 s_xyzToAp1 = mat3(
     1.6410233797, -0.3248032942, -0.2364246952,
    -0.6636628587,  1.6153315917,  0.0167563477,
     0.0117218943, -0.0082844420,  0.9883948585
);
const mat3 D60ToD65_CAT = mat3(
     0.98722400, -0.00611327, 0.01595330,
    -0.00759836,  1.00186000, 0.00533002,
     0.00307257, -0.00509595, 1.08168000
);

#define log10_aces(x) (log(x) * (1.0 / log(10.0)))

// B-spline basis matrix for segmented spline evaluation
const mat3 splineM = mat3(
     0.5, -1.0,  0.5,
    -1.0,  1.0,  0.5,
     0.5,  0.0,  0.0
);

// Segmented spline c5 (RRT tone curve)
struct SegmentedSplineParams_c5 {
    float coeffsLow[6];
    float coeffsHigh[6];
    vec2 minPoint;
    vec2 midPoint;
    vec2 maxPoint;
    float slopeLow;
    float slopeHigh;
};

const SegmentedSplineParams_c5 RRT_PARAMS = SegmentedSplineParams_c5(
    float[6](-4.0000000000, -4.0000000000, -3.1573765773, -0.4852499958, 1.8477324706, 1.8477324706),
    float[6](-0.7185482425, 2.0810307172, 3.6681241237, 4.0000000000, 4.0000000000, 4.0000000000),
    vec2(0.18 * exp2(-15.0), 0.0001),
    vec2(0.18, 4.8),
    vec2(0.18 * exp2(18.0), 10000.0),
    0.0, 0.0
);

float segmented_spline_c5_fwd(float x, SegmentedSplineParams_c5 params) {
    const int N_KNOTS_LOW = 4;
    const int N_KNOTS_HIGH = 4;
    float logMinPoint = log10_aces(params.minPoint.x);
    float logMidPoint = log10_aces(params.midPoint.x);
    float logMaxPoint = log10_aces(params.maxPoint.x);
    float logx = log10_aces(max(x, 1e-6));
    float logy;
    if (logx <= logMinPoint) {
        logy = logx * params.slopeLow + (log10_aces(params.minPoint.y) - params.slopeLow * logMinPoint);
    } else if (logx < logMidPoint) {
        float knot_coord = float(N_KNOTS_LOW - 1) * (logx - logMinPoint) / (logMidPoint - logMinPoint);
        int j = int(knot_coord);
        float t = knot_coord - float(j);
        vec3 cf = vec3(params.coeffsLow[j], params.coeffsLow[j + 1], params.coeffsLow[j + 2]);
        logy = dot(vec3(t * t, t, 1.0), splineM * cf);
    } else if (logx < logMaxPoint) {
        float knot_coord = float(N_KNOTS_HIGH - 1) * (logx - logMidPoint) / (logMaxPoint - logMidPoint);
        int j = int(knot_coord);
        float t = knot_coord - float(j);
        vec3 cf = vec3(params.coeffsHigh[j], params.coeffsHigh[j + 1], params.coeffsHigh[j + 2]);
        logy = dot(vec3(t * t, t, 1.0), splineM * cf);
    } else {
        logy = logx * params.slopeHigh + (log10_aces(params.maxPoint.y) - params.slopeHigh * logMaxPoint);
    }
    return pow(10.0, logy);
}

// Segmented spline c9 (ODT tone curve)
struct SegmentedSplineParams_c9 {
    float coeffsLow[10];
    float coeffsHigh[10];
    vec2 minPoint;
    vec2 midPoint;
    vec2 maxPoint;
    float slopeLow;
    float slopeHigh;
};

const SegmentedSplineParams_c9 ODT_48nits = SegmentedSplineParams_c9(
    float[10](-1.6989700043, -1.6989700043, -1.4779000000, -1.2291000000, -0.8648000000, -0.4480000000, 0.0051800000, 0.4511080334, 0.9113744414, 0.9113744414),
    float[10](0.5154386965, 0.8470437783, 1.1358000000, 1.3802000000, 1.5197000000, 1.5985000000, 1.6467000000, 1.6746091357, 1.6878733390, 1.6878733390),
    vec2(0.18 * exp2(-6.5), 0.02),
    vec2(0.18, 4.8),
    vec2(0.18 * exp2(6.5), 48.0),
    0.0, 0.04
);

float segmented_spline_c9_fwd(float x, SegmentedSplineParams_c9 params) {
    const int N_KNOTS_LOW = 8;
    const int N_KNOTS_HIGH = 8;
    float logMinPoint = log10_aces(params.minPoint.x);
    float logMidPoint = log10_aces(params.midPoint.x);
    float logMaxPoint = log10_aces(params.maxPoint.x);
    float logx = log10_aces(max(x, 1e-6));
    float logy;
    if (logx <= logMinPoint) {
        logy = logx * params.slopeLow + (log10_aces(params.minPoint.y) - params.slopeLow * logMinPoint);
    } else if (logx < logMidPoint) {
        float knot_coord = float(N_KNOTS_LOW - 1) * (logx - logMinPoint) / (logMidPoint - logMinPoint);
        int j = int(knot_coord);
        float t = knot_coord - float(j);
        vec3 cf = vec3(params.coeffsLow[j], params.coeffsLow[j + 1], params.coeffsLow[j + 2]);
        logy = dot(vec3(t * t, t, 1.0), splineM * cf);
    } else if (logx < logMaxPoint) {
        float knot_coord = float(N_KNOTS_HIGH - 1) * (logx - logMidPoint) / (logMaxPoint - logMidPoint);
        int j = int(knot_coord);
        float t = knot_coord - float(j);
        vec3 cf = vec3(params.coeffsHigh[j], params.coeffsHigh[j + 1], params.coeffsHigh[j + 2]);
        logy = dot(vec3(t * t, t, 1.0), splineM * cf);
    } else {
        logy = logx * params.slopeHigh + (log10_aces(params.maxPoint.y) - params.slopeHigh * logMaxPoint);
    }
    return pow(10.0, logy);
}

float moncurve_r(float y, float gamma, float offs) {
    float yb = pow(offs * gamma / ((gamma - 1.0) * (1.0 + offs)), gamma);
    float rs = pow((gamma - 1.0) / offs, gamma - 1.0) * pow((1.0 + offs) / gamma, gamma);
    return y >= yb ? (1.0 + offs) * pow(y, 1.0 / gamma) - offs : y * rs;
}

vec3 dark_surround_to_dim_surround(vec3 linearCV) {
    const float dimSurroundGamma = 0.9811;
    vec3 XYZ = linearCV * s_ap1ToXyz;
    float mul = 1.0 / max(XYZ.x + XYZ.y + XYZ.z, 1e-10);
    vec3 xyY = vec3(XYZ.x * mul, XYZ.y * mul, XYZ.y);
    xyY.z = max(xyY.z, 0.0);
    xyY.z = pow(xyY.z, dimSurroundGamma);
    float mul2 = xyY.z / max(xyY.y, 1e-10);
    XYZ = vec3(xyY.x * mul2, xyY.z, (1.0 - xyY.x - xyY.y) * mul2);
    return XYZ * s_xyzToAp1;
}

vec3 RRT_Full(vec3 aces) {
    float saturation = rgbToSaturation(aces);
    float ycIn = rgbToYc(aces);
    float s = SigmoidShaper(saturation * 5.0 - 2.0);
    float addedGlow = 1.0 + GlowFwd(ycIn, 0.05 * s, 0.08);
    aces *= addedGlow;
    float hue = rgbToHue(aces);
    float centeredHue = CenterHue(hue, 0.0);
    float hueWeight = CubicBasisShaperFit(centeredHue, 135.0);
    aces.r += hueWeight * saturation * (0.03 - aces.r) * (1.0 - 0.82);
    aces = clamp(aces, vec3(0.0), vec3(65535.0));
    vec3 rgbPre = clamp(aces * s_ap0ToAp1, vec3(0.0), vec3(65535.0));
    float luminance = dot(rgbPre, vec3(0.2126, 0.7152, 0.0722));
    rgbPre = mix(vec3(luminance), rgbPre, 0.96);
    vec3 rgbPost;
    rgbPost.r = segmented_spline_c5_fwd(rgbPre.r, RRT_PARAMS);
    rgbPost.g = segmented_spline_c5_fwd(rgbPre.g, RRT_PARAMS);
    rgbPost.b = segmented_spline_c5_fwd(rgbPre.b, RRT_PARAMS);
    return rgbPost;
}

vec3 ODT_sRGB_100nits(vec3 rgbPre) {
    SegmentedSplineParams_c9 params = ODT_48nits;
    params.minPoint.x = segmented_spline_c5_fwd(params.minPoint.x, RRT_PARAMS);
    params.midPoint.x = segmented_spline_c5_fwd(params.midPoint.x, RRT_PARAMS);
    params.maxPoint.x = segmented_spline_c5_fwd(params.maxPoint.x, RRT_PARAMS);
    vec3 rgbPost;
    rgbPost.r = segmented_spline_c9_fwd(rgbPre.r, params);
    rgbPost.g = segmented_spline_c9_fwd(rgbPre.g, params);
    rgbPost.b = segmented_spline_c9_fwd(rgbPre.b, params);
    vec3 linearCV = (rgbPost - vec3(0.02)) / (48.0 - 0.02);
    linearCV = dark_surround_to_dim_surround(linearCV);
    float luminance = GetLuminance(linearCV);
    linearCV = mix(vec3(luminance), linearCV, 0.93);
    vec3 XYZ = linearCV * s_ap1ToXyz;
    XYZ *= D60ToD65_CAT;
    linearCV = XYZ * xyzToRgb;
    linearCV = saturate(linearCV);
    vec3 outputCV;
    outputCV.r = moncurve_r(linearCV.r, 2.4, 0.055);
    outputCV.g = moncurve_r(linearCV.g, 2.4, 0.055);
    outputCV.b = moncurve_r(linearCV.b, 2.4, 0.055);
    return outputCV;
}

vec3 AcademyFull(vec3 color) {
    color *= 1.4;
    color *= sRGBtoACES;
    color = RRT_Full(color);
    color = ODT_sRGB_100nits(color);
    return color;
}

// DerivativeMain/lib/Post/AgX.glsl — AgX_Full.
// Rec.2020 conversion, configurable gamut compression, and parametric curve.
const mat3 LINEAR_SRGB_TO_LINEAR_REC2020 = mat3(
    vec3(0.627404, 0.069097, 0.016391),
    vec3(0.329283, 0.919540, 0.088013),
    vec3(0.043313, 0.011362, 0.895595));
const mat3 LINEAR_REC2020_TO_LINEAR_SRGB = mat3(
    vec3(1.660491, -0.124550, -0.018151),
    vec3(-0.587641, 1.132900, -0.100579),
    vec3(-0.072850, -0.008349, 1.118730));

const float agx_slope = 2.4;
const float agx_toe_power = 3.0;
const float agx_shoulder_power = 3.25;
const vec3 agx_compression = vec3(0.1, 0.1, 0.15);
const vec3 agx_rotation = vec3(2.0, -1.0, -3.0);
const float PI = radians(180.0);

vec3 SRGBtoLinear(vec3 color) {
    return mix(color / 12.92, pow((color + 0.055) / 1.055, vec3(2.4)), lessThan(vec3(0.04045), color));
}

vec3 unproject(vec2 xy) {
    if (xy.y == 0.0) return vec3(0.0);

    float Y = 1.0;
    float X = xy.x / xy.y;
    float Z = (1.0 - xy.x - xy.y) / xy.y;

    return vec3(X, Y, Z);
}

mat3 primaries_to_matrix(vec2 xy_red, vec2 xy_green, vec2 xy_blue, vec2 xy_white) {
    vec3 XYZ_red = unproject(xy_red);
    vec3 XYZ_green = unproject(xy_green);
    vec3 XYZ_blue = unproject(xy_blue);
    vec3 XYZ_white = unproject(xy_white);

    mat3 temp = mat3(
        XYZ_red.x, XYZ_green.x, XYZ_blue.x,
        1.0,       1.0,         1.0,
        XYZ_red.z, XYZ_green.z, XYZ_blue.z);

    mat3 inverseMatrix = inverse(temp);
    vec3 scale = XYZ_white * inverseMatrix;

    return mat3(
        scale.x * XYZ_red.x, scale.y * XYZ_green.x, scale.z * XYZ_blue.x,
        scale.x * XYZ_red.y, scale.y * XYZ_green.y, scale.z * XYZ_blue.y,
        scale.x * XYZ_red.z, scale.y * XYZ_green.z, scale.z * XYZ_blue.z);
}

float RotationToSlide(vec2 primary, vec2 neighborA, vec2 neighborB, float angle) {
    vec2 neighbor = angle >= 0.0 ? neighborA : neighborB;

    float distance_to_neighbor = distance(primary, neighbor);
    float distance_to_center = length(primary);
    float side = sin(angle / 180.0 * PI) * distance_to_center;

    return side / distance_to_neighbor;
}

vec2 SlidePrimary(vec2 primary, vec2 neighborA, vec2 neighborB, float amount) {
    return mix(primary, amount >= 0.0 ? neighborA : neighborB, saturate(abs(amount)));
}

mat3 ComputeCompressionMatrix(vec2 xyR, vec2 xyG, vec2 xyB, vec2 xyW) {
    vec2 offsetR = xyR - xyW;
    vec2 offsetG = xyG - xyW;
    vec2 offsetB = xyB - xyW;

    vec3 slide = vec3(0.0);
    slide.r = RotationToSlide(offsetR, offsetB, offsetG, agx_rotation.r);
    slide.g = RotationToSlide(offsetG, offsetR, offsetB, agx_rotation.g);
    slide.b = RotationToSlide(offsetB, offsetG, offsetR, agx_rotation.b);

    vec3 scale_factor = 1.0 / (1.0 - agx_compression);

    vec2 R = (SlidePrimary(offsetR, offsetB, offsetG, slide.r) * scale_factor.r) + xyW;
    vec2 G = (SlidePrimary(offsetG, offsetR, offsetB, slide.g) * scale_factor.g) + xyW;
    vec2 B = (SlidePrimary(offsetB, offsetG, offsetR, slide.b) * scale_factor.b) + xyW;
    vec2 W = xyW;

    return primaries_to_matrix(R, G, B, W);
}

vec3 open_domain_to_normalized_log2(vec3 in_od, float minimum_ev, float maximum_ev) {
    const float middle_grey = 0.18;
    float total_exposure = maximum_ev - minimum_ev;
    vec3 output_log = clamp(log2(in_od / middle_grey), minimum_ev, maximum_ev);
    return (output_log - minimum_ev) / total_exposure;
}

float equation_hyperbolic(float x, float power) {
    return x / pow(1.0 + pow(x, power), 1.0 / power);
}

float equation_scale(float x_pivot, float y_pivot, float slope_pivot, float power) {
    return pow(pow(slope_pivot * x_pivot, -power) * (pow(slope_pivot * (x_pivot / y_pivot), power) - 1.0), -1.0 / power);
}

float equation_term(float x, float x_pivot, float slope_pivot, float scale) {
    return (slope_pivot * (x - x_pivot)) / scale;
}

float equation_curve(float x, float x_pivot, float y_pivot, float slope_pivot, float toe_power, float shoulder_power, float scale) {
    if (scale < 0.0) {
        return scale * equation_hyperbolic(equation_term(x, x_pivot, slope_pivot, scale), toe_power) + y_pivot;
    } else {
        return scale * equation_hyperbolic(equation_term(x, x_pivot, slope_pivot, scale), shoulder_power) + y_pivot;
    }
}

float equation_full_curve(float x, float x_pivot, float y_pivot, float slope_pivot, float toe_power, float shoulder_power) {
    bool bpivot = x >= x_pivot;
    float scale_x_pivot = mix(x_pivot, 1.0 - x_pivot, bpivot);
    float scale_y_pivot = mix(y_pivot, 1.0 - y_pivot, bpivot);

    float toe_scale = equation_scale(scale_x_pivot, scale_y_pivot, slope_pivot, toe_power);
    float shoulder_scale = equation_scale(scale_x_pivot, scale_y_pivot, slope_pivot, shoulder_power);
    float scale = mix(-toe_scale, shoulder_scale, bpivot);

    return equation_curve(x, x_pivot, y_pivot, slope_pivot, toe_power, shoulder_power, scale);
}

vec3 AgXConfigurable(vec3 rgb) {
    mat3 sRGB_to_XYZ = primaries_to_matrix(
        vec2(0.708, 0.292),
        vec2(0.170, 0.797),
        vec2(0.131, 0.046),
        vec2(0.3127, 0.3290));

    mat3 adjusted_to_XYZ = ComputeCompressionMatrix(
        vec2(0.708, 0.292),
        vec2(0.170, 0.797),
        vec2(0.131, 0.046),
        vec2(0.3127, 0.3290));

    mat3 XYZ_to_adjusted = inverse(adjusted_to_XYZ);

    vec3 xyz = rgb * sRGB_to_XYZ;
    vec3 adjustedRGB = xyz * XYZ_to_adjusted;

    const float min_ev = -8.48;
    const float max_ev = 5.52;

    float x_pivot = abs(min_ev) / (max_ev - min_ev);
    float y_pivot = 0.5;

    vec3 logRGB = open_domain_to_normalized_log2(adjustedRGB, min_ev, max_ev);

    float outputR = equation_full_curve(logRGB.r, x_pivot, y_pivot, agx_slope, agx_toe_power, agx_shoulder_power);
    float outputG = equation_full_curve(logRGB.g, x_pivot, y_pivot, agx_slope, agx_toe_power, agx_shoulder_power);
    float outputB = equation_full_curve(logRGB.b, x_pivot, y_pivot, agx_slope, agx_toe_power, agx_shoulder_power);

    return saturate(vec3(outputR, outputG, outputB));
}

vec3 AgX_Full(vec3 rgb) {
    rgb = LINEAR_SRGB_TO_LINEAR_REC2020 * max(rgb, vec3(0.0));
    rgb = AgXConfigurable(rgb);
    rgb = SRGBtoLinear(rgb);
    rgb = LINEAR_REC2020_TO_LINEAR_SRGB * rgb;
    rgb = LinearToSRGB(rgb);
    return rgb;
}

// DerivativeMain/lib/Post/PurkinjeShift.glsl
// Simulates the Purkinje effect: at very low luminance, human vision shifts
// from cone-mediated (photopic) to rod-mediated (scotopic) perception,
// biasing sensitivity toward blue-green wavelengths.
//
// Mecraft adaptation: clamp scotopicLuminance and purkinje to prevent
// numerical instability. Without clamping, dark scenes cause (xyz.y+xyz.z)/xyz.x
// to explode, producing blue-white blocks. The purkinje clamp ensures the effect
// remains subtle even during daytime.

vec3 PurkinjeShift(vec3 color) {
    const vec3 rodResponse = vec3(7.15e-5, 4.81e-1, 3.28e-1);
    vec3 xyz = color * rgbToXyz;
    // Clamp denominator to avoid division by zero.
    float denom = max(xyz.x, 1e-5);
    float scalar = 1.33 * (1.0 + (xyz.y + xyz.z) / denom) - 1.68;
    vec3 scotopicLuminance = clamp(xyz * scalar, vec3(0.0), vec3(1.0));
    float purkinje = clamp(dot(rodResponse, scotopicLuminance * xyzToRgb), 0.0, 0.08);
    return mix(color, purkinje * vec3(0.5, 0.7, 1.0), fastExp(-purkinje * 90.0));
}

// DerivativeMain/lib/Common.inc helpers

const mat3 acesAp0ToXyz = mat3(
     0.9525523959,  0.0000000000,  0.0000936786,
     0.3439664498,  0.7281660966, -0.0721325464,
     0.0000000000,  0.0000000000,  1.0088251844
);
const mat3 acesXyzToAp0 = mat3(
     1.0498110175,  0.0000000000, -0.0000974845,
    -0.4959030231,  1.3733130458,  0.0982400361,
     0.0000000000,  0.0000000000,  0.9912520182
);

const mat3 acesAp1ToXyz = mat3(
     0.6624541811,  0.1340042065,  0.1561876870,
     0.2722287168,  0.6740817658,  0.0536895174,
    -0.0055746495,  0.0040607335,  1.0103391003
);
const mat3 acesXyzToAp1 = mat3(
     1.6410233797, -0.3248032942, -0.2364246952,
    -0.6636628587,  1.6153315917,  0.0167563477,
     0.0117218943, -0.0082844420,  0.9883948585
);

const mat3 acesAp0ToAp1 = acesAp0ToXyz * acesXyzToAp1;
const mat3 acesAp1ToAp0 = acesAp1ToXyz * acesXyzToAp0;

const float rrtGlowGain = 0.05;
const float rrtGlowMid = 0.08;
const float rrtRedScale = 0.82;
const float rrtRedPivot = 0.03;
const float rrtRedHue = 0.0;
const float rrtRedWidth = 135.0;
const float rrtSatFactor = 0.96;
const float odtSatFactor = 0.93;

vec3 RRTSweeteners(vec3 aces) {
    float saturation = rgbToSaturation(aces);
    float ycIn = rgbToYc(aces);
    float s = SigmoidShaper(saturation * 5.0 - 2.0);
    float addedGlow = 1.0 + GlowFwd(ycIn, rrtGlowGain * s, rrtGlowMid);
    aces *= addedGlow;

    float hue = rgbToHue(aces);
    float centeredHue = CenterHue(hue, rrtRedHue);
    float hueWeight = CubicBasisShaperFit(centeredHue, rrtRedWidth);
    aces.r += hueWeight * saturation * (rrtRedPivot - aces.r) * oneMinus(rrtRedScale);

    aces = clamp16F(aces);
    vec3 rgbPre = clamp16F(aces * acesAp0ToAp1);

    float luminance = GetLuminance(rgbPre);
    return mix(vec3(luminance), rgbPre, rrtSatFactor);
}

vec3 RRTAndODTFit(vec3 rgb) {
    vec3 a = rgb * (rgb + 0.0245786) - 0.000090537;
    vec3 b = rgb * (0.983729 * rgb + 0.4329510) + 0.238081;
    return a / b;
}

vec3 AcademyFit(vec3 rgb) {
    rgb *= 1.4;
    rgb = RRTSweeteners(rgb * acesAp1ToAp0);
    rgb = RRTAndODTFit(rgb);
    rgb = mix(vec3(GetLuminance(rgb)), rgb, odtSatFactor);
    return LinearToSRGB(rgb);
}



float tonemapReinhardScalar(float value) {
    return value / (value + 1.0);
}

float tonemapAcesScalar(float value) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((value * (a * value + b)) / (value * (c * value + d) + e), 0.0, 1.0);
}

float tonemapFilmicScalar(float value) {
    value = max(0.0, value - 0.004);
    return clamp((value * (6.2 * value + 0.5)) / (value * (6.2 * value + 1.7) + 0.06), 0.0, 1.0);
}

// Luma-preserving tonemap for Mecraft-extra modes (0=Reinhard, 2=Filmic).
// DerivativeMain modes (1=AcademyFit, 3=AgX_Minimal, 4=AcademyFull, 5=AgX_Full)
// are handled directly in applyTonemap() and never reach this path.
vec3 tonemapPreserveLuma(vec3 color) {
    color = max(color, vec3(0.0));
    float lumaIn = max(dot(color, vec3(0.2126, 0.7152, 0.0722)), 0.00001);
    float lumaOut;
    vec3 channelMapped;
    if (uTonemapMode == 2) {
        lumaOut = tonemapFilmicScalar(lumaIn);
        channelMapped = tonemapFilmic(color);
    } else {
        lumaOut = tonemapReinhardScalar(lumaIn);
        channelMapped = tonemapReinhard(color);
    }
    vec3 lumaMapped = color * (lumaOut / lumaIn);
    float highlight = smoothstep(0.35, 2.5, lumaIn);
    return clamp(mix(channelMapped, lumaMapped, 0.72 + 0.18 * highlight), 0.0, 1.0);
}

vec3 applyColorTemperature(vec3 color) {
    float t = clamp(uColorTemperature, 0.0, 2.0) - 1.0;
    vec3 warm = vec3(1.08, 1.00, 0.90);
    vec3 cool = vec3(0.90, 0.98, 1.10);
    return color * mix(vec3(1.0), t >= 0.0 ? warm : cool, abs(t));
}

vec3 applyVibrance(vec3 color) {
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float maxChannel = max(max(color.r, color.g), color.b);
    float minChannel = min(min(color.r, color.g), color.b);
    float colorfulness = clamp(maxChannel - minChannel, 0.0, 1.0);
    float amount = uVibrance * (1.0 - colorfulness);
    return mix(vec3(luminance), color, 1.0 + amount);
}


vec3 applySplitTone(vec3 color) {
    float lum = luma709(color);
    vec3 shadowTint = vec3(0.88, 0.94, 1.08);
    vec3 highlightTint = vec3(1.10, 1.035, 0.90);
    float shadowWeight = 1.0 - smoothstep(0.02, 0.55, lum);
    float highlightWeight = smoothstep(0.38, 1.0, lum);
    vec3 toned = color;
    toned *= mix(vec3(1.0), shadowTint, shadowWeight * 0.42);
    toned *= mix(vec3(1.0), highlightTint, highlightWeight * 0.50);
    return mix(color, toned, saturate(uSplitToneStrength));
}

vec3 applyAgxLook(vec3 color) {
    float lum = luma709(color);
    float chromaBoost = mix(1.12, 1.02, smoothstep(0.18, 0.85, lum));
    color = mix(vec3(lum), color, chromaBoost);
    vec3 coolShadows = vec3(0.88, 0.97, 1.12);
    vec3 warmHighlights = vec3(1.08, 1.025, 0.91);
    color *= mix(vec3(1.0), coolShadows, (1.0 - smoothstep(0.025, 0.36, lum)) * 0.20);
    color *= mix(vec3(1.0), warmHighlights, smoothstep(0.42, 1.0, lum) * 0.14);
    return saturate(color);
}

vec3 applyVignette(vec3 color, vec2 uv) {
    vec2 p = uv * 2.0 - 1.0;
    p.x *= 1.15;
    float radial = dot(p, p);
    float fade = 1.0 - smoothstep(0.35, 1.42, radial) * uVignetteStrength;
    return color * clamp(fade, 0.5, 1.0);
}

float readResolvedExposure() {
    if (uAutoExposureEnabled) {
        return texelFetch(uExposureStateTex, ivec2(0, 0), 0).r;
    }
    return uExposure;
}

vec3 applyExposure(vec3 color) {
    float evBias = pow(2.0, uWeatherExposureBias);
    return color * max(g_resolvedExposure * evBias, 0.001);
}

// DerivativeMain Grade.glsl: tonemap is a pure LDR mapping.
// Exposure and vignette are applied in main() before this call.
vec3 applyTonemap(vec3 color) {
    if (uShaderpackGradingEnabled) {
        if (uTonemapMode == 1) {
            color = AcademyFit(color);
        } else if (uTonemapMode == 4) {
            color = AcademyFull(color);
        } else if (uTonemapMode == 3) {
            color = AgX_Minimal(color);
        } else if (uTonemapMode == 5) {
            color = AgX_Full(color);
        } else {
            color = tonemapPreserveLuma(color);
        }
    } else {
        color = vec3(1.0) - exp(-color);
        float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
        color = mix(vec3(luminance), color, uSaturation);
        color = (color - 0.5) * uContrast + 0.5;
        color = pow(max(color, vec3(0.0)), vec3(1.0 / max(uGamma, 0.001)));
    }
    return color;
}

// DerivativeMain Common.inc: bicubic (Mitchell-Netravali) interpolation.
// Used for bloom mip upsampling to reduce bilinear blockiness at low resolutions.
vec4 cubic(float x) {
    float x2 = x * x;
    float x3 = x2 * x;
    vec4 w;
    w.x = -x3 + 3.0 * x2 - 3.0 * x + 1.0;
    w.y = 3.0 * x3 - 6.0 * x2 + 4.0;
    w.z = -3.0 * x3 + 3.0 * x2 + 3.0 * x + 1.0;
    w.w = x3;
    return w * (1.0 / 6.0);
}

vec3 textureBicubic3(sampler2D tex, vec2 coord) {
    vec2 res = vec2(textureSize(tex, 0));
    coord = coord * res - 0.5;
    vec2 fTexel = fract(coord);
    coord -= fTexel;
    vec4 xCubic = cubic(fTexel.x);
    vec4 yCubic = cubic(fTexel.y);
    vec4 c = coord.xxyy + vec2(-0.5, 1.5).xyxy;
    vec4 s = vec4(xCubic.xz + xCubic.yw, yCubic.xz + yCubic.yw);
    vec4 offset = c + vec4(xCubic.y, xCubic.w, yCubic.y, yCubic.w) / s;
    offset *= 1.0 / res.xxyy;
    vec3 sample0 = texture(tex, offset.xz).rgb;
    vec3 sample1 = texture(tex, offset.yz).rgb;
    vec3 sample2 = texture(tex, offset.xw).rgb;
    vec3 sample3 = texture(tex, offset.yw).rgb;
    float sx = s.x / (s.x + s.y);
    float sy = s.z / (s.z + s.w);
    return mix(mix(sample3, sample2, sx), mix(sample1, sample0, sx), sy);
}

vec3 sampleBloomMip(int mip, vec2 uv) {
    // Bloom mips are already blurred; hardware bilinear avoids 4 texture taps
    // per mip during the full-res composite.
    // Mecraft adaptation: each mip is a separate texture, no atlas tile offset.
    if (mip == 0) return texture(uBloomMip0, uv).rgb;
    if (mip == 1) return texture(uBloomMip1, uv).rgb;
    if (mip == 2) return texture(uBloomMip2, uv).rgb;
    if (mip == 3) return texture(uBloomMip3, uv).rgb;
    if (mip == 4) return texture(uBloomMip4, uv).rgb;
    if (mip == 5) return texture(uBloomMip5, uv).rgb;
    return texture(uBloomMip6, uv).rgb;
}

void CalculateBloomFog(vec2 uv, out vec3 bloomData, out vec3 fogBloom) {
    // DerivativeMain/program/Post/Grade.glsl: CalculateBloomFog().
    // Mecraft adaptation: mips are bound as separate textures instead of atlas tiles.
    vec3 sampleTile = sampleBloomMip(0, uv);
    bloomData = sampleTile;
    fogBloom = sampleTile;
    if (uBloomMipCount <= 1) {
        fogBloom += bloomData;
        return;
    }

    sampleTile = sampleBloomMip(1, uv);
    bloomData += sampleTile * 0.83333333;
    fogBloom += sampleTile * 1.5;
    if (uBloomMipCount <= 2) {
        bloomData *= 0.42666150;
        fogBloom *= 0.39473684;
        fogBloom += bloomData;
        return;
    }

    sampleTile = sampleBloomMip(2, uv);
    bloomData += sampleTile * 0.69444444;
    fogBloom += sampleTile * 2.25;
    if (uBloomMipCount <= 3) {
        bloomData *= 0.39560440;
        fogBloom *= 0.21052632;
        fogBloom += bloomData;
        return;
    }

    sampleTile = sampleBloomMip(3, uv);
    bloomData += sampleTile * 0.57870370;
    fogBloom += sampleTile * 3.375;
    if (uBloomMipCount <= 4) {
        bloomData *= 0.32253979;
        fogBloom *= 0.12345679;
        fogBloom += bloomData;
        return;
    }

    sampleTile = sampleBloomMip(4, uv);
    bloomData += sampleTile * 0.48225309;
    fogBloom += sampleTile * 5.0625;
    if (uBloomMipCount <= 5) {
        bloomData *= 0.27809341;
        fogBloom *= 0.07692308;
        fogBloom += bloomData;
        return;
    }

    sampleTile = sampleBloomMip(5, uv);
    bloomData += sampleTile * 0.40187757;
    fogBloom += sampleTile * 7.59375;
    if (uBloomMipCount <= 6) {
        bloomData *= 0.24905660;
        fogBloom *= 0.04854369;
        fogBloom += bloomData;
        return;
    }

    sampleTile = sampleBloomMip(6, uv);
    bloomData += sampleTile * 0.33489798;
    fogBloom += sampleTile * 11.328125;

    bloomData *= 0.23118661;
    fogBloom *= 0.03108305;
    fogBloom += bloomData;
}

// Debug outputs captured during resolveHdrColor for use in debug views.
vec3 g_debugBloomData = vec3(0.0);
vec3 g_debugFogBloom = vec3(0.0);
vec3 g_debugColorBeforeBloomyFog = vec3(0.0);
vec3 g_debugColorAfterBloomyFog = vec3(0.0);
float g_debugRainMask = 0.0;

// ===== Screen Raindrop Effect =====
// Adapted from Heartfelt shader by Martijn Steinrucken (BigWings)
// https://www.shadertoy.com/view/ltffzl
// License: Creative Commons Attribution-NonCommercial-ShareAlike 3.0

#define RAINDROP_SIZE 0.2
#define RAINDROP_S(a, b, t) smoothstep(a, b, t)

// Hash functions for raindrop randomization
vec3 raindropN13(float p) {
    vec3 p3 = fract(vec3(p) * vec3(0.1031, 0.11369, 0.13787));
    p3 += dot(p3, p3.yzx + 19.19);
    return fract(vec3((p3.x + p3.y) * p3.z, (p3.x + p3.z) * p3.y, (p3.y + p3.z) * p3.x));
}

float raindropN(float t) {
    return fract(sin(t * 12345.564) * 7658.76);
}

float raindropSaw(float b, float t) {
    return RAINDROP_S(0.0, b, t) * RAINDROP_S(1.0, b, t);
}

// Calculate raindrop distortion (returns xy offset for UV distortion and trail intensity)
vec2 calculateRaindrops(vec2 uv, float t) {
    vec2 UV = uv;

    // Define grid for raindrops - add time offset for falling animation
    uv.y += t * 0.8; // Vertical scroll speed for falling animation
    vec2 gridScale = vec2(6.0, 1.0);
    vec2 grid = gridScale * 2.0;
    vec2 id = floor(uv * grid);

    // Random Y shift per column
    float colShift = raindropN(id.x);
    uv.y += colShift;

    // Recalculate cell ID after shift
    id = floor(uv * grid);
    vec3 n = raindropN13(id.x * 35.2 + id.y * 2376.1);
    vec2 st = fract(uv * grid) - vec2(0.5, 0.0);

    // Position drops within cell
    float x = n.x - 0.5;
    float y = UV.y * 20.0;

    // Add horizontal distortion based on vertical position
    float distort = sin(y + sin(y));
    x += distort * (0.5 - abs(x)) * (n.z - 0.5);
    x *= 0.7;

    // Animate drop falling
    float ti = fract(t + n.z);
    y = (raindropSaw(0.85, ti) - 0.5) * 0.9 + 0.5;
    vec2 p = vec2(x, y);

    // Calculate drop shape
    float d = length((st - p) * gridScale.yx);
    float dSize = RAINDROP_SIZE;
    float drop = RAINDROP_S(dSize, 0.0, d);

    // Calculate trail
    float r = sqrt(RAINDROP_S(1.0, y, st.y));
    float cd = abs(st.x - x);
    float trail = RAINDROP_S((dSize * 0.5 + 0.03) * r, (dSize * 0.5 - 0.05) * r, cd);
    float trailFront = RAINDROP_S(-0.02, 0.02, st.y - y);
    trail *= trailFront;

    // Calculate droplets along trail
    y = UV.y;
    y += raindropN(id.x);
    float trail2 = RAINDROP_S(dSize * r, 0.0, cd);
    float droplets = max(0.0, (sin(y * (1.0 - y) * 120.0) - st.y)) * trail2 * trailFront * n.z;
    y = fract(y * 10.0) + (st.y - 0.5);
    float dd = length(st - vec2(x, y));
    droplets = RAINDROP_S(dSize * raindropN(id.x), 0.0, dd);

    float m = drop + droplets * r * trailFront;
    return vec2(m, trail);
}

// Static raindrops (for subtle additional texture)
float staticRaindrops(vec2 uv, float t) {
    uv *= 30.0;
    vec2 id = floor(uv);
    uv = fract(uv) - 0.5;
    vec3 n = raindropN13(id.x * 107.45 + id.y * 3543.654);
    vec2 p = (n.xy - 0.5) * 0.5;
    float d = length(uv - p);
    float fade = raindropSaw(0.025, fract(t + n.z));
    float c = RAINDROP_S(RAINDROP_SIZE, 0.0, d) * fract(n.z * 10.0) * fade;
    return c;
}

// Combined rain effect with two layers
vec2 applyScreenRain(vec2 uv, float t) {
    // Screen UVs are top-left on every backend, but the raindrop pattern is
    // authored in a bottom-left (y-up) space; flip so the time scroll moves
    // drops toward the bottom of the screen instead of climbing upward.
    uv.y = 1.0 - uv.y;
    float s = staticRaindrops(uv, t);
    vec2 r1 = calculateRaindrops(uv, t);
    vec2 r2 = calculateRaindrops(uv * 1.8, t);

    float c = s + r1.x + r2.x;
    c = RAINDROP_S(0.3, 1.0, c);

    return vec2(c, max(r1.y, r2.y));
}

// Calculate normal map from raindrop pattern for UV distortion
vec2 calculateRaindropDistortion(vec2 uv, float t, float intensity) {
    if (intensity < 0.01) {
        return vec2(0.0);
    }

    // Sample raindrop pattern
    vec2 rain = applyScreenRain(uv, t);

    // Calculate normals via finite difference
    vec2 offset = vec2(0.001, 0.0);
    float cx = applyScreenRain(uv + offset, t).x;
    float cy = applyScreenRain(uv + offset.yx, t).x;
    vec2 normal = vec2(cx - rain.x, cy - rain.x);

    // Apply refraction distortion scaled by intensity
    // Increased from 0.04 to 0.08 for more visible effect
    return normal * intensity * 0.08;
}

float rainMaskAt(vec2 sampleUv) {
    ivec2 depthSize = textureSize(uDepthTex, 0);
    if (depthSize.x <= 0 || depthSize.y <= 0) {
        return 0.0;
    }

    ivec2 depthTexel = ivec2(clamp(sampleUv, vec2(0.0), vec2(0.999999)) * vec2(depthSize));
    float depth = texelFetch(uDepthTex, depthTexel, 0).r;
    float gbufferSkyMask = step(0.9999, depth);

    float sceneSkyMask = 1.0;
    ivec2 sceneDepthSize = textureSize(uSceneDepthTex, 0);
    if (sceneDepthSize.x > 0 && sceneDepthSize.y > 0) {
        ivec2 sceneDepthTexel = ivec2(clamp(sampleUv, vec2(0.0), vec2(0.999999)) * vec2(sceneDepthSize));
        float sceneDepth = texelFetch(uSceneDepthTex, sceneDepthTexel, 0).r;
        sceneSkyMask = step(0.9999, sceneDepth);
    }
    float skyMask = gbufferSkyMask * sceneSkyMask;

    // Mecraft adaptation of DerivativeMain Grade.glsl rain fog:
    // use smooth sky visibility instead of the particle weather mask. The mask is
    // generated without scene depth here, so sampling it in post exposes rain
    // particle silhouettes as a second vertical rain layer.
    return skyMask * clamp(uCameraRainVisibility, 0.0, 1.0);
}

vec3 resolveHdrColor(vec2 sampleUv, vec2 screenUv) {
    vec4 sceneSample = texture(uSceneTex, sampleUv);
    vec3 color = sceneSample.rgb;
    float fogTransmittance = clamp(sceneSample.a, 0.0, 1.0); // from volumetric_composite.fs

    if (uBloomEnabled) {
        vec3 bloomData;
        vec3 fogBloom;
        CalculateBloomFog(sampleUv, bloomData, fogBloom);
        g_debugBloomData = bloomData;
        g_debugFogBloom = fogBloom;
        // DerivativeMain Grade.glsl line 144: exposure compensation
        float bloomAmount = (uBloomStrength * 0.15) / (max(g_resolvedExposure, 1.0) * 0.7 + 0.3);

        // DerivativeMain Grade.glsl line 136-139: Bloomy Fog
        g_debugColorBeforeBloomyFog = color;
        if (uBloomyFogEnabled && fogTransmittance < 0.999) {
            color = mix(fogBloom * 0.5, color, fogTransmittance);
        }
        g_debugColorAfterBloomyFog = color;
        color += bloomData * bloomAmount;

        // DerivativeMain Grade.glsl:148-153 — rain fog pass.
        // rain = colortex0.b * 0.35 (already factored into rainMaskAt).
        // color = color * oneMinus(rain) + fogBloom * fma(exposure, 0.15, 0.3) * rain
        float wetness = clamp(uSkyWetness, 0.0, 1.0);
        if (!uUnderwaterEnabled && wetness > 0.01) {
            float rain = rainMaskAt(sampleUv);  // already * 0.35
            g_debugRainMask = rain;
            float fogScale = clamp(uWeatherPostRainFog, 0.0, 2.0);
            float snowAmt = clamp(uSnowStrength, 0.0, 1.0);
            float rainAmt = clamp(wetness - snowAmt, 0.0, 1.0);
            // Rain fog: DerivativeMain formula
            float rainBlend = rain * rainAmt * fogScale;
            float rainFogAmount = clamp(g_resolvedExposure, 0.6, 2.0) * 0.15 + 0.3;
            color = color * (1.0 - rainBlend) + fogBloom * rainFogAmount * rainBlend;
            // Snow fog: whiter, higher density
            float snowBlend = rain * snowAmt * fogScale * 1.4;
            vec3 snowFogBloom = mix(fogBloom, vec3(dot(fogBloom, vec3(0.299, 0.587, 0.114)) * 1.1), 0.4);
            float snowFogAmount = clamp(g_resolvedExposure, 0.6, 2.0) * 0.20 + 0.4;
            color = color * (1.0 - snowBlend) + snowFogBloom * snowFogAmount * snowBlend;
        }
    }

    if (uUnderwaterEnabled) {
        float strength = clamp(uUnderwaterStrength, 0.0, 1.0);
        vec3 underwaterTint = srgbToLinear(uUnderwaterTint);
        vec3 tinted = color * underwaterTint;
        color = mix(color, tinted, strength);

        float fog = clamp((1.0 - screenUv.y) * 0.10 * strength, 0.0, 0.12);
        color = mix(color, underwaterTint, fog);
    }
    return color;
}

vec3 applyCasLikeSharpen(vec3 center, vec2 sampleUv, vec2 screenUv) {
    float strength = saturate(uSharpenStrength);
    if (strength <= 0.0001) {
        return center;
    }

    vec2 texel = 1.0 / vec2(textureSize(uSceneTex, 0));
    vec2 uv10 = clamp(sampleUv + texel * vec2( 0.0, -1.0), vec2(0.0), vec2(1.0));
    vec2 uv01 = clamp(sampleUv + texel * vec2(-1.0,  0.0), vec2(0.0), vec2(1.0));
    vec2 uv21 = clamp(sampleUv + texel * vec2( 1.0,  0.0), vec2(0.0), vec2(1.0));
    vec2 uv12 = clamp(sampleUv + texel * vec2( 0.0,  1.0), vec2(0.0), vec2(1.0));

    vec3 centerHdr = texture(uSceneTex, sampleUv).rgb;
    vec3 northHdr = texture(uSceneTex, uv10).rgb;
    vec3 westHdr = texture(uSceneTex, uv01).rgb;
    vec3 eastHdr = texture(uSceneTex, uv21).rgb;
    vec3 southHdr = texture(uSceneTex, uv12).rgb;

    vec3 minHdr = min(centerHdr, min(northHdr, min(westHdr, min(eastHdr, southHdr))));
    vec3 maxHdr = max(centerHdr, max(northHdr, max(westHdr, max(eastHdr, southHdr))));
    vec3 laplace = centerHdr * 4.0 - northHdr - westHdr - eastHdr - southHdr;
    vec3 sharpenedHdr = clamp(centerHdr + laplace * (0.08 * strength), minHdr, maxHdr);

    float centerLuma = max(dot(centerHdr, vec3(0.2126, 0.7152, 0.0722)), 1e-5);
    float sharpenedLuma = max(dot(sharpenedHdr, vec3(0.2126, 0.7152, 0.0722)), 1e-5);
    float lumaRatio = clamp(sharpenedLuma / centerLuma, 0.75, 1.35);
    return clamp(center * lumaRatio, 0.0, 1.0);
}

void main() {
    g_resolvedExposure = readResolvedExposure();

    vec2 centeredUv = vScreenUv - vec2(0.5, 0.5);
    float roll = uShaderpackGradingEnabled ? 0.0 : uScreenRollRadians;
    float c = cos(roll);
    float s = sin(roll);
    mat2 rot = mat2(c, -s,
                    s,  c);
    vec2 rolledScreenUv = rot * centeredUv + vec2(0.5, 0.5);

    // Apply screen raindrop distortion during rain/storm
    // Combine storm intensity with the wetness-driven screen-rain contribution.
    float rainIntensity = max(uWeatherStorm, uWeatherWetness * 0.5);
    vec2 raindropDistortion = vec2(0.0);
    if (rainIntensity > 0.01) {
        // Scale time appropriately - uTime cycles 0-8192 over ~2.3 hours
        // Original shader expects time in seconds with reasonable increment
        float t = uTime * 0.15; // Adjusted scale for visible but not too fast animation
        raindropDistortion = calculateRaindropDistortion(
            rolledScreenUv, t, rainIntensity);
    }
    vec2 distortedScreenUv = clamp(
        rolledScreenUv + raindropDistortion, vec2(0.0), vec2(1.0));
    vec2 distortedTextureUv = rhiScreenUvToTextureUv(distortedScreenUv);
    vec2 rolledTextureUv = rhiScreenUvToTextureUv(rolledScreenUv);
    vec2 textureUv = rhiScreenUvToTextureUv(vScreenUv);

    vec3 color = resolveHdrColor(distortedTextureUv, vScreenUv);
    if (uBloomEnabled) {
        if (uSunRaysEnabled && uSunVisibility > 0.001 && uSunRayStrength > 0.001) {
            vec2 toSun = uSunScreenPos - rolledScreenUv;
            float screenFade = 1.0 - smoothstep(0.55, 1.15, length(uSunScreenPos - vec2(0.5)));
            float rayMask = clamp(uSunVisibility * screenFade, 0.0, 1.0);
            vec3 rays = vec3(0.0);
            float weight = 0.16;
            for (int i = 1; i <= 8; ++i) {
                float t = float(i) / 8.0;
                vec2 sampleScreenUv = rolledScreenUv + toSun * t * 0.86;
                vec2 inBounds = step(vec2(0.0), sampleScreenUv) *
                    step(sampleScreenUv, vec2(1.0));
                float valid = inBounds.x * inBounds.y;
                rays += texture(
                    uBloomTex, rhiScreenUvToTextureUv(sampleScreenUv)).rgb * weight * valid;
                weight *= 0.82;
            }
            color += rays * uSunRayStrength * rayMask;
        }
    }

    // DerivativeMain Grade.glsl line 237: PurkinjeShift before exposure/tonemap.
    // Only active in shaderpack grading mode (DerivativeMain-like pipeline).
    if (uPurkinjeShiftEnabled && uShaderpackGradingEnabled) {
        color = PurkinjeShift(color);
    }

    // DerivativeMain Grade.glsl order: Exposure -> Vignette -> Tonemap
    color = applyExposure(color);
    if (uShaderpackGradingEnabled) {
        color = applyVignette(color, rolledScreenUv);
    }
    vec3 graded = applyTonemap(color);

    // Bloom/Fog debug views — must be before CAS sharpen, which re-enters
    // resolveHdrColor for neighbor pixels and would overwrite debug globals.
    if (uPostprocessDebugMode == 1) {
        // BloomData: CalculateBloomFog() output (DerivativeMain weighted mip sum), tonemapped
        FragColor = vec4(g_debugBloomData / (g_debugBloomData + vec3(1.0)), 1.0);
        return;
    }
    if (uPostprocessDebugMode == 2) {
        // FogTransmittance: alpha channel heatmap (white=clear, dark=dense fog)
        float ft = texture(uSceneTex, textureUv).a;
        FragColor = vec4(vec3(ft), 1.0);
        return;
    }
    if (uPostprocessDebugMode == 3) {
        // Bloomy Fog contribution: color difference from fogBloom mix
        vec3 contribution = abs(g_debugColorAfterBloomyFog - g_debugColorBeforeBloomyFog);
        FragColor = vec4(contribution / (contribution + vec3(1.0)), 1.0);
        return;
    }
    if (uPostprocessDebugMode == 4) {
        // Rain mask: white=outdoor/sky (rain visible), black=indoor (rain hidden)
        FragColor = vec4(vec3(rainMaskAt(rolledTextureUv)), 1.0);
        return;
    }
    if (uPostprocessDebugMode == 5) {
        // Debug: visualize time animation (should pulse between red and green)
        float pulse = sin(uTime) * 0.5 + 0.5;
        FragColor = vec4(pulse, 1.0 - pulse, 0.0, 1.0);
        return;
    }
    if (uPostprocessDebugMode == 6) {
        // Debug: visualize raindrop pattern (static, no animation)
        float rainIntensity = max(uWeatherStorm, uWeatherWetness * 0.5);
        if (rainIntensity < 0.01) rainIntensity = 1.0; // Force on for debug
        vec2 rain = applyScreenRain(rolledScreenUv, uTime);
        FragColor = vec4(vec3(rain.x), 1.0);
        return;
    }
    if (uPostprocessDebugMode == 7) {
        // Debug: visualize raindrop distortion vectors (should show flowing pattern)
        float rainIntensity = max(uWeatherStorm, uWeatherWetness * 0.5);
        if (rainIntensity < 0.01) rainIntensity = 1.0; // Force on for debug
        vec2 distortion = calculateRaindropDistortion(
            rolledScreenUv, uTime, rainIntensity);
        FragColor = vec4(abs(distortion.x) * 10.0, abs(distortion.y) * 10.0, 0.0, 1.0);
        return;
    }

    graded = applyCasLikeSharpen(graded, rolledTextureUv, vScreenUv);
    if (uNoiseDitherStrength > 0.0) {
        float noise = texture(uNoiseTex, gl_FragCoord.xy / vec2(textureSize(uNoiseTex, 0))).r - 0.5;
        graded += noise * uNoiseDitherStrength;
    }
    FragColor = vec4(clamp(graded, 0.0, 1.0), 1.0);
}
