#version 450 core

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uSsaoTex;
layout(binding = 1) uniform sampler2D uDepthTex;
layout(binding = 2) uniform sampler2D uNormalAoTex;

layout(std140, binding = 15) uniform RhiPushConstants {
    vec2 uScreenSize;
    float uNear;
    float uPadding0;
};

void main() {
    float centerDepth = texture(uDepthTex, vTexCoord).r;
    vec3 centerNormal = normalize(texture(uNormalAoTex, vTexCoord).rgb * 2.0 - 1.0);
    float centerAo = texture(uSsaoTex, vTexCoord).r;

    // Linearize NDC depth: linearZ = 2*near / (1 - ndc)
    // Used for perspective-correct depth comparison in bilateral filter.
    float linCenter = 2.0 * uNear / max(1.0 - centerDepth, 1e-7);

    vec2 texelSize = 1.0 / uScreenSize;
    float filteredAo = 0.0;
    float totalWeight = 0.0;

    // 7x7 bilateral filter with improved depth-aware and normal-aware edge stopping.
    // Depth weight uses linearized depth with relative difference for perspective-correct
    // rejection: near objects reject at absolute gaps, far objects tolerate more absolute
    // spread for the same screen-space offset.
    // Normal weight uses raised cosine for sharper normal discontinuity detection.
    for (int y = -3; y <= 3; ++y) {
        for (int x = -3; x <= 3; ++x) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            vec2 sampleUv = vTexCoord + offset;

            float sampleDepth = texture(uDepthTex, sampleUv).r;
            vec3 sampleNormal = normalize(texture(uNormalAoTex, sampleUv).rgb * 2.0 - 1.0);
            float sampleAo = texture(uSsaoTex, sampleUv).r;

            // Depth weight: relative linear depth difference for perspective-correct rejection.
            // |Δz|/z_mean ≈ 0.05 → weight ~0.71, ≈ 0.5 → weight ~0.003.
            float linSample = 2.0 * uNear / max(1.0 - sampleDepth, 1e-7);
            float relDepthDiff = abs(linSample - linCenter) / max(linCenter, 0.1);
            float depthWeight = exp2(-relDepthDiff * 8.0);

            // Normal weight: raised cosine (pow 32) for sharper edge preservation
            float normalWeight = pow(max(dot(sampleNormal, centerNormal), 0.0), 32.0);

            // Spatial weight: Gaussian falloff with sigma ~= 2.5 pixels
            float dist2 = float(x * x + y * y);
            float spatialWeight = exp2(-dist2 * 0.12);

            float weight = depthWeight * normalWeight * spatialWeight;
            filteredAo += sampleAo * weight;
            totalWeight += weight;
        }
    }

    filteredAo = (totalWeight > 0.001) ? filteredAo / totalWeight : centerAo;
    FragColor = vec4(filteredAo, 0.0, 0.0, 1.0);
}
