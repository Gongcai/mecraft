#include "ProjectileDefinitions.h"

#include "../../Paths.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace ecs {
namespace {

using json = nlohmann::json;

struct ProjectileDefinitionStore {
    bool loaded = false;
    std::unordered_map<ItemID, ProjectileDefinition> byItem;
};

ProjectileDefinitionStore& store() {
    static ProjectileDefinitionStore instance;
    return instance;
}

void setError(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
}

std::string describeEntry(const std::size_t index) {
    return "projectiles[" + std::to_string(index) + "]";
}

bool readString(const json& node,
                const char* key,
                std::string& out,
                const std::string& context,
                std::string* error,
                const bool required = false) {
    const auto it = node.find(key);
    if (it == node.end()) {
        if (required) {
            setError(error, context + "." + key + " is required");
            return false;
        }
        return true;
    }
    if (!it->is_string()) {
        setError(error, context + "." + key + " must be a string");
        return false;
    }
    out = it->get<std::string>();
    return true;
}

bool readInt(const json& node,
             const char* key,
             int& out,
             const std::string& context,
             std::string* error) {
    const auto it = node.find(key);
    if (it == node.end()) {
        return true;
    }
    if (!it->is_number_integer()) {
        setError(error, context + "." + key + " must be an integer");
        return false;
    }
    out = it->get<int>();
    return true;
}

bool readFloat(const json& node,
               const char* key,
               float& out,
               const std::string& context,
               std::string* error) {
    const auto it = node.find(key);
    if (it == node.end()) {
        return true;
    }
    if (!it->is_number()) {
        setError(error, context + "." + key + " must be a number");
        return false;
    }
    out = it->get<float>();
    return true;
}

bool parseImpact(const json& node,
                 ProjectileDefinition& definition,
                 const std::string& context,
                 std::string* error) {
    const auto it = node.find("impact");
    if (it == node.end()) {
        return true;
    }
    if (!it->is_object()) {
        setError(error, context + ".impact must be an object");
        return false;
    }

    std::string particleBlockName;
    if (!readString(*it, "particleBlock", particleBlockName, context + ".impact", error) ||
        !readInt(*it, "particleCount", definition.entityImpactParticleCount, context + ".impact", error) ||
        !readString(*it, "sound", definition.impactSoundId, context + ".impact", error)) {
        return false;
    }

    if (!particleBlockName.empty()) {
        if (!BlockRegistry::tryGetIdByName(particleBlockName, definition.entityImpactParticleBlock) ||
            definition.entityImpactParticleBlock == 0) {
            setError(error, context + ".impact.particleBlock references an unknown block: " +
                                particleBlockName);
            return false;
        }
    }
    if (definition.entityImpactParticleBlock != 0 && definition.entityImpactParticleCount <= 0) {
        setError(error, context + ".impact.particleCount must be positive when particleBlock is set");
        return false;
    }

    return true;
}

bool parseProjectileDefinition(const json& node,
                               const std::size_t index,
                               ProjectileDefinition& definition,
                               std::string* error) {
    const std::string context = describeEntry(index);
    if (!node.is_object()) {
        setError(error, context + " must be an object");
        return false;
    }

    std::string itemName;
    if (!readString(node, "item", itemName, context, error, true)) {
        return false;
    }

    if (!ItemRegistry::tryGetIdByName(itemName, definition.itemId) || definition.itemId == 0) {
        setError(error, context + ".item references an unknown item: " + itemName);
        return false;
    }

    definition.entityImpactParticleBlock = defaultProjectileEntityImpactParticleBlock();
    if (!readInt(node, "damage", definition.damage, context, error) ||
        !readFloat(node, "hitRadius", definition.hitRadius, context, error) ||
        !readFloat(node, "gravity", definition.gravity, context, error) ||
        !readFloat(node, "throwSpeed", definition.throwSpeed, context, error) ||
        !readFloat(node, "upwardBias", definition.upwardBias, context, error) ||
        !readFloat(node, "spawnForwardOffset", definition.spawnForwardOffset, context, error) ||
        !readFloat(node, "boundsHalfExtent", definition.boundsHalfExtent, context, error) ||
        !readFloat(node, "lifetimeSeconds", definition.lifetimeSeconds, context, error) ||
        !readFloat(node, "spinSpeedRadians", definition.spinSpeedRadians, context, error) ||
        !readString(node, "throwSound", definition.throwSoundId, context, error) ||
        !parseImpact(node, definition, context, error)) {
        return false;
    }

    if (definition.damage <= 0) {
        setError(error, context + ".damage must be positive");
        return false;
    }
    if (definition.hitRadius <= 0.0f ||
        definition.throwSpeed <= 0.0f ||
        definition.spawnForwardOffset < 0.0f ||
        definition.boundsHalfExtent <= 0.0f ||
        definition.lifetimeSeconds <= 0.0f) {
        setError(error, context + " projectile distances and lifetime must be positive");
        return false;
    }
    if (definition.gravity < 0.0f) {
        setError(error, context + ".gravity must be non-negative");
        return false;
    }

    return true;
}

bool loadProjectileDefinitionsFromFile(const std::string& path, std::string* error) {
    ItemRegistry::init();

    std::ifstream file(path);
    if (!file.is_open()) {
        setError(error, "failed to open projectile definitions: " + path);
        return false;
    }

    json root;
    try {
        file >> root;
    } catch (const std::exception& ex) {
        setError(error, std::string("failed to parse projectile definitions: ") + ex.what());
        return false;
    }

    if (!root.is_object() || !root.contains("projectiles") || !root["projectiles"].is_array()) {
        setError(error, "projectile definitions must contain a 'projectiles' array");
        return false;
    }

    std::unordered_map<ItemID, ProjectileDefinition> parsed;
    const json& projectiles = root["projectiles"];
    for (std::size_t i = 0; i < projectiles.size(); ++i) {
        ProjectileDefinition definition;
        if (!parseProjectileDefinition(projectiles[i], i, definition, error)) {
            return false;
        }
        if (parsed.find(definition.itemId) != parsed.end()) {
            setError(error, describeEntry(i) + ".item is duplicated");
            return false;
        }
        parsed.emplace(definition.itemId, std::move(definition));
    }

    auto& definitions = store();
    definitions.byItem = std::move(parsed);
    definitions.loaded = true;
    return true;
}

bool ensureProjectileDefinitionsLoaded(std::string* error = nullptr) {
    auto& definitions = store();
    if (definitions.loaded) {
        return true;
    }
    return loadProjectileDefinitionsFromFile(PROJECTILES_CONFIG_PATH, error);
}

ProjectileDefinition defaultDefinitionForItem(const ItemID itemId) {
    ProjectileDefinition definition;
    definition.itemId = itemId;
    definition.entityImpactParticleBlock = defaultProjectileEntityImpactParticleBlock();
    return definition;
}

} // namespace

BlockID defaultProjectileEntityImpactParticleBlock() {
    return BlockRegistry::requireIdByName("minecraft:rose");
}

ProjectileDefinition makeAppleProjectileDefinition() {
    static const ItemID appleItem = ItemRegistry::requireIdByName("minecraft:apple");
    ProjectileDefinition definition;
    if (getThrowableProjectileDefinition(appleItem, definition)) {
        return definition;
    }
    return defaultDefinitionForItem(appleItem);
}

bool getThrowableProjectileDefinition(const ItemID itemId, ProjectileDefinition& outDefinition) {
    if (itemId == 0) {
        return false;
    }

    std::string error;
    if (!ensureProjectileDefinitionsLoaded(&error)) {
        return false;
    }

    const auto& definitions = store().byItem;
    const auto it = definitions.find(itemId);
    if (it == definitions.end()) {
        return false;
    }

    outDefinition = it->second;
    return true;
}

ProjectileDefinition projectileDefinitionForItemOrDefault(const ItemID itemId) {
    ProjectileDefinition definition;
    if (getThrowableProjectileDefinition(itemId, definition)) {
        return definition;
    }
    return defaultDefinitionForItem(itemId);
}

} // namespace ecs
