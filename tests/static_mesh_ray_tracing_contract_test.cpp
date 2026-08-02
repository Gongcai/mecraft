#include "renderer/contracts/StaticMeshRayTracingContract.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

[[nodiscard]] bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[static_mesh_ray_tracing_contract_test] " << message << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] bool readSource(const char* relativePath, std::string& source) {
    const std::string path = std::string(MECRAFT_TEST_SOURCE_DIR) + relativePath;
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }
    source.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return !file.bad();
}

} // namespace

int main() {
    using namespace renderer::contracts;

    const StaticMeshPrimitiveMetadata metadata{7u, 101u, 202u, kStaticMeshRayTracingContractVersion};
    bool valid = true;
    valid = requireTrue(kStaticMeshRayTracingVertexStride == 48u && kStaticMeshRayTracingPositionOffset == 0u &&
                            kStaticMeshRayTracingNormalOffset == 12u && kStaticMeshRayTracingTangentOffset == 24u &&
                            kStaticMeshRayTracingUvOffset == 40u && sizeof(metadata) == 16u,
                        "CPU vertex and primitive metadata layouts must remain fixed") &&
            valid;
    valid = requireTrue(metadata.materialIndex == 7u && metadata.stableMaterialId == 101u &&
                            metadata.stableGeometryId == 202u &&
                            metadata.contractVersion == kStaticMeshRayTracingContractVersion,
                        "primitive metadata must preserve material and geometry identity") &&
            valid;

    std::string contractSource;
    std::string querySource;
    valid = requireTrue(readSource("/assets/shaders/static_mesh_ray_tracing_contract.glsl", contractSource) &&
                            readSource("/assets/shaders/static_mesh_ray_query.glsl", querySource),
                        "static-mesh ray-tracing GLSL sources must be readable") &&
            valid;
    valid =
        requireTrue(
            contractSource.find("const uint STATIC_MESH_RAY_TRACING_VERTEX_STRIDE = 48u;") != std::string::npos &&
                contractSource.find("const uint STATIC_MESH_RAY_TRACING_NORMAL_OFFSET = 12u;") != std::string::npos &&
                contractSource.find("const uint STATIC_MESH_RAY_TRACING_TANGENT_OFFSET = 24u;") != std::string::npos &&
                contractSource.find("const uint STATIC_MESH_RAY_TRACING_UV_OFFSET = 40u;") != std::string::npos &&
                contractSource.find("struct StaticMeshPrimitiveMetadata") != std::string::npos,
            "GLSL layout constants must mirror the C++ static-mesh contract") &&
        valid;
    valid = requireTrue(querySource.find("binding = 8") != std::string::npos &&
                            querySource.find("binding = 9") != std::string::npos &&
                            querySource.find("binding = 10") != std::string::npos &&
                            querySource.find("GPU_SCENE_INDEX_TYPE_UINT32") != std::string::npos &&
                            querySource.find("staticMeshRayQueryInterpolateAttributes") != std::string::npos &&
                            querySource.find("staticMeshRayQueryConeTextureLod") != std::string::npos &&
                            querySource.find("staticMeshRayQuerySampleMaterial") != std::string::npos &&
                            querySource.find("staticMeshRayQuerySampleBaseColor") != std::string::npos &&
                            querySource.find("staticMeshRayQueryCandidateAlphaPasses") != std::string::npos &&
                            querySource.find("staticMeshRayQueryCommittedIdentity") != std::string::npos,
                        "ray-query source must expose indexed attribute, material, alpha, and identity paths") &&
            valid;
    const std::size_t candidateStart = querySource.find("bool staticMeshRayQueryCandidateAlphaPasses");
    const std::size_t committedStart = querySource.find("bool staticMeshRayQueryCommittedIdentity");
    const std::string candidateSource =
        candidateStart != std::string::npos && committedStart != std::string::npos && committedStart > candidateStart
            ? querySource.substr(candidateStart, committedStart - candidateStart)
            : std::string{};
    valid = requireTrue(candidateSource.find("staticMeshRayQuerySampleBaseColor") != std::string::npos &&
                            candidateSource.find("staticMeshRayQuerySampleMaterial") == std::string::npos &&
                            candidateSource.find("material.baseColorFactor.a") != std::string::npos,
                        "alpha candidate evaluation must sample only Base Color and apply its factor") &&
            valid;
    return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
