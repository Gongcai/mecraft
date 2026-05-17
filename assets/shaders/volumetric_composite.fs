#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneTex;
uniform sampler2D uVolumetricTex;
uniform sampler2D uDepthTex;
uniform vec2 uInvFullResolution;
uniform mat4 uInvProjection;
uniform int uFrameIndex;

float viewDistanceFromDepth(float depth, vec2 uv) {
    if (depth >= 0.9999) {
        return 1e6;
    }
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 view = uInvProjection * clip;
    view.xyz /= max(view.w, 1e-6);
    return length(view.xyz);
}

float viewDistanceFromDepthTexel(ivec2 texel) {
    ivec2 size = textureSize(uDepthTex, 0);
    ivec2 clampedTexel = clamp(texel, ivec2(0), size - ivec2(1));
    vec2 uv = (vec2(clampedTexel) + 0.5) * uInvFullResolution;
    return viewDistanceFromDepth(texelFetch(uDepthTex, clampedTexel, 0).r, uv);
}

vec4 spatialUpscaleVolumetric(vec2 uv) {
    vec2 fullCoord = gl_FragCoord.xy;
    float centerLinearDepth = viewDistanceFromDepth(texture(uDepthTex, uv).r, uv);
    ivec2 halfSize = textureSize(uVolumetricTex, 0);

    // DerivativeMain spatial upscale: reconstruct from the matching checkerboard
    // half-res texels and weight by linear depth, avoiding screen-space fog sheets.
    ivec2 bias = (ivec2(floor(fullCoord)) + ivec2(uFrameIndex)) & ivec2(1);
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
    vec4 volumetric = spatialUpscaleVolumetric(vTexCoord);
    // Output fog transmittance in alpha for Bloomy Fog in postprocess.
    // volumetric.a = 1 - opacity = transmittance (from volumetric_fog.fs).
    FragColor = vec4(scene * volumetric.a + volumetric.rgb, volumetric.a);
}
