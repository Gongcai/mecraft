#ifndef MECRAFT_SKY_IBL_COMMON_GLSL
#define MECRAFT_SKY_IBL_COMMON_GLSL

const float SKY_IBL_PI = 3.14159265358979323846;
const float SKY_IBL_TWO_PI = 6.28318530717958647692;

vec3 skyIblFaceDirection(uint face, vec2 uv) {
    vec2 coordinate = uv * 2.0 - 1.0;
    switch (face) {
        case 0u: return normalize(vec3(1.0, -coordinate.y, -coordinate.x));
        case 1u: return normalize(vec3(-1.0, -coordinate.y, coordinate.x));
        case 2u: return normalize(vec3(coordinate.x, 1.0, coordinate.y));
        case 3u: return normalize(vec3(coordinate.x, -1.0, -coordinate.y));
        case 4u: return normalize(vec3(coordinate.x, -coordinate.y, 1.0));
        default: return normalize(vec3(-coordinate.x, -coordinate.y, -1.0));
    }
}

float skyIblRadicalInverse(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 skyIblHammersley(uint index, uint count) {
    return vec2(float(index) / float(count), skyIblRadicalInverse(index));
}

vec3 skyIblImportanceSampleGgx(vec2 samplePoint, vec3 normal, float alpha) {
    float alphaSquared = alpha * alpha;
    float phi = SKY_IBL_TWO_PI * samplePoint.x;
    float cosTheta = sqrt((1.0 - samplePoint.y) /
                          (1.0 + (alphaSquared - 1.0) * samplePoint.y));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    vec3 halfway = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0)
                                   : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    return normalize(tangent * halfway.x + bitangent * halfway.y +
                     normal * halfway.z);
}

float skyIblGeometrySchlickGgx(float nDotDirection, float roughness) {
    float k = roughness * roughness * 0.5;
    return nDotDirection /
           (nDotDirection * (1.0 - k) + k);
}

float skyIblGeometrySmith(float nDotV, float nDotL, float roughness) {
    return skyIblGeometrySchlickGgx(nDotV, roughness) *
           skyIblGeometrySchlickGgx(nDotL, roughness);
}

#endif // MECRAFT_SKY_IBL_COMMON_GLSL
