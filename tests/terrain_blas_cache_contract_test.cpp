#include "renderer/mesh/TerrainBlasCache.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {

[[nodiscard]] BlockVertex makeVertex(const float x, const float y, const float z) {
    BlockVertex vertex{};
    vertex.x = x;
    vertex.y = y;
    vertex.z = z;
    return vertex;
}

[[nodiscard]] bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[terrain_blas_cache_contract_test] " << message << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    const std::vector<BlockVertex> opaque{makeVertex(0.0f, 0.0f, 0.0f), makeVertex(1.0f, 0.0f, 0.0f),
                                          makeVertex(0.0f, 1.0f, 0.0f)};
    const std::vector<BlockVertex> cutout{makeVertex(0.0f, 0.0f, 1.0f), makeVertex(1.0f, 0.0f, 1.0f),
                                          makeVertex(0.0f, 1.0f, 1.0f)};
    const std::vector<BlockVertex> cutoutDistance{makeVertex(0.0f, 0.0f, 2.0f), makeVertex(1.0f, 0.0f, 2.0f),
                                                  makeVertex(0.0f, 1.0f, 2.0f)};

    TerrainBlasGeometry geometry;
    const TerrainBlasRequestResult validResult =
        TerrainBlasCache::prepareGeometry(opaque, cutout, cutoutDistance, geometry);

    TerrainBlasGeometry emptyGeometry;
    const TerrainBlasRequestResult emptyResult = TerrainBlasCache::prepareGeometry({}, {}, {}, emptyGeometry);

    TerrainBlasGeometry invalidCountGeometry;
    const TerrainBlasRequestResult invalidCountResult = TerrainBlasCache::prepareGeometry(
        {makeVertex(0.0f, 0.0f, 0.0f), makeVertex(1.0f, 0.0f, 0.0f)}, {}, {}, invalidCountGeometry);

    std::vector<BlockVertex> invalidPosition = opaque;
    invalidPosition.front().x = std::numeric_limits<float>::quiet_NaN();
    TerrainBlasGeometry invalidPositionGeometry;
    const TerrainBlasRequestResult invalidPositionResult =
        TerrainBlasCache::prepareGeometry(invalidPosition, {}, {}, invalidPositionGeometry);

    std::vector<TerrainBlasScheduleKey> schedule{{2u, {8, 1}}, {1u, {9, 3}}, {2u, {7, 5}}, {2u, {7, 2}}};
    std::sort(schedule.begin(), schedule.end());

    const bool valid =
        requireTrue(validResult == TerrainBlasRequestResult::Queued && geometry.opaqueVertexCount == 3u &&
                        geometry.cutoutVertexCount == 6u && geometry.vertexCount() == 9u &&
                        geometry.primitiveCount() == 3u && geometry.vertices[3].z == 1.0f &&
                        geometry.vertices[6].z == 2.0f,
                    "geometry preparation must preserve opaque and cutout triangle ranges") &&
        requireTrue(emptyResult == TerrainBlasRequestResult::Cleared && emptyGeometry.empty(),
                    "empty solid geometry must explicitly clear the resident BLAS") &&
        requireTrue(invalidCountResult == TerrainBlasRequestResult::InvalidGeometry &&
                        invalidPositionResult == TerrainBlasRequestResult::InvalidGeometry,
                    "non-triangle and non-finite geometry must be rejected") &&
        requireTrue(TerrainBlasCache::validKey({0, 0}) && TerrainBlasCache::validKey({0, 15}) &&
                        !TerrainBlasCache::validKey({0, -1}) && !TerrainBlasCache::validKey({0, 16}),
                    "terrain keys must remain inside the fixed SubChunk column range") &&
        requireTrue(terrainBlasClassifyRevision(false, 0u, 1u) == TerrainBlasRevisionRelation::Newer &&
                        terrainBlasClassifyRevision(true, 7u, 8u) == TerrainBlasRevisionRelation::Newer &&
                        terrainBlasClassifyRevision(true, 7u, 7u) == TerrainBlasRevisionRelation::Current &&
                        terrainBlasClassifyRevision(true, 7u, 6u) == TerrainBlasRevisionRelation::Stale,
                    "revision classification must reject stale geometry generations") &&
        requireTrue(schedule[0].requestSequence == 1u && schedule[1].key.chunkKey == 7 && schedule[1].key.scy == 2 &&
                        schedule[2].key.chunkKey == 7 && schedule[2].key.scy == 5 && schedule[3].key.chunkKey == 8,
                    "scheduler order must be deterministic by sequence and SubChunk identity");

    return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
