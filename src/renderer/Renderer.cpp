//
// Created by Caiwe on 2026/3/21.
//

#include "Renderer.h"

#include "ChunkMesher.h"
#include "../world/World.h"

#include <glm/gtc/matrix_transform.hpp>



void Renderer::init(ResourceMgr &resourceMgr) {
    m_resourceMgr = &resourceMgr;
    m_chunkShader = resourceMgr.getShader("chunk");
    m_uiShader = resourceMgr.getShader("ui");
}

void Renderer::shutdown() {
}

void Renderer::render(const World& world, const Camera &camera, const Window &window) {
    beginFrame(camera, window);
    renderWorld(world);
    endFrame(window);
}

void Renderer::beginFrame(const Camera &camera, const Window &window) {
    glClearColor(0.67f, 0.84f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_projection = camera.getProjectionMatrix(window.getAspectRatio());
    m_view = camera.getViewMatrix();
}

void Renderer::renderWorld(const World& world) {
    if (m_chunkShader == nullptr || m_resourceMgr == nullptr) {
        return;
    }

    m_chunkShader->use();
    m_chunkShader->setMat4("viewProj", m_projection * m_view);
    m_chunkShader->setInt("texAtlas", 0);

    const TextureAtlas& atlas = m_resourceMgr->getAtlas();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas.textureID);

    for (const auto& pair : world.getActiveChunks()) {
        Chunk& chunk = *pair.second;
        if (chunk.isDirty()) {
            ChunkMesher::generateMesh(chunk, atlas);
        }

        const ChunkMesh& mesh = chunk.getMesh();
        if (mesh.vertexCount == 0 && mesh.transparentVertexCount == 0) {
            continue;
        }

        glm::mat4 model(1.0f);
        const glm::ivec3 offset = chunk.getWorldOffset();
        model = glm::translate(model, glm::vec3(offset.x, offset.y, offset.z));
        m_chunkShader->setMat4("model", model);

        if (mesh.vertexCount > 0) {
            glBindVertexArray(mesh.vao);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh.vertexCount));
        }

        if (mesh.transparentVertexCount > 0) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);

            glBindVertexArray(mesh.transparentVao);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh.transparentVertexCount));

            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

}

void Renderer::endFrame(const Window &window) {
}

void Renderer::updateFrustum(const glm::mat4 &viewProj) {
    }

bool Renderer::isChunkInFrustum(const glm::vec3 &chunkMin, const glm::vec3 &chunkMax) const {
    return false;
}
