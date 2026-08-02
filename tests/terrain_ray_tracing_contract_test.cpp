#include "renderer/contracts/TerrainRayTracingContract.h"

#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

[[nodiscard]] bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[terrain_ray_tracing_contract_test] " << message << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] bool validateShaderMirror() {
    const std::string path = std::string(MECRAFT_TEST_SOURCE_DIR) + "/assets/shaders/terrain_ray_tracing_contract.glsl";
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }
    const std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const std::string queryPath = std::string(MECRAFT_TEST_SOURCE_DIR) + "/assets/shaders/terrain_ray_query.glsl";
    std::ifstream queryFile(queryPath);
    if (!queryFile.is_open()) {
        return false;
    }
    const std::string querySource{std::istreambuf_iterator<char>(queryFile), std::istreambuf_iterator<char>()};
    const std::string tracePath = std::string(MECRAFT_TEST_SOURCE_DIR) + "/assets/shaders/rtgi_trace.comp";
    std::ifstream traceFile(tracePath);
    if (!traceFile.is_open()) {
        return false;
    }
    const std::string traceSource{std::istreambuf_iterator<char>(traceFile), std::istreambuf_iterator<char>()};
    return source.find("const uint TERRAIN_RAY_TRACING_CONTRACT_VERSION = 1u;") != std::string::npos &&
           source.find("const uint TERRAIN_RAY_TRACING_VERTEX_STRIDE = 32u;") != std::string::npos &&
           source.find("const uint TERRAIN_RAY_TRACING_VERTEX_UV_OFFSET = 12u;") != std::string::npos &&
           source.find("struct TerrainPrimitiveMetadata") != std::string::npos &&
           source.find("uint textureLayer;") != std::string::npos &&
           source.find("uint animationAndFlags;") != std::string::npos &&
           source.find("uint materialAndTint;") != std::string::npos &&
           source.find("uint faceAndFlags;") != std::string::npos &&
           source.find("struct TerrainRayTracingGpuInstance") != std::string::npos &&
           source.find("int terrainPrimitiveFace(TerrainPrimitiveMetadata metadata)") != std::string::npos &&
           querySource.find("layout(buffer_reference, std430, buffer_reference_align = 4)") != std::string::npos &&
           querySource.find("bool terrainRayQueryInterpolateAttributes") != std::string::npos &&
           querySource.find("bool terrainRayQueryConeTextureLod") != std::string::npos &&
           querySource.find("bool terrainRayQueryCandidateAlphaPasses") != std::string::npos &&
           querySource.find("bool terrainRayQueryCommittedSurface") != std::string::npos &&
           querySource.find("decodeLabPbrNormal") != std::string::npos &&
           querySource.find("decodeLabPbrSpecular") != std::string::npos &&
           querySource.find("textureLod(grassColormap") != std::string::npos &&
           querySource.find("surface.emission = albedo * material.emission * emissionScale;") != std::string::npos &&
           traceSource.find("terrainRayQueryCommittedSurface(") != std::string::npos &&
           traceSource.find("rtgiAccumulateWorldLights") != std::string::npos &&
           traceSource.find("worldLightGridCellRange") != std::string::npos &&
           traceSource.find("sampleSkyRadiance(uSkyCapture, rayDirection)") != std::string::npos;
}

} // namespace

