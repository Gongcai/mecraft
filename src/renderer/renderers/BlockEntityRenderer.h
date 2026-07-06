#ifndef MECRAFT_BLOCK_ENTITY_RENDERER_H
#define MECRAFT_BLOCK_ENTITY_RENDERER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "../../world/block/BlockStateRegistry.h"

class IWorldView;
class ResourceMgr;
class Shader;
class Chunk;
class SubChunk;

/// Renders world block entities whose visual geometry is not emitted by terrain meshing.
class BlockEntityRenderer {
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown();
    void beginFrame();

    void renderToGBuffer(const IWorldView& worldView,
                         const glm::mat4& viewProj,
                         const glm::mat4& previousViewProj);
    void renderToShadowMap(const IWorldView& worldView,
                           const glm::mat4& shadowViewProj,
                           const glm::vec3& cameraPos,
                           float splitNear,
                           float splitFar);
    void renderForward(const IWorldView& worldView,
                       const glm::mat4& viewProj,
                       float skyIntensity);

    struct CuboidDefinition {
        glm::vec3 fromPixels{0.0f};
        glm::vec3 toPixels{0.0f};
        float textureU = 0.0f;
        float textureV = 0.0f;
    };

private:
    struct Mesh {
        uint32_t vao = 0;
        uint32_t vbo = 0;
        uint32_t vertexCount = 0;
    };

    struct ModelDefinition {
        std::string textureKey;
        float textureWidth = 64.0f;
        float textureHeight = 64.0f;
        bool usesHorizontalFacing = false;
        std::vector<CuboidDefinition> cuboids;
    };

    struct ModelEntry {
        Mesh mesh;
        uint32_t texture = 0;
        bool usesHorizontalFacing = false;
    };

    struct BlockEntityInstance {
        const Chunk* chunk = nullptr;
        const ModelEntry* model = nullptr;
        BlockID blockId = 0;
        BlockStateId stateId = NULL_BLOCK_STATE;
        int localX = 0;
        int columnY = 0;
        int localZ = 0;
        glm::vec3 blockPosition{0.0f};
        glm::vec3 center{0.0f};
        glm::mat4 modelMatrix{1.0f};
        glm::vec2 light{1.0f, 0.0f};
    };

    struct InstancedDrawData {
        glm::mat4 modelMatrix{1.0f};
        glm::vec2 light{1.0f, 0.0f};
    };

    struct SectionKey {
        int64_t chunkKey = 0;
        int scy = 0;

        bool operator==(const SectionKey& other) const {
            return chunkKey == other.chunkKey && scy == other.scy;
        }
    };

    struct SectionKeyHash {
        std::size_t operator()(const SectionKey& key) const {
            std::size_t hash = std::hash<int64_t>{}(key.chunkKey);
            hash ^= static_cast<std::size_t>(key.scy) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
            return hash;
        }
    };

    struct SectionCache {
        const Chunk* chunk = nullptr;
        uint64_t meshRevision = 0;
        uint64_t syncSerial = 0;
        std::vector<BlockEntityInstance> instances;
    };

    ResourceMgr* m_resourceMgr = nullptr;
    Shader* m_gbufferShader = nullptr;
    Shader* m_shadowShader = nullptr;
    Shader* m_forwardShader = nullptr;
    std::unordered_map<BlockID, ModelEntry> m_models;
    std::unordered_map<SectionKey, SectionCache, SectionKeyHash> m_sectionCaches;
    std::vector<BlockEntityInstance*> m_flatInstances;
    uint32_t m_instanceVbo = 0;
    std::size_t m_instanceCapacity = 0;
    std::vector<InstancedDrawData> m_instanceData;
    uint64_t m_cacheSyncSerial = 0;
    uint64_t m_syncedActiveChunkRevision = 0;
    uint64_t m_syncedBlockContentRevision = 0;
    bool m_hasSyncedRevisions = false;
    bool m_instanceCacheSyncedThisFrame = false;
    bool m_instanceLightsSyncedThisFrame = false;

    static void destroyMesh(Mesh& mesh);
    static Mesh buildMesh(const ModelDefinition& definition);
    static ModelDefinition makeChestDefinition();
    static glm::mat4 buildModelMatrix(const ModelEntry& entry,
                                      BlockStateId stateId,
                                      const glm::vec3& blockPosition);
    void configureInstanceAttributes(const Mesh& mesh) const;
    void ensureInstanceCapacity(std::size_t instanceCount);
    void synchronizeInstanceCache(const IWorldView& worldView);
    void rebuildFlatInstanceList();
    void updateInstanceLightsForFrame();
    void rebuildSectionCache(const Chunk& chunk,
                             const SubChunk& subChunk,
                             int scy,
                             SectionCache& cache) const;
    void drawBlockEntitiesInstanced(const IWorldView& worldView,
                                    bool useSplitCulling,
                                    const glm::vec3& cameraPos,
                                    float splitNear,
                                    float splitFar);

    void drawBlockEntities(const IWorldView& worldView,
                           Shader& shader,
                           int modelLoc,
                           int prevModelLoc,
                           int sunlightLoc,
                           int blockLightLoc,
                           bool useSplitCulling,
                           const glm::vec3& cameraPos,
                           float splitNear,
                           float splitFar);
};

#endif // MECRAFT_BLOCK_ENTITY_RENDERER_H
