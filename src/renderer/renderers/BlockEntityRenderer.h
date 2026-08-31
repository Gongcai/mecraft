#ifndef MECRAFT_BLOCK_ENTITY_RENDERER_H
#define MECRAFT_BLOCK_ENTITY_RENDERER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "../rhi/RhiHandles.h"
#include "../rhi/RhiGrowableBuffer.h"
#include "../contracts/SceneIdentityContract.h"
#include "../../world/block/BlockStateRegistry.h"

class IWorldView;
struct GameResources;
class RhiCommandList;
class RhiDevice;
class Chunk;
class SubChunk;

/// Renders world block entities whose visual geometry is not emitted by terrain meshing.
class BlockEntityRenderer {
public:
    [[nodiscard]] bool init(GameResources& resources, RhiDevice& rhiDevice);
    void shutdown();
    void beginFrame();
    [[nodiscard]] bool prepareFrame(const IWorldView& worldView);
    [[nodiscard]] bool prepareGBuffer(RhiCommandList& commandList);
    [[nodiscard]] bool prepareForward(RhiCommandList& commandList);
    [[nodiscard]] bool prepareShadow(RhiCommandList& commandList, const glm::vec3& cameraPos, float splitNear,
                                     float splitFar);

    void renderToGBuffer(RhiCommandList& commandList, const glm::mat4& viewProj);
    void renderToShadowMap(RhiCommandList& commandList, const glm::mat4& shadowViewProj);
    void renderForward(RhiCommandList& commandList, const glm::mat4& viewProj, float skyIntensity);

    struct CuboidDefinition {
        glm::vec3 fromPixels{0.0f};
        glm::vec3 toPixels{0.0f};
        float textureU = 0.0f;
        float textureV = 0.0f;
    };

private:
    struct Mesh {
        RhiBufferHandle rhiVertexBuffer;
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
        RhiTextureHandle texture;
        RhiTextureViewHandle textureView;
        RhiBindGroupHandle gbufferBindGroup;
        RhiBindGroupHandle shadowBindGroup;
        renderer::contracts::StableMaterialId materialId;
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
        renderer::contracts::StableObjectId objectId;
    };

    struct InstancedDrawData {
        glm::mat4 modelMatrix{1.0f};
        glm::vec2 light{1.0f, 0.0f};
        glm::uvec2 identity{0u};
    };

    struct PreparedModelBatch {
        const ModelEntry* model = nullptr;
        uint64_t instanceOffset = 0u;
        uint32_t instanceCount = 0u;
    };

    struct SectionKey {
        int64_t chunkKey = 0;
        int scy = 0;

        bool operator==(const SectionKey& other) const { return chunkKey == other.chunkKey && scy == other.scy; }
    };

    struct SectionKeyHash {
        std::size_t operator()(const SectionKey& key) const {
            std::size_t hash = std::hash<int64_t>{}(key.chunkKey);
            hash ^= static_cast<std::size_t>(key.scy) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
            return hash;
        }
    };

    struct BlockPositionKey {
        int x = 0;
        int y = 0;
        int z = 0;

        bool operator==(const BlockPositionKey& other) const { return x == other.x && y == other.y && z == other.z; }
    };

    struct BlockPositionKeyHash {
        std::size_t operator()(const BlockPositionKey& key) const {
            std::size_t hash = std::hash<int>{}(key.x);
            hash ^= std::hash<int>{}(key.y) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
            hash ^= std::hash<int>{}(key.z) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
            return hash;
        }
    };

    struct SectionCache {
        const Chunk* chunk = nullptr;
        uint64_t meshRevision = 0;
        uint64_t syncSerial = 0;
        std::vector<BlockEntityInstance> instances;
    };

    GameResources* m_resources = nullptr;
    RhiDevice* m_rhiDevice = nullptr;
    std::unordered_map<BlockID, ModelEntry> m_models;
    std::unordered_map<SectionKey, SectionCache, SectionKeyHash> m_sectionCaches;
    std::unordered_map<BlockPositionKey, renderer::contracts::StableObjectId, BlockPositionKeyHash> m_blockObjectIds;
    std::vector<BlockEntityInstance*> m_flatInstances;
    RhiGrowableBuffer m_rhiInstanceBuffer;
    std::vector<InstancedDrawData> m_instanceData;
    std::vector<PreparedModelBatch> m_gbufferBatches;
    std::vector<PreparedModelBatch> m_shadowBatches;
    RhiSamplerHandle m_rhiSampler;
    RhiShaderHandle m_gbufferVertexShader;
    RhiShaderHandle m_gbufferFragmentShader;
    RhiBindGroupLayoutHandle m_gbufferBindGroupLayout;
    RhiPipelineLayoutHandle m_gbufferPipelineLayout;
    RhiPipelineHandle m_gbufferPipeline;
    RhiShaderHandle m_shadowVertexShader;
    RhiShaderHandle m_shadowFragmentShader;
    RhiBindGroupLayoutHandle m_shadowBindGroupLayout;
    RhiPipelineLayoutHandle m_shadowPipelineLayout;
    RhiPipelineHandle m_shadowPipeline;
    RhiShaderHandle m_forwardVertexShader;
    RhiShaderHandle m_forwardFragmentShader;
    RhiPipelineLayoutHandle m_forwardPipelineLayout;
    RhiPipelineHandle m_forwardPipeline;
    uint64_t m_cacheSyncSerial = 0;
    uint64_t m_syncedActiveChunkRevision = 0;
    uint64_t m_syncedBlockContentRevision = 0;
    bool m_hasSyncedRevisions = false;
    bool m_instanceCacheSyncedThisFrame = false;
    bool m_instanceLightsSyncedThisFrame = false;

    void destroyMesh(Mesh& mesh);
    Mesh buildMesh(const ModelDefinition& definition);
    void createGBufferRhiResources();
    void destroyGBufferRhiResources();
    static ModelDefinition makeChestDefinition();
    static glm::mat4 buildModelMatrix(const ModelEntry& entry, BlockStateId stateId, const glm::vec3& blockPosition);
    [[nodiscard]] bool synchronizeInstanceCache(const IWorldView& worldView);
    void rebuildFlatInstanceList();
    void updateInstanceLightsForFrame();
    [[nodiscard]] bool rebuildSectionCache(const Chunk& chunk, const SubChunk& subChunk, int scy, SectionCache& cache);
};

#endif // MECRAFT_BLOCK_ENTITY_RENDERER_H
