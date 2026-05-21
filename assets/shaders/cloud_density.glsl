// cloud_density.glsl — Shared cloud density functions for cloud_target.fs and volumetric_fog.fs.
// Extracted from cloud_target.fs to eliminate duplication and enable cloud shadow reuse.
// Source: DerivativeMain/shaders/world0/deferred1.fsh (cirrus, cirrocumulus, cumulus density)
//
// Dependencies (must be provided by host shader before #include):
//   - Uniforms: uNoiseTex, uNoiseEnabled, uTime, uCloudTimeScale, uCloudWetness, uCloudDensity, uCameraPos
//   - Functions: atmHenyeyGreensteinPhase (from atmosphere_lut.glsl)

#ifndef CLOUD_DENSITY_GLSL
#define CLOUD_DENSITY_GLSL

// ============================================================
// UTILITY FUNCTIONS (must precede all users)
// ============================================================

const int cloudNoiseResolution = 256;
const float cloudNoisePixelSize = 1.0 / float(cloudNoiseResolution);
const float cloudPlanetRadius = 6371000.0;

float cloudHash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float cloudHash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float cloudCube(float x) { return x * x * x; }

float cloudCurve(float x) { return x * x * (3.0 - 2.0 * x); }
vec3 cloudCurve3(vec3 x) { return x * x * (3.0 - 2.0 * x); }

// Cornette-Shanks phase function (more accurate than HG for forward-peaked scattering)
float cloudCornetteShanksPhase(float cosTheta, float g) {
    float gg = g * g;
    float mu2 = cosTheta * cosTheta;
    float denom = 1.0 + gg - 2.0 * g * cosTheta;
    return (3.0 * (1.0 - gg) * (1.0 + mu2)) /
           (8.0 * 3.14159265 * (2.0 + gg) * denom * sqrt(denom));
}

vec2 cloudRaySphereIntersection(vec3 pos, vec3 dir, float rad) {
    float b = dot(pos, dir);
    float c = dot(pos, pos) - rad * rad;
    float h = b * b - c;
    if (h < 0.0) {
        return vec2(-1.0);
    }
    h = sqrt(h);
    return vec2(-b - h, -b + h);
}

// ============================================================
// CLOUD NOISE FUNCTIONS
// ============================================================

float cirrusCloudNoise(vec2 p) {
    return texture(uNoiseTex, p * 1e-2).a;
}

// 2D noise texture lookup with two-octave blend
float sampleCloudNoise(vec2 p) {
    if (!uNoiseEnabled) {
        return cloudHash12(p);
    }
    vec4 n0 = texture(uNoiseTex, p);
    vec4 n1 = texture(uNoiseTex, p * 2.37 + vec2(0.17, -0.29));
    return n0.r * 0.62 + n1.g * 0.38;
}

// DerivativeMain 3D noise: Z-slice interpolation from 2D noise texture (smooth version)
float cloudNoiseSmooth(vec3 position) {
    vec3 p = floor(position);
    vec3 b = cloudCurve3(position - p);
    vec2 uv = p.xy + b.xy + 97.0 * p.z;
    vec2 coord = (uv + 0.5) * cloudNoisePixelSize;
    vec2 rg = texture(uNoiseTex, coord).xy;
    return mix(rg.x, rg.y, b.z);
}

// DerivativeMain 3D noise: Z-slice interpolation from 2D noise texture (sharp version)
float cloudNoiseSharp(vec3 position) {
    vec3 p = floor(position);
    vec3 f = position - p;
    f = clamp(f, vec3(0.0), vec3(1.0));
    vec2 uv = p.xy + f.xy + p.z * 97.0;
    vec2 coord = (uv + 0.5) * cloudNoisePixelSize;
    vec2 noiseSample = texture(uNoiseTex, coord).xy;
    return mix(noiseSample.x, noiseSample.y, f.z);
}

// ============================================================
// CLOUD DENSITY EVALUATION FUNCTIONS
// ============================================================

