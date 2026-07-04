#include "EntityModelRegistry.h"

#include "../../Paths.h"

#include <fstream>
#include <string>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace ecs {
namespace {

using json = nlohmann::json;

void setError(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
}

std::string describeModel(const std::size_t index) {
    return "models[" + std::to_string(index) + "]";
}

bool readString(const json& node,
                const char* key,
                std::string& out,
                const std::string& context,
                std::string* error) {
    const auto it = node.find(key);
    if (it == node.end()) {
        setError(error, context + "." + key + " is required");
        return false;
    }
    if (!it->is_string()) {
        setError(error, context + "." + key + " must be a string");
        return false;
    }
    out = it->get<std::string>();
    return true;
}

bool readFloat(const json& node,
               const char* key,
               float& out,
               const std::string& context,
               std::string* error) {
    const auto it = node.find(key);
    if (it == node.end()) {
        setError(error, context + "." + key + " is required");
        return false;
    }
    if (!it->is_number()) {
        setError(error, context + "." + key + " must be a number");
        return false;
    }
    out = it->get<float>();
    return true;
}

bool readOptionalFloat(const json& node,
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

bool readVec2(const json& node,
              const char* key,
              glm::vec2& out,
              const std::string& context,
              std::string* error) {
    const auto it = node.find(key);
    if (it == node.end()) {
        setError(error, context + "." + key + " is required");
        return false;
    }
    if (!it->is_array() || it->size() != 2) {
        setError(error, context + "." + key + " must be a 2-number array");
        return false;
    }
    for (std::size_t i = 0; i < 2; ++i) {
        if (!(*it)[i].is_number()) {
            setError(error, context + "." + key + " must contain only numbers");
            return false;
        }
    }
    out = glm::vec2((*it)[0].get<float>(), (*it)[1].get<float>());
    return true;
}

bool readVec3(const json& node,
              const char* key,
              glm::vec3& out,
              const std::string& context,
              std::string* error) {
    const auto it = node.find(key);
    if (it == node.end()) {
        setError(error, context + "." + key + " is required");
        return false;
    }
    if (!it->is_array() || it->size() != 3) {
        setError(error, context + "." + key + " must be a 3-number array");
        return false;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        if (!(*it)[i].is_number()) {
            setError(error, context + "." + key + " must contain only numbers");
            return false;
        }
    }
    out = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
    return true;
}

bool readOptionalVec3(const json& node,
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
    for (std::size_t i = 0; i < 3; ++i) {
        if (!(*it)[i].is_number()) {
            setError(error, context + "." + key + " must contain only numbers");
            return false;
        }
    }
    out = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
    return true;
}

std::array<EntityModelPixelRect, 6> buildCubeUvLayout(const glm::vec2& uv,
                                                       const glm::vec3& size) {
    const float u = uv.x;
    const float v = uv.y;
    const float width = size.x;
    const float height = size.y;
    const float depth = size.z;

    return {{
        {u + depth, v, u + depth + width, v + depth},
        {u + depth + width, v, u + depth + width + width, v + depth},
        {u + depth, v + depth, u + depth + width, v + depth + height},
        {u + depth + width + depth, v + depth, u + depth + width + depth + width, v + depth + height},
        {u, v + depth, u + depth, v + depth + height},
        {u + depth + width, v + depth, u + depth + width + depth, v + depth + height}
    }};
}

bool parseBox(const json& node,
              EntityModelBoxDefinition& box,
              const std::string& context,
              std::string* error) {
    if (!node.is_object()) {
        setError(error, context + " must be an object");
        return false;
    }

    glm::vec2 uv{0.0f};
    if (!readVec3(node, "origin", box.origin, context, error) ||
        !readVec3(node, "size", box.size, context, error) ||
        !readVec2(node, "uv", uv, context, error) ||
        !readOptionalFloat(node, "inflate", box.inflate, context, error)) {
        return false;
    }

    glm::vec3 uvSize = box.size;
    if (!readOptionalVec3(node, "uvSize", uvSize, context, error)) {
        return false;
    }

    if (box.size.x <= 0.0f || box.size.y <= 0.0f || box.size.z <= 0.0f) {
        setError(error, context + ".size values must be positive");
        return false;
    }
    if (uvSize.x <= 0.0f || uvSize.y <= 0.0f || uvSize.z <= 0.0f) {
        setError(error, context + ".uvSize values must be positive");
        return false;
    }
    if (box.size.x + box.inflate * 2.0f <= 0.0f ||
        box.size.y + box.inflate * 2.0f <= 0.0f ||
        box.size.z + box.inflate * 2.0f <= 0.0f) {
        setError(error, context + ".inflate collapses the box geometry");
        return false;
    }

    box.faceUvs = buildCubeUvLayout(uv, uvSize);
    return true;
}

bool parsePart(const json& node,
               EntityModelPartDefinition& part,
               const std::unordered_set<std::string>& knownPartNames,
               const std::string& context,
               std::string* error) {
    if (!node.is_object()) {
        setError(error, context + " must be an object");
        return false;
    }

    if (!readString(node, "name", part.name, context, error) ||
        !readString(node, "parent", part.parent, context, error) ||
        !readVec3(node, "pivot", part.pivot, context, error) ||
        !readVec3(node, "rotation", part.rotation, context, error)) {
        return false;
    }

    if (part.name.empty()) {
        setError(error, context + ".name must not be empty");
        return false;
    }
    if (!part.parent.empty() && knownPartNames.find(part.parent) == knownPartNames.end()) {
        setError(error, context + ".parent must reference an earlier part: " + part.parent);
        return false;
    }

    const auto boxesIt = node.find("boxes");
    if (boxesIt == node.end() || !boxesIt->is_array()) {
        setError(error, context + ".boxes must be an array");
        return false;
    }

    part.boxes.clear();
    part.boxes.reserve(boxesIt->size());
    for (std::size_t i = 0; i < boxesIt->size(); ++i) {
        EntityModelBoxDefinition box;
        const std::string boxContext = context + ".boxes[" + std::to_string(i) + "]";
        if (!parseBox((*boxesIt)[i], box, boxContext, error)) {
            return false;
        }
        part.boxes.push_back(box);
    }

    return true;
}

bool parseModel(const json& node,
                const std::size_t index,
                EntityModelDefinition& model,
                std::string* error) {
    const std::string context = describeModel(index);
    if (!node.is_object()) {
        setError(error, context + " must be an object");
        return false;
    }

    std::string id;
    if (!readString(node, "id", id, context, error) ||
        !readFloat(node, "textureWidth", model.textureWidth, context, error) ||
        !readFloat(node, "textureHeight", model.textureHeight, context, error) ||
        !readString(node, "animation", model.animationId, context, error) ||
        !readString(node, "yawPart", model.yawPartName, context, error)) {
        return false;
    }
    model.id = NamespacedId(id);
    if (model.textureWidth <= 0.0f || model.textureHeight <= 0.0f) {
        setError(error, context + " texture dimensions must be positive");
        return false;
    }

    const auto partsIt = node.find("parts");
    if (partsIt == node.end() || !partsIt->is_array() || partsIt->empty()) {
        setError(error, context + ".parts must be a non-empty array");
        return false;
    }

    model.parts.clear();
    model.parts.reserve(partsIt->size());
    std::unordered_set<std::string> knownPartNames;
    for (std::size_t i = 0; i < partsIt->size(); ++i) {
        EntityModelPartDefinition part;
        const std::string partContext = context + ".parts[" + std::to_string(i) + "]";
        if (!parsePart((*partsIt)[i], part, knownPartNames, partContext, error)) {
            return false;
        }
        if (knownPartNames.find(part.name) != knownPartNames.end()) {
            setError(error, partContext + ".name is duplicated: " + part.name);
            return false;
        }
        knownPartNames.insert(part.name);
        model.parts.push_back(std::move(part));
    }

    if (!model.yawPartName.empty() && knownPartNames.find(model.yawPartName) == knownPartNames.end()) {
        setError(error, context + ".yawPart references an unknown part: " + model.yawPartName);
        return false;
    }

    return true;
}

} // namespace

const EntityModelPartDefinition* EntityModelDefinition::findPart(const std::string_view name) const {
    for (const EntityModelPartDefinition& part : parts) {
        if (part.name == name) {
            return &part;
        }
    }
    return nullptr;
}

EntityModelRegistry& EntityModelRegistry::instance() {
    static EntityModelRegistry registry;
    return registry;
}

bool EntityModelRegistry::ensureLoaded(std::string* error) {
    if (m_loaded) {
        return true;
    }
    return loadFromFile(ENTITY_MODELS_CONFIG_PATH, error);
}

bool EntityModelRegistry::loadFromFile(const std::string& path, std::string* error) {
    std::ifstream file(path);
    if (!file.is_open()) {
        setError(error, "failed to open entity model definitions: " + path);
        return false;
    }

    json root = json::parse(file, nullptr, false);
    if (root.is_discarded()) {
        setError(error, "failed to parse entity model definitions: invalid JSON");
        return false;
    }

    if (!root.is_object() || !root.contains("models") || !root["models"].is_array()) {
        setError(error, "entity model definitions must contain a 'models' array");
        return false;
    }

    std::unordered_map<NamespacedId, EntityModelDefinition> parsedModels;
    const json& models = root["models"];
    for (std::size_t i = 0; i < models.size(); ++i) {
        EntityModelDefinition model;
        if (!parseModel(models[i], i, model, error)) {
            return false;
        }
        if (parsedModels.find(model.id) != parsedModels.end()) {
            setError(error, describeModel(i) + ".id is duplicated: " + model.id.full());
            return false;
        }
        parsedModels.emplace(model.id, std::move(model));
    }

    m_models = std::move(parsedModels);
    m_loaded = true;
    return true;
}

void EntityModelRegistry::clear() {
    m_models.clear();
    m_loaded = false;
}

const EntityModelDefinition* EntityModelRegistry::findModel(const std::string_view id) const {
    return findModel(NamespacedId(id));
}

const EntityModelDefinition* EntityModelRegistry::findModel(const NamespacedId& id) const {
    const auto it = m_models.find(id);
    return it == m_models.end() ? nullptr : &it->second;
}

} // namespace ecs
