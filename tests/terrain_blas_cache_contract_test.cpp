#include "renderer/mesh/TerrainBlasCache.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {

[[nodiscard]] BlockVertex makeVertex(const float x, const float y, const float z, const float u, const float v,
                                     const int8_t face, const uint16_t layer, const uint16_t frameCount = 1u,
                                     const uint8_t framesPerSecond = 0u, const bool animated = false,
                                     const uint8_t tintKind = BlockTintKinds::NONE,
                                     const uint8_t derivativeMaterialId = DerivativeMaterialIds::DEFAULT) {
    return makeBlockVertex(x, y, z, u, v, static_cast<float>(face), 1.0f, 0.0f, 3.0f, static_cast<float>(layer),
                           static_cast<float>(frameCount), static_cast<float>(framesPerSecond), animated ? 1.0f : 0.0f,
                           tintKind, 64u, 128u, derivativeMaterialId);
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
    TerrainBlasCache unsupportedOpacityMicromapCache;
    const bool unsupportedOpacityMicromapRejected =
        !unsupportedOpacityMicromapCache.setOpacityMicromapEnabled(true) &&
        unsupportedOpacityMicromapCache.lastError() ==
            "Terrain opacity micromap mode is unsupported by the active device or texture source";
    TerrainBlasCache invalidOpacityMicromapSourceCache;
    const bool invalidOpacityMicromapSourceRejected =
        !invalidOpacityMicromapSourceCache.setOpacityMicromapSource({}) &&
        invalidOpacityMicromapSourceCache.lastError() == "Terrain opacity micromap source is invalid";
    std::vector<BlockVertex> opaque{makeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2, 17u),
                                    makeVertex(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 2, 17u),
                                    makeVertex(0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 2, 17u)};
    for (BlockVertex& vertex : opaque) {
        setBlockVertexAnalyticLightOwnsEmission(vertex, true);
    }
    const std::vector<BlockVertex> cutout{
        makeVertex(0.0f, 0.0f, 1.0f, 0.0f, 0.0f, -1, 100u, 3u, 4u, true, BlockTintKinds::GRASS, 9u),
        makeVertex(1.0f, 0.0f, 1.0f, 1.0f, 0.0f, -1, 100u, 3u, 4u, true, BlockTintKinds::GRASS, 9u),
        makeVertex(0.0f, 1.0f, 1.0f, 0.0f, 1.0f, -1, 100u, 3u, 4u, true, BlockTintKinds::GRASS, 9u)};
    const std::vector<BlockVertex> cutoutDistance{makeVertex(0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 5, 200u),
                                                  makeVertex(1.0f, 0.0f, 2.0f, 1.0f, 0.0f, 5, 200u),
                                                  makeVertex(0.0f, 1.0f, 2.0f, 0.0f, 1.0f, 5, 200u)};

    TerrainBlasGeometry geometry;
    const TerrainBlasRequestResult validResult =
        TerrainBlasCache::prepareGeometry(opaque, cutout, cutoutDistance, geometry);

    TerrainBlasGeometry emptyGeometry;
    const TerrainBlasRequestResult emptyResult = TerrainBlasCache::prepareGeometry({}, {}, {}, emptyGeometry);

    TerrainBlasGeometry invalidCountGeometry;
    const TerrainBlasRequestResult invalidCountResult = TerrainBlasCache::prepareGeometry(
        {makeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2, 17u), makeVertex(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 2, 17u)}, {}, {},
        invalidCountGeometry);

    std::vector<BlockVertex> invalidPosition = opaque;
    invalidPosition.front().x = std::numeric_limits<float>::quiet_NaN();
    TerrainBlasGeometry invalidPositionGeometry;
    const TerrainBlasRequestResult invalidPositionResult =
        TerrainBlasCache::prepareGeometry(invalidPosition, {}, {}, invalidPositionGeometry);

    std::vector<BlockVertex> invalidUv = opaque;
    invalidUv.front().u = std::numeric_limits<float>::infinity();
    TerrainBlasGeometry invalidUvGeometry;
    const TerrainBlasRequestResult invalidUvResult =
        TerrainBlasCache::prepareGeometry(invalidUv, {}, {}, invalidUvGeometry);

    std::vector<BlockVertex> inconsistentMaterial = opaque;
    inconsistentMaterial[1].layer = 18u;
    TerrainBlasGeometry inconsistentMaterialGeometry;
    const TerrainBlasRequestResult inconsistentMaterialResult =
        TerrainBlasCache::prepareGeometry(inconsistentMaterial, {}, {}, inconsistentMaterialGeometry);

    std::vector<BlockVertex> invalidVertexFlags = opaque;
    for (BlockVertex& vertex : invalidVertexFlags) {
        vertex.animationAndFlags = 1u << 7u;
    }
    TerrainBlasGeometry invalidVertexFlagsGeometry;
    const TerrainBlasRequestResult invalidVertexFlagsResult =
        TerrainBlasCache::prepareGeometry(invalidVertexFlags, {}, {}, invalidVertexFlagsGeometry);

    std::vector<TerrainBlasScheduleKey> schedule{{2u, {8, 1}}, {1u, {9, 3}}, {2u, {7, 5}}, {2u, {7, 2}}};
    std::sort(schedule.begin(), schedule.end());

    const bool valid =
        requireTrue(unsupportedOpacityMicromapRejected && invalidOpacityMicromapSourceRejected,
                    "unsupported OMM mode and invalid alpha sources must be rejected explicitly") &&
        requireTrue(
            validResult == TerrainBlasRequestResult::Queued && geometry.opaqueVertexCount == 3u &&
                geometry.cutoutVertexCount == 6u && geometry.vertexCount() == 9u && geometry.primitiveCount() == 3u &&
                std::abs(geometry.vertices[0].z - kTerrainBlasOpaqueSurfaceExpansion) <= 1.0e-7f &&
                geometry.vertices[3].z == 1.0f && geometry.vertices[6].z == 2.0f &&
                geometry.primitiveMetadata.size() == 3u && geometry.primitiveMetadata[0].textureLayer == 17u &&
                renderer::contracts::terrainPrimitiveAnalyticLightOwnsEmission(geometry.primitiveMetadata[0]) &&
                renderer::contracts::terrainPrimitiveAnimationFrameCount(geometry.primitiveMetadata[1]) == 3u &&
                renderer::contracts::terrainPrimitiveAnimationFramesPerSecond(geometry.primitiveMetadata[1]) == 4u &&
                renderer::contracts::terrainPrimitiveAnimated(geometry.primitiveMetadata[1]) &&
                renderer::contracts::terrainPrimitiveFace(geometry.primitiveMetadata[1]) == -1 &&
                geometry.uploadByteSize() ==
                    sizeof(BlockVertex) * 9u + sizeof(renderer::contracts::TerrainPrimitiveMetadata) * 3u,
            "geometry preparation must seal opaque faces, preserve triangle order, and emit one metadata record per "
            "primitive") &&
        requireTrue(emptyResult == TerrainBlasRequestResult::Cleared && emptyGeometry.empty(),
                    "empty solid geometry must explicitly clear the resident BLAS") &&
        requireTrue(invalidCountResult == TerrainBlasRequestResult::InvalidGeometry &&
                        invalidPositionResult == TerrainBlasRequestResult::InvalidGeometry &&
                        invalidUvResult == TerrainBlasRequestResult::InvalidGeometry &&
                        inconsistentMaterialResult == TerrainBlasRequestResult::InvalidGeometry &&
                        invalidVertexFlagsResult == TerrainBlasRequestResult::InvalidGeometry,
                    "non-triangle, non-finite, and material-inconsistent primitives must be rejected") &&
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
