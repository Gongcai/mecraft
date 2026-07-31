#ifndef MECRAFT_SHADERPACK_DIRECTIVES_H
#define MECRAFT_SHADERPACK_DIRECTIVES_H

// Shaderpack directives — Iris/OptiFine shaderpack configuration contract.
// Even though Mecraft currently only supports the built-in DerivativeMain pack,
// this layer prevents shadow/lighting parameters from scattering as hardcoded
// constants across Renderer, shaders, and debug code.
//
// DerivativeMain source of truth:
//   shaders.properties         — shadow.culling, blend.shadow
//   lib/Lighting/SunLighting.glsl — shadowDistance, shadowDistanceRenderMul

namespace shaderpack {

struct ShadowDirectives {
    float shadowDistance = 192.0f; // DerivativeMain: const float shadowDistance = 192.0
    float shadowDistanceRenderMul = 1.0f; // DerivativeMain: const float shadowDistanceRenderMul = 1.0
    float intervalSize = 2.0f; // DerivativeMain: const float shadowIntervalSize = 2.0
    float sunPathRotation = -35.0f; // Mecraft constant (DerivativeMain uses 0, Mecraft uses -35)
    int resolution = 2048;
    float nearPlane = -100.05f; // Iris ShadowMatrices.NEAR
    float farPlane = 156.0f; // Iris ShadowMatrices.FAR

    // DerivativeMain: shadow.culling = false (conditional on GI_ENABLED)
    // This disables *advanced* shadow frustum culling, but Iris still bounds
    // shadow caster submission through shadow render distance / BoxCuller fallback.
    bool culling = false;

    bool renderTerrain = true;
    bool renderTranslucent = true;
    bool renderEntities = true;
    bool renderBlockEntities = true;
    bool renderPlayer = true;
    bool backfaceCulling = false; // DerivativeMain: SHADOW_BACKFACE_CULLING
    float entityShadowDistanceMul = 1.0f;
};

struct ShaderpackDirectives {
    ShadowDirectives shadow;
    // P2: BufferDirectives, ProgramDirectives
};

// DerivativeMain built-in default values.
inline ShaderpackDirectives createDerivativeMainDirectives() {
    return ShaderpackDirectives{};
}

} // namespace shaderpack

#endif // MECRAFT_SHADERPACK_DIRECTIVES_H
