#ifndef MECRAFT_TEXTURE_CONTRACT_H
#define MECRAFT_TEXTURE_CONTRACT_H
// MecraftTextureContract — thin helper for binding CSM shadow samplers.
// Centralizes the sampler-to-texture-unit mapping and comparison/raw depth
// contract so that each draw path does not duplicate (or forget) bindings.
//
// Usage:
//   MecraftTextureContract::ShadowTextureBundle bundle{...};
//   MecraftTextureContract::bindShadowSamplers(program, baseUnit, bundle);
// or when shadow data is not available:
//   MecraftTextureContract::bindShadowFallbacks(program, baseUnit);

#include <glad/glad.h>

namespace MecraftTextureContract {

// Six CSM shadow textures consumed by mecraft_shadow.glsl.
// Comparison textures must have GL_TEXTURE_COMPARE_MODE = GL_COMPARE_REF_TO_TEXTURE.
// Raw textures must have GL_TEXTURE_COMPARE_MODE = GL_NONE.
struct ShadowTextureBundle {
    GLuint csmDepthCompare = 0;    // sampler2DArrayShadow uCsmShadowMap       (shadowtex1)
    GLuint csmDepthRaw = 0;        // sampler2DArray       uCsmShadowDepthRaw  (shadowtex1 raw)
    GLuint csmDepthAllCompare = 0; // sampler2DArrayShadow uCsmShadowDepthAll  (shadowtex0)
    GLuint csmDepthAllRaw = 0;     // sampler2DArray       uCsmShadowDepthAllRaw (shadowtex0 raw)
    GLuint csmColor0 = 0;          // sampler2DArray       uCsmShadowColor0
    GLuint csmColor1 = 0;          // sampler2DArray       uCsmShadowColor1
};

// Sampler uniform names in mecraft_shadow.glsl, in binding order.
// Index 0 = baseUnit, index 1 = baseUnit+1, etc.
inline constexpr const char* kShadowSamplerNames[6] = {
    "uCsmShadowMap",
    "uCsmShadowDepthRaw",
    "uCsmShadowDepthAll",
    "uCsmShadowDepthAllRaw",
    "uCsmShadowColor0",
    "uCsmShadowColor1",
};

// Bind 6 CSM shadow samplers to consecutive texture units [baseUnit .. baseUnit+5].
// Sets uniform values via glProgramUniform1i (program-local, shader does not need
// to be current). Then binds each texture to the corresponding unit.
// Handles zero handles gracefully (skips binding, shader reads default/black).
inline void bindShadowSamplers(GLuint program, int baseUnit,
                               const ShadowTextureBundle& textures) {
    const GLuint handles[6] = {
        textures.csmDepthCompare,
        textures.csmDepthRaw,
        textures.csmDepthAllCompare,
        textures.csmDepthAllRaw,
        textures.csmColor0,
        textures.csmColor1,
    };

    for (int i = 0; i < 6; ++i) {
        const GLint loc = glGetUniformLocation(program, kShadowSamplerNames[i]);
        if (loc >= 0) {
            glProgramUniform1i(program, loc, baseUnit + i);
        }
        if (handles[i] != 0) {
            glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + baseUnit + i));
            glBindTexture(GL_TEXTURE_2D_ARRAY, handles[i]);
        }
    }
}

// Lazily create 1x1 fallback textures that represent "no shadow" (depth = 1.0).
// These are shared across all callers.
namespace fallback_detail {
    inline GLuint depthRaw = 0;
    inline GLuint depthCompare = 0;
    inline GLuint color0 = 0;
    inline GLuint color1 = 0;
    inline bool initialized = false;

    inline void ensure() {
        if (initialized) return;
        initialized = true;

        // Raw depth: GL_DEPTH_COMPONENT32F, COMPARE_MODE = GL_NONE, cleared to 1.0
        glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &depthRaw);
        glTextureStorage3D(depthRaw, 1, GL_DEPTH_COMPONENT32F, 1, 1, 1);
        glTextureParameteri(depthRaw, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(depthRaw, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(depthRaw, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(depthRaw, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTextureParameteri(depthRaw, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        constexpr float depth = 1.0f;
        glClearTexImage(depthRaw, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);

        // Comparison depth: glTextureView of same storage, COMPARE_REF_TO_TEXTURE
        glGenTextures(1, &depthCompare);
        glTextureView(depthCompare, GL_TEXTURE_2D_ARRAY,
                      depthRaw, GL_DEPTH_COMPONENT32F, 0, 1, 0, 1);
        glTextureParameteri(depthCompare, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(depthCompare, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(depthCompare, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(depthCompare, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTextureParameteri(depthCompare, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTextureParameteri(depthCompare, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

        // Color 0: RGBA8, black with full alpha
        glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &color0);
        glTextureStorage3D(color0, 1, GL_RGBA8, 1, 1, 1);
        glTextureParameteri(color0, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(color0, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(color0, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(color0, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        const unsigned char black[] = {0, 0, 0, 255};
        glTextureSubImage3D(color0, 0, 0, 0, 0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, black);

        // Color 1: RGBA16F, all zeros
        glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &color1);
        glTextureStorage3D(color1, 1, GL_RGBA16F, 1, 1, 1);
        glTextureParameteri(color1, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(color1, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(color1, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(color1, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        const float zeros[] = {0.0f, 0.0f, 0.0f, 0.0f};
        glTextureSubImage3D(color1, 0, 0, 0, 0, 1, 1, 1, GL_RGBA, GL_FLOAT, zeros);
    }
} // namespace fallback_detail

// Return individual fallback texture handles. Creates them on first call.
inline GLuint fallbackDepthCompare()    { fallback_detail::ensure(); return fallback_detail::depthCompare; }
inline GLuint fallbackDepthRaw()        { fallback_detail::ensure(); return fallback_detail::depthRaw; }
inline GLuint fallbackColor0()          { fallback_detail::ensure(); return fallback_detail::color0; }
inline GLuint fallbackColor1()          { fallback_detail::ensure(); return fallback_detail::color1; }

// Build a ShadowTextureBundle filled with fallback textures (no real shadow data).
inline ShadowTextureBundle fallbackBundle() {
    fallback_detail::ensure();
    return {
        fallback_detail::depthCompare,
        fallback_detail::depthRaw,
        fallback_detail::depthCompare,
        fallback_detail::depthRaw,
        fallback_detail::color0,
        fallback_detail::color1,
    };
}

// Bind 1x1 fallback textures to all 6 shadow sampler slots.
// Use when shadow data is not available (e.g., first frame, shadow disabled).
inline void bindShadowFallbacks(GLuint program, int baseUnit) {
    bindShadowSamplers(program, baseUnit, fallbackBundle());
}

// Destroy fallback textures. Call during shutdown to release GPU resources.
inline void destroyFallbacks() {
    if (fallback_detail::depthRaw != 0) {
        glDeleteTextures(1, &fallback_detail::depthRaw);
        fallback_detail::depthRaw = 0;
    }
    if (fallback_detail::depthCompare != 0) {
        glDeleteTextures(1, &fallback_detail::depthCompare);
        fallback_detail::depthCompare = 0;
    }
    if (fallback_detail::color0 != 0) {
        glDeleteTextures(1, &fallback_detail::color0);
        fallback_detail::color0 = 0;
    }
    if (fallback_detail::color1 != 0) {
        glDeleteTextures(1, &fallback_detail::color1);
        fallback_detail::color1 = 0;
    }
    fallback_detail::initialized = false;
}

} // namespace MecraftTextureContract

#endif // MECRAFT_TEXTURE_CONTRACT_H
