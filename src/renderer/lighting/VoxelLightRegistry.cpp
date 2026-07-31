#include "VoxelLightRegistry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "world/IWorldView.h"
#include "world/block/Block.h"
#include "world/block/BlockStateRegistry.h"
#include "world/chunk/Chunk.h"

namespace renderer::lighting {
namespace {

struct CachedVoxelLightSource final {
    uint32_t localBlockIndex = 0u;
    BlockID blockId = RUNTIME_ID_NULL;
    renderer::contracts::StableLightId lightId;
};

struct CachedVoxelLightChunk final {
    const Chunk* instance = nullptr;
    uint64_t blockContentRevision = 0u;
    int chunkX = 0;
    int chunkZ = 0;
    std::vector<CachedVoxelLightSource> sources;
};

struct ActiveChunkEntry final {
    int64_t key = 0;
    const Chunk* chunk = nullptr;
};

[[nodiscard]] bool sameSourceSet(const std::vector<CachedVoxelLightSource>& lhs,
                                 const std::vector<CachedVoxelLightSource>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0u; index < lhs.size(); ++index) {
        if (lhs[index].localBlockIndex != rhs[index].localBlockIndex || lhs[index].blockId != rhs[index].blockId) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool sourceEnabledForState(const BlockAnalyticLightDefinition& definition, const BlockStateId state) {
    if (definition.enabledStatePropertyIndex == BlockAnalyticLightDefinition::kUnconditionalStateIndex) {
        return definition.enabledStateValueIndex == BlockAnalyticLightDefinition::kUnconditionalStateIndex;
    }
    return definition.enabledStateValueIndex != BlockAnalyticLightDefinition::kUnconditionalStateIndex &&
           BlockStateRegistry::getPropertyIndex(state, definition.enabledStatePropertyIndex) ==
               definition.enabledStateValueIndex;
}

} // namespace

struct VoxelLightRegistry::Impl final {
    [[nodiscard]] bool synchronize(const IWorldView& worldView);
    [[nodiscard]] bool rebuildChunk(const CachedVoxelLightChunk* previous, const Chunk& chunk,
                                    CachedVoxelLightChunk& rebuilt);
    void setError(std::string message) { error = std::move(message); }

    const IWorldView* worldView = nullptr;
    uint64_t activeChunkRevision = 0u;
    uint64_t blockContentRevision = 0u;
    uint64_t lightRevision = 1u;
    std::size_t sourceCount = 0u;
    std::unordered_map<int64_t, CachedVoxelLightChunk> chunks;
    std::vector<int64_t> orderedChunkKeys;
    std::string error;
};

bool VoxelLightRegistry::Impl::rebuildChunk(const CachedVoxelLightChunk* previous, const Chunk& chunk,
                                            CachedVoxelLightChunk& rebuilt) {
    std::unordered_map<uint32_t, renderer::contracts::StableLightId> previousIds;
    if (previous != nullptr) {
        previousIds.reserve(previous->sources.size());
        for (const CachedVoxelLightSource& source : previous->sources) {
            previousIds.emplace(source.localBlockIndex, source.lightId);
        }
    }

    rebuilt.instance = &chunk;
    rebuilt.blockContentRevision = chunk.getBlockContentRevision();
    rebuilt.chunkX = chunk.m_chunkX;
    rebuilt.chunkZ = chunk.m_chunkZ;
    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                const BlockStateId state = chunk.getBlock(x, y, z);
                const BlockID blockId = BlockStateRegistry::getBlockId(state);
                const BlockDef& block = BlockRegistry::getFast(blockId);
                if (!block.analyticLight.has_value() || !sourceEnabledForState(*block.analyticLight, state)) {
                    continue;
                }
                const uint32_t localIndex = static_cast<uint32_t>(Chunk::toIndex(x, y, z));
                renderer::contracts::StableLightId lightId;
                const auto previousId = previousIds.find(localIndex);
                if (previousId != previousIds.end()) {
                    lightId = previousId->second;
                } else {
                    const std::optional<renderer::contracts::StableLightId> allocated =
                        renderer::contracts::allocateStableSceneId<renderer::contracts::StableLightIdTag>();
                    if (!allocated.has_value()) {
                        setError("stable voxel light identity space is exhausted");
                        return false;
                    }
                    lightId = *allocated;
                }
                rebuilt.sources.push_back({localIndex, blockId, lightId});
            }
        }
    }
    return true;
}

