#include "PlayerSerializer.h"
#include "../Diagnostics.h"
#include "../player/Inventory.h"
#include "../world/block/Block.h"
#include "../item/Item.h"

#include <fstream>
#include <cstdio>
#include <limits>

namespace save {

namespace {

const nlohmann::json* findField(const nlohmann::json& object, const char* key) {
    if (!object.is_object()) {
        return nullptr;
    }
    const auto it = object.find(key);
    return it != object.end() ? &(*it) : nullptr;
}

bool readIntField(const nlohmann::json& object, const char* key, int& out) {
    const nlohmann::json* value = findField(object, key);
    if (value == nullptr) {
        return true;
    }
    if (!value->is_number_integer()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid integer field: %s\n", key);
        return false;
    }
    const auto raw = value->get<int64_t>();
    if (raw < std::numeric_limits<int>::min() || raw > std::numeric_limits<int>::max()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Integer field out of range: %s\n", key);
        return false;
    }
    out = static_cast<int>(raw);
    return true;
}

bool readUint16Field(const nlohmann::json& object, const char* key, uint16_t& out) {
    const nlohmann::json* value = findField(object, key);
    if (value == nullptr) {
        return true;
    }

    uint64_t raw = 0;
    if (value->is_number_unsigned()) {
        raw = value->get<uint64_t>();
    } else if (value->is_number_integer()) {
        const auto signedRaw = value->get<int64_t>();
        if (signedRaw < 0) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Negative value for unsigned field: %s\n", key);
            return false;
        }
        raw = static_cast<uint64_t>(signedRaw);
    } else {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid unsigned integer field: %s\n", key);
        return false;
    }

    if (raw > std::numeric_limits<uint16_t>::max()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Unsigned integer field out of range: %s\n", key);
        return false;
    }
    out = static_cast<uint16_t>(raw);
    return true;
}

bool readFloatField(const nlohmann::json& object, const char* key, float& out) {
    const nlohmann::json* value = findField(object, key);
    if (value == nullptr) {
        return true;
    }
    if (!value->is_number()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid number field: %s\n", key);
        return false;
    }
    out = value->get<float>();
    return true;
}

bool readBoolField(const nlohmann::json& object, const char* key, bool& out) {
    const nlohmann::json* value = findField(object, key);
    if (value == nullptr) {
        return true;
    }
    if (!value->is_boolean()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid boolean field: %s\n", key);
        return false;
    }
    out = value->get<bool>();
    return true;
}

bool readStringField(const nlohmann::json& object, const char* key, std::string& out) {
    const nlohmann::json* value = findField(object, key);
    if (value == nullptr) {
        return true;
    }
    if (!value->is_string()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid string field: %s\n", key);
        return false;
    }
    out = value->get<std::string>();
    return true;
}

bool readFloat3Field(const nlohmann::json& object,
                     const char* key,
                     float& x,
                     float& y,
                     float& z) {
    const nlohmann::json* value = findField(object, key);
    if (value == nullptr) {
        return true;
    }
    if (!value->is_array() || value->size() < 3 ||
        !(*value)[0].is_number() || !(*value)[1].is_number() || !(*value)[2].is_number()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid vec3 field: %s\n", key);
        return false;
    }
    x = (*value)[0].get<float>();
    y = (*value)[1].get<float>();
    z = (*value)[2].get<float>();
    return true;
}

} // namespace

nlohmann::json PlayerSerializer::serialize(const PlayerData& data) {
    nlohmann::json j;
    j["version"] = 1;
    j["position"] = {data.posX, data.posY, data.posZ};
    j["velocity"] = {data.velX, data.velY, data.velZ};
    j["yaw"] = data.yaw;
    j["pitch"] = data.pitch;
    j["selectedSlot"] = data.selectedSlot;

    j["health"] = {{"current", data.health}, {"max", data.healthMax}};
    j["armor"] = {{"current", data.armor}, {"max", data.armorMax}};
    j["food"] = {
        {"current", data.food},
        {"max", data.foodMax},
        {"saturation", data.saturation}
    };
    j["flight"] = {{"isFlying", data.isFlying}};

    // Serialize non-empty inventory slots
    nlohmann::json inv = nlohmann::json::array();
    for (size_t i = 0; i < data.inventory.size(); ++i) {
        const auto& slot = data.inventory[i];
        if (!slot.item.empty() && slot.count > 0) {
            nlohmann::json s;
            s["slot"] = static_cast<int>(i);
            s["item"] = slot.item;
            s["count"] = slot.count;
            if (slot.durability > 0) {
                s["durability"] = slot.durability;
            }
            inv.push_back(std::move(s));
        }
    }
    j["inventory"] = std::move(inv);

    return j;
}

