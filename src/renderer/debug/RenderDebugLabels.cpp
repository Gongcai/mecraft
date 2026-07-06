#include "RenderDebugLabels.h"

#include <glad/glad.h>

#ifdef MECRAFT_DEBUG
#include <cstdlib>
#include <cstring>
#endif

namespace renderer::debug {

bool labelsEnabled() {
#ifdef MECRAFT_DEBUG
    static bool cached = false;
    static bool enabled = true;
    if (!cached) {
        cached = true;
        const char* env = std::getenv("MEC_RENDER_LABELS");
        if (env && std::strcmp(env, "0") == 0) {
            enabled = false;
        }
    }
    return enabled;
#else
    return false;
#endif
}

void pushGroup(const char* name) {
#ifdef MECRAFT_DEBUG
    if (labelsEnabled() && glPushDebugGroup) {
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name);
    }
#else
    (void)name;
#endif
}

void popGroup() {
#ifdef MECRAFT_DEBUG
    if (labelsEnabled() && glPopDebugGroup) {
        glPopDebugGroup();
    }
#endif
}

void insertEvent(const char* name) {
#ifdef MECRAFT_DEBUG
    if (labelsEnabled() && glDebugMessageInsert) {
        glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER,
                             0, GL_DEBUG_SEVERITY_NOTIFICATION, -1, name);
    }
#else
    (void)name;
#endif
}

void labelTexture(uint32_t id, const char* name) {
#ifdef MECRAFT_DEBUG
    if (labelsEnabled() && glObjectLabel && id != 0) {
        glObjectLabel(GL_TEXTURE, id, -1, name);
    }
#else
    (void)id;
    (void)name;
#endif
}

void labelBuffer(uint32_t id, const char* name) {
#ifdef MECRAFT_DEBUG
    if (labelsEnabled() && glObjectLabel && id != 0) {
        glObjectLabel(GL_BUFFER, id, -1, name);
    }
#else
    (void)id;
    (void)name;
#endif
}

void labelFramebuffer(uint32_t id, const char* name) {
#ifdef MECRAFT_DEBUG
    if (labelsEnabled() && glObjectLabel && id != 0) {
        glObjectLabel(GL_FRAMEBUFFER, id, -1, name);
    }
#else
    (void)id;
    (void)name;
#endif
}

void labelVertexArray(uint32_t id, const char* name) {
#ifdef MECRAFT_DEBUG
    if (labelsEnabled() && glObjectLabel && id != 0) {
        glObjectLabel(GL_VERTEX_ARRAY, id, -1, name);
    }
#else
    (void)id;
    (void)name;
#endif
}

void labelProgram(uint32_t id, const char* name) {
#ifdef MECRAFT_DEBUG
    if (labelsEnabled() && glObjectLabel && id != 0) {
        glObjectLabel(GL_PROGRAM, id, -1, name);
    }
#else
    (void)id;
    (void)name;
#endif
}

ScopedDebugGroup::ScopedDebugGroup(const char* name) {
    pushGroup(name);
}

ScopedDebugGroup::~ScopedDebugGroup() {
    popGroup();
}

} // namespace renderer::debug
