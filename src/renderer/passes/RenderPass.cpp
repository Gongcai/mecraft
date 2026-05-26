#include "RenderPass.h"
#include "../core/Shader.h"

void RenderPass::renderFullscreen(GLuint vao, Shader& shader) {
    shader.use();
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}
