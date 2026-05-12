#ifndef MECRAFT_ATMOSPHERE_LUT_GLSL
#define MECRAFT_ATMOSPHERE_LUT_GLSL

#include "render_contract.glsl"

const float atmPi = 3.14159265359;
const float atmTwoPi = 6.28318530718;
const float atmPlanetRadius = 6371000.0;
const float atmSunAngularRadius = 0.012;
const float atmMiePhaseG = 0.77;

const float atmTransmittanceTextureWidth = 256.0;
const float atmTransmittanceTextureHeight = 64.0;
const float atmScatteringTextureRSize = 32.0;
const float atmScatteringTextureMuSize = 128.0;
const float atmScatteringTextureMuSSize = 32.0;
const float atmScatteringTextureNuSize = 8.0;
const float atmIrradianceTextureWidth = 64.0;
const float atmIrradianceTextureHeight = 16.0;

const float atmAtmosphereBottomAltitude = 1000.0;
const float atmAtmosphereTopAltitude = 100000.0;
const float atmAtmosphereBottomRadius = atmPlanetRadius - atmAtmosphereBottomAltitude;
const float atmAtmosphereTopRadius = atmPlanetRadius + atmAtmosphereTopAltitude;
const float atmAtmosphereBottomRadiusSq = atmAtmosphereBottomRadius * atmAtmosphereBottomRadius;
const float atmAtmosphereTopRadiusSq = atmAtmosphereTopRadius * atmAtmosphereTopRadius;
const float atmMuSMin = -0.2;

struct AtmosphereParameters {
    vec3 solar_irradiance;
    vec3 rayleigh_scattering;
    vec3 mie_scattering;
    vec3 ground_albedo;
};

const AtmosphereParameters atmModel = AtmosphereParameters(
    vec3(1.474000, 1.850400, 1.911980),
    vec3(0.005802, 0.013558, 0.033100),
    vec3(0.003996, 0.003996, 0.003996),
    vec3(0.1)
);

float atmLuminance(vec3 c) {
    return dot(c, vec3(0.2722, 0.6741, 0.0537));
}

float atmClampCosine(float mu) {
    return clamp(mu, -1.0, 1.0);
}

float atmClampRadius(float r) {
    return clamp(r, atmAtmosphereBottomRadius, atmAtmosphereTopRadius);
}

float atmSafeSqrt(float a) {
    return sqrt(max(a, 0.0));
}

float atmDistanceToTopAtmosphereBoundary(float r, float mu) {
    float discriminant = r * r * (mu * mu - 1.0) + atmAtmosphereTopRadiusSq;
    return max(-r * mu + atmSafeSqrt(discriminant), 0.0);
}

float atmDistanceToBottomAtmosphereBoundary(float r, float mu) {
    float discriminant = r * r * (mu * mu - 1.0) + atmAtmosphereBottomRadiusSq;
    return max(-r * mu - atmSafeSqrt(discriminant), 0.0);
}

bool atmRayIntersectsGround(float r, float mu) {
    return mu < 0.0 && r * r * (mu * mu - 1.0) + atmAtmosphereBottomRadiusSq >= 0.0;
}

float atmRayleighPhase(float cosTheta) {
    const float c = 3.0 / 16.0 * (1.0 / atmPi);
    return cosTheta * cosTheta * c + c;
}

float atmHenyeyGreensteinPhase(float cosTheta, float g) {
    float gg = g * g;
    float phase = 1.0 + gg - 2.0 * g * cosTheta;
    return (1.0 - gg) / (4.0 * atmPi * phase * sqrt(phase));
}

float atmGetTextureCoordFromUnitRange(float x, float textureSize) {
    return 0.5 / textureSize + x * (1.0 - 1.0 / textureSize);
}

vec2 atmDirectionToSkyCaptureUv(vec3 dir) {
    dir = normalize(dir);
    float phi = atan(dir.x, -dir.z);
    float u = phi / atmTwoPi + 0.5;
    float v = dir.y * 0.5 + 0.5;
    return vec2(fract(u), clamp(v, 0.0, 1.0));
}

