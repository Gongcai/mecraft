#version 450 core
#include "gbuffer_contract.glsl"

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uReflectionTex;
uniform sampler2D uDepthTex;
uniform sampler2D uNormalAoTex;
uniform sampler2D uMaterialTex;
uniform sampler2D uMaterialAuxTex;

uniform vec2 uScreenSize;
uniform float uFilterStrength;

vec3 reconstructNormal(vec3 packedNormal) {
    return normalize(packedNormal * 2.0 - 1.0);
}

void main() {
    vec4 reflection = texture(uReflectionTex, vTexCoord);
    float depth = texture(uDepthTex, vTexCoord).r;

    // Sky pixels: pass through
    if (depth >= 0.9999) {
        FragColor = reflection;
        return;
    }

    SurfaceMaterialAux centerAux = unpackGBufferMaterialAux(texture(uMaterialAuxTex, vTexCoord));
    TranslucentMask centerTransMask = decodeTranslucentMask(centerAux.materialKind);
    if (centerTransMask.isTranslucent) {
        FragColor = reflection;
        return;
    }

    float centerLuma = dot(reflection.rgb, vec3(0.2126, 0.7152, 0.0722));
    if (centerLuma < 1e-5) {
        FragColor = reflection;
        return;
    }

    vec3 centerNormal = reconstructNormal(texture(uNormalAoTex, vTexCoord).rgb);
    SurfaceMaterial centerMaterial = unpackGBufferMaterial(texture(uMaterialTex, vTexCoord));
    float centerRoughness = clamp(centerMaterial.roughness, 0.0, 1.0);

    // Skip filtering for very smooth surfaces (they have clean reflections)
    if (centerRoughness < 0.05) {
        FragColor = reflection;
        return;
    }

    vec3 result = vec3(0.0);
    float totalWeight = 0.0;
    vec2 texelSize = 1.0 / uScreenSize;

    // Filter radius based on roughness
    int radius = int(mix(1, 3, smoothstep(0.1, 0.8, centerRoughness)));

    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            vec2 sampleUv = vTexCoord + offset;

            vec4 sampleReflection = texture(uReflectionTex, sampleUv);
            float sampleDepth = texture(uDepthTex, sampleUv).r;
            vec3 sampleNormal = reconstructNormal(texture(uNormalAoTex, sampleUv).rgb);

            // Depth weight: reject samples at very different depths
            float depthWeight = exp2(-abs(sampleDepth - depth) * 64.0 / max(depth, 0.001));

            // Normal weight: reject samples with different normals
            float normalWeight = pow(max(dot(centerNormal, sampleNormal), 0.0), 16.0);

            // Spatial weight: Gaussian falloff
            float spatialWeight = exp2(-float(x * x + y * y) * 0.2);

            float centerLumaForWeight = dot(reflection.rgb, vec3(0.2126, 0.7152, 0.0722));
            float sampleLumaForWeight = dot(sampleReflection.rgb, vec3(0.2126, 0.7152, 0.0722));
            float lumaScale = max(centerLumaForWeight * 0.75, 0.04);
            float lumaWeight = exp2(-abs(sampleLumaForWeight - centerLumaForWeight) / lumaScale);

            float distanceWeight = exp2(-abs(sampleReflection.a - reflection.a) * 4.0);
            float confidenceWeight = max(sampleLumaForWeight, 0.01);

            float weight = depthWeight * normalWeight * spatialWeight *
                           distanceWeight * lumaWeight * confidenceWeight;
            result += sampleReflection.rgb * weight;
            totalWeight += weight;
        }
    }

    result /= max(totalWeight, 0.0001);

    // Luma-chroma sharpening: separate luminance and chroma
    float filteredLuma = dot(result, vec3(0.2126, 0.7152, 0.0722));

    // Sharpen luminance slightly, keep filtered chroma
    float sharpenedLuma = mix(filteredLuma, centerLuma, 0.25 * uFilterStrength);
    vec3 chromaRatio = result / max(filteredLuma, 1e-6);
    vec3 sharpened = chromaRatio * sharpenedLuma;

    // Blend based on roughness: only filter rough surfaces
    float filterMix = smoothstep(0.05, 0.35, centerRoughness) * uFilterStrength;
    vec3 finalColor = mix(reflection.rgb, sharpened, filterMix);

    FragColor = vec4(finalColor, reflection.a);
}
