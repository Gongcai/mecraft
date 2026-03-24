//
// Created by Caiwe on 2026/3/21.
//

#include "Renderer.h"

#include "ChunkMesher.h"

#include <glm/gtc/matrix_transform.hpp>



void Renderer::init(ResourceMgr &resourceMgr) {
    m_resourceMgr = &resourceMgr;
    m_chunkShader = resourceMgr.getShader("chunk");
    m_uiShader = resourceMgr.getShader("ui");
    m_testCube = new TestCube({8.0f, 3.5f, 8.0f}, resourceMgr);
    m_testCube->setScale({0.5f, 0.5f, 0.5f});
    m_testChunk = std::make_unique<Chunk>(0, 0);

    // Build a tiny test terrain: grass top, dirt below, stone base, and a glass pillar.
    for (int z = 0; z < Chunk::SIZE_Z; ++z) {
        for (int x = 0; x < Chunk::SIZE_X; ++x) {
            m_testChunk->setBlock(x, 0, z, BlockType::STONE);
            m_testChunk->setBlock(x, 1, z, BlockType::DIRT);
            m_testChunk->setBlock(x, 2, z, BlockType::GRASS);
            m_testChunk->setSunlight(x, 2, z, 15);
        }
    }

    m_testChunk->setBlock(8, 3, 8, BlockType::GLASS);
    m_testChunk->setBlock(8, 4, 8, BlockType::GLASS);
    m_testChunk->setSunlight(8, 3, 8, 15);
    m_testChunk->setSunlight(8, 4, 8, 15);

    ChunkMesher::generateMesh(*m_testChunk, resourceMgr.getAtlas());


}

void Renderer::shutdown() {
    delete m_testCube;
    m_testCube = nullptr;
    m_testChunk.reset();
}

void Renderer::render(const Camera &camera, const Window &window) {
    if (m_testCube != nullptr) {
        m_testCube->update();
    }
    beginFrame(camera, window);
    renderWorld();
    endFrame(window);
}

void Renderer::beginFrame(const Camera &camera, const Window &window) {
    glClearColor(0.67f, 0.84f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_projection = camera.getProjectionMatrix(window.getAspectRatio());
    m_view = camera.getViewMatrix();
}

void Renderer::renderWorld() {
    if (m_chunkShader == nullptr || m_resourceMgr == nullptr || !m_testChunk) {
        return;
    }
    if (m_testCube != nullptr) {
        m_testCube->setViewProjection(m_projection * m_view);
        m_testCube->draw();
    }
    const ChunkMesh& mesh = m_testChunk->getMesh();
    if (mesh.vertexCount == 0 && mesh.transparentVertexCount == 0) {
        return;
    }

    m_chunkShader->use();
    m_chunkShader->setMat4("viewProj", m_projection * m_view);

    glm::mat4 model(1.0f);
    const glm::ivec3 offset = m_testChunk->getWorldOffset();
    model = glm::translate(model, glm::vec3(offset.x, offset.y, offset.z));
    m_chunkShader->setMat4("model", model);
    m_chunkShader->setInt("texAtlas", 0);

    const TextureAtlas& atlas = m_resourceMgr->getAtlas();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas.textureID);

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

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);


}

void Renderer::endFrame(const Window &window) {
    window.swapBuffers();
}

void Renderer::updateFrustum(const glm::mat4 &viewProj) {
    }

bool Renderer::isChunkInFrustum(const glm::vec3 &chunkMin, const glm::vec3 &chunkMax) const {
    return false;
}
