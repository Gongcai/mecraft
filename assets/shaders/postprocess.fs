#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneTex;
uniform sampler2D uBloomTex;
uniform sampler2D uNoiseTex;

uniform bool uBloomEnabled;
uniform float uBloomStrength;
uniform float uExposure;
uniform bool uSunRaysEnabled;
uniform vec2 uSunScreenPos;
uniform float uSunVisibility;
uniform float uSunRayStrength;
uniform bool uShaderpackGradingEnabled;
uniform int uTonemapMode;
uniform float uColorTemperature;
uniform float uVibrance;
uniform float uKappaGradingStrength;
uniform float uHighlightCompression;
uniform float uFilmEmulationStrength;
uniform float uRedModifierStrength;
uniform vec3 uColorLuma;
uniform float uSplitToneStrength;
uniform float uVignetteStrength;
uniform float uNoiseDitherStrength;
uniform float uSharpenStrength;
uniform bool uUnderwaterEnabled;
uniform vec3 uUnderwaterTint;
uniform float uUnderwaterStrength;
uniform float uScreenRollRadians;
uniform float uGamma;
uniform float uSaturation;
uniform float uContrast;

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

vec3 tonemapAgx(vec3 color) {
    color = agxInset(max(color, vec3(0.0)));
    color = clamp(log2(max(color, vec3(1e-6))), -12.47393, 4.026069);
    color = (color + 12.47393) / 16.5;
    color = agxContrastApprox(color);
    color = agxOutset(color);
    color = pow(max(color, vec3(0.0)), vec3(2.2));
    return clamp(color, 0.0, 1.0);
}

float saturate(float value) {
    return clamp(value, 0.0, 1.0);
}

vec3 saturate(vec3 value) {
    return clamp(value, 0.0, 1.0);
}

float luma709(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

float maxOf(vec3 v) {
    return max(v.x, max(v.y, v.z));
}

float minOf(vec3 v) {
    return min(v.x, min(v.y, v.z));
}

float sqr(float x) {
    return x * x;
}

float cube(float x) {
    return x * x * x;
}

float curve(float x) {
    return sqr(x) * (3.0 - 2.0 * x);
}

float oneMinus(float x) {
    return 1.0 - x;
}

vec3 clamp16F(vec3 color) {
    return clamp(color, vec3(0.0), vec3(65535.0));
}

vec3 LinearToSRGB(vec3 color) {
    color = max(color, vec3(0.0));
    vec3 lo = color * 12.92;
    vec3 hi = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), color));
}

float GetLuminance(vec3 color) {
    return luma709(color);
}

float rgbToSaturation(vec3 rgb) {
    return (max(maxOf(rgb), 1e-10) - max(minOf(rgb), 1e-10)) / max(maxOf(rgb), 1e-2);
}

float rgbToHue(vec3 rgb) {
    if (rgb.r == rgb.g && rgb.g == rgb.b) {
        return 0.0;
    }

    const float TAU = 6.283185307179586;
    float hue = (360.0 / TAU) * atan(2.0 * rgb.r - rgb.g - rgb.b, sqrt(3.0) * (rgb.g - rgb.b));
    if (hue < 0.0) {
        hue += 360.0;
    }
    return hue;
}

float rgbToYc(vec3 rgb) {
    const float yc_radius_weight = 1.75;
    float chroma = sqrt(rgb.b * (rgb.b - rgb.g) + rgb.g * (rgb.g - rgb.r) + rgb.r * (rgb.r - rgb.b));
    return (rgb.r + rgb.g + rgb.b + yc_radius_weight * chroma) / 3.0;
}

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

float GlowFwd(float ycIn, float glowGainIn, float glowMid) {
    if (ycIn <= 2.0 / 3.0 * glowMid) {
        return glowGainIn;
    }
    if (ycIn >= 2.0 * glowMid) {
        return 0.0;
    }
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
    if (hueCentered < -180.0) {
        hueCentered += 360.0;
    } else if (hueCentered > 180.0) {
        hueCentered -= 360.0;
    }
    return hueCentered;
}

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

