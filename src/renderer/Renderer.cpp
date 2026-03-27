//
// Created by Caiwe on 2026/3/21.
//

#include "Renderer.h"

#include "ChunkMesher.h"
#include "../world/World.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <utility>

Renderer::~Renderer() {
    shutdown();
}

void Renderer::init(ResourceMgr &resourceMgr) {
    m_resourceMgr = &resourceMgr;
    m_chunkShader = resourceMgr.getShader("chunk");
    m_uiShader = resourceMgr.getShader("ui");
    m_meshingService.start();
}

void Renderer::shutdown() {
    m_meshingService.shutdown();
    m_meshingInFlight.clear();
}

void Renderer::render(const World& world, const Camera &camera, const Window &window) {
    beginFrame(camera, window);
    renderWorld(world);
    endFrame(window);
}

void Renderer::setMeshingSubmitBudget(const int budget) {
    m_meshingSubmitBudget = std::max(1, budget);
}

int Renderer::getMeshingSubmitBudget() const {
    return m_meshingSubmitBudget;
}

Renderer::MeshingFrameStats Renderer::getMeshingFrameStats() const {
    MeshingFrameStats stats;
    stats.submitBudget = m_meshingSubmitBudget;
    stats.submitted = m_meshingSubmittedThisFrame;
    stats.completed = m_meshingCompletedThisFrame;
    stats.inFlight = static_cast<int>(m_meshingInFlight.size());
    return stats;
}

const std::array<float, Renderer::MESHING_HISTORY_SIZE>& Renderer::getMeshingSubmittedHistory() const {
    return m_meshingSubmittedHistory;
}

const std::array<float, Renderer::MESHING_HISTORY_SIZE>& Renderer::getMeshingCompletedHistory() const {
    return m_meshingCompletedHistory;
}

const std::array<float, Renderer::MESHING_HISTORY_SIZE>& Renderer::getMeshingInFlightHistory() const {
    return m_meshingInFlightHistory;
}

size_t Renderer::getMeshingHistoryCount() const {
    return m_meshingHistoryCount;
}

void Renderer::beginFrame(const Camera &camera, const Window &window) {
    glClearColor(0.67f, 0.84f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_projection = camera.getProjectionMatrix(window.getAspectRatio());
    m_view = camera.getViewMatrix();
    updateFrustum(m_projection * m_view);
    drawCallCount = 0;

    m_meshingSubmittedThisFrame = 0;
    m_meshingCompletedThisFrame = 0;
}

void Renderer::renderWorld(const World& world) {
    if (m_chunkShader == nullptr || m_resourceMgr == nullptr) {
        return;
    }

    drainMeshingResults(world);

    m_chunkShader->use();
    m_chunkShader->setMat4("viewProj", m_projection * m_view);
    m_chunkShader->setInt("texAtlas", 0);

    const TextureAtlas& atlas = m_resourceMgr->getAtlas();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas.textureID);

    int submittedThisPass = 0;
    for (const auto& pair : world.getActiveChunks()) {
        const int64_t chunkKey = pair.first;
        Chunk& chunk = *pair.second;

        if (chunk.isDirty() &&
            submittedThisPass < m_meshingSubmitBudget &&
            m_meshingInFlight.find(chunkKey) == m_meshingInFlight.end()) {
            ChunkMeshingJob job;
            job.chunkKey = chunkKey;
            job.revision = chunk.getMeshRevision();
            job.snapshot = ChunkMesher::captureSnapshot(chunk);
            job.atlas = &atlas;
            m_meshingService.submit(std::move(job));
            m_meshingInFlight.insert(chunkKey);
            ++submittedThisPass;
            ++m_meshingSubmittedThisFrame;
        }

        glm::vec3 chunkMin = glm::vec3(chunk.getWorldOffset());
        glm::vec3 chunkMax = chunkMin + glm::vec3(Chunk::SIZE_X, Chunk::SIZE_Y, Chunk::SIZE_Z);
        if (!isChunkInFrustum(chunkMin, chunkMax)) {
            continue;
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
            ++drawCallCount;
        }

        if (mesh.transparentVertexCount > 0) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);

            glBindVertexArray(mesh.transparentVao);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh.transparentVertexCount));
            drawCallCount++;
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

}