vec2 atmGetTransmittanceTextureUvFromRMu(float r, float mu) {
    float H = sqrt(atmAtmosphereTopRadiusSq - atmAtmosphereBottomRadiusSq);
    float rho = atmSafeSqrt(r * r - atmAtmosphereBottomRadiusSq);
    float d = atmDistanceToTopAtmosphereBoundary(r, mu);
    float dMin = atmAtmosphereTopRadius - r;
    float dMax = rho + H;
    return vec2(
        atmGetTextureCoordFromUnitRange((d - dMin) / (dMax - dMin), atmTransmittanceTextureWidth),
        atmGetTextureCoordFromUnitRange(rho / H, atmTransmittanceTextureHeight)
    );
}

vec3 atmGetTransmittanceToTopAtmosphereBoundary(float r, float mu) {
    vec2 uv = atmGetTransmittanceTextureUvFromRMu(r, mu);
    uv = clamp(uv, vec2(0.5 / 256.0, 0.5 / 64.0), vec2(255.5 / 256.0, 63.5 / 64.0));
    return vec3(texture(uAtmosphereLut, vec3(uv * vec2(1.0, 0.5), 32.5 / 33.0)));
}

vec3 atmGetTransmittance(float r, float mu, float d, bool rayIntersectsGround) {
    float rd = atmClampRadius(sqrt(d * d + 2.0 * r * mu * d + r * r));
    float muD = atmClampCosine((r * mu + d) / rd);
    if (rayIntersectsGround) {
        return min(
            atmGetTransmittanceToTopAtmosphereBoundary(rd, -muD) /
            atmGetTransmittanceToTopAtmosphereBoundary(r, -mu),
            vec3(1.0));
    }
    return min(
        atmGetTransmittanceToTopAtmosphereBoundary(r, mu) /
        atmGetTransmittanceToTopAtmosphereBoundary(rd, muD),
        vec3(1.0));
}

vec3 atmGetTransmittanceToSun(float r, float muS) {
    float sinThetaH = atmAtmosphereBottomRadius / r;
    float cosThetaH = -sqrt(max(1.0 - sinThetaH * sinThetaH, 0.0));
    return atmGetTransmittanceToTopAtmosphereBoundary(r, muS) *
        smoothstep(-sinThetaH * atmSunAngularRadius,
                    sinThetaH * atmSunAngularRadius,
                    muS - cosThetaH);
}

vec4 atmGetScatteringTextureUvwzFromRMuMuSNu(float r, float mu, float muS, float nu, bool rayIntersectsGround) {
    float H = sqrt(atmAtmosphereTopRadiusSq - atmAtmosphereBottomRadiusSq);
    float rho = atmSafeSqrt(r * r - atmAtmosphereBottomRadiusSq);
    float uR = atmGetTextureCoordFromUnitRange(rho / H, atmScatteringTextureRSize);

    float rMu = r * mu;
    float discriminant = rMu * rMu - r * r + atmAtmosphereBottomRadiusSq;
    float uMu;

    if (rayIntersectsGround) {
        float d = -rMu - atmSafeSqrt(discriminant);
        float dMin = r - atmAtmosphereBottomRadius;
        float dMax = rho;
        uMu = 0.5 - 0.5 * atmGetTextureCoordFromUnitRange(
            dMax == dMin ? 0.0 : (d - dMin) / (dMax - dMin),
            atmScatteringTextureMuSize * 0.5);
    } else {
        float d = -rMu + atmSafeSqrt(discriminant + H * H);
        float dMin = atmAtmosphereTopRadius - r;
        float dMax = rho + H;
        uMu = 0.5 + 0.5 * atmGetTextureCoordFromUnitRange(
            (d - dMin) / (dMax - dMin),
            atmScatteringTextureMuSize * 0.5);
    }

    float d = atmDistanceToTopAtmosphereBoundary(atmAtmosphereBottomRadius, muS);
    float dMin = atmAtmosphereTopRadius - atmAtmosphereBottomRadius;
    float dMax = H;
    float a = (d - dMin) / (dMax - dMin);
    float D = atmDistanceToTopAtmosphereBoundary(atmAtmosphereBottomRadius, atmMuSMin);
    float A = (D - dMin) / (dMax - dMin);
    float uMuS = atmGetTextureCoordFromUnitRange(max(1.0 - a / A, 0.0) / (1.0 + a), atmScatteringTextureMuSize);
    float uNu = nu * 0.5 + 0.5;
    return vec4(uNu, uMuS, uMu, uR);
}

