#ifndef MECRAFT_ECS_ENTITY_MODEL_REGISTRY_H
#define MECRAFT_ECS_ENTITY_MODEL_REGISTRY_H

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "../../engine/registry/NamespacedId.h"

namespace ecs {

struct EntityModelPixelRect {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};

struct EntityModelBoxDefinition {
    glm::vec3 origin{0.0f};
    glm::vec3 size{0.0f};
    float inflate = 0.0f;
    std::array<EntityModelPixelRect, 6> faceUvs{};
};

struct EntityModelPartDefinition {
    std::string name;
    std::string parent;
    glm::vec3 pivot{0.0f};
    glm::vec3 rotation{0.0f};
    std::vector<EntityModelBoxDefinition> boxes;
};

struct EntityModelDefinition {
    NamespacedId id;
    float textureWidth = 0.0f;
    float textureHeight = 0.0f;
    std::string animationId;
    std::string yawPartName;
    std::vector<EntityModelPartDefinition> parts;

    [[nodiscard]] const EntityModelPartDefinition* findPart(std::string_view name) const;
};

class EntityModelRegistry {
public:
    static EntityModelRegistry& instance();

    bool ensureLoaded(std::string* error = nullptr);
    bool loadFromFile(const std::string& path, std::string* error = nullptr);
    void clear();

    [[nodiscard]] const EntityModelDefinition* findModel(std::string_view id) const;
    [[nodiscard]] const EntityModelDefinition* findModel(const NamespacedId& id) const;
    [[nodiscard]] std::size_t modelCount() const { return m_models.size(); }

private:
    bool m_loaded = false;
    std::unordered_map<NamespacedId, EntityModelDefinition> m_models;
};

} // namespace ecs

#endif // MECRAFT_ECS_ENTITY_MODEL_REGISTRY_H
