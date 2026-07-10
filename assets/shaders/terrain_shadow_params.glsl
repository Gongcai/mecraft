layout(std140, binding = 4) uniform TerrainShadowParams {
    mat4 rhiShadowModelView;
    mat4 rhiShadowProjection;
    vec4 rhiShadowLightAnimation;
    vec4 rhiShadowTimePass;
};

#define uShadowModelView rhiShadowModelView
#define uShadowProjection rhiShadowProjection
#define uShadowLightDirection rhiShadowLightAnimation.xyz
#define uAnimationTime rhiShadowLightAnimation.w
#define uTime rhiShadowTimePass.x
#define uShadowPassMode int(rhiShadowTimePass.y)
#define uForceBaseLod 1
