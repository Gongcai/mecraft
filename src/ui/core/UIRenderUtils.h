#pragma once

#include <array>
#include <vector>
#include <glad/glad.h>

class Shader;

namespace UIRenderUtils
{
    // Push 6 vertices (2 triangles) for a color-only quad into a buffer.
    // Vertex format: 2 floats per vertex (x, y).
    inline void pushColorQuad(std::vector<float>& buf,
                              float x0, float y0, float x1, float y1)
    {
        buf.push_back(x0); buf.push_back(y0);
        buf.push_back(x1); buf.push_back(y0);
        buf.push_back(x1); buf.push_back(y1);
        buf.push_back(x0); buf.push_back(y0);
        buf.push_back(x1); buf.push_back(y1);
        buf.push_back(x0); buf.push_back(y1);
    }

    // Push 6 vertices (2 triangles) for a textured quad into a buffer.
    // Vertex format: 4 floats per vertex (x, y, u, v).
    inline void pushTexturedQuad(std::vector<float>& buf,
                                 float x0, float y0, float x1, float y1,
                                 float u0, float v0, float u1, float v1)
    {
        buf.push_back(x0); buf.push_back(y0); buf.push_back(u0); buf.push_back(v0);
        buf.push_back(x1); buf.push_back(y0); buf.push_back(u1); buf.push_back(v0);
        buf.push_back(x1); buf.push_back(y1); buf.push_back(u1); buf.push_back(v1);
        buf.push_back(x0); buf.push_back(y0); buf.push_back(u0); buf.push_back(v0);
        buf.push_back(x1); buf.push_back(y1); buf.push_back(u1); buf.push_back(v1);
        buf.push_back(x0); buf.push_back(y1); buf.push_back(u0); buf.push_back(v1);
    }

    // Thread-local UI render scope depth counter.
    // When > 0, GLStateGuard skips glGet* queries and uses known UI defaults.
    inline thread_local int s_uiRenderDepth = 0;

    inline bool isInsideUIScope() { return s_uiRenderDepth > 0; }

    // RAII guard that increments the UI render scope depth.
    // Wrap the top-level UI render loop in this to enable nested GLStateGuard optimization.
    class UIScopeGuard
    {
    public:
        UIScopeGuard() { ++s_uiRenderDepth; }
        ~UIScopeGuard() { --s_uiRenderDepth; }
        UIScopeGuard(const UIScopeGuard&) = delete;
        UIScopeGuard& operator=(const UIScopeGuard&) = delete;
    };

    // RAII guard that saves GL depth/blend state on construction and restores on destruction.
    // When inside a UI scope (UIScopeGuard active), skips glGet* queries and uses known defaults.
    class GLStateGuard
    {
    public:
        GLStateGuard()
        {
            if (isInsideUIScope()) {
                m_depthTest = GL_FALSE;
                m_depthWrite = GL_FALSE;
                m_blendSrc = GL_SRC_ALPHA;
                m_blendDst = GL_ONE_MINUS_SRC_ALPHA;
            } else {
                glGetBooleanv(GL_DEPTH_TEST, &m_depthTest);
                glGetBooleanv(GL_DEPTH_WRITEMASK, &m_depthWrite);
                glGetIntegerv(GL_BLEND_SRC_RGB, &m_blendSrc);
                glGetIntegerv(GL_BLEND_DST_RGB, &m_blendDst);
            }

            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        // Construct with custom blend function.
        GLStateGuard(GLenum srcFactor, GLenum dstFactor)
        {
            if (isInsideUIScope()) {
                m_depthTest = GL_FALSE;
                m_depthWrite = GL_FALSE;
                m_blendSrc = GL_SRC_ALPHA;
                m_blendDst = GL_ONE_MINUS_SRC_ALPHA;
            } else {
                glGetBooleanv(GL_DEPTH_TEST, &m_depthTest);
                glGetBooleanv(GL_DEPTH_WRITEMASK, &m_depthWrite);
                glGetIntegerv(GL_BLEND_SRC_RGB, &m_blendSrc);
                glGetIntegerv(GL_BLEND_DST_RGB, &m_blendDst);
            }

            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFunc(srcFactor, dstFactor);
        }

        ~GLStateGuard()
        {
            if (m_depthTest) glEnable(GL_DEPTH_TEST);
            glDepthMask(m_depthWrite);
            glBlendFunc(m_blendSrc, m_blendDst);
        }

        GLStateGuard(const GLStateGuard&) = delete;
        GLStateGuard& operator=(const GLStateGuard&) = delete;

    private:
        GLboolean m_depthTest = GL_FALSE;
        GLboolean m_depthWrite = GL_TRUE;
        GLint m_blendSrc = GL_SRC_ALPHA;
        GLint m_blendDst = GL_ONE_MINUS_SRC_ALPHA;
    };
}
