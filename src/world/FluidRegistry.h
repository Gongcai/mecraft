#pragma once

#include <cstdint>

#include "Block.h"

enum class FluidKind : uint8_t {
    None = 0,
    Water = 1
};

struct FluidDesc {
    FluidKind kind = FluidKind::None;
    BlockID blockId = BlockIds::AIR;
    uint64_t tickDelay = 0;
    uint8_t maxLevel = 0;
    uint8_t slopeSearchDistance = 0;
    bool canCreateInfiniteSource = false;
    uint8_t infiniteSourceNeighborCount = 0;
    bool requiresSupportForInfiniteSource = false;
};

class FluidRegistry {
public:
    static void init();
    static void ensureInitialized();

    [[nodiscard]] static const FluidDesc& get(FluidKind kind);
    [[nodiscard]] static const FluidDesc* tryGet(FluidKind kind);
    [[nodiscard]] static const FluidDesc* tryGetByBlock(BlockID blockId);
    [[nodiscard]] static FluidKind kindForBlock(BlockID blockId);
    [[nodiscard]] static uint64_t defaultTickDelay();

private:
    static void registerFallbackWater();

    static bool s_initialized;
    static FluidDesc s_none;
    static FluidDesc s_water;
};
