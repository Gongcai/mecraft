#include "FluidRegistry.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>

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
    throw std::runtime_error("Unsupported fluid id in fluids.json: " + id);
}

const nlohmann::json& requireField(const nlohmann::json& fluidJson,
                                   const std::string& fluidId,
                                   const char* fieldName) {
    if (!fluidJson.contains(fieldName)) {
        throw std::runtime_error("Fluid " + fluidId + " is missing required field: " + fieldName);
    }
    return fluidJson[fieldName];
}

uint64_t requirePositiveInteger(const nlohmann::json& fluidJson,
                                const std::string& fluidId,
                                const char* fieldName) {
    const nlohmann::json& value = requireField(fluidJson, fluidId, fieldName);
    if (!value.is_number_integer()) {
        throw std::runtime_error("Fluid " + fluidId + " requires integer field: " + fieldName);
    }
    const int64_t parsed = value.get<int64_t>();
    if (parsed <= 0) {
        throw std::runtime_error("Fluid " + fluidId + " requires positive field: " + fieldName);
    }
    return static_cast<uint64_t>(parsed);
}

uint8_t requireIntegerInRange(const nlohmann::json& fluidJson,
                              const std::string& fluidId,
                              const char* fieldName,
                              const int minValue,
                              const int maxValue) {
    const nlohmann::json& value = requireField(fluidJson, fluidId, fieldName);
    if (!value.is_number_integer()) {
        throw std::runtime_error("Fluid " + fluidId + " requires integer field: " + fieldName);
    }
    const int parsed = value.get<int>();
    if (parsed < minValue || parsed > maxValue) {
        throw std::runtime_error("Fluid " + fluidId + " field is out of range: " + fieldName);
    }
    return static_cast<uint8_t>(parsed);
}

bool requireBoolean(const nlohmann::json& fluidJson,
                    const std::string& fluidId,
                    const char* fieldName) {
    const nlohmann::json& value = requireField(fluidJson, fluidId, fieldName);
    if (!value.is_boolean()) {
        throw std::runtime_error("Fluid " + fluidId + " requires boolean field: " + fieldName);
    }
    return value.get<bool>();
}

void applyConfigValues(FluidDesc& desc, const nlohmann::json& fluidJson, const std::string& fluidId) {
    desc.tickDelay = requirePositiveInteger(fluidJson, fluidId, "tickDelay");
    desc.maxLevel = requireIntegerInRange(fluidJson, fluidId, "maxLevel", 0, 7);
    desc.slopeSearchDistance = requireIntegerInRange(fluidJson, fluidId, "slopeSearchDistance", 0, 32);
    desc.canCreateInfiniteSource = requireBoolean(fluidJson, fluidId, "canCreateInfiniteSource");
    desc.infiniteSourceNeighborCount =
        requireIntegerInRange(fluidJson, fluidId, "infiniteSourceNeighborCount", 0, 4);
    desc.requiresSupportForInfiniteSource =
        requireBoolean(fluidJson, fluidId, "requiresSupportForInfiniteSource");
}
}

void FluidRegistry::init() {
    if (s_initialized) {
        return;
    }

    s_none = FluidDesc{};

    s_water = FluidDesc{};
    s_water.kind = FluidKind::Water;
    s_water.blockId = BlockRegistry::requireIdByName("minecraft:water");

    std::ifstream file(kFluidsConfigPath);
    if (!file.is_open()) {
        throw std::runtime_error(std::string("Failed to open fluids config: ") + kFluidsConfigPath);
    }

    nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded()) {
        throw std::runtime_error("Failed to parse fluids config: invalid JSON");
    }

    if (!root.contains("fluids") || !root["fluids"].is_array()) {
        throw std::runtime_error("fluids.json requires a fluids array");
    }

    bool foundWater = false;
    for (const auto& fluidJson : root["fluids"]) {
        if (!fluidJson.contains("id") || !fluidJson["id"].is_string()) {
            throw std::runtime_error("Every fluid entry requires a string id");
        }

        const std::string fluidId = fluidJson["id"].get<std::string>();
        const FluidKind kind = parseFluidKind(fluidId);
        switch (kind) {
            case FluidKind::Water:
                applyConfigValues(s_water, fluidJson, fluidId);
                foundWater = true;
                break;
            case FluidKind::None:
            default:
                break;
        }
    }

    if (!foundWater) {
        throw std::runtime_error("fluids.json requires minecraft:water");
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
    return s_water.tickDelay;
}
