#version 450 core

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

const float kPi = 3.14159265359;
const float kTwoPi = 6.28318530718;
const float kGoldenAngle = kTwoPi / (1.0 + (1.0 + sqrt(5.0)) / 2.0);
const int kSamples = 6;

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

    // Dither rotation per pixel for temporal stability
    vec2 noiseUv = vTexCoord * vec2(textureSize(uNoiseTex, 0)) * uInvResolution;
    float dither = texture(uNoiseTex, noiseUv).r;

    float rSteps = 1.0 / float(kSamples);
    float maxSqLen = viewPos.z * viewPos.z * 0.25;

    // Step size in screen space, scaled by projection
    float aspect = uInvResolution.y / uInvResolution.x;
    vec2 rayStep = vec2(0.6 * aspect, 0.6) /
                   max((-1.0 - viewPos.z) * 0.5, 5.0) * uProjection[1][1];

    // Golden-angle rotation matrix
    mat2 goldenRotate = mat2(
        cos(kGoldenAngle), -sin(kGoldenAngle),
        sin(kGoldenAngle),  cos(kGoldenAngle)
    );

    vec2 rot = vec2(cos(dither * kTwoPi), sin(dither * kTwoPi)) * rSteps;
    vec2 radius = vec2(0.0);
    float total = 0.0;

    for (int i = 0; i < kSamples; ++i, rot *= goldenRotate) {
        radius += rayStep;

        // Sample at +rot
        vec2 sampleUv = vTexCoord + rot * radius;
        float sampleDepth = texture(uDepthTex, sampleUv).r;
        vec3 samplePos = screenToViewPos(sampleUv, sampleDepth);
        vec3 diff = samplePos - viewPos;
        float diffSqLen = dot(diff, diff);
        if (diffSqLen > 1e-5 && diffSqLen < maxSqLen) {
            float NdotL = clamp(dot(normal, diff * inversesqrt(diffSqLen)), 0.0, 1.0);
            total += NdotL * clamp(1.0 - diffSqLen / maxSqLen, 0.0, 1.0);
        }

        // Sample at -rot
        sampleUv = vTexCoord - rot * radius;
        sampleDepth = texture(uDepthTex, sampleUv).r;
        samplePos = screenToViewPos(sampleUv, sampleDepth);
        diff = samplePos - viewPos;
        diffSqLen = dot(diff, diff);
        if (diffSqLen > 1e-5 && diffSqLen < maxSqLen) {
            float NdotL = clamp(dot(normal, diff * inversesqrt(diffSqLen)), 0.0, 1.0);
            total += NdotL * clamp(1.0 - diffSqLen / maxSqLen, 0.0, 1.0);
        }
    }

    float ao = max(1.0 - total * rSteps * uStrength, 0.0);
    ao *= sqrt(ao);  // perceptual curve matching DerivativeMain
    FragColor = vec4(vec3(ao), 1.0);
}
