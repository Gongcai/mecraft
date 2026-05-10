#version 450 core
in vec3 vWorldDir;
in vec2 vUV;
in vec4 vColor;

out vec4 FragColor;

uniform int uMode;
uniform sampler2D uTexture;
uniform vec3 uSkyTopColor;
uniform vec3 uSkyHorizonColor;
uniform vec3 uSunDirection;
uniform vec3 uMoonDirection;
uniform vec3 uSunScatterColor;
uniform vec3 uMoonLightColor;
uniform vec4 uTintColor;
uniform float uHorizonHaze;
uniform float uSunGlare;
uniform float uSunVisibility;
uniform float uMoonVisibility;
uniform float uNightFactor;
uniform float uBlackKeyThreshold;
uniform float uBlackKeySoftness;

const float kPi = 3.14159265359;
const float kTwoPi = 6.28318530718;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 hash22(vec2 p) {
    float x = hash12(p + vec2(17.13, 3.71));
    float y = hash12(p + vec2(5.29, 41.37));
    return vec2(x, y);
}

vec3 starDirectionFromCell(vec2 cell) {
    vec2 jitter = hash22(cell);
    vec2 uv = (cell + jitter) / vec2(160.0, 80.0);
    float phi = uv.x * kTwoPi - kPi;
    float cosTheta = uv.y * 2.0 - 1.0;
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    return normalize(vec3(sin(phi) * sinTheta, cosTheta, -cos(phi) * sinTheta));
}

float starField(vec3 dir) {
    float upperSky = smoothstep(0.04, 0.42, dir.y);
    vec2 sphereUv = vec2(atan(dir.x, -dir.z) / kTwoPi + 0.5, dir.y * 0.5 + 0.5);
    vec2 baseCell = floor(sphereUv * vec2(160.0, 80.0));
    float stars = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 cell = baseCell + vec2(float(x), float(y));
            vec2 wrappedCell = vec2(mod(cell.x, 160.0), clamp(cell.y, 0.0, 79.0));
            float rnd = hash12(wrappedCell);
            float visible = smoothstep(0.982, 0.998, rnd);
            vec3 starDir = starDirectionFromCell(wrappedCell);
            float angular = max(dot(normalize(dir), starDir), 0.0);
            float size = mix(0.9999985, 0.9999920, hash12(wrappedCell + vec2(7.7, 91.3)));
            float core = smoothstep(size, 1.0, angular);
            float twinkle = mix(0.70, 1.18, hash12(wrappedCell + vec2(19.7, 4.2)));
            stars += visible * core * twinkle;
        }
    }
    return min(stars, 1.0) * upperSky;
}

vec3 evaluateSkyRadiance(vec3 dir) {
    float height = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    float gradient = smoothstep(0.0, 1.0, height);
    vec3 color = mix(uSkyHorizonColor, uSkyTopColor, gradient);

    float horizon = pow(1.0 - clamp(abs(dir.y), 0.0, 1.0), 2.25);
    color = mix(color, uSkyHorizonColor * 1.12, horizon * clamp(uHorizonHaze, 0.0, 1.0));

    float sunDot = max(dot(dir, normalize(uSunDirection)), 0.0);
    float glow = pow(sunDot, 24.0) * uSunGlare;
    float wideGlow = pow(sunDot, 4.0) * uSunGlare * 0.22;
    color += uSunScatterColor * (glow + wideGlow) * smoothstep(-0.08, 0.18, uSunDirection.y);

    float moonDot = max(dot(dir, normalize(uMoonDirection)), 0.0);
    float moonGlow = pow(moonDot, 36.0) * 0.32 + pow(moonDot, 8.0) * 0.070;
    color += uMoonLightColor * moonGlow * clamp(uMoonVisibility, 0.0, 1.0);

    float nightHorizon = horizon * clamp(uNightFactor, 0.0, 1.0);
    color += vec3(0.04, 0.08, 0.12) * nightHorizon;
    float stars = starField(dir) * clamp(uNightFactor, 0.0, 1.0) * (1.0 - clamp(uSunVisibility, 0.0, 1.0));
    color += vec3(0.72, 0.82, 1.0) * stars * 1.15;
    return color;
}

void main() {
    if (uMode == 0) {
        vec3 dir = normalize(vWorldDir);
        FragColor = vec4(srgbToLinear(evaluateSkyRadiance(dir)), 1.0);
        return;
    }

    if (uMode == 4) {
        vec2 uv = clamp(vUV, vec2(0.0), vec2(1.0));
        float phi = uv.x * kTwoPi - kPi;
        float cosTheta = uv.y * 2.0 - 1.0;
        float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
        vec3 dir = normalize(vec3(sin(phi) * sinTheta, cosTheta, -cos(phi) * sinTheta));
        FragColor = vec4(srgbToLinear(evaluateSkyRadiance(dir)), 1.0);
        return;
    }

    if (uMode == 1) {
        vec4 texel = texture(uTexture, vUV);
        float brightness = max(max(texel.r, texel.g), texel.b);
        float keyedAlpha = smoothstep(uBlackKeyThreshold, uBlackKeyThreshold + uBlackKeySoftness, brightness);
        if (keyedAlpha <= 0.001) {
            discard;
        }
        vec3 unassociatedColor = texel.rgb / max(keyedAlpha, 0.001);
        unassociatedColor = min(unassociatedColor, vec3(1.0));
        vec3 color = srgbToLinear(unassociatedColor) * srgbToLinear(uTintColor.rgb);
        FragColor = vec4(color, texel.a * keyedAlpha * uTintColor.a);
        return;
    }

    if (uMode == 3) {
        FragColor = vec4(srgbToLinear(uTintColor.rgb) * vColor.r, uTintColor.a);
        return;
    }

    FragColor = vec4(srgbToLinear(vColor.rgb) * srgbToLinear(uTintColor.rgb), vColor.a * uTintColor.a);
}
