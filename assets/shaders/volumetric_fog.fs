#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uDepthTex;
uniform sampler2D uSkyCaptureTex;
uniform mat4 uInvViewProj;
uniform vec3 uCameraPos;
uniform vec3 uSunDirection;
uniform vec3 uMoonDirection;
uniform vec3 uSunLightColor;
uniform vec3 uMoonLightColor;
uniform vec3 uHorizonScatterColor;
uniform float uSkyIntensity;
uniform float uMoonVisibility;
uniform float uAerialStrength;
uniform float uHorizonScatterStrength;
uniform float uVolumetricFogStrength;

const float kTwoPi = 6.28318530718;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

vec2 directionToSkyCaptureUv(vec3 dir) {
    dir = normalize(dir);
    float phi = atan(dir.x, -dir.z);
    float u = phi / kTwoPi + 0.5;
    float v = dir.y * 0.5 + 0.5;
    return vec2(fract(u), clamp(v, 0.0, 1.0));
}

vec3 sampleSkyCapture(vec3 dir) {
    return texture(uSkyCaptureTex, directionToSkyCaptureUv(dir)).rgb;
}

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    return world.xyz / max(world.w, 0.00001);
}

void main() {
    float depth = texture(uDepthTex, vTexCoord).r;
    if (depth >= 1.0) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
    vec3 ray = worldPos - uCameraPos;
    float distance = length(ray);
    vec3 viewDir = ray / max(distance, 0.0001);

    float dayFactor = clamp(uSkyIntensity, 0.0, 1.0);
    float nightFactor = 1.0 - dayFactor;
    float horizon = pow(1.0 - clamp(abs(viewDir.y), 0.0, 1.0), 1.45);
    float heightDensity = 1.0 - smoothstep(92.0, 230.0, worldPos.y);
    float density = (0.00028 + 0.00072 * horizon) *
                    clamp(uAerialStrength, 0.0, 2.0) *
                    clamp(uVolumetricFogStrength, 0.0, 2.0) *
                    heightDensity;
    float opticalDepth = distance * density;
    float transmittance = exp(-opticalDepth);
    float opacity = clamp(1.0 - transmittance, 0.0, 0.30);

    vec3 captureDir = normalize(vec3(viewDir.x, viewDir.y * 0.30, viewDir.z));
    vec3 skyColor = sampleSkyCapture(captureDir);
    vec3 fogColor = mix(skyColor, uHorizonScatterColor, horizon * clamp(uHorizonScatterStrength, 0.0, 2.0));

    vec3 sunDir = normalize(uSunDirection);
    vec3 moonDir = normalize(uMoonDirection);
    float sunVisibility = smoothstep(-0.08, 0.18, sunDir.y) * dayFactor;
    float sunForward = pow(max(dot(viewDir, sunDir), 0.0), 18.0);
    float sunWide = pow(max(dot(viewDir, sunDir), 0.0), 4.0);
    float moonForward = pow(max(dot(viewDir, moonDir), 0.0), 10.0) * clamp(uMoonVisibility, 0.0, 1.0);
    fogColor += uSunLightColor * (sunWide * 0.12 + sunForward * 0.42) *
                sunVisibility * clamp(uHorizonScatterStrength, 0.0, 2.0);
    fogColor += uMoonLightColor * moonForward * nightFactor * 0.18;

    vec3 scattering = max(fogColor, vec3(0.0)) * opacity;
    FragColor = vec4(scattering, 1.0 - opacity);
}
