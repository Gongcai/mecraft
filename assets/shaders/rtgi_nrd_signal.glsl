#ifndef MECRAFT_RTGI_NRD_SIGNAL_GLSL
#define MECRAFT_RTGI_NRD_SIGNAL_GLSL

const float RTGI_NRD_FP16_MAX = 65504.0;
const float RTGI_NRD_EPSILON = 1.0e-6;

// Tests whether every component can be represented by finite floating-point arithmetic.
bool rtgiNrdFinite(float value) {
    return !isnan(value) && !isinf(value);
}

bool rtgiNrdFinite(vec3 value) {
    return !any(isnan(value)) && !any(isinf(value));
}

bool rtgiNrdFinite(vec4 value) {
    return !any(isnan(value)) && !any(isinf(value));
}

// Converts linear RGB to the exact YCoCg transform used by NRD 4.17.
vec3 rtgiNrdLinearToYCoCg(vec3 color) {
    float luminance = dot(color, vec3(0.25, 0.5, 0.25));
    float orangeChroma = dot(color, vec3(0.5, 0.0, -0.5));
    float greenChroma = dot(color, vec3(-0.25, 0.5, -0.25));
    return vec3(luminance, orangeChroma, greenChroma);
}

// Reproduces NRD's roughness-dependent REBLUR hit-distance scale.
float rtgiReblurHitDistanceNormalization(float viewZ, vec3 hitDistanceParameters, float roughness) {
    float magicCurve = 1.0 - exp2(-200.0 * roughness * roughness);
    magicCurve *= pow(clamp(roughness, 0.0, 1.0), 0.5);
    return (hitDistanceParameters.x + abs(viewZ) * hitDistanceParameters.y) *
           mix(hitDistanceParameters.z, 1.0, magicCurve);
}

// Reproduces REBLUR_FrontEnd_GetNormHitDist for a sampled diffuse lobe.
float rtgiReblurFrontEndGetNormalizedHitDistance(float hitDistance, float viewZ,
                                                 vec3 hitDistanceParameters, float roughness) {
    float normalization =
        rtgiReblurHitDistanceNormalization(viewZ, hitDistanceParameters, roughness);
    return max(clamp(hitDistance / normalization, 0.0, 1.0), RTGI_NRD_EPSILON);
}

// Reproduces RELAX_FrontEnd_PackRadianceAndHitDist after project validation.
vec4 rtgiRelaxFrontEndPackRadianceAndHitDistance(vec3 radiance, float hitDistance) {
    return vec4(clamp(radiance, vec3(0.0), vec3(RTGI_NRD_FP16_MAX)),
                clamp(hitDistance, 0.0, RTGI_NRD_FP16_MAX));
}

// Reproduces REBLUR_FrontEnd_PackRadianceAndNormHitDist after project validation.
vec4 rtgiReblurFrontEndPackRadianceAndNormalizedHitDistance(vec3 radiance,
                                                            float normalizedHitDistance) {
    vec3 boundedRadiance = clamp(radiance, vec3(0.0), vec3(RTGI_NRD_FP16_MAX));
    return vec4(rtgiNrdLinearToYCoCg(boundedRadiance), clamp(normalizedHitDistance, 0.0, 1.0));
}

#endif // MECRAFT_RTGI_NRD_SIGNAL_GLSL
