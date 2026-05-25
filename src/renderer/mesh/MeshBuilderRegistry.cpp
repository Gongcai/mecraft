#include "MeshBuilderRegistry.h"

#include <unordered_map>

#include "ChunkMesher.h"

namespace {
struct MeshBuilderEntry {
    uint8_t tag = MeshBuilderRegistry::INVALID_TAG;
    MeshShapeClass shapeClass = MeshShapeClass::Cube;
    MeshBuilderFn fn = nullptr;
};

std::unordered_map<std::string, MeshBuilderEntry> g_entriesByName;
std::unordered_map<uint8_t, MeshBuilderEntry> g_entriesByTag;
bool g_initialized = false;
}

void MeshBuilderRegistry::registerBuilder(const std::string& shapeName,
                                          const uint8_t tag,
                                          const MeshShapeClass shapeClass,
                                          const MeshBuilderFn fn) {
    const MeshBuilderEntry entry{tag, shapeClass, fn};
    g_entriesByName[shapeName] = entry;
    g_entriesByTag[tag] = entry;
}

MeshBuilderFn MeshBuilderRegistry::getBuilder(const std::string& shapeName) {
    const auto it = g_entriesByName.find(shapeName);
    return (it != g_entriesByName.end()) ? it->second.fn : nullptr;
}

MeshBuilderFn MeshBuilderRegistry::getBuilder(const uint8_t tag) {
    const auto it = g_entriesByTag.find(tag);
    return (it != g_entriesByTag.end()) ? it->second.fn : nullptr;
}

uint8_t MeshBuilderRegistry::getShapeTag(const std::string& shapeName) {
    const auto it = g_entriesByName.find(shapeName);
    return (it != g_entriesByName.end()) ? it->second.tag : INVALID_TAG;
}

MeshShapeClass MeshBuilderRegistry::getShapeClass(const uint8_t tag) {
    const auto it = g_entriesByTag.find(tag);
    return (it != g_entriesByTag.end()) ? it->second.shapeClass : MeshShapeClass::Cube;
}

void MeshBuilderRegistry::initBuiltinBuilders() {
    if (g_initialized) {
        return;
    }

    registerBuilder("cube", CUBE_TAG, MeshShapeClass::Cube, nullptr);
    registerBuilder("cross", CROSS_TAG, MeshShapeClass::Cross, &ChunkMeshBuilders::buildCross);
    registerBuilder("torch", TORCH_TAG, MeshShapeClass::Custom, &ChunkMeshBuilders::buildTorch);
    registerBuilder("water", WATER_TAG, MeshShapeClass::Custom, &ChunkMeshBuilders::buildWater);

    g_initialized = true;
}
