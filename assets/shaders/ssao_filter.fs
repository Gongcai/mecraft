#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSsaoTex;
uniform sampler2D uDepthTex;
uniform sampler2D uNormalAoTex;
uniform vec2 uScreenSize;

vec3 decodeNormal(vec4 packed) {
    return normalize(packed.rgb * 2.0 - 1.0);
}

void main() {
    float centerDepth = texture(uDepthTex, vTexCoord).r;
    vec3 centerNormal = decodeNormal(texture(uNormalAoTex, vTexCoord));
    float centerAo = texture(uSsaoTex, vTexCoord).r;

    vec2 texelSize = 1.0 / uScreenSize;
    float filteredAo = 0.0;
    float totalWeight = 0.0;

    // 5x5 bilateral filter with depth and normal awareness
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            vec2 sampleUv = vTexCoord + offset;

            float sampleDepth = texture(uDepthTex, sampleUv).r;
            vec3 sampleNormal = decodeNormal(texture(uNormalAoTex, sampleUv));
            float sampleAo = texture(uSsaoTex, sampleUv).r;

            // Depth weight: reject samples at significantly different depths
            float depthDiff = abs(sampleDepth - centerDepth);
            float depthWeight = exp2(-depthDiff * 200.0);

            // Normal weight: reject samples with significantly different normals
            float normalWeight = pow(max(dot(sampleNormal, centerNormal), 0.0), 16.0);

            // Spatial weight: Gaussian falloff
            float spatialWeight = exp2(-float(x * x + y * y) * 0.25);

            float weight = depthWeight * normalWeight * spatialWeight;
            filteredAo += sampleAo * weight;
            totalWeight += weight;
        }
    }

    filteredAo = (totalWeight > 0.001) ? filteredAo / totalWeight : centerAo;
    FragColor = vec4(filteredAo, 0.0, 0.0, 1.0);
}
