#include "UIRenderUtils.h"

#include <glad/glad.h>

namespace UIRenderUtils {

namespace {

void captureUiState(uint8_t& depthTest,
                    uint8_t& depthWrite,
                    int32_t& blendSrc,
                    int32_t& blendDst) {
    if (isInsideUIScope()) {
        depthTest = GL_FALSE;
        depthWrite = GL_FALSE;
        blendSrc = GL_SRC_ALPHA;
        blendDst = GL_ONE_MINUS_SRC_ALPHA;
        return;
    }

    GLboolean glDepthTest = GL_FALSE;
    GLboolean glDepthWrite = GL_TRUE;
    GLint glBlendSrc = GL_SRC_ALPHA;
    GLint glBlendDst = GL_ONE_MINUS_SRC_ALPHA;
    glGetBooleanv(GL_DEPTH_TEST, &glDepthTest);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &glDepthWrite);
    glGetIntegerv(GL_BLEND_SRC_RGB, &glBlendSrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &glBlendDst);

    depthTest = glDepthTest;
    depthWrite = glDepthWrite;
    blendSrc = glBlendSrc;
    blendDst = glBlendDst;
}

} // namespace

GLStateGuard::GLStateGuard() {
    captureUiState(m_depthTest, m_depthWrite, m_blendSrc, m_blendDst);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

GLStateGuard::GLStateGuard(const uint32_t srcFactor, const uint32_t dstFactor) {
    captureUiState(m_depthTest, m_depthWrite, m_blendSrc, m_blendDst);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(static_cast<GLenum>(srcFactor), static_cast<GLenum>(dstFactor));
}

GLStateGuard::~GLStateGuard() {
    if (m_depthTest != 0) {
        glEnable(GL_DEPTH_TEST);
    }
    glDepthMask(static_cast<GLboolean>(m_depthWrite));
    glBlendFunc(static_cast<GLenum>(m_blendSrc), static_cast<GLenum>(m_blendDst));
}

} // namespace UIRenderUtils
