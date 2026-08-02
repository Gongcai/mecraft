#ifndef MECRAFT_WORLD_LIGHT_GRID_GLSL
#define MECRAFT_WORLD_LIGHT_GRID_GLSL

#include "world_light_grid_contract.glsl"

#ifndef MECRAFT_CLUSTER_BIND_SET
#define MECRAFT_CLUSTER_BIND_SET 1
#endif

layout(set = MECRAFT_CLUSTER_BIND_SET, binding = 7, std430) readonly buffer WorldLightCellBuffer {
    WorldLightCell uWorldLightCells[];
};
layout(set = MECRAFT_CLUSTER_BIND_SET, binding = 8, std430) readonly buffer WorldLightIndexBuffer {
    uint uWorldLightIndices[];
};
layout(set = MECRAFT_CLUSTER_BIND_SET, binding = 9, std430) readonly buffer WorldLightGridHeaderBuffer {
    WorldLightGridHeader uWorldLightGrid;
};

int worldLightGridCompareCoordinate(ivec3 lhs, ivec3 rhs) {
    if (lhs.x != rhs.x) {
        return lhs.x < rhs.x ? -1 : 1;
    }
    if (lhs.y != rhs.y) {
        return lhs.y < rhs.y ? -1 : 1;
    }
    if (lhs.z != rhs.z) {
        return lhs.z < rhs.z ? -1 : 1;
    }
    return 0;
}

bool worldLightGridHeaderValid() {
    uint cellCount = uWorldLightGrid.countsAndVersion.x;
    uint indexCount = uWorldLightGrid.countsAndVersion.y;
    uint globalLightCount = uWorldLightGrid.countsAndVersion.z;
    return uWorldLightGrid.countsAndVersion.w == WORLD_LIGHT_GRID_CONTRACT_VERSION &&
           uWorldLightGrid.cellSizeAndInverse.x > 0.0 &&
           uWorldLightGrid.cellSizeAndInverse.y > 0.0 &&
           !isnan(uWorldLightGrid.cellSizeAndInverse.x) && !isinf(uWorldLightGrid.cellSizeAndInverse.x) &&
           !isnan(uWorldLightGrid.cellSizeAndInverse.y) && !isinf(uWorldLightGrid.cellSizeAndInverse.y) &&
           abs(uWorldLightGrid.cellSizeAndInverse.x * uWorldLightGrid.cellSizeAndInverse.y - 1.0) <= 1.0e-5 &&
           globalLightCount <= indexCount && cellCount <= indexCount;
}

bool worldLightGridCellRange(vec3 cameraRelativeSurface, out uvec2 indexRange) {
    indexRange = uvec2(0u);
    if (!worldLightGridHeaderValid() || any(isnan(cameraRelativeSurface)) || any(isinf(cameraRelativeSurface))) {
        return false;
    }
    ivec3 target = ivec3(floor(cameraRelativeSurface * uWorldLightGrid.cellSizeAndInverse.y));
    uint low = 0u;
    uint high = uWorldLightGrid.countsAndVersion.x;
    while (low < high) {
        uint middle = low + (high - low) / 2u;
        WorldLightCell cell = uWorldLightCells[middle];
        if (cell.coordinate.w != 0 || cell.indexRangeAndVersion.w != WORLD_LIGHT_GRID_CONTRACT_VERSION) {
            return false;
        }
        int comparison = worldLightGridCompareCoordinate(cell.coordinate.xyz, target);
        if (comparison < 0) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    if (low >= uWorldLightGrid.countsAndVersion.x) {
        return true;
    }
    WorldLightCell cell = uWorldLightCells[low];
    if (worldLightGridCompareCoordinate(cell.coordinate.xyz, target) != 0) {
        return true;
    }
    uint indexCount = uWorldLightGrid.countsAndVersion.y;
    uint offset = cell.indexRangeAndVersion.x;
    uint count = cell.indexRangeAndVersion.y;
    if (cell.indexRangeAndVersion.z != 0u || offset > indexCount || count > indexCount - min(offset, indexCount)) {
        return false;
    }
    indexRange = uvec2(offset, count);
    return true;
}

#endif // MECRAFT_WORLD_LIGHT_GRID_GLSL
