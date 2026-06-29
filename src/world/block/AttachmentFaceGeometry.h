#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>

#include <glm/vec3.hpp>

#include "PropIndices.h"

namespace AttachmentFaceGeometry {

/// Returns true when `face` identifies a block face that can host an attached model.
inline bool isAttachmentFace(const uint16_t face) {
    if (face == PropIndices::INVALID) {
        return false;
    }
    return face == PropIndices::FACE_FLOOR ||
           face == PropIndices::FACE_CEILING ||
           face == PropIndices::FACE_NORTH ||
           face == PropIndices::FACE_SOUTH ||
           face == PropIndices::FACE_EAST ||
           face == PropIndices::FACE_WEST;
}

/// Returns each attachment face supported by face-oriented devices.
inline std::array<uint16_t, 6> attachmentFaces() {
    return {{
        PropIndices::FACE_FLOOR,
        PropIndices::FACE_CEILING,
        PropIndices::FACE_NORTH,
        PropIndices::FACE_SOUTH,
        PropIndices::FACE_EAST,
        PropIndices::FACE_WEST,
    }};
}

/// Returns the outward normal from the supporting block face into the attached block cell.
inline glm::ivec3 surfaceNormal(const uint16_t face) {
    if (face == PropIndices::FACE_FLOOR) {
        return {0, 1, 0};
    }
    if (face == PropIndices::FACE_CEILING) {
        return {0, -1, 0};
    }
    if (face == PropIndices::FACE_NORTH) {
        return {0, 0, -1};
    }
    if (face == PropIndices::FACE_SOUTH) {
        return {0, 0, 1};
    }
    if (face == PropIndices::FACE_EAST) {
        return {1, 0, 0};
    }
    if (face == PropIndices::FACE_WEST) {
        return {-1, 0, 0};
    }
    throw std::runtime_error("Attachment face geometry received an unsupported face value");
}

/// Resolves the attachment face represented by an outward support-face normal.
inline uint16_t faceFromSurfaceNormal(const glm::ivec3& normal) {
    if (normal == glm::ivec3(0, 1, 0)) {
        return PropIndices::FACE_FLOOR;
    }
    if (normal == glm::ivec3(0, -1, 0)) {
        return PropIndices::FACE_CEILING;
    }
    if (normal == glm::ivec3(0, 0, -1)) {
        return PropIndices::FACE_NORTH;
    }
    if (normal == glm::ivec3(0, 0, 1)) {
        return PropIndices::FACE_SOUTH;
    }
    if (normal == glm::ivec3(1, 0, 0)) {
        return PropIndices::FACE_EAST;
    }
    if (normal == glm::ivec3(-1, 0, 0)) {
        return PropIndices::FACE_WEST;
    }
    throw std::runtime_error("Attachment face geometry received an unsupported surface normal");
}

/// Returns the relative position of the solid block that supports this face.
inline glm::ivec3 supportOffset(const uint16_t face) {
    return -surfaceNormal(face);
}

/// Converts a face property value to the equivalent facing property value used by face-plane blocks.
inline uint16_t facingValueForFace(const uint16_t face) {
    if (face == PropIndices::FACE_FLOOR) {
        return PropIndices::FACING_FLOOR;
    }
    if (face == PropIndices::FACE_CEILING) {
        return PropIndices::FACING_CEILING;
    }
    if (face == PropIndices::FACE_NORTH) {
        return PropIndices::FACING_NORTH;
    }
    if (face == PropIndices::FACE_SOUTH) {
        return PropIndices::FACING_SOUTH;
    }
    if (face == PropIndices::FACE_EAST) {
        return PropIndices::FACING_EAST;
    }
    if (face == PropIndices::FACE_WEST) {
        return PropIndices::FACING_WEST;
    }
    throw std::runtime_error("Attachment face geometry received an unsupported face value");
}

/// Returns the world-space direction represented by a six-way facing value.
inline glm::ivec3 directionFromFacing(const uint16_t facing) {
    if (facing == PropIndices::FACING_EAST) {
        return {1, 0, 0};
    }
    if (facing == PropIndices::FACING_WEST) {
        return {-1, 0, 0};
    }
    if (facing == PropIndices::FACING_SOUTH) {
        return {0, 0, 1};
    }
    if (facing == PropIndices::FACING_NORTH) {
        return {0, 0, -1};
    }
    if (facing == PropIndices::FACING_UP) {
        return {0, 1, 0};
    }
    if (facing == PropIndices::FACING_DOWN) {
        return {0, -1, 0};
    }
    throw std::runtime_error("Attachment face geometry received an unsupported facing value");
}

/// Returns true when `direction` lies in the plane hosted by `face`.
inline bool isDirectionInPlane(const uint16_t face, const glm::ivec3& direction) {
    const glm::ivec3 normal = surfaceNormal(face);
    return normal.x * direction.x + normal.y * direction.y + normal.z * direction.z == 0;
}

} // namespace AttachmentFaceGeometry
