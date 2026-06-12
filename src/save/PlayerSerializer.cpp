#include "PlayerSerializer.h"
#include "../Diagnostics.h"
#include "../player/Inventory.h"
#include "../world/block/Block.h"
#include "../item/Item.h"

#include <fstream>
#include <cstdio>

namespace save {

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
    try {
        const int version = j.value("version", 0);
        if (version != 1) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Unsupported player data version: %d\n", version);
            return false;
        }

        // Position
        if (j.contains("position") && j["position"].is_array() && j["position"].size() >= 3) {
            out.posX = j["position"][0].get<float>();
            out.posY = j["position"][1].get<float>();
            out.posZ = j["position"][2].get<float>();
        }

        // Velocity
        if (j.contains("velocity") && j["velocity"].is_array() && j["velocity"].size() >= 3) {
            out.velX = j["velocity"][0].get<float>();
            out.velY = j["velocity"][1].get<float>();
            out.velZ = j["velocity"][2].get<float>();
        }

        // Rotation
        out.yaw = j.value("yaw", out.yaw);
        out.pitch = j.value("pitch", out.pitch);
        out.selectedSlot = j.value("selectedSlot", out.selectedSlot);

        // Health
        if (j.contains("health") && j["health"].is_object()) {
            out.health = j["health"].value("current", out.health);
            out.healthMax = j["health"].value("max", out.healthMax);
        }

        // Armor
        if (j.contains("armor") && j["armor"].is_object()) {
            out.armor = j["armor"].value("current", out.armor);
            out.armorMax = j["armor"].value("max", out.armorMax);
        }

        // Food
        if (j.contains("food") && j["food"].is_object()) {
            out.food = j["food"].value("current", out.food);
            out.foodMax = j["food"].value("max", out.foodMax);
            out.saturation = j["food"].value("saturation", out.saturation);
        }

        // Flight
        if (j.contains("flight") && j["flight"].is_object()) {
            out.isFlying = j["flight"].value("isFlying", out.isFlying);
        }

        // Inventory
        if (j.contains("inventory") && j["inventory"].is_array()) {
            for (const auto& s : j["inventory"]) {
                const int slot = s.value("slot", -1);
                if (slot < 0 || slot >= Inventory::INVENTORY_SIZE) continue;

                PlayerData::Slot slotData;
                slotData.item = s.value("item", "");
                slotData.count = s.value("count", static_cast<uint16_t>(0));
                slotData.durability = s.value("durability", static_cast<uint16_t>(0));

                // Ensure inventory vector is large enough
                if (out.inventory.size() <= static_cast<size_t>(slot)) {
                    out.inventory.resize(static_cast<size_t>(slot) + 1);
                }
                out.inventory[static_cast<size_t>(slot)] = std::move(slotData);
            }
        }

        return true;
    } catch (const std::exception& e) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to parse player data: %s\n", e.what());
        return false;
    }
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

    try {
        nlohmann::json j;
        file >> j;
        return deserialize(j, out);
    } catch (const std::exception& e) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to read player file: %s\n", e.what());
        return false;
    }
}

} // namespace save