float compressLmt(float distToAch, float lim, float thr, float pwr) {
    if (distToAch >= thr) {
        float scl = (lim - thr) / pow(pow((1.0 - thr) / (lim - thr), -pwr) - 1.0, 1.0 / pwr);
        float nd = (distToAch - thr) / max(scl, 0.00001);
        return thr + scl * nd / pow(1.0 + pow(nd, pwr), 1.0 / pwr);
    }
    return distToAch;
}

vec3 acesCompressionLmt(vec3 color) {
    float achromatic = max(max(color.r, color.g), color.b);
    if (achromatic <= 0.00001) {
        return color;
    }
    vec3 distToAch = (achromatic - color) / abs(achromatic);
    const float pwr = 2.07846097; // 1.2 * sqrt(3)
    vec3 compressedDist = vec3(
        compressLmt(distToAch.r, 1.147, 0.815, pwr),
        compressLmt(distToAch.g, 1.264, 0.803, pwr),
        compressLmt(distToAch.b, 1.312, 0.880, pwr)
    );
    vec3 compressed = achromatic - compressedDist * abs(achromatic);
    return mix(color, compressed, saturate(uHighlightCompression));
}

float rgbSaturation(vec3 color) {
    float mx = max(max(color.r, color.g), color.b);
    float mn = min(min(color.r, color.g), color.b);
    return (mx - mn) / max(mx, 0.00001);
}

float hueWeightRed(vec3 color) {
    float redDominance = color.r - max(color.g, color.b);
    float chroma = max(max(color.r, color.g), color.b) - min(min(color.r, color.g), color.b);
    return smoothstep(0.02, 0.22, redDominance) * smoothstep(0.04, 0.45, chroma);
}

vec3 rrtSweeteners(vec3 color) {
    float lum = luma709(color);
    float sat = rgbSaturation(color);
    float glow = 1.0 + 0.035 * smoothstep(0.35, 0.65, sat) * smoothstep(0.05, 1.2, lum);
    color *= glow;

    float redWeight = hueWeightRed(color) * saturate(uRedModifierStrength);
    color.r += redWeight * sat * (0.03 - color.r) * 0.18;

    float gray = luma709(color);
    color = mix(vec3(gray), color, 0.94);
    color = pow(max(color, vec3(0.0)), vec3(0.985));
    return color;
}

vec3 kappaVibranceSaturation(vec3 color) {
    float lum = luma709(color);
    float mn = min(min(color.r, color.g), color.b);
    float mx = max(max(color.r, color.g), color.b);
    float sat = (1.0 - saturate(mx - mn)) * saturate(1.0 - mx) * lum * 5.0;
    vec3 light = vec3((mn + mx) * 0.5);
    float vibranceInt = 1.0 + uVibrance;

    color = mix(color, mix(light, color, vibranceInt), saturate(sat));
    color = mix(color, light, saturate(1.0 - light) * (1.0 - vibranceInt) * 0.5 * abs(vibranceInt));
    return color;
}

vec3 kappaFilmEmulation(vec3 color) {
    vec3 toeSlope = vec3(1.28, 1.21, 1.19) * 1.04;
    vec3 toeColor = color * toeSlope;
    const float midPoint = 0.18;
    vec3 midColor = (color - vec3(midPoint)) * vec3(1.04, 1.0, 0.97) + vec3(midPoint);
    vec3 toeAlpha = 1.0 - saturate(color / 0.29);
    toeAlpha = pow(toeAlpha, vec3(2.0, 1.8, 1.5));
    vec3 film = mix(midColor * 1.02, toeColor, toeAlpha);
    film *= 1.0 / (1.0 + max(film - vec3(midPoint), vec3(0.0)) * vec3(1.3, 1.7, 2.4) * 0.04);
    return mix(color, film, saturate(uFilmEmulationStrength));
}

vec3 derivativeAcademyCurve(vec3 color) {
    color = max(color, vec3(0.0));
    vec3 shoulder = color / (color + vec3(0.86));
    vec3 toe = 1.0 - exp(-color * vec3(1.18, 1.10, 1.02));
    float luma = luma709(color);
    float shoulderWeight = smoothstep(0.32, 3.8, luma);
    vec3 mapped = mix(toe, shoulder, shoulderWeight);
    mapped = pow(max(mapped, vec3(0.0)), vec3(0.92, 0.96, 1.02));
    vec3 printDensity = vec3(1.045, 1.01, 0.965);
    return saturate(mapped * printDensity);
}

