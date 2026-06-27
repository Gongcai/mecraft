#ifndef MECRAFT_REDSTONE_RUNTIME_STATE_H
#define MECRAFT_REDSTONE_RUNTIME_STATE_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <unordered_map>

#include <glm/glm.hpp>

struct RedstonePositionHash {
    [[nodiscard]] std::size_t operator()(const glm::ivec3& value) const noexcept {
        std::size_t seed = 0;
        const auto mix = [&seed](const int component) {
            seed ^= std::hash<int>{}(component) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        };
        mix(value.x);
        mix(value.y);
        mix(value.z);
        return seed;
    }
};

struct RedstoneTorchRuntimeState {
    std::deque<uint64_t> turnOffTicks;
    bool burnedOut = false;
    uint64_t cooldownEndsAtTick = 0;
};

struct RedstoneRuntimeState {
    std::unordered_map<glm::ivec3, RedstoneTorchRuntimeState, RedstonePositionHash> torches;

    void clear() {
        torches.clear();
    }

    void eraseTorch(const glm::ivec3& position) {
        torches.erase(position);
    }
};

#endif // MECRAFT_REDSTONE_RUNTIME_STATE_H