bool VoxelLightRegistry::Impl::synchronize(const IWorldView& currentWorldView) {
    bool sourceSetChanged = false;
    if (worldView != &currentWorldView) {
        sourceSetChanged = sourceCount != 0u;
        chunks.clear();
        orderedChunkKeys.clear();
        sourceCount = 0u;
        worldView = &currentWorldView;
        activeChunkRevision = 0u;
        blockContentRevision = 0u;
    }

    const uint64_t currentActiveRevision = currentWorldView.getActiveChunkRevision();
    const uint64_t currentContentRevision = currentWorldView.getBlockContentRevision();
    if (currentActiveRevision == activeChunkRevision && currentContentRevision == blockContentRevision) {
        error.clear();
        return true;
    }

    const IWorldView::ChunkMap& activeChunks = currentWorldView.getActiveChunks();
    std::vector<ActiveChunkEntry> active;
    active.reserve(activeChunks.size());
    for (const auto& [key, chunk] : activeChunks) {
        if (!chunk) {
            setError("active voxel chunk map contains a null chunk");
            return false;
        }
        if (IWorldView::chunkKey(chunk->m_chunkX, chunk->m_chunkZ) != key) {
            setError("active voxel chunk key does not match its coordinates");
            return false;
        }
        active.push_back({key, chunk.get()});
    }
    std::sort(active.begin(), active.end(), [](const ActiveChunkEntry& lhs, const ActiveChunkEntry& rhs) {
        if (lhs.chunk->m_chunkX != rhs.chunk->m_chunkX) {
            return lhs.chunk->m_chunkX < rhs.chunk->m_chunkX;
        }
        return lhs.chunk->m_chunkZ < rhs.chunk->m_chunkZ;
    });

    std::vector<std::pair<int64_t, CachedVoxelLightChunk>> rebuiltChunks;
    std::unordered_set<int64_t> activeKeys;
    activeKeys.reserve(active.size());
    for (const ActiveChunkEntry& entry : active) {
        activeKeys.insert(entry.key);
        const auto existing = chunks.find(entry.key);
        const CachedVoxelLightChunk* previous = existing != chunks.end() ? &existing->second : nullptr;
        if (previous != nullptr && previous->instance == entry.chunk &&
            previous->blockContentRevision == entry.chunk->getBlockContentRevision()) {
            continue;
        }
        CachedVoxelLightChunk rebuilt;
        if (!rebuildChunk(previous, *entry.chunk, rebuilt)) {
            return false;
        }
        sourceSetChanged =
            sourceSetChanged ||
            (previous == nullptr ? !rebuilt.sources.empty() : !sameSourceSet(previous->sources, rebuilt.sources));
        rebuiltChunks.emplace_back(entry.key, std::move(rebuilt));
    }

    for (auto it = chunks.begin(); it != chunks.end();) {
        if (activeKeys.find(it->first) == activeKeys.end()) {
            sourceSetChanged = sourceSetChanged || !it->second.sources.empty();
            it = chunks.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& [key, rebuilt] : rebuiltChunks) {
        chunks[key] = std::move(rebuilt);
    }

    orderedChunkKeys.clear();
    orderedChunkKeys.reserve(active.size());
    sourceCount = 0u;
    for (const ActiveChunkEntry& entry : active) {
        orderedChunkKeys.push_back(entry.key);
        sourceCount += chunks.at(entry.key).sources.size();
    }
    activeChunkRevision = currentActiveRevision;
    blockContentRevision = currentContentRevision;
    if (sourceSetChanged) {
        ++lightRevision;
    }
    error.clear();
    return true;
}

VoxelLightRegistry::VoxelLightRegistry() : m_impl(std::make_unique<Impl>()) {}

VoxelLightRegistry::~VoxelLightRegistry() = default;

bool VoxelLightRegistry::buildSceneLights(const IWorldView& worldView, const glm::vec3& cameraPositionMeters,
                                          std::vector<renderer::contracts::SceneLight>& lights) {
    if (!std::isfinite(cameraPositionMeters.x) || !std::isfinite(cameraPositionMeters.y) ||
        !std::isfinite(cameraPositionMeters.z)) {
        m_impl->setError("voxel light camera position must be finite");
        return false;
    }
    if (!m_impl->synchronize(worldView)) {
        return false;
    }

    using namespace renderer::contracts;
    std::vector<SceneLight> built;
    built.reserve(m_impl->sourceCount);
    for (const int64_t chunkKey : m_impl->orderedChunkKeys) {
        const CachedVoxelLightChunk& chunk = m_impl->chunks.at(chunkKey);
        for (const CachedVoxelLightSource& source : chunk.sources) {
            const BlockDef& block = BlockRegistry::getFast(source.blockId);
            if (!block.analyticLight.has_value()) {
                m_impl->setError("cached voxel light references a block without an analytic definition");
                return false;
            }
            const BlockAnalyticLightDefinition& definition = *block.analyticLight;
            const uint32_t y = source.localBlockIndex / static_cast<uint32_t>(Chunk::SIZE_X * Chunk::SIZE_Z);
            const uint32_t horizontal = source.localBlockIndex % static_cast<uint32_t>(Chunk::SIZE_X * Chunk::SIZE_Z);
            const uint32_t z = horizontal / static_cast<uint32_t>(Chunk::SIZE_X);
            const uint32_t x = horizontal % static_cast<uint32_t>(Chunk::SIZE_X);
            const double worldX =
                static_cast<double>(chunk.chunkX) * Chunk::SIZE_X + x + definition.positionOffsetMeters.x;
            const double worldY = static_cast<double>(y) + definition.positionOffsetMeters.y;
            const double worldZ =
                static_cast<double>(chunk.chunkZ) * Chunk::SIZE_Z + z + definition.positionOffsetMeters.z;

            GpuLightNormalizationInput input;
            input.lightId = source.lightId;
            input.type = GpuLightType::Point;
            input.positionMeters = {static_cast<float>(worldX - cameraPositionMeters.x),
                                    static_cast<float>(worldY - cameraPositionMeters.y),
                                    static_cast<float>(worldZ - cameraPositionMeters.z)};
            input.rangeMeters = definition.rangeMeters;
            input.colorLinear = definition.colorLinear;
            input.intensity = definition.luminousFluxLumens;
            input.intensityUnit = GpuLightIntensityUnit::Lumen;
            input.contributionFlags = gpuLightContributionFlagBit(GpuLightContributionFlag::Diffuse) |
                                      gpuLightContributionFlagBit(GpuLightContributionFlag::Specular) |
                                      gpuLightContributionFlagBit(GpuLightContributionFlag::Volumetric);
            const GpuLightNormalizationResult normalized = normalizeGpuLight(input);
            if (!normalized.succeeded()) {
                m_impl->setError(
                    "voxel light normalization failed [block=" + BlockRegistry::getNamespacedId(source.blockId).full() +
                    ", error=" + gpuLightNormalizationErrorStableId(normalized.error) +
                    ", field=" + gpuLightFieldStableId(normalized.field) + "]");
                return false;
            }
            SceneLight sceneLight;
            sceneLight.light = normalized.light;
            sceneLight.requestedShadowPolicy = definition.shadowPolicy == BlockAnalyticLightShadowPolicy::RasterCached
                                                   ? GpuLightShadowPolicy::RasterCached
                                                   : GpuLightShadowPolicy::None;
            built.push_back(sceneLight);
        }
    }
    lights = std::move(built);
    m_impl->error.clear();
    return true;
}

void VoxelLightRegistry::reset() {
    const bool attached = m_impl->worldView != nullptr;
    m_impl->worldView = nullptr;
    m_impl->activeChunkRevision = 0u;
    m_impl->blockContentRevision = 0u;
    m_impl->sourceCount = 0u;
    m_impl->chunks.clear();
    m_impl->orderedChunkKeys.clear();
    m_impl->error.clear();
    if (attached) {
        ++m_impl->lightRevision;
    }
}

uint64_t VoxelLightRegistry::lightRevision() const {
    return m_impl->lightRevision;
}

std::size_t VoxelLightRegistry::sourceCount() const {
    return m_impl->sourceCount;
}

const std::string& VoxelLightRegistry::lastError() const {
    return m_impl->error;
}

} // namespace renderer::lighting
