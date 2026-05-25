#ifndef MECRAFT_LIGHTTYPES_H
#define MECRAFT_LIGHTTYPES_H

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "Block.h"

class Chunk;

enum class LightKind : uint8_t {
    Sky,
    Block
};

enum class LightDirtyReason : uint8_t {
    ChunkLoaded,
    BlockChanged,
    NeighborBoundary,
};

struct BorderLightNode {
    uint8_t localX = 0;
    uint8_t y = 0;
    uint8_t localZ = 0;
    uint8_t level = 0;
    LightKind kind = LightKind::Sky;
};

struct BorderUpdateBatch {
    int64_t targetChunkKey = 0;
    uint64_t sourceRevision = 0;
    uint8_t fromDirection = 0; // 0=+X,1=-X,2=+Z,3=-Z
    uint32_t dirtySubChunkMask = 0;
    std::vector<BorderLightNode> nodes;
};

struct LocalLightChange {
    uint8_t localX = 0;
    uint8_t y = 0;
    uint8_t localZ = 0;
    BlockID oldBlock = BlockIds::AIR;
    BlockID newBlock = BlockIds::AIR;
};

struct LightJob {
    int64_t chunkKey = 0;
    uint64_t revision = 0;
    std::shared_ptr<Chunk> chunk;
    std::shared_ptr<const Chunk> neighborPosX;
    std::shared_ptr<const Chunk> neighborNegX;
    std::shared_ptr<const Chunk> neighborPosZ;
    std::shared_ptr<const Chunk> neighborNegZ;
    std::vector<BlockID> blockSnapshot;
    std::vector<uint8_t> packedLightSnapshot;
    std::vector<uint8_t> baseLightPacked; // pre-computed intrinsic base (no boundary seeds)
    std::vector<LocalLightChange> blockChanges;
    std::vector<BorderUpdateBatch> previousInbox;
    std::vector<BorderUpdateBatch> inbox;
    std::array<bool, 4> changedBoundaryDirections{};
    LightDirtyReason reason = LightDirtyReason::NeighborBoundary;
};

struct LightChunkDelta {
    int64_t chunkKey = 0;
    uint64_t revision = 0;
    std::vector<uint8_t> packedLight;
    uint32_t dirtySubChunkMask = 0;
};

struct LightResult {
    LightChunkDelta selfDelta;
    std::vector<BorderUpdateBatch> outgoing;
    uint32_t nodesVisited = 0;
    float workerMs = 0.0f;
};

struct LightFrameStats {
    int submitted = 0;
    int completed = 0;
    int inFlight = 0;
    int queued = 0;
    int dirty = 0;
    int pendingCompleted = 0;
    int boundarySync = 0;
    int nodesVisited = 0;
    int staleDropped = 0;
    int requeued = 0;
    float workerMs = 0.0f;
    float mergeMs = 0.0f;
};

#endif // MECRAFT_LIGHTTYPES_H