vec3 atmGetExtrapolatedSingleMieScattering(AtmosphereParameters atmosphere, vec4 scattering) {
    if (scattering.r <= 0.0) {
        return vec3(0.0);
    }
    return scattering.rgb * scattering.a / scattering.r *
        (atmosphere.rayleigh_scattering.r / atmosphere.mie_scattering.r) *
        (atmosphere.mie_scattering / atmosphere.rayleigh_scattering);
}

vec3 atmGetCombinedScattering(AtmosphereParameters atmosphere,
                              float r,
                              float mu,
                              float muS,
                              float nu,
                              bool rayIntersectsGround,
                              out vec3 singleMieScattering) {
    vec4 uvwz = atmGetScatteringTextureUvwzFromRMuMuSNu(r, mu, muS, nu, rayIntersectsGround);
    float texCoordX = uvwz.x * (atmScatteringTextureNuSize - 1.0);
    float texX = floor(texCoordX);
    float lerp = texCoordX - texX;
    vec3 uvw0 = vec3((texX + uvwz.y) / atmScatteringTextureNuSize, uvwz.z, uvwz.w);
    vec3 uvw1 = vec3((texX + 1.0 + uvwz.y) / atmScatteringTextureNuSize, uvwz.z, uvwz.w);
    vec4 combined = texture(uAtmosphereLut, uvw0) * (1.0 - lerp)
                  + texture(uAtmosphereLut, uvw1) * lerp;
    vec3 scattering = vec3(combined);
    singleMieScattering = atmGetExtrapolatedSingleMieScattering(atmosphere, combined);
    return scattering;
}

vec3 atmGetIrradiance(float r, float muS) {
    float xR = (r - atmAtmosphereBottomRadius) / (atmAtmosphereTopRadius - atmAtmosphereBottomRadius);
    float xMuS = muS * 0.5 + 0.5;
    vec2 uv = vec2(atmGetTextureCoordFromUnitRange(xMuS, atmIrradianceTextureWidth),
                   atmGetTextureCoordFromUnitRange(xR, atmIrradianceTextureHeight));
    uv = clamp(uv, vec2(0.5 / 64.0, 0.5 / 16.0), vec2(63.5 / 64.0, 15.5 / 16.0));
    return vec3(texture(uAtmosphereLut, vec3(uv * vec2(0.25, 0.125) + vec2(0.0, 0.5), 32.5 / 33.0)));
}

vec3 atmGetSkyRadianceForLight(float eyeAltitude, vec3 viewRay, vec3 lightDirection, out vec3 transmittance) {
    vec3 camera = vec3(0.0, atmPlanetRadius + eyeAltitude, 0.0);
    float r = length(camera);
    float rMu = dot(camera, viewRay);
    float distanceToTop = -rMu - sqrt(rMu * rMu - r * r + atmAtmosphereTopRadiusSq);

    if (distanceToTop > 0.0) {
        camera += viewRay * distanceToTop;
        r = atmAtmosphereTopRadius;
        rMu += distanceToTop;
    } else if (r > atmAtmosphereTopRadius) {
        transmittance = vec3(1.0);
        return vec3(0.0);
    }

    float mu = rMu / r;
    float muS = dot(camera, lightDirection) / r;
    float nu = dot(viewRay, lightDirection);

    bool rayIntersectsGround = atmRayIntersectsGround(r, mu);
    transmittance = rayIntersectsGround ? vec3(0.0) : atmGetTransmittanceToTopAtmosphereBoundary(r, mu);
    rayIntersectsGround = false;

    vec3 singleMieScattering;
    vec3 scattering = atmGetCombinedScattering(atmModel, r, mu, muS, nu, rayIntersectsGround, singleMieScattering);
    vec3 rayleigh = scattering * atmRayleighPhase(nu);
    vec3 mie = singleMieScattering * atmHenyeyGreensteinPhase(nu, atmMiePhaseG);
    return rayleigh + mie;
}

#endif
