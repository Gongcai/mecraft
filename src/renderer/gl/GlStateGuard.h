#pragma once

#include <array>

#include <glad/glad.h>

namespace renderer::gl {

class ScopedCapabilityDisable {
public:
    explicit ScopedCapabilityDisable(GLenum capability)
        : m_capability(capability),
          m_wasEnabled(glIsEnabled(capability) == GL_TRUE) {
        glDisable(m_capability);
    }

    ~ScopedCapabilityDisable() {
        if (m_wasEnabled) {
            glEnable(m_capability);
        } else {
            glDisable(m_capability);
        }
    }

    ScopedCapabilityDisable(const ScopedCapabilityDisable&) = delete;
    ScopedCapabilityDisable& operator=(const ScopedCapabilityDisable&) = delete;

private:
    GLenum m_capability = 0;
    bool m_wasEnabled = false;
};

class ScopedCullFaceDisable final : public ScopedCapabilityDisable {
public:
    ScopedCullFaceDisable()
        : ScopedCapabilityDisable(GL_CULL_FACE) {}
};

class ScopedStateSnapshot {
public:
    ScopedStateSnapshot() {
        glGetIntegerv(GL_VIEWPORT, m_viewport);
        m_depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &m_depthMask);
        m_cullFaceEnabled = glIsEnabled(GL_CULL_FACE);
        m_blendEnabled = glIsEnabled(GL_BLEND);
        m_scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
        glGetIntegerv(GL_SCISSOR_BOX, m_scissorBox);
        glGetIntegerv(GL_CULL_FACE_MODE, &m_cullFaceMode);
        glGetIntegerv(GL_DEPTH_FUNC, &m_depthFunc);
        glGetIntegerv(GL_BLEND_SRC_RGB, &m_blendSrcRgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &m_blendDstRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &m_blendSrcAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &m_blendDstAlpha);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &m_activeTexture);
        glGetIntegerv(GL_CURRENT_PROGRAM, &m_currentProgram);
        glGetDoublev(GL_DEPTH_RANGE, m_depthRange);
        glGetDoublev(GL_DEPTH_CLEAR_VALUE, &m_depthClearValue);
        for (int i = 0; i < kTrackedTextureUnits; ++i) {
            glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + i));
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_texture2DBindings[i]);
            glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &m_texture2DArrayBindings[i]);
        }
        glActiveTexture(static_cast<GLenum>(m_activeTexture));
    }

    ~ScopedStateSnapshot() {
        glViewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3]);
        restoreCapability(GL_DEPTH_TEST, m_depthTestEnabled);
        glDepthMask(m_depthMask);
        glDepthFunc(m_depthFunc);
        glDepthRange(m_depthRange[0], m_depthRange[1]);
        restoreCapability(GL_CULL_FACE, m_cullFaceEnabled);
        glCullFace(static_cast<GLenum>(m_cullFaceMode));
        restoreCapability(GL_BLEND, m_blendEnabled);
        glBlendFuncSeparate(
            static_cast<GLenum>(m_blendSrcRgb),
            static_cast<GLenum>(m_blendDstRgb),
            static_cast<GLenum>(m_blendSrcAlpha),
            static_cast<GLenum>(m_blendDstAlpha)
        );
        restoreCapability(GL_SCISSOR_TEST, m_scissorEnabled);
        glScissor(m_scissorBox[0], m_scissorBox[1], m_scissorBox[2], m_scissorBox[3]);
        glClearDepth(m_depthClearValue);
        glUseProgram(static_cast<GLuint>(m_currentProgram));
        for (int i = 0; i < kTrackedTextureUnits; ++i) {
            glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + i));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(m_texture2DBindings[i]));
            glBindTexture(GL_TEXTURE_2D_ARRAY, static_cast<GLuint>(m_texture2DArrayBindings[i]));
        }
        glActiveTexture(static_cast<GLenum>(m_activeTexture));
    }

    ScopedStateSnapshot(const ScopedStateSnapshot&) = delete;
    ScopedStateSnapshot& operator=(const ScopedStateSnapshot&) = delete;

private:
    static constexpr int kTrackedTextureUnits = 16;

    static void restoreCapability(GLenum capability, GLboolean enabled) {
        if (enabled == GL_TRUE) {
            glEnable(capability);
        } else {
            glDisable(capability);
        }
    }

    GLint m_viewport[4] = {0, 0, 0, 0};
    GLint m_scissorBox[4] = {0, 0, 0, 0};
    GLint m_cullFaceMode = GL_BACK;
    GLint m_depthFunc = GL_LESS;
    GLint m_blendSrcRgb = GL_SRC_ALPHA;
    GLint m_blendDstRgb = GL_ONE_MINUS_SRC_ALPHA;
    GLint m_blendSrcAlpha = GL_SRC_ALPHA;
    GLint m_blendDstAlpha = GL_ONE_MINUS_SRC_ALPHA;
    GLint m_activeTexture = GL_TEXTURE0;
    GLint m_currentProgram = 0;
    GLboolean m_depthTestEnabled = GL_FALSE;
    GLboolean m_depthMask = GL_TRUE;
    GLboolean m_cullFaceEnabled = GL_FALSE;
    GLboolean m_blendEnabled = GL_FALSE;
    GLboolean m_scissorEnabled = GL_FALSE;
    GLdouble m_depthRange[2] = {0.0, 1.0};
    GLdouble m_depthClearValue = 1.0;
    std::array<GLint, kTrackedTextureUnits> m_texture2DBindings{};
    std::array<GLint, kTrackedTextureUnits> m_texture2DArrayBindings{};
};

} // namespace renderer::gl