bool PlayerSerializer::deserialize(const nlohmann::json& j, PlayerData& out) {
    if (!j.is_object()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Player data root must be an object\n");
        return false;
    }

    int version = 0;
    if (!readIntField(j, "version", version)) {
        return false;
    }
    if (version != 1) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Unsupported player data version: %d\n", version);
        return false;
    }

    if (!readFloat3Field(j, "position", out.posX, out.posY, out.posZ) ||
        !readFloat3Field(j, "velocity", out.velX, out.velY, out.velZ) ||
        !readFloatField(j, "yaw", out.yaw) ||
        !readFloatField(j, "pitch", out.pitch) ||
        !readIntField(j, "selectedSlot", out.selectedSlot)) {
        return false;
    }

    if (const nlohmann::json* health = findField(j, "health")) {
        if (!health->is_object() ||
            !readIntField(*health, "current", out.health) ||
            !readIntField(*health, "max", out.healthMax)) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid health object\n");
            return false;
        }
    }

    if (const nlohmann::json* armor = findField(j, "armor")) {
        if (!armor->is_object() ||
            !readIntField(*armor, "current", out.armor) ||
            !readIntField(*armor, "max", out.armorMax)) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid armor object\n");
            return false;
        }
    }

    if (const nlohmann::json* food = findField(j, "food")) {
        if (!food->is_object() ||
            !readIntField(*food, "current", out.food) ||
            !readIntField(*food, "max", out.foodMax) ||
            !readIntField(*food, "saturation", out.saturation)) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid food object\n");
            return false;
        }
    }

    if (const nlohmann::json* flight = findField(j, "flight")) {
        if (!flight->is_object() || !readBoolField(*flight, "isFlying", out.isFlying)) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid flight object\n");
            return false;
        }
    }

    if (const nlohmann::json* inventory = findField(j, "inventory")) {
        if (!inventory->is_array()) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid inventory array\n");
            return false;
        }

        for (const auto& s : *inventory) {
            if (!s.is_object()) {
                MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid inventory slot object\n");
                return false;
            }

            int slot = -1;
            PlayerData::Slot slotData;
            if (!readIntField(s, "slot", slot) ||
                !readStringField(s, "item", slotData.item) ||
                !readUint16Field(s, "count", slotData.count) ||
                !readUint16Field(s, "durability", slotData.durability)) {
                return false;
            }
            if (slot < 0 || slot >= Inventory::INVENTORY_SIZE) {
                MECRAFT_LOG_FPRINTF(stderr, "[Save] Inventory slot index out of range: %d\n", slot);
                return false;
            }

            if (out.inventory.size() <= static_cast<size_t>(slot)) {
                out.inventory.resize(static_cast<size_t>(slot) + 1);
            }
            out.inventory[static_cast<size_t>(slot)] = std::move(slotData);
        }
    }

    return true;
}

void PlayerSerializer::saveToFile(const std::string& path, const PlayerData& data) {
    const std::string tmpPath = path + ".tmp";

    {
        std::ofstream file(tmpPath);
        if (!file.is_open()) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to write %s\n", tmpPath.c_str());
            return;
        }
        file << serialize(data).dump(2) << '\n';
        file.flush();
    }

    std::error_code ec;
    const std::string bakPath = path + ".bak";
    if (std::filesystem::exists(path, ec)) {
        std::filesystem::rename(path, bakPath, ec);
        ec.clear();
    }
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to rename player file: %s\n", ec.message().c_str());
    }
}

bool PlayerSerializer::loadFromFile(const std::string& path, PlayerData& out) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return false;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    nlohmann::json j = nlohmann::json::parse(file, nullptr, false);
    if (j.is_discarded()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to parse player file: invalid JSON\n");
        return false;
    }
    return deserialize(j, out);
}

} // namespace save
