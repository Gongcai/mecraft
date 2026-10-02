#ifndef MECRAFT_WORLDGENERATIONMODE_H
#define MECRAFT_WORLDGENERATIONMODE_H

#include <cstdint>
#include <optional>
#include <string_view>

enum class WorldGenerationMode : uint8_t {
    Default = 0,
    Superflat = 1,
    Amplified = 2,
};

[[nodiscard]] constexpr std::string_view worldGenerationModeId(const WorldGenerationMode mode) {
    switch (mode) {
    case WorldGenerationMode::Default: return "default";
    case WorldGenerationMode::Superflat: return "superflat";
    case WorldGenerationMode::Amplified: return "amplified";
    }
    return {};
}

[[nodiscard]] inline std::optional<WorldGenerationMode> worldGenerationModeFromId(const std::string_view id) {
    if (id == "default") {
        return WorldGenerationMode::Default;
    }
    if (id == "superflat") {
        return WorldGenerationMode::Superflat;
    }
    if (id == "amplified") {
        return WorldGenerationMode::Amplified;
    }
    return std::nullopt;
}

#endif // MECRAFT_WORLDGENERATIONMODE_H
