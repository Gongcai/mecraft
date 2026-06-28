#ifndef MECRAFT_IWORLD_VIEW_H
#define MECRAFT_IWORLD_VIEW_H

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>

#include "block/Block.h"
#include "block/BlockStateRegistry.h"
#include "gen/TerrainGenerator.h"

class Chunk;
class World;

/// Read-only world view interface for decoupling the renderer from the concrete World.
/// Both World (server-authoritative) and ClientWorld (client-side mirror) implement this.
class IWorldView {
public:
    virtual ~IWorldView() = default;

    using ChunkMap = std::unordered_map<int64_t, std::shared_ptr<Chunk>>;

    /// Get all currently active (loaded) chunks.
    [[nodiscard]] virtual const ChunkMap& getActiveChunks() const = 0;

    /// Get a revision counter that increments when the chunk set changes.
    [[nodiscard]] virtual uint64_t getActiveChunkRevision() const = 0;

    /// Get a revision counter that increments when loaded block contents change.
    [[nodiscard]] virtual uint64_t getBlockContentRevision() const = 0;

    /// Query a block state at world coordinates.
    [[nodiscard]] virtual BlockStateId getBlock(int x, int y, int z) const = 0;

    /// Query packed light (block light in low nibble, sky light in high nibble).
    [[nodiscard]] virtual uint8_t getPackedLight(int x, int y, int z) const = 0;

    /// Query block state at world coordinates.
    [[nodiscard]] virtual BlockStateId getBlockState(int x, int y, int z) const = 0;

    /// Query fluid state at world coordinates.
    [[nodiscard]] virtual BlockStateId getFluidState(int x, int y, int z) const = 0;

    /// Check if the chunk containing the given block coordinates is loaded.
    [[nodiscard]] virtual bool isChunkLoadedForBlock(int x, int y, int z) const = 0;

    /// Get the current render distance in chunks.
    [[nodiscard]] virtual int getRenderDistance() const = 0;

    /// Convert world block coordinates to chunk coordinates.
    [[nodiscard]] virtual glm::ivec2 getChunkCoords(int worldX, int worldZ) const = 0;

    /// Get the biome at the given world coordinates.
    [[nodiscard]] virtual TerrainBiome getBiome(int x, int z) const = 0;

    /// Downcast to concrete World if available. Returns nullptr for ClientWorld.
    /// This bridge allows renderer subsystems that still need concrete World access
    /// (e.g., weather, day/night) to obtain it without changing all signatures.
    [[nodiscard]] virtual const World* asWorld() const { return nullptr; }

    /// Static utility: compute chunk key from chunk coordinates.
    static int64_t chunkKey(int cx, int cz) {
        return (static_cast<int64_t>(cx) << 32) | static_cast<int64_t>(static_cast<uint32_t>(cz));
    }
};

#endif // MECRAFT_IWORLD_VIEW_H
