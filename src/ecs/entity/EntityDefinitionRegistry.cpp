#include "EntityDefinitionRegistry.h"

#include "../../Paths.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace ecs {
namespace {

using json = nlohmann::json;

void setError(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
}

std::string describeEntry(const std::size_t index) {
    return "entities[" + std::to_string(index) + "]";
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

bool readUInt(const json& node,
              const char* key,
              uint32_t& out,
              const std::string& context,
              std::string* error) {
    int value = static_cast<int>(out);
    if (!readInt(node, key, value, context, error)) {
        return false;
    }
    if (value < 0) {
        setError(error, context + "." + key + " must be non-negative");
        return false;
    }
    out = static_cast<uint32_t>(value);
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

bool readVec3(const json& node,
              const char* key,
              glm::vec3& out,
              const std::string& context,
              std::string* error) {
    const auto it = node.find(key);
    if (it == node.end()) {
        return true;
    }
    if (!it->is_array() || it->size() != 3) {
        setError(error, context + "." + key + " must be a 3-number array");
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (!(*it)[static_cast<std::size_t>(i)].is_number()) {
            setError(error, context + "." + key + " must contain only numbers");
            return false;
        }
    }
    out = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
    return true;
}

bool parseHealth(const json& node,
                 MobEntityDefinition& definition,
                 const std::string& context,
                 std::string* error) {
    const auto it = node.find("health");
    if (it == node.end()) {
        return true;
    }
    if (it->is_number_integer()) {
        definition.health = it->get<int>();
        definition.maxHealth = definition.health;
    } else if (it->is_object()) {
        if (!readInt(*it, "current", definition.health, context + ".health", error) ||
            !readInt(*it, "max", definition.maxHealth, context + ".health", error)) {
            return false;
        }
    } else {
        setError(error, context + ".health must be an integer or object");
        return false;
    }

    if (definition.maxHealth <= 0 || definition.health <= 0) {
        setError(error, context + ".health values must be positive");
        return false;
    }
    definition.health = std::min(definition.health, definition.maxHealth);
    return true;
}

bool parsePhysics(const json& node,
                  MobEntityDefinition& definition,
                  const std::string& context,
                  std::string* error) {
    const auto it = node.find("physics");
    if (it == node.end()) {
        return true;
    }
    if (!it->is_object()) {
        setError(error, context + ".physics must be an object");
        return false;
    }

    return readVec3(*it, "halfExtents", definition.physics.halfExtents, context + ".physics", error) &&
           readVec3(*it, "colliderOffset", definition.physics.colliderOffset, context + ".physics", error) &&
           readFloat(*it, "eyeOffsetY", definition.physics.eyeOffsetY, context + ".physics", error);
}

bool parseAI(const json& node,
             MobEntityDefinition& definition,
             const std::string& context,
             std::string* error) {
    const auto it = node.find("ai");
    if (it == node.end()) {
        return true;
    }
    if (!it->is_object()) {
        setError(error, context + ".ai must be an object");
        return false;
    }

    return readFloat(*it, "wanderInterval", definition.ai.wanderInterval, context + ".ai", error) &&
           readFloat(*it, "wanderSpeed", definition.ai.wanderSpeed, context + ".ai", error) &&
           readFloat(*it, "pursueSpeed", definition.ai.pursueSpeed, context + ".ai", error) &&
           readFloat(*it, "acquisitionRange", definition.ai.acquisitionRange, context + ".ai", error) &&
           readFloat(*it, "loseTargetRange", definition.ai.loseTargetRange, context + ".ai", error) &&
           readFloat(*it, "attackRange", definition.ai.attackRange, context + ".ai", error) &&
           readFloat(*it, "attackCooldownSeconds", definition.ai.attackCooldownSeconds, context + ".ai", error) &&
           readInt(*it, "attackDamage", definition.ai.attackDamage, context + ".ai", error);
}

bool parseDrops(const json& node,
                MobEntityDefinition& definition,
                const std::string& context,
                std::string* error) {
    const auto it = node.find("drops");
    if (it == node.end()) {
        return true;
    }
    if (!it->is_array()) {
        setError(error, context + ".drops must be an array");
        return false;
    }

    definition.drops.clear();
    for (std::size_t i = 0; i < it->size(); ++i) {
        const json& dropNode = (*it)[i];
        const std::string dropContext = context + ".drops[" + std::to_string(i) + "]";
        if (!dropNode.is_object()) {
            setError(error, dropContext + " must be an object");
            return false;
        }

        std::string itemName;
        if (!readString(dropNode, "item", itemName, dropContext, error, true)) {
            return false;
        }

        ItemID itemId = 0;
        if (!ItemRegistry::tryGetIdByName(itemName, itemId) || itemId == 0) {
            setError(error, dropContext + ".item references an unknown item: " + itemName);
            return false;
        }

        MobDropDefinition drop;
        drop.itemId = itemId;
        if (!readUInt(dropNode, "min", drop.minCount, dropContext, error) ||
            !readUInt(dropNode, "max", drop.maxCount, dropContext, error)) {
            return false;
        }
        if (drop.minCount == 0 || drop.maxCount == 0 || drop.maxCount < drop.minCount) {
            setError(error, dropContext + " must satisfy 0 < min <= max");
            return false;
        }
        definition.drops.push_back(drop);
    }

    return true;
}

bool parseDeathEffect(const json& node,
                      MobEntityDefinition& definition,
                      const std::string& context,
                      std::string* error) {
    const auto it = node.find("deathEffect");
    if (it == node.end()) {
        return true;
    }
    if (!it->is_object()) {
        setError(error, context + ".deathEffect must be an object");
        return false;
    }

    MobDeathEffectDefinition effect;
    effect.enabled = true;

    std::string particleBlockName;
    if (!readString(*it, "particleBlock", particleBlockName, context + ".deathEffect", error) ||
        !readInt(*it, "particleCount", effect.particleCount, context + ".deathEffect", error) ||
        !readString(*it, "sound", effect.soundId, context + ".deathEffect", error) ||
        !readFloat(*it, "volume", effect.volume, context + ".deathEffect", error)) {
        return false;
    }

    if (!particleBlockName.empty()) {
        if (!BlockRegistry::tryGetIdByName(particleBlockName, effect.particleBlock) ||
            effect.particleBlock == 0) {
            setError(error, context + ".deathEffect.particleBlock references an unknown block: " +
                                particleBlockName);
            return false;
        }
    }

    if (effect.particleBlock != 0 && effect.particleCount <= 0) {
        setError(error, context + ".deathEffect.particleCount must be positive when particleBlock is set");
        return false;
    }
    if (effect.volume < 0.0f) {
        setError(error, context + ".deathEffect.volume must be non-negative");
        return false;
    }

    definition.deathEffect = std::move(effect);
    return true;
}

bool parseHurtEffect(const json& node,
                     MobEntityDefinition& definition,
                     const std::string& context,
                     std::string* error) {
    const auto it = node.find("hurtEffect");
    if (it == node.end()) {
        return true;
    }
    if (!it->is_object()) {
        setError(error, context + ".hurtEffect must be an object");
        return false;
    }

    MobHurtEffectDefinition effect;
    effect.enabled = true;
    if (!readString(*it, "sound", effect.soundId, context + ".hurtEffect", error) ||
        !readFloat(*it, "volume", effect.volume, context + ".hurtEffect", error) ||
        !readFloat(*it, "flashDurationSeconds", effect.flashDurationSeconds, context + ".hurtEffect", error)) {
        return false;
    }

    if (effect.volume < 0.0f) {
        setError(error, context + ".hurtEffect.volume must be non-negative");
        return false;
    }
    if (effect.flashDurationSeconds <= 0.0f) {
        setError(error, context + ".hurtEffect.flashDurationSeconds must be positive");
        return false;
    }

    definition.hurtEffect = std::move(effect);
    return true;
}

bool parseMobDefinition(const json& node,
                        const std::size_t index,
                        MobEntityDefinition& definition,
                        std::string* error) {
    const std::string context = describeEntry(index);
    if (!node.is_object()) {
        setError(error, context + " must be an object");
        return false;
    }

    std::string id;
    if (!readString(node, "id", id, context, error, true) ||
        !readString(node, "model", definition.model, context, error, true)) {
        return false;
    }
    definition.id = NamespacedId(id);

    if (!readFloat(node, "eyeHeight", definition.eyeHeight, context, error) ||
        !parseHealth(node, definition, context, error) ||
        !parsePhysics(node, definition, context, error) ||
        !parseAI(node, definition, context, error) ||
        !parseDrops(node, definition, context, error) ||
        !parseHurtEffect(node, definition, context, error) ||
        !parseDeathEffect(node, definition, context, error)) {
        return false;
    }

    return true;
}

} // namespace

EntityDefinitionRegistry& EntityDefinitionRegistry::instance() {
    static EntityDefinitionRegistry registry;
    return registry;
}

bool EntityDefinitionRegistry::ensureLoaded(std::string* error) {
    if (m_loaded) {
        return true;
    }
    return loadFromFile(ENTITIES_CONFIG_PATH, error);
}

bool EntityDefinitionRegistry::loadFromFile(const std::string& path, std::string* error) {
    ItemRegistry::init();

    std::ifstream file(path);
    if (!file.is_open()) {
        setError(error, "failed to open entity definitions: " + path);
        return false;
    }

    json root;
    try {
        file >> root;
    } catch (const std::exception& ex) {
        setError(error, std::string("failed to parse entity definitions: ") + ex.what());
        return false;
    }

    if (!root.is_object() || !root.contains("entities") || !root["entities"].is_array()) {
        setError(error, "entity definitions must contain an 'entities' array");
        return false;
    }

    std::unordered_map<NamespacedId, MobEntityDefinition> parsedMobs;
    const json& entities = root["entities"];
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const json& entityNode = entities[i];
        const std::string context = describeEntry(i);
        if (!entityNode.is_object()) {
            setError(error, context + " must be an object");
            return false;
        }
        std::string kind = "mob";
        if (!readString(entityNode, "kind", kind, context, error)) {
            return false;
        }
        if (kind != "mob") {
            setError(error, context + ".kind is unsupported: " + kind);
            return false;
        }

        MobEntityDefinition definition;
        if (!parseMobDefinition(entityNode, i, definition, error)) {
            return false;
        }

        if (parsedMobs.find(definition.id) != parsedMobs.end()) {
            setError(error, context + ".id is duplicated: " + definition.id.full());
            return false;
        }
        parsedMobs.emplace(definition.id, std::move(definition));
    }

    m_mobs = std::move(parsedMobs);
    m_loaded = true;
    return true;
}

void EntityDefinitionRegistry::clear() {
    m_mobs.clear();
    m_loaded = false;
}

const MobEntityDefinition* EntityDefinitionRegistry::findMob(const std::string_view id) const {
    return findMob(NamespacedId(id));
}

const MobEntityDefinition* EntityDefinitionRegistry::findMob(const NamespacedId& id) const {
    const auto it = m_mobs.find(id);
    return it == m_mobs.end() ? nullptr : &it->second;
}

} // namespace ecs
