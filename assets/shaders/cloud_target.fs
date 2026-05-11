#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uDepthTex;
uniform sampler2D uSkyCaptureTex;
uniform sampler2D uNoiseTex;
uniform mat4 uInvViewProj;
uniform vec3 uCameraPos;
uniform vec3 uSunDirection;
uniform vec3 uMoonDirection;
uniform vec3 uSunLightColor;
uniform vec3 uMoonLightColor;
uniform vec3 uSkyAmbientColor;
uniform vec3 uShadowTintColor;
uniform vec3 uHorizonScatterColor;
uniform float uSkyIntensity;
uniform float uMoonVisibility;
uniform float uWeatherMist;
uniform float uWeatherWetness;
uniform float uWeatherStorm;
uniform float uAerialStrength;
uniform float uHorizonScatterStrength;
uniform float uSunWarmth;
uniform float uSkyCoolness;
uniform float uAerialReduction;
uniform int uCloudShadowsEnabled;
uniform float uCloudShadowStrength;
uniform float uCloudShadowScale;
uniform float uCloudShadowSpeed;
uniform float uCloudCoverage;
uniform float uCloudDensity;
uniform float uCloudHeight;
uniform float uCloudThickness;
uniform float uTime;
uniform bool uNoiseEnabled;

const float kTwoPi = 6.28318530718;

vec2 directionToSkyCaptureUv(vec3 dir) {
    dir = normalize(dir);
    float phi = atan(dir.x, -dir.z);
    float u = phi / kTwoPi + 0.5;
    float v = dir.y * 0.5 + 0.5;
    return vec2(fract(u), clamp(v, 0.0, 1.0));
}

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    return world.xyz / max(world.w, 0.00001);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float sampleCloudNoise(vec2 p) {
    if (!uNoiseEnabled) {
        return hash12(p);
    }
    vec4 n0 = texture(uNoiseTex, p);
    vec4 n1 = texture(uNoiseTex, p * 2.37 + vec2(0.17, -0.29));
    return n0.r * 0.62 + n1.g * 0.38;
}

void main() {
    float depth = texture(uDepthTex, vTexCoord).r;
    vec3 targetPos = depth >= 0.9999
        ? uCameraPos + normalize(vec3(vTexCoord * 2.0 - 1.0, 1.0)) * 4096.0
        : reconstructWorldPosition(vTexCoord, depth);
    vec3 ray = normalize(targetPos - uCameraPos);

    float cloudTop = uCloudHeight + max(uCloudThickness, 1.0);
    float layerT = (uCloudHeight - uCameraPos.y) / max(ray.y, 0.03);
    vec2 cloudUv = (uCameraPos.xz + ray.xz * max(layerT, 0.0)) * 0.0016;
    cloudUv += vec2(uTime * 0.003, -uTime * 0.002);

    float weatherCoverage = clamp(uCloudCoverage + uWeatherMist * 0.18 + uWeatherWetness * 0.18 + uWeatherStorm * 0.28, 0.0, 1.0);
    float n = sampleCloudNoise(cloudUv);
    float detail = sampleCloudNoise(cloudUv * 3.1 + vec2(7.3, -2.1));
    float coverage = smoothstep(1.0 - weatherCoverage, 1.0, n * 0.72 + detail * 0.28);
    float heightFade = smoothstep(-0.05, 0.18, ray.y) * (1.0 - smoothstep(cloudTop, cloudTop + 80.0, uCameraPos.y));
    float opacity = clamp(coverage * heightFade * max(uCloudDensity, 0.0), 0.0, 1.0);

    vec3 sky = texture(uSkyCaptureTex, directionToSkyCaptureUv(ray)).rgb;
    float day = clamp(uSkyIntensity, 0.0, 1.0);
    float sunFacing = pow(max(dot(ray, normalize(uSunDirection)), 0.0), 5.0);
    vec3 directTint = mix(uMoonLightColor * clamp(uMoonVisibility, 0.0, 1.0), uSunLightColor, day);
    vec3 cloudColor = mix(sky, uHorizonScatterColor, clamp(uHorizonScatterStrength * 0.22, 0.0, 1.0));
    cloudColor = mix(cloudColor, directTint, 0.18 + sunFacing * 0.28);
    cloudColor *= 0.62 + day * 0.48 + clamp(uAerialStrength, 0.0, 2.0) * 0.08;
    cloudColor *= mix(1.0, 0.74, clamp(uWeatherWetness + uWeatherStorm, 0.0, 1.0) * 0.45);

    FragColor = vec4(max(cloudColor, vec3(0.0)), opacity);
}
