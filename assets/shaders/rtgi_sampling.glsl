#ifndef MECRAFT_RTGI_SAMPLING_GLSL
#define MECRAFT_RTGI_SAMPLING_GLSL

const float RTGI_TWO_PI = 6.28318530717958647692;
const vec2 RTGI_R2_INCREMENT = vec2(0.7548776662466927, 0.5698402909980532);
const float RTGI_VOXEL_SURFACE_EXPANSION = 1.0 / 2048.0;
const float RTGI_MINIMUM_RAY_ORIGIN_BIAS = RTGI_VOXEL_SURFACE_EXPANSION * 2.0;

const uint RTGI_TRACE_CLASS_SKY = 0u;
const uint RTGI_TRACE_CLASS_TRANSLUCENT = 1u;
const uint RTGI_TRACE_CLASS_MISS = 2u;
const uint RTGI_TRACE_CLASS_HIT = 3u;
const uint RTGI_TRACE_CLASS_NON_FINITE = 4u;
const uint RTGI_TRACE_VALIDATION_CLASSIFICATION_MASK = 0xffu;
const uint RTGI_TRACE_VALIDATION_CANDIDATE_SHIFT = 8u;
const uint RTGI_TRACE_VALIDATION_CANDIDATE_MASK = 0xfffu;
const uint RTGI_TRACE_VALIDATION_CONFIRMED_SHIFT = 20u;
const uint RTGI_TRACE_VALIDATION_CONFIRMED_MASK = 0xfffu;
const uint RTGI_SECONDARY_LIGHTING_TERRAIN_NORMAL_MAP_BIT = 1u << 0u;
const uint RTGI_SECONDARY_LIGHTING_TERRAIN_SPECULAR_MAP_BIT = 1u << 1u;

uint rtgiTraceValidationWord(uint classification, uint candidateCount, uint confirmedCount) {
    return (classification & RTGI_TRACE_VALIDATION_CLASSIFICATION_MASK) |
           (min(candidateCount, RTGI_TRACE_VALIDATION_CANDIDATE_MASK) << RTGI_TRACE_VALIDATION_CANDIDATE_SHIFT) |
           (min(confirmedCount, RTGI_TRACE_VALIDATION_CONFIRMED_MASK) << RTGI_TRACE_VALIDATION_CONFIRMED_SHIFT);
}

// Applies the integer permutation shared with RtgiSamplingContract.
uint rtgiSampleHash(uint value) {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

// Hashes stable material and geometry identities for the validation image.
uint rtgiStableHitIdentityHash(uint stableMaterialId, uint stableGeometryId) {
    return rtgiSampleHash(stableMaterialId * 0x9e3779b9u ^ stableGeometryId * 0x85ebca6bu);
}

// Hashes the resident terrain BLAS revision and exact TLAS hit location for validation views.
uint rtgiTerrainHitIdentityHash(uint revisionLow, uint revisionHigh, uint customIndex,
                                uint geometryIndex, uint primitiveIndex) {
    uint revisionMix = revisionLow ^ revisionHigh * 0x9e3779b9u;
    uint geometryMix = geometryIndex * 0x85ebca6bu ^ primitiveIndex * 0xc2b2ae35u;
    return rtgiSampleHash(revisionMix ^ customIndex * 0x27d4eb2du ^ geometryMix);
}

// Advances the shared Cranley-Patterson rotation with a low-discrepancy R2 sequence.
vec2 rtgiCranleyPattersonRotation(uint frameIndex) {
    float sequenceIndex = float(frameIndex & 0x00ffffffu) + 1.0;
    return fract(RTGI_R2_INCREMENT * sequenceIndex);
}

// Maps one low-discrepancy sample to a cosine-weighted unit direction around normal.
vec3 rtgiCosineHemisphereDirection(vec2 sampleValue, vec3 normal) {
    vec3 unitNormal = normalize(normal);
    vec3 helper = abs(unitNormal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helper, unitNormal));
    vec3 bitangent = cross(unitNormal, tangent);
    float radius = sqrt(sampleValue.y);
    float angle = RTGI_TWO_PI * sampleValue.x;
    return normalize(tangent * (radius * cos(angle)) + bitangent * (radius * sin(angle)) +
                     unitNormal * sqrt(1.0 - sampleValue.y));
}

#endif // MECRAFT_RTGI_SAMPLING_GLSL