int main() {
    using namespace renderer::contracts;

    const std::optional<TerrainPrimitiveMetadata> encoded =
        encodeTerrainPrimitiveMetadata({100u, 3u, 4u, true, 0xabcdu, kTerrainPrimitiveFaceCrossBiomeTint});
    if (!requireTrue(encoded.has_value(), "valid primitive metadata must encode successfully")) {
        return EXIT_FAILURE;
    }
    bool valid = requireTrue(
        encoded->textureLayer == 100u && encoded->animationAndFlags == 0x1103u && encoded->materialAndTint == 0xabcdu &&
            encoded->faceAndFlags == 0xffu && terrainPrimitiveAnimationFrameCount(*encoded) == 3u &&
            terrainPrimitiveAnimationFramesPerSecond(*encoded) == 4u && terrainPrimitiveAnimated(*encoded) &&
            terrainPrimitiveMaterialAndTint(*encoded) == 0xabcdu &&
            terrainPrimitiveFace(*encoded) == kTerrainPrimitiveFaceCrossBiomeTint &&
            validTerrainPrimitiveMetadata(*encoded),
        "primitive metadata must preserve the exact 16-byte packed layout");

    valid =
        requireTrue(
            !encodeTerrainPrimitiveMetadata({1024u, 1u, 0u, false, 0u, 0}).has_value() &&
                !encodeTerrainPrimitiveMetadata({0u, 0u, 0u, false, 0u, 0}).has_value() &&
                !encodeTerrainPrimitiveMetadata({0u, 64u, 1u, true, 0u, 0}).has_value() &&
                !encodeTerrainPrimitiveMetadata({0u, 2u, 64u, true, 0u, 0}).has_value() &&
                !encodeTerrainPrimitiveMetadata({1023u, 2u, 1u, true, 0u, 0}).has_value() &&
                !encodeTerrainPrimitiveMetadata({0u, 1u, 1u, true, 0u, 0}).has_value() &&
                !encodeTerrainPrimitiveMetadata({0u, 2u, 0u, true, 0u, 0}).has_value() &&
                !encodeTerrainPrimitiveMetadata({0u, 1u, 0u, false, 0u, kTerrainPrimitiveFaceMinimum - 1})
                     .has_value() &&
                !encodeTerrainPrimitiveMetadata({0u, 1u, 0u, false, 0u, kTerrainPrimitiveFaceMaximum + 1}).has_value(),
            "primitive metadata must reject invalid texture, animation, and face fields") &&
        valid;

    TerrainPrimitiveMetadata reservedBits = *encoded;
    reservedBits.animationAndFlags |= 1u << 31u;
    valid = requireTrue(!validTerrainPrimitiveMetadata(reservedBits),
                        "primitive metadata must reject non-zero reserved bits") &&
            valid;

    TerrainRayTracingHitData mixed;
    mixed.revision = 0x0000000400000007ull;
    mixed.vertexAddress = 0x0000000210001000ull;
    mixed.primitiveMetadataAddress = 0x0000000320002000ull;
    mixed.geometryCount = 2u;
    mixed.geometries[0] = {0u, TerrainRayTracingGeometryClass::Opaque, 0u, 6u, 0u, 2u};
    mixed.geometries[1] = {1u, TerrainRayTracingGeometryClass::Cutout, 6u, 3u, 2u, 1u};

    TerrainRayTracingHitData cutoutOnly;
    cutoutOnly.revision = 8u;
    cutoutOnly.vertexAddress = 0x3000u;
    cutoutOnly.primitiveMetadataAddress = 0x4000u;
    cutoutOnly.geometryCount = 1u;
    cutoutOnly.geometries[0] = {0u, TerrainRayTracingGeometryClass::Cutout, 0u, 3u, 0u, 1u};
    valid = requireTrue(validTerrainRayTracingHitData(mixed) && validTerrainRayTracingHitData(cutoutOnly),
                        "hit data must accept canonical mixed and cutout-only Geometry Index mappings") &&
            valid;

    const std::optional<TerrainRayTracingGpuInstance> gpuInstance = encodeTerrainRayTracingGpuInstance(mixed);
    valid = requireTrue(gpuInstance.has_value() &&
                            gpuInstance->vertexAddressWords == std::array<uint32_t, 2u>{0x10001000u, 2u} &&
                            gpuInstance->primitiveMetadataAddressWords == std::array<uint32_t, 2u>{0x20002000u, 3u} &&
                            gpuInstance->geometryCount == 2u && gpuInstance->revisionLow == 7u &&
                            gpuInstance->revisionHigh == 4u &&
                            gpuInstance->contractVersion == kTerrainRayTracingContractVersion &&
                            gpuInstance->geometries[0] == TerrainRayTracingGpuGeometry{0u, 0u, 2u, 0u} &&
                            gpuInstance->geometries[1] == TerrainRayTracingGpuGeometry{6u, 2u, 1u, 1u},
                        "GPU hit data must preserve 64-bit addresses and every Geometry Index range") &&
            valid;

    TerrainRayTracingHitData invalidMapping = mixed;
    invalidMapping.geometries[1].primitiveBase = 1u;
    TerrainRayTracingHitData invalidOrder = mixed;
    invalidOrder.geometries[0].geometryClass = TerrainRayTracingGeometryClass::Cutout;
    TerrainRayTracingHitData invalidStride = mixed;
    invalidStride.vertexStride = 16u;
    TerrainRayTracingHitData invalidUnusedRange = cutoutOnly;
    invalidUnusedRange.geometries[1].primitiveCount = 1u;
    valid = requireTrue(
                !validTerrainRayTracingHitData(invalidMapping) && !validTerrainRayTracingHitData(invalidOrder) &&
                    !validTerrainRayTracingHitData(invalidStride) && !validTerrainRayTracingHitData(invalidUnusedRange),
                "hit data must reject ambiguous bases, ordering, strides, and unused ranges") &&
            valid;
    valid = requireTrue(!encodeTerrainRayTracingGpuInstance(invalidMapping).has_value(),
                        "GPU hit-data encoding must reject invalid CPU snapshots") &&
            valid;

    valid =
        requireTrue(validateShaderMirror(), "GLSL terrain ray-tracing fields must mirror the C++ contract") && valid;
    return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
