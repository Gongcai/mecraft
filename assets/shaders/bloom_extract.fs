#version 450 core

// DerivativeMain DownSample0: 5-tap cross average, no brightness threshold.
// HDR values above 1.0 naturally dominate; exposure compensation in the
// final composite prevents bloom from blowing out in bright scenes.

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneTex;
uniform float uExposure; // Current auto-exposure value

void main() {
    vec2 texel = 1.0 / vec2(textureSize(uSceneTex, 0));
    vec3 bloom  = texture(uSceneTex, vTexCoord).rgb;
    bloom += texture(uSceneTex, vTexCoord + vec2( 1.0,  1.0) * texel).rgb;
    bloom += texture(uSceneTex, vTexCoord + vec2(-1.0,  1.0) * texel).rgb;
    bloom += texture(uSceneTex, vTexCoord + vec2( 1.0, -1.0) * texel).rgb;
    bloom += texture(uSceneTex, vTexCoord + vec2(-1.0, -1.0) * texel).rgb;
    bloom *= 0.2;

    // Clamp to prevent extreme HDR values from dominating bloom.
    // DerivativeMain's scene values are typically 0-2 after exposure;
    // our scene can reach 50+ due to directIlluminance*64 scaling.
    // The exposure compensation in the final composite handles the rest.
    float maxComponent = max(bloom.r, max(bloom.g, bloom.b));
    float clampThreshold = max(uExposure * 8.0, 0.5);
    if (maxComponent > clampThreshold) {
        bloom *= clampThreshold / maxComponent;
    }

    FragColor = vec4(bloom, 1.0);
}
