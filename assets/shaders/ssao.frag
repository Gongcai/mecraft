#version 450 core

#include "derivative_shadow.glsl"

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uDepthTex;
layout(binding = 1) uniform sampler2D uNormalAoTex;
layout(binding = 2) uniform sampler2D uNoiseTex;

layout(push_constant) uniform RhiPushConstants {
    mat4 uProjection;
    mat4 uInvProjection;
    vec4 uSsaoParams0;
    ivec4 uSsaoParams1;
};

vec3 screenToViewPos(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 view = uInvProjection * clip;
    return view.xyz / max(view.w, 0.00001);
}

void main() {
    float centerDepth = texture(uDepthTex, vTexCoord).r;
    if (centerDepth >= 1.0) {
        FragColor = vec4(1.0);
        return;
    }

    vec3 viewPos = screenToViewPos(vTexCoord, centerDepth);
    vec3 normal = normalize(texture(uNormalAoTex, vTexCoord).rgb * 2.0 - 1.0);

    // Dither rotation per pixel using tiled noise texture.
    // gl_FragCoord.xy / noiseSize tiles the noise across the screen exactly once per noise repeat.
    vec2 noiseUv = gl_FragCoord.xy / vec2(textureSize(uNoiseTex, 0));
    float dither = texture(uNoiseTex, noiseUv).r;

    int sampleCount = uSsaoParams1.y;
    float rSteps = 1.0 / float(sampleCount);
    float maxSqLen = sqr(viewPos.z) * 0.25;

    // Step size in screen space, scaled by projection and user radius.
    // Scale by sqrt(sampleCount/6) so total radius grows sub-linearly with more samples,
    // keeping occlusion spread consistent across different sample counts.
    float stepScale = sqrt(float(sampleCount) / 6.0);
    float aspect = uSsaoParams0.y / uSsaoParams0.x;
    vec2 rayStep = vec2(uSsaoParams0.z * aspect, uSsaoParams0.z) /
                   max((-1.0 - viewPos.z) * 0.5, 5.0) * uProjection[1][1]
                   / stepScale;

    // Golden-angle rotation matrix
    const float goldenAngle = TAU / ((1.0 + sqrt(5.0)) / 2.0 + 1.0);
    mat2 goldenRotate = mat2(
        cos(goldenAngle), -sin(goldenAngle),
        sin(goldenAngle),  cos(goldenAngle)
    );

    vec2 rot = sincos(dither * TAU) * rSteps;
    vec2 radius = vec2(0.0);
    float total = 0.0;

    for (int i = 0; i < sampleCount; ++i, rot *= goldenRotate) {
        radius += rayStep;

        // Sample at +rot
        vec2 sampleUv = vTexCoord + rot * radius;
        float sampleDepth = texture(uDepthTex, sampleUv).r;
        vec3 samplePos = screenToViewPos(sampleUv, sampleDepth);
        vec3 diff = samplePos - viewPos;
        float diffSqLen = dotSelf(diff);
        if (diffSqLen > 1e-5 && diffSqLen < maxSqLen) {
            float NdotL = saturate(dot(normal, diff * inversesqrt(diffSqLen)));
            total += NdotL * saturate(1.0 - diffSqLen / maxSqLen);
        }

        // Sample at -rot
        sampleUv = vTexCoord - rot * radius;
        sampleDepth = texture(uDepthTex, sampleUv).r;
        samplePos = screenToViewPos(sampleUv, sampleDepth);
        diff = samplePos - viewPos;
        diffSqLen = dotSelf(diff);
        if (diffSqLen > 1e-5 && diffSqLen < maxSqLen) {
            float NdotL = saturate(dot(normal, diff * inversesqrt(diffSqLen)));
            total += NdotL * saturate(1.0 - diffSqLen / maxSqLen);
        }
    }

    float ao = max0(1.0 - total * rSteps * uSsaoParams0.w);
    ao *= sqrt(ao);  // Perceptual curve matching DerivativeMain
    FragColor = vec4(vec3(ao), 1.0);
}
