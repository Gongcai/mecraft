#pragma once

#include <array>
#include <algorithm>
#include <cmath>
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

    inline void pushCircle(std::vector<float>& buf,
                           float cx, float cy, float radius,
                           int segments = 16)
    {
        if (radius <= 0.0f || segments < 3) {
            return;
        }

        constexpr float kTwoPi = 6.28318530718f;
        for (int i = 0; i < segments; ++i) {
            const float a0 = static_cast<float>(i) * kTwoPi / static_cast<float>(segments);
            const float a1 = static_cast<float>(i + 1) * kTwoPi / static_cast<float>(segments);
            buf.push_back(cx); buf.push_back(cy);
            buf.push_back(cx + std::cos(a0) * radius); buf.push_back(cy + std::sin(a0) * radius);
            buf.push_back(cx + std::cos(a1) * radius); buf.push_back(cy + std::sin(a1) * radius);
        }
    }

    inline void pushArcFan(std::vector<float>& buf,
                           float cx, float cy, float radius,
                           float startAngle, float endAngle,
                           int segments)
    {
        if (radius <= 0.0f || segments < 1) {
            return;
        }

        const float step = (endAngle - startAngle) / static_cast<float>(segments);
        for (int i = 0; i < segments; ++i) {
            const float a0 = startAngle + static_cast<float>(i) * step;
            const float a1 = startAngle + static_cast<float>(i + 1) * step;
            buf.push_back(cx); buf.push_back(cy);
            buf.push_back(cx + std::cos(a0) * radius); buf.push_back(cy + std::sin(a0) * radius);
            buf.push_back(cx + std::cos(a1) * radius); buf.push_back(cy + std::sin(a1) * radius);
        }
    }

    inline void pushCapsule(std::vector<float>& buf,
                            float x0, float y0, float x1, float y1,
                            int segments = 16)
    {
        if (x1 <= x0 || y1 <= y0) {
            return;
        }

        constexpr float kPi = 3.14159265359f;
        const int halfSegments = std::max(4, segments / 2);
        const float w = x1 - x0;
        const float h = y1 - y0;

        if (w >= h) {
            const float r = h * 0.5f;
            const float cy = (y0 + y1) * 0.5f;
            const float leftCx = x0 + r;
            const float rightCx = x1 - r;
            if (rightCx > leftCx) {
                pushColorQuad(buf, leftCx, y0, rightCx, y1);
            }
            pushArcFan(buf, leftCx, cy, r, kPi * 0.5f, kPi * 1.5f, halfSegments);
            pushArcFan(buf, rightCx, cy, r, -kPi * 0.5f, kPi * 0.5f, halfSegments);
        } else {
            const float r = w * 0.5f;
            const float cx = (x0 + x1) * 0.5f;
            const float bottomCy = y0 + r;
            const float topCy = y1 - r;
            if (topCy > bottomCy) {
                pushColorQuad(buf, x0, bottomCy, x1, topCy);
            }
            pushArcFan(buf, cx, bottomCy, r, kPi, kPi * 2.0f, halfSegments);
            pushArcFan(buf, cx, topCy, r, 0.0f, kPi, halfSegments);
        }
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
