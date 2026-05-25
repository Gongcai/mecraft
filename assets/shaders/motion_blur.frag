#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneTex;
uniform sampler2D uVelocityTex;
uniform sampler2D uDepthTex;
uniform float uStrength;
uniform int uSamples;
uniform vec2 uScreenSize;

// Interleaved Gradient Noise for reducing banding artifacts
float interleavedGradientNoise(vec2 screenPos) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(screenPos, magic.xy)));
}

void main() {
    vec2 velocity = texture(uVelocityTex, vTexCoord).rg;
    float speed = length(velocity) * uScreenSize.x;

    // Skip blur for nearly static pixels
    if (speed < 0.5) {
        FragColor = texture(uSceneTex, vTexCoord);
        return;
    }

    float noise = interleavedGradientNoise(gl_FragCoord.xy);
    vec2 direction = normalize(velocity);
    float blurSize = min(speed * uStrength * 0.01, 0.05);

    vec4 color = vec4(0.0);
    float totalWeight = 0.0;

    for (int i = 0; i < uSamples; ++i) {
        float t = (float(i) + noise) / float(uSamples) - 0.5;
        vec2 sampleUv = vTexCoord + direction * t * blurSize;
        color += texture(uSceneTex, sampleUv);
        totalWeight += 1.0;
    }

    FragColor = color / totalWeight;
}
