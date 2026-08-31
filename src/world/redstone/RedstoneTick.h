#ifndef MECRAFT_REDSTONE_TICK_H
#define MECRAFT_REDSTONE_TICK_H

#include <cstdint>

/// Conversion helpers between game ticks and redstone ticks. The redstone
/// simulation steps at half the game tick rate (10 Hz vs 20 Hz): a redstone
/// step runs on every even game tick, and redstone tick N covers game ticks
/// 2N and 2N+1.
namespace RedstoneTick {

/// Number of game ticks that span one redstone tick.
inline constexpr uint64_t kGameTicksPerRedstoneTick = 2;

/// Returns true when the redstone simulation steps on the given game tick.
[[nodiscard]] inline constexpr bool runsOnGameTick(const uint64_t gameTick) noexcept {
    return (gameTick % kGameTicksPerRedstoneTick) == 0u;
}

/// Converts a game tick index to the redstone tick index it belongs to.
[[nodiscard]] inline constexpr uint64_t fromGameTick(const uint64_t gameTick) noexcept {
    return gameTick / kGameTicksPerRedstoneTick;
}

} // namespace RedstoneTick

#endif // MECRAFT_REDSTONE_TICK_H
