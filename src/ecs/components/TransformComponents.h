#ifndef MECRAFT_ECS_TRANSFORM_COMPONENTS_H
#define MECRAFT_ECS_TRANSFORM_COMPONENTS_H

#include <vector>

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace ecs {

struct TransformComponent {
    glm::vec3 position{0.0f};
    float eyeHeight = 1.62f;
};

struct ParentComponent {
    entt::entity parent = entt::null;
};

struct ChildrenComponent {
    std::vector<entt::entity> children;
};

struct LocalTransformComponent {
    glm::vec3 localPosition{0.0f};
    glm::vec3 localRotation{0.0f}; // Euler angles in degrees
    glm::vec3 localScale{1.0f};

    [[nodiscard]] glm::mat4 toMatrix() const {
        glm::mat4 M(1.0f);
        M = glm::translate(M, localPosition);
        M = glm::rotate(M, glm::radians(localRotation.z), glm::vec3(0, 0, 1));
        M = glm::rotate(M, glm::radians(localRotation.y), glm::vec3(0, 1, 0));
        M = glm::rotate(M, glm::radians(localRotation.x), glm::vec3(1, 0, 0));
        M = glm::scale(M, localScale);
        return M;
    }
};

struct WorldTransformComponent {
    glm::mat4 worldMatrix{1.0f};
};

} // namespace ecs

#endif // MECRAFT_ECS_TRANSFORM_COMPONENTS_H
