#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uDepthTex;
uniform sampler2D uNormalAoTex;
uniform mat4 uProjection;
uniform vec2 uInvResolution;
uniform float uRadius;
uniform float uStrength;

float linear01(float depth) {
    return depth;
}

void main() {
    float centerDepth = texture(uDepthTex, vTexCoord).r;
    if (centerDepth >= 1.0) {
        FragColor = vec4(1.0);
        return;
    }

    vec3 normal = texture(uNormalAoTex, vTexCoord).rgb * 2.0 - 1.0;
    float radiusPixels = clamp(uRadius * 3.0, 2.0, 24.0);
    float occlusion = 0.0;
    float samples = 0.0;

    const vec2 dirs[8] = vec2[](
        vec2( 1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0,  1.0), vec2(0.0, -1.0),
        vec2( 0.7, 0.7), vec2(-0.7, 0.7), vec2(0.7, -0.7), vec2(-0.7, -0.7)
    );

    for (int i = 0; i < 8; ++i) {
        vec2 uv = vTexCoord + dirs[i] * uInvResolution * radiusPixels;
        float sampleDepth = texture(uDepthTex, uv).r;
        float delta = centerDepth - sampleDepth;
        float range = smoothstep(0.00002, 0.0025, abs(delta));
        occlusion += (delta > 0.00008 ? 1.0 : 0.0) * range;
        samples += 1.0;
    }

    float bentNormalBoost = clamp(normal.y * 0.1 + 0.95, 0.85, 1.0);
    float ao = 1.0 - (occlusion / max(samples, 1.0)) * clamp(uStrength, 0.0, 4.0);
    FragColor = vec4(vec3(clamp(ao * bentNormalBoost, 0.0, 1.0)), 1.0);
}
