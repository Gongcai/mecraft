#version 450 core

// DerivativeMain DownSample0 (composite4): 5-tap cross average at lod 0.
// DerivativeMain DownSample (composite10): weighted kernel from scene mip chain.
// Mecraft adaptation: each bloom mip is a separate FBO, so no atlas tile offset.
// BLUR_SAMPLES=1 matches DerivativeMain default (3x3 kernel per mip).

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneTex;
uniform int uSourceLod;

// DerivativeMain/program/Post/DownSample0.glsl: DualBlurDownSample().
// 5-tap cross average from scene at lod 0.
vec3 downsample0(vec2 uv) {
    vec2 texel = 1.0 / vec2(textureSize(uSceneTex, 0));
    vec3 bloom  = textureLod(uSceneTex, uv, 0.0).rgb;
    bloom += textureLod(uSceneTex, uv + vec2( 1.0,  1.0) * texel, 0.0).rgb;
    bloom += textureLod(uSceneTex, uv + vec2(-1.0,  1.0) * texel, 0.0).rgb;
    bloom += textureLod(uSceneTex, uv + vec2( 1.0, -1.0) * texel, 0.0).rgb;
    bloom += textureLod(uSceneTex, uv + vec2(-1.0, -1.0) * texel, 0.0).rgb;
    return bloom * 0.2;
}

// DerivativeMain/program/Post/DownSample.glsl: DualBlurDownSample(lod).
// Weighted kernel reads from scene mip chain at the given lod.
vec3 derivativeDownsample(vec2 uv, int lod) {
    // Mecraft adaptation: vTexCoord is already [0,1] per-mip UV, no tile offset.
    const int BLUR_SAMPLES = 1; // Match DerivativeMain default: 3x3 kernel
    vec2 texelOffset = exp2(float(lod)) / vec2(textureSize(uSceneTex, 0));
    vec3 bloom = vec3(0.0);
    float sumWeight = 0.0;
    for (int y = -BLUR_SAMPLES; y <= BLUR_SAMPLES; ++y) {
        for (int x = -BLUR_SAMPLES; x <= BLUR_SAMPLES; ++x) {
            float weight = clamp(1.0 - length(vec2(x, y)) * 0.25, 0.0, 1.0);
            weight *= weight;
            bloom += textureLod(uSceneTex, uv + vec2(x, y) * texelOffset, float(lod)).rgb * weight;
            sumWeight += weight;
        }
    }
    return bloom / max(sumWeight, 0.0001);
}

void main() {
    int lod = max(uSourceLod, 0);
    vec3 bloom = lod == 0 ? downsample0(vTexCoord) : derivativeDownsample(vTexCoord, lod);

    // DerivativeMain DownSample0/DownSample: raw HDR downsample, no brightness clamp.
    // Exposure compensation in Grade (bloomAmount /= fma(max(exposure,1.0),0.7,0.3))
    // prevents bloom from blowing out in bright scenes.
    FragColor = vec4(clamp(bloom, 0.0, 65535.0), 1.0);
}
