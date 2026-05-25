// Particle scene-resolved fragment shader — Mecraft Phase 5.5.
// Renders block-break particles with basic voxel light from GBuffer.
// This is NOT a GBuffer MRT pass — outputs a single RGBA target with
// alpha blending into SceneResolved. Particles are composited before
// volumetric fog so the fog pass applies atmospheric scattering uniformly.

#version 450 core
out vec4 FragColor;

in vec2 vUV;
in float vLayer;
in float vAlpha;
in float vBiomeTintFactor;
in vec3 vWorldPos;

uniform sampler2DArray texArray;
// GBuffer voxel light texture (RG8): sky light.r, block light.g
uniform sampler2D uVoxelLightTex;
// GBuffer opaque depth. SceneComposite has no depth attachment, so particles
// perform explicit screen-space occlusion against this texture.
uniform sampler2D uDepthTex;
uniform vec3 uBiomeTintColor;
uniform vec2 uScreenSize;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

void main() {
    vec2 screenUV = gl_FragCoord.xy / uScreenSize;
    ivec2 depthSize = textureSize(uDepthTex, 0);
    ivec2 depthTexel = clamp(ivec2(gl_FragCoord.xy), ivec2(0), depthSize - ivec2(1));
    float sceneDepth = texelFetch(uDepthTex, depthTexel, 0).r;
    if (sceneDepth < 0.999999 && gl_FragCoord.z > sceneDepth + 1.0e-5) {
        discard;
    }

    vec4 texColor = texture(texArray, vec3(vUV, vLayer));
    if (texColor.a < 0.1)
        discard;

    vec3 albedo = srgbToLinear(texColor.rgb);
    if (vBiomeTintFactor > 0.5) {
        albedo *= srgbToLinear(uBiomeTintColor);
    }

    // Sample voxel light from GBuffer at screen-space position.
    // Particles don't write to the GBuffer, so we read the voxel light
    // that terrain wrote at the same pixel. This gives particles basic
    // sky/block light response without full deferred lighting.
    vec2 voxelLight = texture(uVoxelLightTex, screenUV).rg;
    float skyLight = voxelLight.r;
    float blockLight = voxelLight.g;

    // Combine: sky light provides outdoor brightness, block light provides
    // indoor/cave brightness. Max ensures at least one channel contributes.
    float lightLevel = max(skyLight, blockLight);
    albedo *= max(lightLevel, 0.05);

    FragColor = vec4(albedo, texColor.a * vAlpha);
}
