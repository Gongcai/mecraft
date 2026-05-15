#version 450 core
// DerivativeMain-style Depth of Field (program/Post/DoF.glsl)
// Golden-angle spiral sampling with thin-lens CoC

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneTex;
uniform sampler2D uDepthTex;
uniform sampler2D uNoiseTex;
uniform mat4 uProjection;
uniform mat4 uInvProjection;
uniform float uFocusDistance;    // in blocks (world units)
uniform float uAperture;         // f-stop (e.g. 2.8)
uniform float uDofIntensity;     // 0.0 = off, 0.05 = subtle, 0.3 = strong
uniform float uDofAnamorphic;    // anamorphic ratio (1.0 = normal)
uniform vec2 uScreenSize;

const float PHI = 1.61803398875;
const float GOLDEN_ANGLE = 6.28318530718 / (PHI + 1.0);
const float TAU = 6.28318530718;

void main() {
    ivec2 texel = ivec2(gl_FragCoord.xy);
    vec2 screenCoord = gl_FragCoord.xy / uScreenSize;

    float depth = texelFetch(uDepthTex, texel, 0).r;

    // Skip sky pixels
    if (depth >= 0.9999) {
        FragColor = texelFetch(uSceneTex, texel, 0);
        return;
    }

    // ---- DerivativeMain CoC calculation ----
    // Auto-focus: sample depth at screen center (DerivativeMain FOCUS_MODE = 0)
    ivec2 centerTexel = ivec2(uScreenSize * 0.5);
    float centerDepth = texelFetch(uDepthTex, centerTexel, 0).r;
    float centerDist = 1.0 / (centerDepth * uInvProjection[2][3] + uInvProjection[3][3]);
    // Smooth focus: blend with previous focus (simplified - just use center depth)
    float focusDist = centerDist;

    // Focal length from projection matrix (DerivativeMain line 52)
    // gbufferProjection[1][1] = cot(fov/2), focalLength = 0.5 * 0.035 * cot(fov/2)
    float projY11 = uProjection[1][1];
    float focalLength = 0.5 * 0.035 * projY11;

    // Aperture from f-stop (DerivativeMain line 53)
    float aperture = focalLength / max(uAperture, 0.8);

    // Pixel distance: convert depth-buffer value to linear block distance
    // DerivativeMain: dist = 1.0 / (depth * projInv[2][3] + projInv[3][3])
    float dist = 1.0 / (depth * uInvProjection[2][3] + uInvProjection[3][3]);

    // Circle of Confusion (DerivativeMain line 31)
    // CoC = (1 - focusDist/pixelDist) * aperture * focalLength / (focusDist - focalLength)
    float CoC = (1.0 - focusDist / dist) * aperture * focalLength / (focusDist - focalLength);

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

    for (int i = 0; i < SAMPLES; ++i) {
        // sqrt spacing: more samples near center, fewer at edges
        vec2 sampleOffset = rot * sqrt((noise + float(i)) * rSteps);
        ivec2 sampleCoord = texel + ivec2(sampleOffset);
        sampleCoord = clamp(sampleCoord, ivec2(0), ivec2(uScreenSize) - 1);
        color += texelFetch(uSceneTex, sampleCoord, 0);
        rot *= goldenRotate;
    }

    color *= rSteps;

    FragColor = color;
}
