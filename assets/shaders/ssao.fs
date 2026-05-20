#version 450 core

#include "derivative_shadow.glsl"

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uDepthTex;
uniform sampler2D uNormalAoTex;
uniform sampler2D uNoiseTex;
uniform mat4 uProjection;
uniform mat4 uInvProjection;
uniform vec2 uInvResolution;
uniform float uRadius;
uniform float uStrength;
uniform int uFrameIndex;
uniform int uSamples;

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

    float rSteps = 1.0 / float(uSamples);
    float maxSqLen = sqr(viewPos.z) * 0.25;

    // Step size in screen space, scaled by projection and user radius.
    // Scale by sqrt(sampleCount/6) so total radius grows sub-linearly with more samples,
    // keeping occlusion spread consistent across different sample counts.
    float stepScale = sqrt(float(uSamples) / 6.0);
    float aspect = uInvResolution.y / uInvResolution.x;
    vec2 rayStep = vec2(uRadius * aspect, uRadius) /
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

    for (int i = 0; i < uSamples; ++i, rot *= goldenRotate) {
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

    float ao = max0(1.0 - total * rSteps * uStrength);
    ao *= sqrt(ao);  // Perceptual curve matching DerivativeMain
    FragColor = vec4(vec3(ao), 1.0);
}
