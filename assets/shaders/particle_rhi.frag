#version 450 core

layout(location = 0) in vec2 vUv;
layout(location = 1) in float vLayer;
layout(location = 2) in float vAlpha;
layout(location = 3) in float vBiomeTintFactor;
layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2DArray uTextureArray;
#ifdef PARTICLE_DEFERRED
layout(binding = 1) uniform sampler2D uVoxelLightTexture;
layout(binding = 2) uniform sampler2D uDepthTexture;
#endif

layout(std140, binding = 15) uniform RhiPushConstants {
    mat4 uViewProj;
    vec4 uBiomeTint;
    vec4 uScreenParams;
};

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

void main() {
#ifdef PARTICLE_DEFERRED
    ivec2 depthSize = textureSize(uDepthTexture, 0);
    ivec2 depthTexel = clamp(ivec2(gl_FragCoord.xy), ivec2(0), depthSize - ivec2(1));
    float sceneDepth = texelFetch(uDepthTexture, depthTexel, 0).r;
    if (sceneDepth < 0.999999 && gl_FragCoord.z > sceneDepth + 1.0e-5) {
        discard;
    }
#endif
    vec4 texel = texture(uTextureArray, vec3(vUv, vLayer));
    if (texel.a < 0.1) {
        discard;
    }
    vec3 albedo = texel.rgb;
#ifdef PARTICLE_DEFERRED
    albedo = srgbToLinear(albedo);
#endif
    if (vBiomeTintFactor > 0.5) {
#ifdef PARTICLE_DEFERRED
        albedo *= srgbToLinear(uBiomeTint.rgb);
#else
        albedo *= uBiomeTint.rgb;
#endif
    }
#ifdef PARTICLE_DEFERRED
    vec2 screenUv = gl_FragCoord.xy / max(uScreenParams.xy, vec2(1.0));
    vec2 voxelLight = texture(uVoxelLightTexture, screenUv).rg;
    albedo *= max(max(voxelLight.r, voxelLight.g), 0.05);
#endif
    fragColor = vec4(albedo, texel.a * vAlpha);
}
