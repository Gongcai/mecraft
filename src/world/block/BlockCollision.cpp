#include "BlockCollision.h"

#include <algorithm>
#include <array>
#include <stdexcept>

#include <glm/common.hpp>

#include "Block.h"

namespace {

void expand(BlockCollisionBox& box, const glm::vec3& point) {
    box.min = glm::min(box.min, point);
    box.max = glm::max(box.max, point);
}

glm::vec3 rotateModelPointX90(const glm::vec3& point, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {point.x, 1.0f - point.z, point.y};
        case 2: return {point.x, 1.0f - point.y, 1.0f - point.z};
        case 3: return {point.x, point.z, 1.0f - point.y};
        case 0:
        default: return point;
    }
}

glm::vec3 rotateModelPointY90(const glm::vec3& point, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {1.0f - point.z, point.y, point.x};
        case 2: return {1.0f - point.x, point.y, 1.0f - point.z};
        case 3: return {point.z, point.y, 1.0f - point.x};
        case 0:
        default: return point;
    }
}

glm::vec3 rotateModelPointZ90(const glm::vec3& point, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {1.0f - point.y, point.x, point.z};
        case 2: return {1.0f - point.x, 1.0f - point.y, point.z};
        case 3: return {point.y, 1.0f - point.x, point.z};
        case 0:
        default: return point;
    }
}

glm::vec3 applyModelCollisionTransform(glm::vec3 point, const ModelTransform& transform) {
    point = rotateModelPointX90(point, transform.rotX);
    point = rotateModelPointY90(point, transform.rotY);
    point = rotateModelPointZ90(point, transform.rotZ);
    return point;
}

BlockCollisionBox makeElementBox(const ModelElement& element, const ModelTransform& transform) {
    const glm::vec3 from(element.from[0] / 16.0f, element.from[1] / 16.0f, element.from[2] / 16.0f);
    const glm::vec3 to(element.to[0] / 16.0f, element.to[1] / 16.0f, element.to[2] / 16.0f);
    const std::array<glm::vec3, 8> corners = {{
        {from.x, from.y, from.z},
        {to.x, from.y, from.z},
        {from.x, to.y, from.z},
        {to.x, to.y, from.z},
        {from.x, from.y, to.z},
        {to.x, from.y, to.z},
        {from.x, to.y, to.z},
        {to.x, to.y, to.z},
    }};

    BlockCollisionBox box;
    bool hasPoint = false;
    for (const glm::vec3& corner : corners) {
        const glm::vec3 transformed = applyModelCollisionTransform(corner, transform);
        if (!hasPoint) {
            box.min = transformed;
            box.max = transformed;
            hasPoint = true;
        } else {
            expand(box, transformed);
        }
    }

    box.min = glm::clamp(box.min, glm::vec3(0.0f), glm::vec3(1.0f));
    box.max = glm::clamp(box.max, glm::vec3(0.0f), glm::vec3(1.0f));
    if (box.min.x >= box.max.x || box.min.y >= box.max.y || box.min.z >= box.max.z) {
        throw std::runtime_error("Model collision element has an empty box");
    }
    return box;
}

bool aabbIntersects(const glm::vec3& aMin,
                    const glm::vec3& aMax,
                    const glm::vec3& bMin,
                    const glm::vec3& bMax) {
    return aMin.x < bMax.x && aMax.x > bMin.x &&
           aMin.y < bMax.y && aMax.y > bMin.y &&
           aMin.z < bMax.z && aMax.z > bMin.z;
}

bool pointInside(const glm::vec3& point, const glm::vec3& boxMin, const glm::vec3& boxMax) {
    return point.x >= boxMin.x && point.x <= boxMax.x &&
           point.y >= boxMin.y && point.y <= boxMax.y &&
           point.z >= boxMin.z && point.z <= boxMax.z;
}

const ModelVariant& requireModelVariant(const StateID stateId) {
    const ModelVariant* variant = BlockStateRegistry::getModelVariant(stateId);
    if (variant == nullptr || variant->model == nullptr) {
        throw std::runtime_error("Model collision requires a registered model variant");
    }
    if (variant->model->elements.empty()) {
        throw std::runtime_error("Model collision requires at least one element");
    }
    return *variant;
}

} // namespace

namespace BlockCollision {

std::vector<BlockCollisionBox> getBoxes(const StateID stateId) {
    std::vector<BlockCollisionBox> boxes;
    if (stateId == BlockIds::AIR) {
        return boxes;
    }

    const BlockDef& def = BlockRegistry::getFast(stateId);
    if (def.renderShapeName == "face_plane" || def.renderShapeName == "redstone_wire") {
        return boxes;
    }
    if (def.renderShapeName == "model") {
        const ModelVariant& variant = requireModelVariant(stateId);

        boxes.reserve(variant.model->elements.size());
        for (const ModelElement& element : variant.model->elements) {
            boxes.push_back(makeElementBox(element, variant.transform));
        }
        return boxes;
    }

    if (def.isSolid) {
        boxes.push_back({});
    }
    return boxes;
}

bool intersects(const StateID stateId,
                const glm::ivec3& blockPos,
                const glm::vec3& queryMin,
                const glm::vec3& queryMax) {
    const glm::vec3 blockOffset(blockPos);

    if (stateId == BlockIds::AIR) {
        return false;
    }

    const BlockDef& def = BlockRegistry::getFast(stateId);
    if (def.renderShapeName == "face_plane" || def.renderShapeName == "redstone_wire") {
        return false;
    }
    if (def.renderShapeName == "model") {
        const ModelVariant& variant = requireModelVariant(stateId);
        for (const ModelElement& element : variant.model->elements) {
            const BlockCollisionBox box = makeElementBox(element, variant.transform);
            if (aabbIntersects(queryMin, queryMax, blockOffset + box.min, blockOffset + box.max)) {
                return true;
            }
        }
        return false;
    }

    if (def.isSolid) {
        if (aabbIntersects(queryMin, queryMax, blockOffset, blockOffset + glm::vec3(1.0f))) {
            return true;
        }
    }
    return false;
}

bool containsPoint(const StateID stateId,
                   const glm::ivec3& blockPos,
                   const glm::vec3& point) {
    const glm::vec3 blockOffset(blockPos);

    if (stateId == BlockIds::AIR) {
        return false;
    }

    const BlockDef& def = BlockRegistry::getFast(stateId);
    if (def.renderShapeName == "face_plane" || def.renderShapeName == "redstone_wire") {
        return false;
    }
    if (def.renderShapeName == "model") {
        const ModelVariant& variant = requireModelVariant(stateId);
        for (const ModelElement& element : variant.model->elements) {
            const BlockCollisionBox box = makeElementBox(element, variant.transform);
            if (pointInside(point, blockOffset + box.min, blockOffset + box.max)) {
                return true;
            }
        }
        return false;
    }

    if (def.isSolid) {
        if (pointInside(point, blockOffset, blockOffset + glm::vec3(1.0f))) {
            return true;
        }
    }
    return false;
}

} // namespace BlockCollision
