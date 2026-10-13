#version 450 core
// DerivativeMain-style Depth of Field (program/Post/DoF.glsl)
// Golden-angle spiral sampling with thin-lens CoC

layout(location = 0) in vec2 vScreenUv;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uSceneTex;
layout(binding = 1) uniform sampler2D uDepthTex;
layout(binding = 2) uniform sampler2D uNoiseTex;
layout(binding = 3) uniform usampler2D uObjectMaterialIdTex;

layout(push_constant) uniform RhiPushConstants {
    mat4 uProjection;
    mat4 uInvProjection;
    vec4 uDofParams;
    vec4 uScreenParams;
    uint uFirstPersonObjectId;
};

const float PHI = 1.61803398875;
const float GOLDEN_ANGLE = 6.28318530718 / (PHI + 1.0);
const float TAU = 6.28318530718;

float linearViewDistance(float depthValue) {
    return 1.0 / (depthValue * uInvProjection[2][3] + uInvProjection[3][3]);
}

void main() {
    float uAperture = uDofParams.y;         // f-stop (e.g. 2.8)
    float uDofIntensity = uDofParams.z;     // 0.0 = off, 0.05 = subtle, 0.3 = strong
    float uDofAnamorphic = uDofParams.w;    // anamorphic ratio (1.0 = normal)
    vec2 uScreenSize = uScreenParams.xy;

    ivec2 texel = ivec2(gl_FragCoord.xy);
    vec2 screenCoord = gl_FragCoord.xy / uScreenSize;

    // The first-person object is kept sharp and is excluded from neighboring
    // samples so its color cannot bleed into the blurred world.
    bool excludeFirstPerson = uFirstPersonObjectId != 0u;
    uint centerObjectId = texelFetch(uObjectMaterialIdTex, texel, 0).r;
    if (excludeFirstPerson && centerObjectId == uFirstPersonObjectId) {
        FragColor = texelFetch(uSceneTex, texel, 0);
        return;
    }

    float depth = texelFetch(uDepthTex, texel, 0).r;

    // Sky pixels use the hyperfocal focus target below so distant scenery stays
    // within the chosen circle-of-confusion limit.

    // ---- DerivativeMain CoC calculation ----
    // Auto-focus uses the screen center, but searches past the first-person
    // object so the held item cannot become the focus target.
    float projY11 = uProjection[1][1];
    float focalLength = 0.5 * 0.035 * projY11;
    float aperture = focalLength / max(uAperture, 0.8);
    float cocScale = max(uScreenSize.x * uDofAnamorphic, uScreenSize.y) * uDofIntensity * 1e2 * TAU;
    float acceptableCoc = 0.5 / max(cocScale, 0.001);
    float hyperfocalDistance = focalLength * focalLength / max(aperture * acceptableCoc, 0.000001) + focalLength;
    float farDistance = linearViewDistance(0.999999);
    if (isnan(farDistance) || isinf(farDistance) || farDistance <= 0.001) {
        farDistance = 100000.0;
    }
    float skyFocusDistance = min(hyperfocalDistance, farDistance);
    float focusDist = max(uDofParams.x, 0.001);
    ivec2 focusTexel = ivec2(uScreenSize * 0.5);
    float focusCenterDepth = texelFetch(uDepthTex, focusTexel, 0).r;
    uint focusCenterObjectId = texelFetch(uObjectMaterialIdTex, focusTexel, 0).r;
    bool focusCenterIsHeldItem = excludeFirstPerson && focusCenterObjectId == uFirstPersonObjectId;
    bool hasSceneFocus = false;
    bool centerSeesSky = focusCenterDepth >= 0.999999 && !focusCenterIsHeldItem;
    if (centerSeesSky) {
        focusDist = skyFocusDistance;
    } else {
        for (int radius = 0; radius <= 8 && !hasSceneFocus; ++radius) {
            for (int y = -radius; y <= radius && !hasSceneFocus; ++y) {
                for (int x = -radius; x <= radius; ++x) {
                    if (radius != 0 && max(abs(x), abs(y)) != radius) {
                        continue;
                    }
                    ivec2 candidateTexel = clamp(focusTexel + ivec2(x, y), ivec2(0), ivec2(uScreenSize) - 1);
                    float candidateDepth = texelFetch(uDepthTex, candidateTexel, 0).r;
                    uint candidateObjectId = texelFetch(uObjectMaterialIdTex, candidateTexel, 0).r;
                    if (candidateDepth >= 0.999999 ||
                        (excludeFirstPerson && candidateObjectId == uFirstPersonObjectId)) {
                        continue;
                    }
                    float candidateDistance = linearViewDistance(candidateDepth);
                    if (candidateDistance > 0.001 && candidateDistance < 100000.0) {
                        focusDist = candidateDistance;
                        hasSceneFocus = true;
                        break;
                    }
                }
            }
        }
        if (!hasSceneFocus && focusCenterIsHeldItem) {
            focusDist = skyFocusDistance;
        }
    }

    // Pixel distance: convert depth-buffer value to linear block distance
    // DerivativeMain: dist = 1.0 / (depth * projInv[2][3] + projInv[3][3])
    float dist = linearViewDistance(depth);

    // Circle of Confusion (DerivativeMain line 31)
    // CoC = (1 - focusDist/pixelDist) * aperture * focalLength / (focusDist - focalLength)
    float CoC = (1.0 - focusDist / dist) * aperture * focalLength /
                max(abs(focusDist - focalLength), 0.001) * sign(focusDist - focalLength);

    // Scale to screen pixels (DerivativeMain line 58)
    // CoC *= screenSize * DOF_INTENSITY * 1e2 * TAU
    vec2 CoC2 = abs(CoC) * vec2(uDofAnamorphic, 1.0) * uScreenSize * uDofIntensity * 1e2 * TAU;

    // Skip if CoC is negligible
    if (length(CoC2) < 0.5) {
        FragColor = texelFetch(uSceneTex, texel, 0);
        return;
    }

    // ---- Golden-angle spiral sampling (DerivativeMain line 59-84) ----
    const int SAMPLES = 32;
    mat2 goldenRotate = mat2(cos(GOLDEN_ANGLE), -sin(GOLDEN_ANGLE),
                             sin(GOLDEN_ANGLE), cos(GOLDEN_ANGLE));

    float noise = texture(uNoiseTex, screenCoord * 3.0).r;
    vec2 rot = vec2(cos(noise * TAU), sin(noise * TAU)) * CoC2;

    vec4 color = vec4(0.0);
    float rSteps = 1.0 / float(SAMPLES);
    int validSamples = 0;

    for (int i = 0; i < SAMPLES; ++i) {
        // sqrt spacing: more samples near center, fewer at edges
        vec2 sampleOffset = rot * sqrt((noise + float(i)) * rSteps);
        ivec2 sampleCoord = texel + ivec2(sampleOffset);
        sampleCoord = clamp(sampleCoord, ivec2(0), ivec2(uScreenSize) - 1);
        uint sampleObjectId = texelFetch(uObjectMaterialIdTex, sampleCoord, 0).r;
        if (excludeFirstPerson && sampleObjectId == uFirstPersonObjectId) {
            rot *= goldenRotate;
            continue;
        }
        color += texelFetch(uSceneTex, sampleCoord, 0);
        validSamples++;
        rot *= goldenRotate;
    }

    if (validSamples == 0) {
        FragColor = texelFetch(uSceneTex, texel, 0);
        return;
    }
    color /= float(validSamples);

    FragColor = color;
}
