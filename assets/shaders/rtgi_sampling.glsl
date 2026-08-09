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

// Hashes a terrain BLAS generation without depending on its mutable TLAS custom index.
uint rtgiTerrainHitIdentityHash(uint revisionLow, uint revisionHigh, uvec2 vertexAddressWords) {
    uint revisionMix = revisionLow ^ revisionHigh * 0x9e3779b9u;
    uint addressMix = vertexAddressWords.x * 0x27d4eb2du ^ vertexAddressWords.y * 0x85ebca6bu;
    return rtgiSampleHash(revisionMix ^ addressMix);
}

// Advances the shared Cranley-Patterson rotation with a low-discrepancy R2 sequence.
vec2 rtgiCranleyPattersonRotation(uint frameIndex) {
    float sequenceIndex = float(frameIndex & 0x00ffffffu) + 1.0;
    return fract(RTGI_R2_INCREMENT * sequenceIndex);
}

// Applies a stable per-pixel stride and dihedral orientation to the shared R2
// sequence. Every pixel remains low-discrepancy over time, while localized
// bright hits no longer advance in one screen-wide correlated phase.
vec2 rtgiPixelScrambledCranleyPattersonRotation(uint frameIndex, uvec2 pixel) {
    uint scramble = rtgiSampleHash(pixel.x * 1973u + pixel.y * 9277u + 26699u);
    uint stride = 1u + 2u * ((scramble >> 3u) & 3u);
    vec2 rotation = rtgiCranleyPattersonRotation(frameIndex * stride);
    if ((scramble & 1u) != 0u) {
        rotation = rotation.yx;
    }
    if ((scramble & 2u) != 0u) {
        rotation.x = fract(-rotation.x);
    }
    if ((scramble & 4u) != 0u) {
        rotation.y = fract(-rotation.y);
    }
    return rotation;
}

// Uses one independently rotated, XOR-scrambled 64-point Hammersley set.
// Splitting the six-bit indices by bit parity keeps both 32-sample halves
// balanced in every dyadic interval of either dimension, while any 64
// consecutive frames enumerate the same complete set.
vec2 rtgiReferenceHammersleySample(uint frameIndex, uvec2 pixel) {
    const uint batchSize = 64u;
    uint sampleIndex = frameIndex % batchSize;
    uint hashX = rtgiSampleHash(pixel.x * 1973u + pixel.y * 9277u + 0x68bc21ebu);
    uint hashY = rtgiSampleHash(pixel.x * 92821u + pixel.y * 68917u + 0x02e5be93u);
    vec2 batchRotation = vec2(hashX & 0x00ffffffu, hashY & 0x00ffffffu) / 16777216.0;
    uint subsetIndex = sampleIndex & 31u;
    uint subsetParity = (subsetIndex ^ (subsetIndex >> 1u) ^ (subsetIndex >> 2u) ^
                         (subsetIndex >> 3u) ^ (subsetIndex >> 4u)) & 1u;
    uint orderedIndex = subsetIndex | ((subsetParity ^ (sampleIndex >> 5u)) << 5u);
    uint scrambledIndex = orderedIndex ^ (hashX & 63u);
    uint reversedBits = bitfieldReverse(scrambledIndex);
    vec2 hammersley = vec2(float(scrambledIndex) + 0.5, float(reversedBits) + 0.5) / vec2(64.0, 4294967296.0);
    return fract(batchRotation + hammersley);
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
