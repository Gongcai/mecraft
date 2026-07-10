#version 450 core

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uInputTex;
layout(std140, binding = 15) uniform RhiPushConstants {
    vec4 pSourceSize;
    ivec4 pFlags;
};

#define uSourceSize pSourceSize.xy
#define uSourceIsScene (pFlags.x != 0)
#define uSourceLod pFlags.y

float luminance709(vec3 color) {
    return dot(max(color, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
}

float centerWeight(vec2 uv) {
    float radial = length(uv * 2.0 - 1.0);
    float weight = 1.0 - smoothstep(0.25, 0.75, radial);
    weight = weight * weight * (3.0 - 2.0 * weight);
    return weight * 0.9 + 0.1;
}

vec2 readExposureSample(ivec2 texel) {
    ivec2 clampedTexel = clamp(texel, ivec2(0), ivec2(uSourceSize) - ivec2(1));
    if (uSourceIsScene) {
        ivec2 sceneSize = textureSize(uInputTex, 0);
        int stride = 1 << clamp(uSourceLod, 0, 12);
        ivec2 sceneTexel = clamp(clampedTexel * stride + ivec2(stride / 2), ivec2(0), sceneSize - ivec2(1));
        vec3 color = texelFetch(uInputTex, sceneTexel, 0).rgb;
        float lum = max(luminance709(color), 1e-6);
        float weight = centerWeight((vec2(clampedTexel) + 0.5) / max(uSourceSize, vec2(1.0)));
        return vec2(max(log(lum), -18.0) * weight, weight);
    }
    return texelFetch(uInputTex, clampedTexel, 0).rg;
}

void main() {
    ivec2 baseTexel = ivec2(gl_FragCoord.xy) * 2;
    vec2 exposureData = vec2(0.0);
    exposureData += readExposureSample(baseTexel + ivec2(0, 0));
    exposureData += readExposureSample(baseTexel + ivec2(1, 0));
    exposureData += readExposureSample(baseTexel + ivec2(0, 1));
    exposureData += readExposureSample(baseTexel + ivec2(1, 1));
    FragColor = vec4(exposureData * 0.25, 0.0, 1.0);
}
