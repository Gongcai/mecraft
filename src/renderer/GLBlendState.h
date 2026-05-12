#ifndef MECRAFT_GL_BLEND_STATE_H
#define MECRAFT_GL_BLEND_STATE_H

#include <glad/glad.h>

// Per-buffer (per-attachment) blend state control.
// OpenGL 4.x supports independent blend per color attachment via
// glEnablei/glDisablei/glBlendFunci/glBlendFuncSeparatei.
//
// DerivativeMain requires this for gbuffers passes where colortex6 (albedo)
// needs alpha blending while other attachments (colortex7, colortex3) are opaque.

namespace glBlendState {

// Enable/disable blend for a specific color attachment index.
inline void enable(GLuint attachmentIndex) {
    glEnablei(GL_BLEND, attachmentIndex);
}

inline void disable(GLuint attachmentIndex) {
    glDisablei(GL_BLEND, attachmentIndex);
}

// Set blend function for a specific color attachment (same src/dst for RGB and alpha).
inline void func(GLuint attachmentIndex, GLenum src, GLenum dst) {
    glBlendFunci(attachmentIndex, src, dst);
}

// Set separate blend functions for RGB and alpha channels on a specific attachment.
inline void funcSeparate(GLuint attachmentIndex,
                         GLenum srcRGB, GLenum dstRGB,
                         GLenum srcAlpha, GLenum dstAlpha) {
    glBlendFuncSeparatei(attachmentIndex, srcRGB, dstRGB, srcAlpha, dstAlpha);
}

// Set blend equation for a specific color attachment.
inline void equation(GLuint attachmentIndex, GLenum mode) {
    glBlendEquationi(attachmentIndex, mode);
}

// Convenience: configure one attachment for standard alpha blending,
// all others opaque (disabled). Assumes a fixed attachment count.
inline void setAlphaOnAttachment(GLuint targetAttachment, GLsizei totalAttachments) {
    for (GLsizei i = 0; i < totalAttachments; ++i) {
        if (static_cast<GLuint>(i) == targetAttachment) {
            enable(i);
            funcSeparate(i, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                         GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            disable(i);
        }
    }
}

// Convenience: disable blend on all attachments (opaque pass).
inline void disableAll(GLsizei totalAttachments) {
    for (GLsizei i = 0; i < totalAttachments; ++i) {
        disable(i);
    }
}

} // namespace glBlendState

#endif // MECRAFT_GL_BLEND_STATE_H
