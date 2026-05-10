#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneTex;
uniform sampler2D uBloomTex;
uniform sampler2D uNoiseTex;

uniform bool uBloomEnabled;
uniform float uBloomStrength;
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
uniform float uExposure;
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
        return mix(agxMapped, kappaMapped, saturate(uKappaGradingStrength) * 0.28);
    }
    vec3 mapped = kappaAcesApprox(color);
    vec3 fallback = tonemapPreserveLuma(color);
    return mix(fallback, mapped, saturate(uKappaGradingStrength));
}

vec3 applySplitTone(vec3 color) {
    float lum = luma709(color);
    vec3 shadowTint = vec3(0.88, 0.94, 1.08);
    vec3 highlightTint = vec3(1.10, 1.035, 0.90);
    float shadowWeight = smoothstep(0.55, 0.02, lum);
    float highlightWeight = smoothstep(0.38, 1.0, lum);
    vec3 toned = color;
    toned *= mix(vec3(1.0), shadowTint, shadowWeight * 0.42);
    toned *= mix(vec3(1.0), highlightTint, highlightWeight * 0.50);
    return mix(color, toned, saturate(uSplitToneStrength));
}

vec3 applyAgxLook(vec3 color) {
    float lum = luma709(color);
    float chromaBoost = mix(1.09, 1.03, smoothstep(0.18, 0.85, lum));
    color = mix(vec3(lum), color, chromaBoost);
    vec3 coolShadows = vec3(0.92, 0.98, 1.08);
    vec3 warmHighlights = vec3(1.06, 1.025, 0.94);
    color *= mix(vec3(1.0), coolShadows, smoothstep(0.34, 0.02, lum) * 0.16);
    color *= mix(vec3(1.0), warmHighlights, smoothstep(0.42, 1.0, lum) * 0.10);
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
        color = applyColorTemperature(color);
        color = applyKappaHdrGrade(color);
        color = applyKappaTonemap(color);
        if (uTonemapMode == 3) {
            color = applyAgxLook(color);
        }
        color = applySplitTone(color);
    } else {
        color = vec3(1.0) - exp(-color);
    }
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, uSaturation);
    color = (color - 0.5) * uContrast + 0.5;
    color = pow(max(color, vec3(0.0)), vec3(1.0 / max(uGamma, 0.001)));
    return color;
}

vec3 resolveHdrColor(vec2 sampleUv, vec2 screenUv) {
    vec3 color = texture(uSceneTex, sampleUv).rgb;
    if (uBloomEnabled) {
        color += texture(uBloomTex, sampleUv).rgb * uBloomStrength;
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
    vec3 left = resolveGradedColor(clamp(sampleUv - vec2(texel.x, 0.0), vec2(0.0), vec2(1.0)),
                                   clamp(screenUv - vec2(texel.x, 0.0), vec2(0.0), vec2(1.0)));
    vec3 right = resolveGradedColor(clamp(sampleUv + vec2(texel.x, 0.0), vec2(0.0), vec2(1.0)),
                                    clamp(screenUv + vec2(texel.x, 0.0), vec2(0.0), vec2(1.0)));
    vec3 down = resolveGradedColor(clamp(sampleUv - vec2(0.0, texel.y), vec2(0.0), vec2(1.0)),
                                   clamp(screenUv - vec2(0.0, texel.y), vec2(0.0), vec2(1.0)));
    vec3 up = resolveGradedColor(clamp(sampleUv + vec2(0.0, texel.y), vec2(0.0), vec2(1.0)),
                                 clamp(screenUv + vec2(0.0, texel.y), vec2(0.0), vec2(1.0)));
    vec3 blur = (left + right + down + up) * 0.25;

    float contrastGate = smoothstep(0.015, 0.18, abs(luma709(center) - luma709(blur)));
    vec3 sharpened = center + (center - blur) * (0.45 * strength * contrastGate);
    return clamp(sharpened, 0.0, 1.0);
}

void main() {
    vec2 centeredUv = vTexCoord - vec2(0.5, 0.5);
    float c = cos(uScreenRollRadians);
    float s = sin(uScreenRollRadians);
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

