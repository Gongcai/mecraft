#version 450 core
#include "gbuffer_contract.glsl"

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uAlbedoTex;
uniform sampler2D uNormalAoTex;
uniform sampler2D uVoxelLightTex;
uniform sampler2D uMaterialTex;
uniform sampler2D uDepthTex;
uniform sampler2D uShadowMap;
uniform sampler2D uSsaoTex;
uniform sampler2D uSceneLightingTex;
uniform sampler2D uTransparentCompositeTex;
uniform sampler2D uTransparentCompositeDepthTex;
uniform sampler2D uVolumetricTex;
uniform sampler2D uSkyCaptureTex;
uniform sampler2D uVelocityTex;
uniform sampler2D uHistorySceneTex;
uniform mat4 uShadowViewProj;
uniform mat4 uInvViewProj;
uniform float uShadowExtent;
uniform float uShadowMapSize;
uniform int uDebugViewMode;

vec3 tonemapPreview(vec3 color) {
    color = max(color, vec3(0.0));
    return color / (color + vec3(1.0));
}

vec3 heatmap(float v) {
    v = clamp(v, 0.0, 1.0);
    vec3 a = mix(vec3(0.02, 0.04, 0.18), vec3(0.05, 0.35, 0.95), smoothstep(0.0, 0.35, v));
    vec3 b = mix(vec3(0.05, 0.35, 0.95), vec3(0.95, 0.86, 0.18), smoothstep(0.35, 0.72, v));
    vec3 c = mix(vec3(0.95, 0.86, 0.18), vec3(1.0, 0.08, 0.02), smoothstep(0.72, 1.0, v));
    return v < 0.35 ? a : (v < 0.72 ? b : c);
}

float linearizeDepthPreview(float depth) {
    if (depth >= 0.9999) {
        return 1.0;
    }
    float ndc = depth * 2.0 - 1.0;
    float nearPlane = 0.05;
    float farPlane = 512.0;
    float linearDepth = (2.0 * nearPlane * farPlane) / max(farPlane + nearPlane - ndc * (farPlane - nearPlane), 0.0001);
    return clamp(linearDepth / 192.0, 0.0, 1.0);
}

void main() {
    if (uDebugViewMode == 1) {
        FragColor = vec4(texture(uAlbedoTex, vTexCoord).rgb, 1.0);
        return;
    }
    if (uDebugViewMode == 2) {
        FragColor = vec4(texture(uNormalAoTex, vTexCoord).rgb, 1.0);
        return;
    }
    if (uDebugViewMode == 3) {
        float ao = texture(uNormalAoTex, vTexCoord).a;
        FragColor = vec4(vec3(ao), 1.0);
        return;
    }
    if (uDebugViewMode == 4) {
        vec2 light = texture(uVoxelLightTex, vTexCoord).rg;
        FragColor = vec4(light.r, light.g, 0.0, 1.0);
        return;
    }
    if (uDebugViewMode == 5) {
        SurfaceMaterial material = unpackGBufferMaterial(texture(uMaterialTex, vTexCoord));
        FragColor = vec4(material.roughness, material.f0 * 3.0, material.emission, 1.0);
        return;
    }
    if (uDebugViewMode == 6) {
        SurfaceMaterial material = unpackGBufferMaterial(texture(uMaterialTex, vTexCoord));
        FragColor = vec4(heatmap(material.sss), 1.0);
        return;
    }
    if (uDebugViewMode == 7) {
        float depth = texture(uDepthTex, vTexCoord).r;
        FragColor = vec4(heatmap(1.0 - linearizeDepthPreview(depth)), 1.0);
        return;
    }
    if (uDebugViewMode == 8) {
        FragColor = vec4(vec3(texture(uShadowMap, vTexCoord).r), 1.0);
        return;
    }
    if (uDebugViewMode == 9) {
        FragColor = vec4(vec3(texture(uSsaoTex, vTexCoord).r), 1.0);
        return;
    }
    if (uDebugViewMode == 10) {
        FragColor = vec4(tonemapPreview(texture(uSceneLightingTex, vTexCoord).rgb), 1.0);
        return;
    }
    if (uDebugViewMode == 11) {
        FragColor = vec4(tonemapPreview(texture(uTransparentCompositeTex, vTexCoord).rgb), 1.0);
        return;
    }
    if (uDebugViewMode == 12) {
        float depth = texture(uTransparentCompositeDepthTex, vTexCoord).r;
        FragColor = vec4(heatmap(1.0 - linearizeDepthPreview(depth)), 1.0);
        return;
    }
    if (uDebugViewMode == 13) {
        vec4 volumetric = texture(uVolumetricTex, vTexCoord);
        FragColor = vec4(tonemapPreview(volumetric.rgb * 4.0), 1.0);
        return;
    }
    if (uDebugViewMode == 14) {
        float transmittance = texture(uVolumetricTex, vTexCoord).a;
        FragColor = vec4(vec3(transmittance), 1.0);
        return;
    }
    if (uDebugViewMode == 15) {
        FragColor = vec4(tonemapPreview(texture(uSkyCaptureTex, vTexCoord).rgb), 1.0);
        return;
    }
    if (uDebugViewMode == 16) {
        vec2 velocity = texture(uVelocityTex, vTexCoord).rg;
        float speed = length(velocity);
        FragColor = vec4(heatmap(speed * 50.0), 1.0);
        return;
    }
    if (uDebugViewMode == 17) {
        FragColor = vec4(tonemapPreview(texture(uHistorySceneTex, vTexCoord).rgb), 1.0);
        return;
    }
    if (uDebugViewMode == 18) {
        // Shadow Projection debug: visualize frustum coverage and texel density
        vec4 worldPos = uInvViewProj * vec4(vTexCoord * 2.0 - 1.0, 0.5, 1.0);
        worldPos /= max(worldPos.w, 0.0001);
        vec4 shadowClip = uShadowViewProj * worldPos;
        vec2 shadowUv = shadowClip.xy / max(shadowClip.w, 0.0001) * 0.5 + 0.5;

        vec3 outOfBounds = vec3(0.0);
        if (shadowUv.x < 0.0 || shadowUv.x > 1.0 || shadowUv.y < 0.0 || shadowUv.y > 1.0)
            outOfBounds = vec3(1.0, 0.0, 0.0);

        float texelDensity = uShadowExtent / max(uShadowMapSize, 1.0);
        float densityHeat = clamp(texelDensity * 2.0, 0.0, 1.0);

        vec3 coverageColor = heatmap(densityHeat);
        FragColor = vec4(mix(coverageColor, outOfBounds, 0.6), 1.0);
        return;
    }

    FragColor = vec4(tonemapPreview(texture(uSceneLightingTex, vTexCoord).rgb), 1.0);
}
