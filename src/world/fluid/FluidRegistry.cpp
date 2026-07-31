#include "FluidRegistry.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "Diagnostics.h"
#include "Paths.h"

bool FluidRegistry::s_initialized = false;
FluidDesc FluidRegistry::s_none{};
FluidDesc FluidRegistry::s_water{};

namespace {
constexpr const char* kFluidsConfigPath = FLUIDS_CONFIG_PATH;

bool parseFluidKind(const std::string& id, FluidKind& outKind) {
    if (id == "minecraft:water") {
        outKind = FluidKind::Water;
        return true;
    }
    MECRAFT_LOG_FPRINTF(stderr, "[FluidRegistry] Unsupported fluid id in fluids.json: %s\n", id.c_str());
    return false;
}

const nlohmann::json* findField(const nlohmann::json& fluidJson, const std::string& fluidId, const char* fieldName) {
    const auto it = fluidJson.find(fieldName);
    if (it == fluidJson.end()) {
        MECRAFT_LOG_FPRINTF(stderr, "[FluidRegistry] Fluid %s is missing required field: %s\n", fluidId.c_str(),
                            fieldName);
        return nullptr;
    }
    return &(*it);
}

bool readUnsignedInteger(const nlohmann::json& value, const std::string& fluidId, const char* fieldName,
                         uint64_t& outValue) {
    if (value.is_number_unsigned()) {
        outValue = value.get<uint64_t>();
        return true;
    }
    if (value.is_number_integer()) {
        const int64_t parsed = value.get<int64_t>();
        if (parsed < 0) {
            MECRAFT_LOG_FPRINTF(stderr, "[FluidRegistry] Fluid %s requires unsigned field: %s\n", fluidId.c_str(),
                                fieldName);
            return false;
        }
        outValue = static_cast<uint64_t>(parsed);
        return true;
    }
    MECRAFT_LOG_FPRINTF(stderr, "[FluidRegistry] Fluid %s requires integer field: %s\n", fluidId.c_str(), fieldName);
    return false;
}

bool readPositiveInteger(const nlohmann::json& fluidJson, const std::string& fluidId, const char* fieldName,
                         uint64_t& outValue) {
    const nlohmann::json* value = findField(fluidJson, fluidId, fieldName);
    if (value == nullptr || !readUnsignedInteger(*value, fluidId, fieldName, outValue)) {
        return false;
    }
    if (outValue == 0) {
        MECRAFT_LOG_FPRINTF(stderr, "[FluidRegistry] Fluid %s requires positive field: %s\n", fluidId.c_str(),
                            fieldName);
        return false;
    }
    return true;
}

bool readIntegerInRange(const nlohmann::json& fluidJson, const std::string& fluidId, const char* fieldName,
                        const int minValue, const int maxValue, uint8_t& outValue) {
    const nlohmann::json* value = findField(fluidJson, fluidId, fieldName);
    uint64_t parsed = 0;
    if (value == nullptr || !readUnsignedInteger(*value, fluidId, fieldName, parsed)) {
        return false;
    }
    if (parsed < static_cast<uint64_t>(minValue) || parsed > static_cast<uint64_t>(maxValue)) {
        MECRAFT_LOG_FPRINTF(stderr, "[FluidRegistry] Fluid %s field is out of range: %s\n", fluidId.c_str(), fieldName);
        return false;
    }
    outValue = static_cast<uint8_t>(parsed);
    return true;
}

bool readBoolean(const nlohmann::json& fluidJson, const std::string& fluidId, const char* fieldName, bool& outValue) {
    const nlohmann::json* value = findField(fluidJson, fluidId, fieldName);
    if (value == nullptr) {
        return false;
    }
    if (!value->is_boolean()) {
        MECRAFT_LOG_FPRINTF(stderr, "[FluidRegistry] Fluid %s requires boolean field: %s\n", fluidId.c_str(),
                            fieldName);
        return false;
    }
    outValue = value->get<bool>();
    return true;
}

bool applyConfigValues(FluidDesc& desc, const nlohmann::json& fluidJson, const std::string& fluidId) {
    return readPositiveInteger(fluidJson, fluidId, "tickDelay", desc.tickDelay) &&
           readIntegerInRange(fluidJson, fluidId, "maxLevel", 0, 7, desc.maxLevel) &&
           readIntegerInRange(fluidJson, fluidId, "slopeSearchDistance", 0, 32, desc.slopeSearchDistance) &&
           readBoolean(fluidJson, fluidId, "canCreateInfiniteSource", desc.canCreateInfiniteSource) &&
           readIntegerInRange(fluidJson, fluidId, "infiniteSourceNeighborCount", 0, 4,
                              desc.infiniteSourceNeighborCount) &&
           readBoolean(fluidJson, fluidId, "requiresSupportForInfiniteSource", desc.requiresSupportForInfiniteSource);
}
} // namespace