// Planar cloud (cirrus) density at worldPos (DerivativeMain PlanarClouds.glsl:16-38)
float cirrusCloudDensity(vec2 worldPos, float coverage) {
    float cloudTime = uTime * uCloudTimeScale;
    vec2 wind = vec2(cloudTime * 0.0003, -cloudTime * 0.0002);
    vec2 pos = worldPos * 4e-5 - wind;

    // Domain warp (PlanarClouds.glsl:20)
    pos += texture(uNoiseTex, pos * 0.04).y * 0.1;

    // Local coverage for non-linear remap (PlanarClouds.glsl:22,34,38)
    float localCoverage = texture(uNoiseTex, pos * 2e-3 + 0.15).x;

    float amplitude = 0.5;
    const float GOLDEN_ANGLE = 6.28318530718 / (1.61803398875 + 1.0);
    mat2 goldenRot = mat2(cos(GOLDEN_ANGLE), -sin(GOLDEN_ANGLE),
                          sin(GOLDEN_ANGLE), cos(GOLDEN_ANGLE));

    // First octave before loop (PlanarClouds.glsl:28)
    float noise = cirrusCloudNoise(pos);
    // 5 more octaves with cumulative wind + frequency perturbation (PlanarClouds.glsl:29-31)
    for (int i = 1; i < 6; ++i) {
        pos = goldenRot * 3.2 * (pos - wind);
        vec2 freqPerturb = 1.0 + vec2(-0.35, 0.05) * sqrt(float(i));
        noise += cirrusCloudNoise(pos * freqPerturb) * amplitude;
        amplitude *= 0.43;
    }

    // Local coverage spatial variation (PlanarClouds.glsl:34)
    noise -= max(localCoverage * 4.0 - 1.6, 0.0);
    noise = clamp(noise * 1.36 + coverage - 1.7, 0.0, 1.0) * max(noise, 0.0);

    return noise;
}

// Cumulus cloud density at worldPos (DerivativeMain CloudDensity)
float cloudDensityAt(vec3 worldPos, float normalizedHeight, float weatherCoverage, float noiseDetail) {
    float cloudTime = uTime * uCloudTimeScale;
    vec3 wind = vec3(2e-3, 2e-4, 1e-3) * cloudTime;
    float noiseScale = 4e-4 + 6e-5 * uCloudWetness;
    vec3 position = worldPos * noiseScale - wind;

    // Local coverage
    float localCoverage = texture(uNoiseTex, worldPos.xz * 2e-7 - wind.xz * 2e-3 + 0.5).y;
    localCoverage = clamp(localCoverage * 3.0 + uCloudWetness - 0.4, 0.0, 1.0) * 0.5 + 0.5;
    if (localCoverage < 0.1) return 0.0;

    float density = noiseDetail * 0.03;
    float weight = 0.5;
    const float octWeight = 0.5;
    const float octScale = 3.0;

    for (int i = 0; i < 4; ++i) {
        density += weight * cloudNoiseSmooth(position);
        position = position * octScale - wind;
        weight *= octWeight;
    }
    density += octWeight / octScale / 4.0;
    if (density < 1e-6) return 0.0;

    density *= localCoverage;

    float heightAttenuation = clamp(normalizedHeight * 6.6, 0.0, 1.0)
                            * clamp((1.0 - normalizedHeight) * (2.0 + uCloudWetness), 0.0, 1.0);

    if (weatherCoverage != 1.0) {
        density = clamp((density - 1.0 + weatherCoverage) / weatherCoverage, 0.0, 1.0);
    }

    density *= heightAttenuation * 1.9;
    density -= heightAttenuation * 0.9 + normalizedHeight * 0.5 + 0.1;

    return clamp(density * 3.0 * uCloudDensity, 0.0, 1.0);
}

// Cirrocumulus cloud density (DerivativeMain lower planar layer with curl noise)
float cirrocumulusDensity(vec2 worldPos) {
    float cloudTime = uTime * uCloudTimeScale;
    vec2 wind = vec2(cloudTime * 0.0003, -cloudTime * 0.0002);
    worldPos /= 1.0 + length(worldPos - uCameraPos.xz) * 2e-5;
    vec2 position = worldPos * 1e-4 - wind;

    float baseCoverage = cloudCurve(texture(uNoiseTex, position * 0.08).z * 0.7 + 0.1);
    baseCoverage *= max(1.07 - texture(uNoiseTex, position * 0.003).y * 1.4, 0.0);

    vec2 curl = texture(uNoiseTex, position * 0.05).xy * 0.04;
    curl += texture(uNoiseTex, position * 0.1).xy * 0.02;
    position += curl;

    float noise = 0.5 * texture(uNoiseTex, position * vec2(0.4, 0.16)).z;
    noise += texture(uNoiseTex, position * 0.9).z - 0.24;
    noise = clamp(noise, 0.0, 1.0);

    noise *= clamp((baseCoverage + 0.5 - 0.6) * 0.9, 0.0, 0.14);
    if (noise < 1e-6) return 0.0;

    position.x += noise * 0.2;
    noise += 0.02 * texture(uNoiseTex, position * 3.0).z;
    noise += 0.01 * texture(uNoiseTex, position * 5.0 + curl).z - 0.05;

    return cloudCube(clamp(noise * 4.0, 0.0, 1.0));
}

#endif // CLOUD_DENSITY_GLSL
