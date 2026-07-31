#pragma once

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

#include <glm/vec3.hpp>

#include "../block/PropIndices.h"

namespace WireFaceGeometry {

[[noreturn]] inline void failWireFaceGeometry(const char* message) {
    std::fputs(message, stderr);
    std::fputc('\n', stderr);
    std::abort();
}

struct ConnectionDirection {
    uint16_t property;
    uint16_t noneValue;
    uint16_t sideValue;
    glm::ivec3 offset;
};

/// Returns true when `facing` identifies a face that can host a redstone wire.
inline bool isWireFacing(const uint16_t facing) {
    if (facing == PropIndices::INVALID) {
        return false;
    }
    return facing == PropIndices::FACING_FLOOR || facing == PropIndices::FACING_CEILING ||
           facing == PropIndices::FACING_NORTH || facing == PropIndices::FACING_SOUTH ||
           facing == PropIndices::FACING_EAST || facing == PropIndices::FACING_WEST;
}

/// Returns every face that can host a redstone wire.
inline std::array<uint16_t, 6> wireFacings() {
    return {{
        PropIndices::FACING_FLOOR,
        PropIndices::FACING_CEILING,
        PropIndices::FACING_NORTH,
        PropIndices::FACING_SOUTH,
        PropIndices::FACING_EAST,
        PropIndices::FACING_WEST,
    }};
}

/// Returns the outward normal from the supporting block face into the wire cell.
inline glm::ivec3 surfaceNormal(const uint16_t facing) {
    if (facing == PropIndices::FACING_FLOOR) {
        return {0, 1, 0};
    }
    if (facing == PropIndices::FACING_CEILING) {
        return {0, -1, 0};
    }
    if (facing == PropIndices::FACING_NORTH) {
        return {0, 0, -1};
    }
    if (facing == PropIndices::FACING_SOUTH) {
        return {0, 0, 1};
    }
    if (facing == PropIndices::FACING_EAST) {
        return {1, 0, 0};
    }
    if (facing == PropIndices::FACING_WEST) {
        return {-1, 0, 0};
    }
    failWireFaceGeometry("Wire face geometry received an unsupported facing value");
}

/// Resolves the wire facing represented by an outward support-face normal.
inline uint16_t facingFromSurfaceNormal(const glm::ivec3& normal) {
    if (normal == glm::ivec3(0, 1, 0)) {
        return PropIndices::FACING_FLOOR;
    }
    if (normal == glm::ivec3(0, -1, 0)) {
        return PropIndices::FACING_CEILING;
    }
    if (normal == glm::ivec3(0, 0, -1)) {
        return PropIndices::FACING_NORTH;
    }
    if (normal == glm::ivec3(0, 0, 1)) {
        return PropIndices::FACING_SOUTH;
    }
    if (normal == glm::ivec3(1, 0, 0)) {
        return PropIndices::FACING_EAST;
    }
    if (normal == glm::ivec3(-1, 0, 0)) {
        return PropIndices::FACING_WEST;
    }
    failWireFaceGeometry("Wire face geometry received an unsupported surface normal");
}

/// Returns the relative position of the solid block that supports this wire face.
inline glm::ivec3 supportOffset(const uint16_t facing) {
    return -surfaceNormal(facing);
}

/// Returns the solid support block position for a wire at `wirePosition`.
inline glm::ivec3 supportPosition(const glm::ivec3& wirePosition, const uint16_t facing) {
    return wirePosition + supportOffset(facing);
}

/// Returns the wire cell that occupies `facing` on the given support block.
inline glm::ivec3 wirePositionOnSupportFace(const glm::ivec3& supportPosition, const uint16_t facing) {
    return supportPosition + surfaceNormal(facing);
}

/// Returns true when two wire facings meet along an edge of the same support block.
inline bool arePerpendicularFacings(const uint16_t facingA, const uint16_t facingB) {
    const glm::ivec3 normalA = surfaceNormal(facingA);
    const glm::ivec3 normalB = surfaceNormal(facingB);
    return normalA.x * normalB.x + normalA.y * normalB.y + normalA.z * normalB.z == 0;
}

/// Returns true when two wire cells occupy perpendicular faces of the same support block.
inline bool isCornerNeighbor(const glm::ivec3& positionA, const uint16_t facingA, const glm::ivec3& positionB,
                             const uint16_t facingB) {
    return arePerpendicularFacings(facingA, facingB) &&
           supportPosition(positionA, facingA) == supportPosition(positionB, facingB);
}

/// Returns the cell whose solid contents block an outer-corner connection.
inline glm::ivec3 outerCornerBlockingPosition(const glm::ivec3& support, const uint16_t facingA,
                                              const uint16_t facingB) {
    if (!arePerpendicularFacings(facingA, facingB)) {
        failWireFaceGeometry("Outer-corner blocking position requires perpendicular facings");
    }
    return support + surfaceNormal(facingA) + surfaceNormal(facingB);
}

/// Maps the north/south/east/west visual properties to world offsets for a wire face.
inline std::array<ConnectionDirection, 4> connectionDirections(const uint16_t facing) {
    if (facing == PropIndices::FACING_FLOOR || facing == PropIndices::FACING_CEILING) {
        return {{
            {PropIndices::NORTH, PropIndices::NORTH_NONE, PropIndices::NORTH_SIDE, {0, 0, -1}},
            {PropIndices::SOUTH, PropIndices::SOUTH_NONE, PropIndices::SOUTH_SIDE, {0, 0, 1}},
            {PropIndices::EAST, PropIndices::EAST_NONE, PropIndices::EAST_SIDE, {1, 0, 0}},
            {PropIndices::WEST, PropIndices::WEST_NONE, PropIndices::WEST_SIDE, {-1, 0, 0}},
        }};
    }
    if (facing == PropIndices::FACING_NORTH || facing == PropIndices::FACING_SOUTH) {
        return {{
            {PropIndices::NORTH, PropIndices::NORTH_NONE, PropIndices::NORTH_SIDE, {0, 1, 0}},
            {PropIndices::SOUTH, PropIndices::SOUTH_NONE, PropIndices::SOUTH_SIDE, {0, -1, 0}},
            {PropIndices::EAST, PropIndices::EAST_NONE, PropIndices::EAST_SIDE, {1, 0, 0}},
            {PropIndices::WEST, PropIndices::WEST_NONE, PropIndices::WEST_SIDE, {-1, 0, 0}},
        }};
    }
    if (facing == PropIndices::FACING_EAST || facing == PropIndices::FACING_WEST) {
        return {{
            {PropIndices::NORTH, PropIndices::NORTH_NONE, PropIndices::NORTH_SIDE, {0, 1, 0}},
            {PropIndices::SOUTH, PropIndices::SOUTH_NONE, PropIndices::SOUTH_SIDE, {0, -1, 0}},
            {PropIndices::EAST, PropIndices::EAST_NONE, PropIndices::EAST_SIDE, {0, 0, 1}},
            {PropIndices::WEST, PropIndices::WEST_NONE, PropIndices::WEST_SIDE, {0, 0, -1}},
        }};
    }
    failWireFaceGeometry("Wire face geometry received an unsupported facing value");
}

/// Returns the visual connection property that points along `offset` on `facing`.
inline ConnectionDirection connectionDirectionForPlanarOffset(const uint16_t facing, const glm::ivec3& offset) {
    for (const ConnectionDirection& connection : connectionDirections(facing)) {
        if (connection.offset == offset) {
            return connection;
        }
    }
    failWireFaceGeometry("Wire face geometry received an offset outside the wire plane");
}

} // namespace WireFaceGeometry