bool FluidRegistry::init() {
    if (s_initialized) {
        return true;
    }

    s_none = FluidDesc{};

    s_water = FluidDesc{};
    s_water.kind = FluidKind::Water;
    if (!BlockRegistry::tryGetIdByName("minecraft:water", s_water.blockId)) {
        MECRAFT_LOG_FPRINTF(stderr, "[FluidRegistry] Required fluid block is missing: minecraft:water\n");
        return false;
    }

    std::ifstream file(kFluidsConfigPath);
    if (!file.is_open()) {
        MECRAFT_LOG_FPRINTF(stderr, "[FluidRegistry] Failed to open fluids config: %s\n", kFluidsConfigPath);
        return false;
    }

    nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded()) {
        MECRAFT_LOG_FPRINTF(stderr, "[FluidRegistry] Failed to parse fluids config: invalid JSON\n");
        return false;
    }

    if (!root.contains("fluids") || !root["fluids"].is_array()) {
        MECRAFT_LOG_FPRINTF(stderr, "[FluidRegistry] fluids.json requires a fluids array\n");
        return false;
    }

    bool foundWater = false;
    for (const auto& fluidJson : root["fluids"]) {
        if (!fluidJson.contains("id") || !fluidJson["id"].is_string()) {
            MECRAFT_LOG_FPRINTF(stderr, "[FluidRegistry] Every fluid entry requires a string id\n");
            return false;
        }

        const std::string fluidId = fluidJson["id"].get<std::string>();
        FluidKind kind = FluidKind::None;
        if (!parseFluidKind(fluidId, kind)) {
            return false;
        }
        switch (kind) {
        case FluidKind::Water:
            if (!applyConfigValues(s_water, fluidJson, fluidId)) {
                return false;
            }
            foundWater = true;
            break;
        case FluidKind::None:
        default: break;
        }
    }

    if (!foundWater) {
        MECRAFT_LOG_FPRINTF(stderr, "[FluidRegistry] fluids.json requires minecraft:water\n");
        return false;
    }

    s_initialized = true;
    return true;
}

bool FluidRegistry::ensureInitialized() {
    if (!s_initialized) {
        return init();
    }
    return true;
}

const FluidDesc& FluidRegistry::get(const FluidKind kind) {
    if (const FluidDesc* desc = tryGet(kind)) {
        return *desc;
    }
    return s_none;
}

const FluidDesc* FluidRegistry::tryGet(const FluidKind kind) {
    if (!ensureInitialized()) {
        return nullptr;
    }
    switch (kind) {
    case FluidKind::Water: return &s_water;
    case FluidKind::None:
    default: return nullptr;
    }
}

const FluidDesc* FluidRegistry::tryGetByBlock(const BlockID blockId) {
    return tryGet(kindForBlock(blockId));
}

FluidKind FluidRegistry::kindForBlock(const BlockID blockId) {
    if (!ensureInitialized()) {
        return FluidKind::None;
    }
    if (blockId == s_water.blockId) {
        return FluidKind::Water;
    }
    return FluidKind::None;
}

uint64_t FluidRegistry::defaultTickDelay() {
    if (!ensureInitialized()) {
        return 0;
    }
    return s_water.tickDelay;
}
