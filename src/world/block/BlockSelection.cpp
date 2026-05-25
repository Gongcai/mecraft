#include "BlockSelection.h"

#include "PropIndices.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>

namespace {

constexpr float kPixel = 1.0f / 16.0f;
constexpr float kTorchCoreMin = 7.0f * kPixel;
constexpr float kTorchCoreMax = 9.0f * kPixel;
constexpr float kTorchCoreTop = 10.0f * kPixel;
constexpr float kCrossInset = 0.1464f;

void expand(BlockSelectionBox& box, const glm::vec3& point) {
    box.min.x = std::min(box.min.x, point.x);
    box.min.y = std::min(box.min.y, point.y);
    box.min.z = std::min(box.min.z, point.z);
    box.max.x = std::max(box.max.x, point.x);
    box.max.y = std::max(box.max.y, point.y);
    box.max.z = std::max(box.max.z, point.z);
}

glm::mat4 makeRotation(const float angleDegrees, const glm::vec3& axis, const glm::vec3& origin) {
    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, origin);
    transform = glm::rotate(transform, glm::radians(angleDegrees), axis);
    transform = glm::translate(transform, -origin);
    return transform;
}

glm::mat4 buildWallTorchTransform(const uint16_t facingValue) {
    const glm::mat4 tilt = makeRotation(-22.5f,
                                        glm::vec3(0.0f, 0.0f, 1.0f),
                                        glm::vec3(0.0f, 3.5f * kPixel, 8.0f * kPixel));

    float yDegrees = 0.0f;
    if (facingValue == PropIndices::FACING_NORTH) {
        yDegrees = 90.0f;
    } else if (facingValue == PropIndices::FACING_SOUTH) {
        yDegrees = -90.0f;
    } else if (facingValue == PropIndices::FACING_WEST) {
        yDegrees = 180.0f;
    } else if (facingValue == PropIndices::FACING_EAST) {
        yDegrees = 0.0f;
    }

    const glm::mat4 yaw = makeRotation(yDegrees,
                                       glm::vec3(0.0f, 1.0f, 0.0f),
                                       glm::vec3(0.5f, 0.5f, 0.5f));
    return yaw * tilt;
}

BlockSelectionBox makeTransformedBox(const glm::vec3& from,
                                     const glm::vec3& to,
                                     const glm::mat4& transform = glm::mat4(1.0f)) {
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

    BlockSelectionBox box;
    box.min = glm::vec3(transform * glm::vec4(corners[0], 1.0f));
    box.max = box.min;
    for (size_t i = 1; i < corners.size(); ++i) {
        expand(box, glm::vec3(transform * glm::vec4(corners[i], 1.0f)));
    }

    box.min = glm::clamp(box.min, glm::vec3(0.0f), glm::vec3(1.0f));
    box.max = glm::clamp(box.max, glm::vec3(0.0f), glm::vec3(1.0f));
    return box;
}

BlockSelectionBox getTorchBox(const StateID stateId) {
    uint16_t facingValue = PropIndices::FACING_FLOOR;
    if (PropIndices::FACING != PropIndices::INVALID) {
        const uint16_t value = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::FACING);
        if (value != BlockStateRegistry::INVALID_INDEX) {
            facingValue = value;
        }
    }

    if (facingValue == PropIndices::FACING_FLOOR) {
        return {glm::vec3(kTorchCoreMin, 0.0f, kTorchCoreMin),
                glm::vec3(kTorchCoreMax, kTorchCoreTop, kTorchCoreMax)};
    }

    return makeTransformedBox(
        glm::vec3(-1.0f * kPixel, 3.5f * kPixel, 7.0f * kPixel),
        glm::vec3( 1.0f * kPixel, 13.5f * kPixel, 9.0f * kPixel),
        buildWallTorchTransform(facingValue));
}

} // namespace

BlockSelectionBox BlockSelection::getBox(const StateID stateId) {
    const BlockDef& def = BlockRegistry::getFast(stateId);

    if (def.renderShapeName == "torch") {
        return getTorchBox(stateId);
    }
    if (def.renderShape == BlockRenderShape::Cross) {
        return {glm::vec3(kCrossInset, 0.0f, kCrossInset),
                glm::vec3(1.0f - kCrossInset, 1.0f, 1.0f - kCrossInset)};
    }

    return {};
}
