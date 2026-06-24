#pragma once

#include <cstdint>
#include <string>

#include "../../world/block/Block.h"

struct ChunkMeshData;
struct SubChunkMeshingSnapshot;

using MeshBuilderFn = void(*)(ChunkMeshData& meshData,
                              const SubChunkMeshingSnapshot& snapshot,
                              BlockID blockId,
                              const BlockDef& def,
                              int x,
                              int y,
                              int z);

enum class MeshShapeClass : uint8_t {
    Cube = 0,
    Cross = 1,
    Custom = 2
};

class MeshBuilderRegistry {
public:
    static constexpr uint8_t INVALID_TAG = 0xFF;
    static constexpr uint8_t CUBE_TAG = 0;
    static constexpr uint8_t CROSS_TAG = 1;
    static constexpr uint8_t TORCH_TAG = 2;
    static constexpr uint8_t WATER_TAG = 3;
    static constexpr uint8_t MODEL_TAG = 4;
    static constexpr uint8_t BLOCK_ENTITY_TAG = 5;

    static void registerBuilder(const std::string& shapeName,
                                uint8_t tag,
                                MeshShapeClass shapeClass,
                                MeshBuilderFn fn);
    static MeshBuilderFn getBuilder(const std::string& shapeName);
    static MeshBuilderFn getBuilder(uint8_t tag);
    static uint8_t getShapeTag(const std::string& shapeName);
    static MeshShapeClass getShapeClass(uint8_t tag);
    static void initBuiltinBuilders();
};
