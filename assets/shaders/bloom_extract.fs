#version 450 core

// DerivativeMain DownSample0: 5-tap cross average, no brightness threshold.
// HDR values above 1.0 naturally dominate; exposure compensation in the
// final composite prevents bloom from blowing out in bright scenes.

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneTex;
uniform int uSourceLod;

vec3 downsample0(vec2 uv) {
    vec2 texel = 1.0 / vec2(textureSize(uSceneTex, 0));
    vec3 bloom  = textureLod(uSceneTex, uv, 0.0).rgb;
    bloom += textureLod(uSceneTex, uv + vec2( 1.0,  1.0) * texel, 0.0).rgb;
    bloom += textureLod(uSceneTex, uv + vec2(-1.0,  1.0) * texel, 0.0).rgb;
    bloom += textureLod(uSceneTex, uv + vec2( 1.0, -1.0) * texel, 0.0).rgb;
    bloom += textureLod(uSceneTex, uv + vec2(-1.0, -1.0) * texel, 0.0).rgb;
    return bloom * 0.2;
}

vec3 derivativeDownsample(vec2 uv, int lod) {
    // DerivativeMain/program/Post/DownSample.glsl: DualBlurDownSample().
    // Mecraft adaptation: each bloom tile is a separate texture, so vTexCoord is
    // already local tile UV and no atlas tile offset is needed.
    const int BLUR_SAMPLES = 4;
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
