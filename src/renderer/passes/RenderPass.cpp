#include "RenderPass.h"
#include "../core/Shader.h"

#include <glad/glad.h>

void RenderPass::renderFullscreen(const uint32_t vao, Shader& shader) {
    shader.use();
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}