void Renderer::endFrame(const Window &window) {
    recordMeshingHistory();
}

void Renderer::recordMeshingHistory() {
    if (m_meshingHistoryCount < MESHING_HISTORY_SIZE) {
        m_meshingSubmittedHistory[m_meshingHistoryCount] = static_cast<float>(m_meshingSubmittedThisFrame);
        m_meshingCompletedHistory[m_meshingHistoryCount] = static_cast<float>(m_meshingCompletedThisFrame);
        m_meshingInFlightHistory[m_meshingHistoryCount] = static_cast<float>(m_meshingInFlight.size());
        ++m_meshingHistoryCount;
        return;
    }

    for (size_t i = 1; i < MESHING_HISTORY_SIZE; ++i) {
        m_meshingSubmittedHistory[i - 1] = m_meshingSubmittedHistory[i];
        m_meshingCompletedHistory[i - 1] = m_meshingCompletedHistory[i];
        m_meshingInFlightHistory[i - 1] = m_meshingInFlightHistory[i];
    }

    m_meshingSubmittedHistory[MESHING_HISTORY_SIZE - 1] = static_cast<float>(m_meshingSubmittedThisFrame);
    m_meshingCompletedHistory[MESHING_HISTORY_SIZE - 1] = static_cast<float>(m_meshingCompletedThisFrame);
    m_meshingInFlightHistory[MESHING_HISTORY_SIZE - 1] = static_cast<float>(m_meshingInFlight.size());
}

void Renderer::drainMeshingResults(const World& world) {
    ChunkMeshingResult result;
    while (m_meshingService.tryPopCompleted(result)) {
        ++m_meshingCompletedThisFrame;
        m_meshingInFlight.erase(result.chunkKey);

        const auto& activeChunks = world.getActiveChunks();
        const auto it = activeChunks.find(result.chunkKey);
        if (it == activeChunks.end() || !it->second) {
            continue;
        }

        Chunk& chunk = *it->second;
        if (chunk.getMeshRevision() != result.revision) {
            continue;
        }

        ChunkMesh mesh;
        mesh.upload(result.meshData.opaqueVertices);
        mesh.uploadTransparent(result.meshData.transparentVertices);
        chunk.setMesh(std::move(mesh));
    }
}

void Renderer::updateFrustum(const glm::mat4 &viewProj) {
    for (int i = 0; i < 4; ++i) m_frustumPlanes[0][i] = viewProj[i][3] + viewProj[i][0];
    for (int i = 0; i < 4; ++i) m_frustumPlanes[1][i] = viewProj[i][3] - viewProj[i][0];
    for (int i = 0; i < 4; ++i) m_frustumPlanes[2][i] = viewProj[i][3] + viewProj[i][1];
    for (int i = 0; i < 4; ++i) m_frustumPlanes[3][i] = viewProj[i][3] - viewProj[i][1];
    for (int i = 0; i < 4; ++i) m_frustumPlanes[4][i] = viewProj[i][3] + viewProj[i][2]; // for OpenGL depth is -1 to 1, near is +z
    for (int i = 0; i < 4; ++i) m_frustumPlanes[5][i] = viewProj[i][3] - viewProj[i][2];

    for (auto& plane : m_frustumPlanes) {
        float length = glm::length(glm::vec3(plane));
        plane /= length;
    }
}

bool Renderer::isChunkInFrustum(const glm::vec3 &chunkMin, const glm::vec3 &chunkMax) const {
    for (const auto& plane : m_frustumPlanes) {
        glm::vec3 p = chunkMin;
        if (plane.x >= 0) p.x = chunkMax.x;
        if (plane.y >= 0) p.y = chunkMax.y;
        if (plane.z >= 0) p.z = chunkMax.z;

        if (glm::dot(glm::vec3(plane), p) + plane.w < 0) {
            return false;
        }
    }
    return true;
}

int Renderer::getDrawCallCount() const {
    return drawCallCount;
}
