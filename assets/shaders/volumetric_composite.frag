#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneTex;
uniform sampler2D uVolumetricTex;
uniform sampler2D uDepthTex;
uniform float uNearPlane;
uniform float uFarPlane;
uniform int uFrameIndex;
uniform int uFreezeBias; // A/B test: 1 = use static bias (no temporal rotation)
uniform int uIsEyeInWater;
uniform int uVolumetricFogActive; // 0 = fog disabled, output transmittance=1.0

float viewDistanceFromDepth(float depth) {
    if (depth >= 0.9999) {
        return 1e6;
    }
    // DerivativeMain/lib/Head/Functions.inc GetDepthLinear(): depth-only
    // view-space z distance. Do not use ray length here; SpatialUpscale's
    // sigmaZ is tuned for linear depth and ray length jitters at screen edges.
    return (uNearPlane * uFarPlane) / (depth * (uNearPlane - uFarPlane) + uFarPlane);
}

float viewDistanceFromDepthTexel(ivec2 texel) {
    ivec2 size = textureSize(uDepthTex, 0);
    ivec2 clampedTexel = clamp(texel, ivec2(0), size - ivec2(1));
    return viewDistanceFromDepth(texelFetch(uDepthTex, clampedTexel, 0).r);
}

vec4 spatialUpscaleVolumetric(vec2 uv) {
    vec2 fullCoord = gl_FragCoord.xy;
    float centerLinearDepth = viewDistanceFromDepth(texture(uDepthTex, uv).r);
    ivec2 halfSize = textureSize(uVolumetricTex, 0);

    // DerivativeMain spatial upscale: reconstruct from the matching checkerboard
    // half-res texels and weight by linear depth, avoiding screen-space fog sheets.
    // DerivativeMain lib/Atmosphere/Fogs.glsl:46: bias rotates with frameCounter
    // so each frame samples a different 2x2 quarter, providing temporal variation.
    // A/B test: uFreezeBias=1 uses static bias (no temporal rotation).
    ivec2 bias = (uFreezeBias != 0 || uIsEyeInWater != 0)
        ? ivec2(floor(fullCoord)) & ivec2(1)
        : ivec2(fullCoord + float(uFrameIndex)) & ivec2(1);
    ivec2 baseTexel = ivec2(floor(fullCoord * 0.5)) + bias * 2;
    ivec2 offsets[4] = ivec2[](
        ivec2(-2, -2),
        ivec2(-2,  0),
        ivec2( 0,  0),
        ivec2( 0, -2)
    );

    float sigmaZ = 64.0 / max(centerLinearDepth, 1.0);
    vec4 sum = vec4(0.0);
    float weightSum = 0.0;

    for (int i = 0; i < 4; ++i) {
        ivec2 sampleTexel = clamp(baseTexel + offsets[i], ivec2(0), halfSize - ivec2(1));
        float sampleLinearDepth = viewDistanceFromDepthTexel(sampleTexel * 2);
        float weight = max(exp2(-abs(sampleLinearDepth - centerLinearDepth) * sigmaZ), 1e-6);
        sum += texelFetch(uVolumetricTex, sampleTexel, 0) * weight;
        weightSum += weight;
    }

    return sum / max(weightSum, 0.0001);
}

void main() {
    vec3 scene = texture(uSceneTex, vTexCoord).rgb;

    // When volumetric fog is disabled, output transmittance=1.0 (no fog) so that
    // Bloomy Fog in postprocess doesn't misinterpret alpha=0 as "fully fogged".
    if (uVolumetricFogActive == 0) {
        FragColor = vec4(scene, 1.0);
        return;
    }

    vec4 volumetric = (uIsEyeInWater != 0)
        ? texture(uVolumetricTex, vTexCoord)
        : spatialUpscaleVolumetric(vTexCoord);
    // Output fog transmittance in alpha for Bloomy Fog in postprocess.
    // volumetric.a = 1 - opacity = transmittance (from volumetric_fog.fs).
    FragColor = vec4(scene * volumetric.a + volumetric.rgb, volumetric.a);
}
