#version 450 core

#include "rhi_screen_coordinates.glsl"

layout(location = 0) in vec2 vScreenUv;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uSceneTex;
layout(binding = 1) uniform sampler2D uVolumetricTex;
layout(binding = 2) uniform sampler2D uDepthTex;

layout(push_constant) uniform RhiPushConstants {
    vec4 uDepthParams;
    ivec4 uCompositeFlags;
};

float viewDistanceFromDepth(float depth) {
    if (depth >= 0.9999) {
        return 1e6;
    }
    // DerivativeMain/lib/Head/Functions.inc GetDepthLinear(): depth-only
    // view-space z distance. Do not use ray length here; SpatialUpscale's
    // sigmaZ is tuned for linear depth and ray length jitters at screen edges.
    return (uDepthParams.x * uDepthParams.y) /
           (depth * (uDepthParams.x - uDepthParams.y) + uDepthParams.y);
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
    ivec2 bias = (uCompositeFlags.y != 0 || uCompositeFlags.z != 0)
        ? ivec2(floor(fullCoord)) & ivec2(1)
        : ivec2(fullCoord + float(uCompositeFlags.x)) & ivec2(1);
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
    vec2 textureUv = rhiScreenUvToTextureUv(vScreenUv);
    vec3 scene = texture(uSceneTex, textureUv).rgb;

    // When volumetric fog is disabled, output transmittance=1.0 (no fog) so that
    // Bloomy Fog in postprocess doesn't misinterpret alpha=0 as "fully fogged".
    if (uCompositeFlags.w == 0) {
        FragColor = vec4(scene, 1.0);
        return;
    }

    vec4 volumetric = (uCompositeFlags.z != 0)
        ? texture(uVolumetricTex, textureUv)
        : spatialUpscaleVolumetric(textureUv);
    // Output fog transmittance in alpha for Bloomy Fog in postprocess.
    // volumetric.a = 1 - opacity = transmittance (from volumetric_fog.fs).
    FragColor = vec4(scene * volumetric.a + volumetric.rgb, volumetric.a);
}
