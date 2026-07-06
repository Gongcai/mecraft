#ifndef MECRAFT_RHI_DEBUG_H
#define MECRAFT_RHI_DEBUG_H

#include <glm/glm.hpp>

struct RhiDebugLabel {
    const char* name = nullptr;
    glm::vec4 color = glm::vec4(1.0f);
};

#endif // MECRAFT_RHI_DEBUG_H
