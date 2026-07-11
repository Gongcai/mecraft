#version 450 core
layout(location = 0) in vec3 vWorldDir;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 15) uniform RhiPushConstants {
    mat4 uProjection;
    mat4 uView;
    vec4 uSkyTopHaze;
    vec4 uSkyHorizonGlare;
    vec4 uSunDirectionVisibility;
    vec4 uMoonDirectionVisibility;
    vec4 uSunScatterNight;
    vec4 uMoonLightPhase;
};
#define uSunDirection uSunDirectionVisibility.xyz
#define uSunVisibility uSunDirectionVisibility.w
#define uMoonDirection uMoonDirectionVisibility.xyz
#define uMoonVisibility uMoonDirectionVisibility.w
#define uMoonPhaseAngle uMoonLightPhase.w
#include "procedural_celestials.glsl"
void main() {
    vec3 dir = normalize(vWorldDir);
    float height = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 color = mix(uSkyHorizonGlare.xyz, uSkyTopHaze.xyz,
                     smoothstep(0.0, 1.0, height));
    float horizon = pow(1.0 - clamp(abs(dir.y), 0.0, 1.0), 2.25);
    color = mix(color, uSkyHorizonGlare.xyz * 1.12,
                horizon * clamp(uSkyTopHaze.w, 0.0, 1.0));
    float sunDot = max(dot(dir, normalize(uSunDirection)), 0.0);
    float glare = uSkyHorizonGlare.w;
    color += uSunScatterNight.xyz *
             (pow(sunDot, 24.0) * glare + pow(sunDot, 4.0) * glare * 0.22) *
             smoothstep(-0.08, 0.18, uSunDirection.y);
    float moonDot = max(dot(dir, normalize(uMoonDirection)), 0.0);
    color += uMoonLightPhase.xyz *
             (pow(moonDot, 36.0) * 0.32 + pow(moonDot, 8.0) * 0.07) *
             clamp(uMoonVisibility, 0.0, 1.0);
    color += vec3(0.04, 0.08, 0.12) * horizon * clamp(uSunScatterNight.w, 0.0, 1.0);
    color += renderProceduralMoonDisk(dir);
    color += renderProceduralSunDisk(dir);
    fragColor = vec4(max(color, vec3(0.0)), 1.0);
}