vec3 kappaAcesApprox(vec3 color) {
    color = acesCompressionLmt(max(color, vec3(0.0)));
    color = rrtSweeteners(color);
    color *= 1.06;
    vec3 mapped = (color * (color + vec3(0.0313)) - vec3(0.00006)) /
                  (color * (0.983729 * color + vec3(0.512951)) + vec3(0.168081));
    float white = 97.409091;
    float mappedWhite = ((white * (white + 0.0313)) - 0.00006) /
                        (white * (0.983729 * white + 0.512951) + 0.168081);
    return saturate(mapped / mappedWhite);
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

float tonemapAgxScalar(float value) {
    return luma709(tonemapAgx(vec3(value)));
}

vec3 tonemapPreserveLuma(vec3 color) {
    color = max(color, vec3(0.0));
    float lumaIn = max(dot(color, vec3(0.2126, 0.7152, 0.0722)), 0.00001);
    float lumaOut;
    if (uTonemapMode == 3) {
        lumaOut = tonemapAgxScalar(lumaIn);
    } else if (uTonemapMode == 1) {
        lumaOut = tonemapAcesScalar(lumaIn);
    } else if (uTonemapMode == 2) {
        lumaOut = tonemapFilmicScalar(lumaIn);
    } else {
        lumaOut = tonemapReinhardScalar(lumaIn);
    }

    vec3 lumaMapped = color * (lumaOut / lumaIn);
    vec3 channelMapped;
    if (uTonemapMode == 3) {
        channelMapped = tonemapAgx(color);
    } else if (uTonemapMode == 1) {
        channelMapped = tonemapAces(color);
    } else if (uTonemapMode == 2) {
        channelMapped = tonemapFilmic(color);
    } else {
        channelMapped = tonemapReinhard(color);
    }
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

vec3 applyKappaHdrGrade(vec3 color) {
    vec3 graded = color;
    graded = kappaVibranceSaturation(graded);
    graded *= clamp(uColorLuma, vec3(0.5), vec3(1.5));
    graded = kappaFilmEmulation(graded);
    return mix(color, graded, saturate(uKappaGradingStrength));
}

vec3 applyKappaTonemap(vec3 color) {
    if (uTonemapMode == 3) {
        vec3 agxMapped = tonemapPreserveLuma(color);
        vec3 kappaMapped = kappaAcesApprox(color);
        vec3 academyMapped = derivativeAcademyCurve(color * 0.92);
        vec3 base = mix(agxMapped, kappaMapped, saturate(uKappaGradingStrength) * 0.22);
        return mix(base, academyMapped, saturate(uKappaGradingStrength) * 0.38);
    }
    vec3 mapped = kappaAcesApprox(color);
    vec3 fallback = tonemapPreserveLuma(color);
    return mix(fallback, mapped, saturate(uKappaGradingStrength));
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

vec3 applyGrade(vec3 color) {
    color *= max(uExposure, 0.001);
    if (uShaderpackGradingEnabled) {
        if (uTonemapMode == 1) {
            color = AcademyFit(color);
        } else if (uTonemapMode == 3) {
            color = tonemapAgx(color);
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

vec3 resolveHdrColor(vec2 sampleUv, vec2 screenUv) {
    vec3 color = texture(uSceneTex, sampleUv).rgb;
    if (uBloomEnabled) {
        vec3 bloom = texture(uBloomTex, sampleUv).rgb;
        // DerivativeMain Grade.glsl line 144: exposure compensation
        float bloomAmount = (uBloomStrength * 0.15) / (max(uExposure, 1.0) * 0.7 + 0.3);
        color += bloom * bloomAmount;
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

vec3 resolveGradedColor(vec2 sampleUv, vec2 screenUv) {
    vec3 graded = applyGrade(resolveHdrColor(sampleUv, screenUv));
    if (uShaderpackGradingEnabled) {
        graded = applyVignette(graded, sampleUv);
    }
    return graded;
}

vec3 applyCasLikeSharpen(vec3 center, vec2 sampleUv, vec2 screenUv) {
    float strength = saturate(uSharpenStrength);
    if (strength <= 0.0001) {
        return center;
    }

    vec2 texel = 1.0 / vec2(textureSize(uSceneTex, 0));
    vec2 uv00 = clamp(sampleUv + texel * vec2(-1.0, -1.0), vec2(0.0), vec2(1.0));
    vec2 uv10 = clamp(sampleUv + texel * vec2( 0.0, -1.0), vec2(0.0), vec2(1.0));
    vec2 uv20 = clamp(sampleUv + texel * vec2( 1.0, -1.0), vec2(0.0), vec2(1.0));
    vec2 uv01 = clamp(sampleUv + texel * vec2(-1.0,  0.0), vec2(0.0), vec2(1.0));
    vec2 uv21 = clamp(sampleUv + texel * vec2( 1.0,  0.0), vec2(0.0), vec2(1.0));
    vec2 uv02 = clamp(sampleUv + texel * vec2(-1.0,  1.0), vec2(0.0), vec2(1.0));
    vec2 uv12 = clamp(sampleUv + texel * vec2( 0.0,  1.0), vec2(0.0), vec2(1.0));
    vec2 uv22 = clamp(sampleUv + texel * vec2( 1.0,  1.0), vec2(0.0), vec2(1.0));

    vec3 a = resolveGradedColor(uv00, uv00);
    vec3 b = resolveGradedColor(uv10, uv10);
    vec3 c = resolveGradedColor(uv20, uv20);
    vec3 d = resolveGradedColor(uv01, uv01);
    vec3 e = center;
    vec3 f = resolveGradedColor(uv21, uv21);
    vec3 g = resolveGradedColor(uv02, uv02);
    vec3 h = resolveGradedColor(uv12, uv12);
    vec3 i = resolveGradedColor(uv22, uv22);

    vec3 minColor = min(a, min(b, min(c, min(d, min(e, min(f, min(g, min(h, i))))))));
    vec3 maxColor = max(a, max(b, max(c, max(d, max(e, max(f, max(g, max(h, i))))))));
    vec3 sharpeningAmount = sqrt(max(vec3(0.0), min(vec3(1.0) - maxColor, minColor) / max(maxColor, vec3(1e-5))));
    vec3 w = sharpeningAmount * mix(-0.125, -0.2, strength);
    return clamp(((b + d + f + h) * w + e) / (4.0 * w + vec3(1.0)), 0.0, 1.0);
}

void main() {
    vec2 centeredUv = vTexCoord - vec2(0.5, 0.5);
    float roll = uShaderpackGradingEnabled ? 0.0 : uScreenRollRadians;
    float c = cos(roll);
    float s = sin(roll);
    mat2 rot = mat2(c, -s,
                    s,  c);
    vec2 rolledUv = rot * centeredUv + vec2(0.5, 0.5);

    vec3 color = resolveHdrColor(rolledUv, vTexCoord);
    if (uBloomEnabled) {
        if (uSunRaysEnabled && uSunVisibility > 0.001 && uSunRayStrength > 0.001) {
            vec2 toSun = uSunScreenPos - rolledUv;
            float screenFade = 1.0 - smoothstep(0.55, 1.15, length(uSunScreenPos - vec2(0.5)));
            float rayMask = clamp(uSunVisibility * screenFade, 0.0, 1.0);
            vec3 rays = vec3(0.0);
            float weight = 0.16;
            for (int i = 1; i <= 8; ++i) {
                float t = float(i) / 8.0;
                vec2 sampleUv = rolledUv + toSun * t * 0.86;
                vec2 inBounds = step(vec2(0.0), sampleUv) * step(sampleUv, vec2(1.0));
                float valid = inBounds.x * inBounds.y;
                rays += texture(uBloomTex, sampleUv).rgb * weight * valid;
                weight *= 0.82;
            }
            color += rays * uSunRayStrength * rayMask;
        }
    }

    vec3 graded = applyGrade(color);

    if (uShaderpackGradingEnabled) {
        graded = applyVignette(graded, rolledUv);
    }
    graded = applyCasLikeSharpen(graded, rolledUv, vTexCoord);
    if (uNoiseDitherStrength > 0.0) {
        float noise = texture(uNoiseTex, gl_FragCoord.xy / vec2(textureSize(uNoiseTex, 0))).r - 0.5;
        graded += noise * uNoiseDitherStrength;
    }
    FragColor = vec4(clamp(graded, 0.0, 1.0), 1.0);
}
