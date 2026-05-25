#include "FluidRegistry.h"

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

#include "Paths.h"

bool FluidRegistry::s_initialized = false;
FluidDesc FluidRegistry::s_none{};
FluidDesc FluidRegistry::s_water{};

namespace {
constexpr const char* kFluidsConfigPath = FLUIDS_CONFIG_PATH;

FluidKind parseFluidKind(const std::string& id) {
    if (id == "minecraft:water") {
        return FluidKind::Water;
    }
    return FluidKind::None;
}

void applyConfigValues(FluidDesc& desc, const nlohmann::json& fluidJson) {
    if (fluidJson.contains("tickDelay") && fluidJson["tickDelay"].is_number_integer()) {
        desc.tickDelay = std::max<uint64_t>(1, fluidJson["tickDelay"].get<uint64_t>());
    }
    if (fluidJson.contains("maxLevel") && fluidJson["maxLevel"].is_number_integer()) {
        desc.maxLevel = static_cast<uint8_t>(std::clamp(fluidJson["maxLevel"].get<int>(), 0, 7));
    }
    if (fluidJson.contains("slopeSearchDistance") && fluidJson["slopeSearchDistance"].is_number_integer()) {
        desc.slopeSearchDistance = static_cast<uint8_t>(
            std::clamp(fluidJson["slopeSearchDistance"].get<int>(), 0, 32));
    }
    if (fluidJson.contains("canCreateInfiniteSource") && fluidJson["canCreateInfiniteSource"].is_boolean()) {
        desc.canCreateInfiniteSource = fluidJson["canCreateInfiniteSource"].get<bool>();
    }
    if (fluidJson.contains("infiniteSourceNeighborCount") && fluidJson["infiniteSourceNeighborCount"].is_number_integer()) {
        desc.infiniteSourceNeighborCount = static_cast<uint8_t>(
            std::clamp(fluidJson["infiniteSourceNeighborCount"].get<int>(), 0, 4));
    }
    if (fluidJson.contains("requiresSupportForInfiniteSource") &&
        fluidJson["requiresSupportForInfiniteSource"].is_boolean()) {
        desc.requiresSupportForInfiniteSource = fluidJson["requiresSupportForInfiniteSource"].get<bool>();
    }
}
}

void FluidRegistry::registerFallbackWater() {
    s_none = FluidDesc{};

    s_water = FluidDesc{};
    s_water.kind = FluidKind::Water;
    s_water.blockId = BlockIds::WATER;
    s_water.tickDelay = 5;
    s_water.maxLevel = 7;
    s_water.slopeSearchDistance = 5;
    s_water.canCreateInfiniteSource = true;
    s_water.infiniteSourceNeighborCount = 2;
    s_water.requiresSupportForInfiniteSource = true;
}

void FluidRegistry::init() {
    registerFallbackWater();

    std::ifstream file(kFluidsConfigPath);
    if (!file.is_open()) {
        s_initialized = true;
        return;
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (const std::exception&) {
        s_initialized = true;
        return;
    }

    if (!root.contains("fluids") || !root["fluids"].is_array()) {
        s_initialized = true;
        return;
    }

    for (const auto& fluidJson : root["fluids"]) {
        if (!fluidJson.contains("id") || !fluidJson["id"].is_string()) {
            continue;
        }

        const FluidKind kind = parseFluidKind(fluidJson["id"].get<std::string>());
        switch (kind) {
            case FluidKind::Water:
                applyConfigValues(s_water, fluidJson);
                break;
            case FluidKind::None:
            default:
                break;
        }
    }

    s_initialized = true;
}

void FluidRegistry::ensureInitialized() {
    if (!s_initialized) {
        init();
    }
}

const FluidDesc& FluidRegistry::get(const FluidKind kind) {
    if (const FluidDesc* desc = tryGet(kind)) {
        return *desc;
    }
    return s_none;
}

const FluidDesc* FluidRegistry::tryGet(const FluidKind kind) {
    ensureInitialized();
    switch (kind) {
        case FluidKind::Water:
            return &s_water;
        case FluidKind::None:
        default:
            return nullptr;
    }
}

const FluidDesc* FluidRegistry::tryGetByBlock(const BlockID blockId) {
    return tryGet(kindForBlock(blockId));
}

FluidKind FluidRegistry::kindForBlock(const BlockID blockId) {
    ensureInitialized();
    if (blockId == s_water.blockId) {
        return FluidKind::Water;
    }
    return FluidKind::None;
}

uint64_t FluidRegistry::defaultTickDelay() {
    ensureInitialized();
    return s_water.tickDelay > 0 ? s_water.tickDelay : 1;
}
