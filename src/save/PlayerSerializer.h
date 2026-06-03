#ifndef MECRAFT_PLAYER_SERIALIZER_H
#define MECRAFT_PLAYER_SERIALIZER_H

// PlayerSerializer: persists local player state to/from JSON.
//
// Saves: position, velocity, yaw, pitch, health, armor, food, flight, inventory.
// Does NOT save: transient gameplay state (hurt effects, footstep timers, etc.)

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

class Inventory;

namespace ecs {
struct TransformComponent;
struct VelocityComponent;
struct HealthComponent;
struct ArmorComponent;
struct FoodComponent;
struct FlightStateComponent;
struct CameraStateComponent;
struct InventoryComponent;
struct InventoryDataComponent;
}

namespace save {

struct PlayerData {
    float posX = 0.0f;
    float posY = 68.0f;
    float posZ = 0.0f;
    float velX = 0.0f;
    float velY = 0.0f;
    float velZ = 0.0f;
    float yaw = -90.0f;
    float pitch = 0.0f;
    int health = 20;
    int healthMax = 20;
    int armor = 0;
    int armorMax = 20;
    int food = 20;
    int foodMax = 20;
    int saturation = 5;
    bool isFlying = false;
    int selectedSlot = 0;

    struct Slot {
        std::string item;   // NamespacedId, e.g. "minecraft:stone"
        uint16_t count = 0;
        uint16_t durability = 0;
    };
    std::vector<Slot> inventory;  // Only non-empty slots
};

class PlayerSerializer {
public:
    // Serialize player state to JSON.
    [[nodiscard]] static nlohmann::json serialize(const PlayerData& data);

    // Deserialize player state from JSON. Returns false on error.
    [[nodiscard]] static bool deserialize(const nlohmann::json& j, PlayerData& out);

    // Save player data to file (atomic write).
    static void saveToFile(const std::string& path, const PlayerData& data);

    // Load player data from file. Returns false if file doesn't exist or is corrupt.
    [[nodiscard]] static bool loadFromFile(const std::string& path, PlayerData& out);
};

} // namespace save

#endif // MECRAFT_PLAYER_SERIALIZER_H
