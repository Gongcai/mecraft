#include "GlStateGuard.h"

#include <glad/glad.h>

namespace renderer::gl {

ScopedCapabilityDisable::ScopedCapabilityDisable(const uint32_t capability)
    : m_capability(capability),
      m_wasEnabled(glIsEnabled(static_cast<GLenum>(capability)) == GL_TRUE) {
    glDisable(static_cast<GLenum>(m_capability));
}

ScopedCapabilityDisable::~ScopedCapabilityDisable() {
    if (m_wasEnabled) {
        glEnable(static_cast<GLenum>(m_capability));
    } else {
        glDisable(static_cast<GLenum>(m_capability));
    }
}

ScopedCullFaceDisable::ScopedCullFaceDisable()
    : ScopedCapabilityDisable(GL_CULL_FACE) {
}

ScopedStateSnapshot::ScopedStateSnapshot() {
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

ScopedStateSnapshot::~ScopedStateSnapshot() {
    glViewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3]);
    restoreCapability(GL_DEPTH_TEST, m_depthTestEnabled);
    glDepthMask(static_cast<GLboolean>(m_depthMask));
    glDepthFunc(static_cast<GLenum>(m_depthFunc));
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

void ScopedStateSnapshot::restoreCapability(const uint32_t capability, const uint8_t enabled) {
    if (enabled != 0) {
        glEnable(static_cast<GLenum>(capability));
    } else {
        glDisable(static_cast<GLenum>(capability));
    }
}

} // namespace renderer::gl
