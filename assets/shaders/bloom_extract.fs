#version 450 core

// DerivativeMain DownSample0: 5-tap cross average, no brightness threshold.
// HDR values above 1.0 naturally dominate; exposure compensation in the
// final composite prevents bloom from blowing out in bright scenes.

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneTex;

void main() {
    vec2 texel = 1.0 / vec2(textureSize(uSceneTex, 0));
    vec3 bloom  = texture(uSceneTex, vTexCoord).rgb;
    bloom += texture(uSceneTex, vTexCoord + vec2( 1.0,  1.0) * texel).rgb;
    bloom += texture(uSceneTex, vTexCoord + vec2(-1.0,  1.0) * texel).rgb;
    bloom += texture(uSceneTex, vTexCoord + vec2( 1.0, -1.0) * texel).rgb;
    bloom += texture(uSceneTex, vTexCoord + vec2(-1.0, -1.0) * texel).rgb;
    bloom *= 0.2;

    // DerivativeMain DownSample0: raw HDR downsample, no brightness clamp.
    // Exposure compensation in Grade (bloomAmount /= fma(max(exposure,1.0),0.7,0.3))
    // prevents bloom from blowing out in bright scenes.
    FragColor = vec4(bloom, 1.0);
}
