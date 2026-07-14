#version 450 core

// DerivativeMain/program/Post/DownSample.glsl::DualBlurDownSample.
// Mecraft adaptation: each bloom mip is a separate FBO. The C++ pass binds
// DerivativeMain lod 1..7 to Mecraft bloom mip 0..6, so the texture UV is already
// the tile-local coord after DerivativeMain's CalculateTileOffset mapping.
// BLUR_SAMPLES=1 matches DerivativeMain default.

#include "rhi_screen_coordinates.glsl"

layout(location = 0) in vec2 vScreenUv;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uSceneTex;
layout(push_constant) uniform RhiPushConstants {
    ivec4 pSource;
};

#define uSourceLod pSource.x

vec3 DualBlurDownSample(vec2 uv, int lod) {
    const int BLUR_SAMPLES = 1;
    vec2 texelOffset = exp2(float(lod)) / vec2(textureSize(uSceneTex, 0));
    vec3 bloom = vec3(0.0);
    float sumWeight = 0.0;
    for (int y = -BLUR_SAMPLES; y <= BLUR_SAMPLES; ++y) {
        for (int x = -BLUR_SAMPLES; x <= BLUR_SAMPLES; ++x) {
            float weight = clamp(1.0 - length(vec2(x, y)) * 0.25, 0.0, 1.0);
            weight *= weight;
            bloom += texture(uSceneTex, uv + vec2(x, y) * texelOffset).rgb * weight;
            sumWeight += weight;
        }
    }
    return bloom / max(sumWeight, 0.0001);
}

void main() {
    int lod = clamp(uSourceLod, 1, 7);
    vec3 bloom = DualBlurDownSample(rhiScreenUvToTextureUv(vScreenUv), lod);

    // DerivativeMain DownSample: raw HDR downsample, no brightness threshold.
    // Exposure compensation in Grade (bloomAmount /= fma(max(exposure,1.0),0.7,0.3))
    // prevents bloom from blowing out in bright scenes.
    FragColor = vec4(clamp(bloom, 0.0, 65535.0), 1.0);
}
