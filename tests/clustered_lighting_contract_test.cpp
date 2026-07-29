#include "renderer/contracts/ClusteredLightingContract.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[clustered_lighting_contract_test] FAIL: "
                  << message << '\n';
        return false;
    }
    return true;
}

bool readProjectFile(const char* relativePath, std::string& source) {
    const std::string path = std::string(MECRAFT_TEST_SOURCE_DIR) +
        "/" + relativePath;
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        return false;
    }
    source.assign(std::istreambuf_iterator<char>(stream),
                  std::istreambuf_iterator<char>());
    return true;
}

std::string removeWhitespace(const std::string& source) {
    std::string normalized;
    normalized.reserve(source.size());
    for (const unsigned char character : source) {
        if (std::isspace(character) == 0) {
            normalized.push_back(static_cast<char>(character));
        }
    }
    return normalized;
}

renderer::contracts::GpuLight makePointLight(const uint32_t id,
                                              const glm::vec3 position,
                                              const float range) {
    using namespace renderer::contracts;
    GpuLightNormalizationInput input;
    input.lightId = StableLightId{id};
    input.type = GpuLightType::Point;
    input.positionMeters = position;
    input.rangeMeters = range;
    input.intensity = 100.0f;
    input.intensityUnit = GpuLightIntensityUnit::Candela;
    return normalizeGpuLight(input).light;
}

bool testGridAndLogarithmicSlices() {
    using namespace renderer::contracts;
    const std::optional<ClusterGrid> grid =
        buildClusterGrid(1920u, 1080u, 0.1f, 1000.0f);
    return requireTrue(grid.has_value(),
                       "valid render extents must build a cluster grid") &&
           requireTrue(grid->tileCountX == 120u && grid->tileCountY == 68u,
                       "16-pixel tiles must round the render extent upward") &&
           requireTrue(grid->depthSliceCount == 24u &&
                           grid->clusterCount == 195840u,
                       "the initial contract must use 24 logarithmic slices") &&
           requireTrue(kClusterCoverageWorkgroupSize == 64u &&
                           kClusterMaxLightCount == 65535u,
                       "coverage dispatch must use one bounded workgroup per light") &&
           requireTrue(clusterDepthSlice(*grid, grid->nearPlane) == 0u,
                       "the near plane must map to the first slice") &&
           requireTrue(clusterDepthSlice(*grid, grid->farPlane) == 23u,
                       "the far plane must map to the last slice") &&
           requireTrue(!buildClusterGrid(0u, 1080u, 0.1f, 1000.0f).has_value(),
                       "zero render extents must fail explicitly") &&
           requireTrue(!buildClusterGrid(1920u, 1080u, 1.0f, 1.0f).has_value(),
                       "invalid depth ranges must fail explicitly");
}

bool testCoverageAndCapacity() {
    using namespace renderer::contracts;
    const ClusterGrid grid = *buildClusterGrid(1280u, 720u, 0.1f, 500.0f);
    const glm::mat4 view(1.0f);
    const glm::mat4 projection =
        glm::perspective(glm::radians(70.0f), 1280.0f / 720.0f,
                         0.1f, 500.0f);

    GpuLightNormalizationInput directionalInput;
    directionalInput.lightId = StableLightId{1u};
    directionalInput.type = GpuLightType::Directional;
    directionalInput.emissionDirection = {0.0f, -1.0f, 0.0f};
    directionalInput.intensity = 100000.0f;
    directionalInput.intensityUnit = GpuLightIntensityUnit::Lux;
    const GpuLight directional = normalizeGpuLight(directionalInput).light;
    const auto directionalBounds =
        buildGpuClusterLightBounds(directional, grid, view, projection);
    if (!requireTrue(directionalBounds.has_value() &&
                         directionalBounds->minCluster.w == 1u,
                     "directional lights must intersect every cluster") ||
        !requireTrue(clusterLightCoverageCount(*directionalBounds) ==
                         grid.clusterCount,
                     "directional coverage must equal the complete lattice")) {
        return false;
    }

    const GpuLight center = makePointLight(2u, {0.0f, 0.0f, -8.0f}, 2.0f);
    const auto centerBounds =
        buildGpuClusterLightBounds(center, grid, view, projection);
    if (!requireTrue(centerBounds.has_value() && centerBounds->minCluster.w == 1u,
                     "a centered point light must produce active bounds") ||
        !requireTrue(centerBounds->minCluster.x <= centerBounds->maxCluster.x &&
                         centerBounds->minCluster.y <= centerBounds->maxCluster.y &&
                         centerBounds->minCluster.z <= centerBounds->maxCluster.z,
                     "active light bounds must be ordered") ||
        !requireTrue(clusterLightCoverageCount(*centerBounds) > 0u &&
                         clusterLightCoverageCount(*centerBounds) < grid.clusterCount,
                     "a finite local light must cover a compact cluster subset")) {
        return false;
    }

    const GpuLight outside =
        makePointLight(3u, {10000.0f, 0.0f, -8.0f}, 1.0f);
    const auto outsideBounds =
        buildGpuClusterLightBounds(outside, grid, view, projection);
    if (!requireTrue(outsideBounds.has_value() &&
                         outsideBounds->minCluster.w == 0u &&
                         clusterLightCoverageCount(*outsideBounds) == 0u,
                     "off-screen local lights must remain explicitly inactive")) {
        return false;
    }

    const std::vector<GpuClusterLightBounds> bounds{
        *directionalBounds, *centerBounds, *outsideBounds};
    const std::optional<uint32_t> capacity =
        requiredClusterLightIndexCount(bounds);
    const std::optional<uint32_t> inactiveCapacity =
        requiredClusterLightIndexCount({*outsideBounds});
    return requireTrue(capacity.has_value(),
                       "valid coverage counts must produce a compact capacity") &&
           requireTrue(*capacity == grid.clusterCount +
                           clusterLightCoverageCount(*centerBounds),
                       "capacity must exactly equal the sum of light coverage") &&
           requireTrue(inactiveCapacity.has_value() &&
                           *inactiveCapacity == 0u,
                       "a complete off-screen light set must produce valid zero coverage");
}

bool testReadbackCompletionContract() {
    std::string pass;
    std::string pipeline;
    if (!requireTrue(readProjectFile(
                         "src/renderer/passes/ClusteredLightingPass.cpp",
                         pass),
                     "clustered-light pass source must be readable") ||
        !requireTrue(readProjectFile(
                         "src/renderer/core/DeferredPipeline.cpp",
                         pipeline),
                     "deferred pipeline source must be readable")) {
        return false;
    }
    return requireTrue(
               pass.find("isSubmissionComplete(token, complete)") !=
                   std::string::npos,
               "statistics readback must query its exact GPU submission") &&
           requireTrue(
               pass.find("m_statsReadbackSlotAvailable = false") !=
                   std::string::npos,
               "pending readback slots must remain unavailable for overwrite") &&
           requireTrue(
               pipeline.find(
                   "executed.succeeded(), executed.completionToken()") !=
                   std::string::npos,
               "the clustered pass must retain the graph completion token");
}

bool testEmptyLightingSteadyStateContract() {
    std::string pass;
    std::string lightingPass;
    std::string deferred;
    if (!requireTrue(readProjectFile(
                         "src/renderer/passes/ClusteredLightingPass.cpp",
                         pass),
                     "clustered-light pass source must be readable") ||
        !requireTrue(readProjectFile(
                         "src/renderer/passes/DeferredLightingPass.cpp",
                         lightingPass),
                     "deferred-light pass source must be readable") ||
        !requireTrue(readProjectFile(
                         "assets/shaders/deferred_lighting.frag", deferred),
                     "deferred-light shader source must be readable")) {
        return false;
    }

    const size_t emptySkip = pass.find(
        "m_lights.empty() && m_emptyBuildReady");
    const size_t dependencyReturn = emptySkip == std::string::npos
        ? std::string::npos
        : pass.find("return dependency;", emptySkip);
    const size_t uploadPass = pass.find("ClusteredLighting.Upload");
    const size_t activeLightBranch = deferred.find(
        "if (uClusterActiveLightCount > 0)");
    const size_t clusteredEvaluation = deferred.find(
        "evaluateClusteredSurfaceLighting(");
    return requireTrue(
               emptySkip != std::string::npos &&
                   dependencyReturn != std::string::npos &&
                   uploadPass != std::string::npos &&
                   dependencyReturn < uploadPass,
               "a validated empty build must bypass the complete cluster graph chain") &&
           requireTrue(
               pass.find("m_emptyBuildReady = completionValid") !=
                       std::string::npos &&
                   pass.find("publishEmptyFrameStats()") !=
                       std::string::npos,
               "empty-build reuse must begin only after successful graph submission") &&
           requireTrue(
               lightingPass.find(
                   "m_clusteredLightingPass->activeLightCount()") !=
                       std::string::npos &&
                   deferred.find(
                       "#define uClusterActiveLightCount pFlags5.z") !=
                       std::string::npos &&
                   activeLightBranch != std::string::npos &&
                   clusteredEvaluation != std::string::npos &&
                   activeLightBranch < clusteredEvaluation,
               "deferred lighting must bypass clustered buffer queries when no light intersects the view");
}

bool testComputeShaderContracts() {
    std::string scan;
    std::string count;
    std::string fill;
    if (!requireTrue(readProjectFile(
                         "assets/shaders/cluster_scan.comp", scan),
                     "hierarchical scan shader must be readable") ||
        !requireTrue(readProjectFile(
                         "assets/shaders/cluster_count.comp", count),
                     "cluster count shader must be readable") ||
        !requireTrue(readProjectFile(
                         "assets/shaders/cluster_fill.comp", fill),
                     "compact list fill shader must be readable")) {
        return false;
    }
    return requireTrue(scan.find("shared uint uScan[512]") !=
                           std::string::npos,
                       "prefix sum must scan 512 values per workgroup") &&
           requireTrue(scan.find("uBlockSums") != std::string::npos,
                       "prefix sum must emit hierarchical block sums") &&
           requireTrue(count.find("atomicAdd(uClusterCounts") !=
                           std::string::npos,
                       "coverage counting must execute on the GPU") &&
           requireTrue(count.find("gl_WorkGroupID.x") !=
                           std::string::npos &&
                           count.find("gl_LocalInvocationID.x") !=
                           std::string::npos &&
                           count.find("CLUSTER_COVERAGE_WORKGROUP_SIZE") !=
                           std::string::npos &&
                           count.find("gl_WorkGroupID.y") !=
                           std::string::npos,
                       "count must distribute every light slice across one workgroup") &&
           requireTrue(fill.find("uCompactLightIndices") !=
                           std::string::npos,
                       "fill must write the compact light index list") &&
           requireTrue(fill.find("gl_WorkGroupID.x") !=
                           std::string::npos &&
                           fill.find("gl_LocalInvocationID.x") !=
                           std::string::npos &&
                           fill.find("bounds.maxCluster.w") !=
                           std::string::npos &&
                           fill.find("CLUSTER_COVERAGE_WORKGROUP_SIZE") !=
                           std::string::npos &&
                           fill.find("gl_WorkGroupID.y") !=
                           std::string::npos,
                       "fill must distribute every active light slice and preserve the source light index") &&
           requireTrue(fill.find("CLUSTER_BUILD_ERROR") !=
                           std::string::npos,
                       "capacity and cursor failures must be explicit");
}

bool testSharedLightingConsumers() {
    std::string deferred;
    std::string staticMeshForward;
    std::string terrainForward;
    std::string sharedLighting;
    std::string lightEvaluation;
    std::string pipeline;
    if (!requireTrue(readProjectFile(
                         "assets/shaders/deferred_lighting.frag", deferred),
                     "deferred lighting shader must be readable") ||
        !requireTrue(readProjectFile(
                         "assets/shaders/static_mesh_transparent_rhi.frag",
                         staticMeshForward),
                     "static mesh Forward+ shader must be readable") ||
        !requireTrue(readProjectFile(
                         "assets/shaders/chunk_lit_common.frag",
                         terrainForward),
                     "terrain Forward+ shader must be readable") ||
        !requireTrue(readProjectFile(
                         "assets/shaders/clustered_lighting.glsl",
                         sharedLighting),
                     "shared clustered-light shader must be readable") ||
        !requireTrue(readProjectFile(
                         "assets/shaders/clustered_light_evaluation.glsl",
                         lightEvaluation),
                     "clustered-light evaluation shader must be readable") ||
        !requireTrue(readProjectFile(
                         "src/renderer/core/DeferredPipeline.cpp", pipeline),
                     "deferred Render Graph source must be readable")) {
        return false;
    }

    const size_t contributionEvaluation = sharedLighting.find(
        "GpuLightSurfaceContribution contribution = evaluateGpuLight(");
    const size_t shadowSampling = sharedLighting.find(
        "shadowVisibilityValue = localShadowVisibility(");
    if (!requireTrue(deferred.find("clustered_lighting.glsl") !=
                         std::string::npos &&
                         deferred.find("evaluateClusteredSurfaceLighting") !=
                         std::string::npos,
                     "deferred lighting must use the shared clustered evaluator") ||
        !requireTrue(staticMeshForward.find("clustered_lighting.glsl") !=
                         std::string::npos &&
                         staticMeshForward.find(
                             "evaluateClusteredSurfaceLighting") !=
                         std::string::npos,
                     "static mesh Forward+ must use the shared clustered evaluator") ||
        !requireTrue(terrainForward.find("clustered_lighting.glsl") !=
                         std::string::npos &&
                         terrainForward.find(
                             "evaluateClusteredSurfaceLighting") !=
                         std::string::npos,
                     "terrain Forward+ must use the shared clustered evaluator") ||
        !requireTrue(sharedLighting.find("evaluateGpuLight(") !=
                         std::string::npos,
                     "every clustered consumer must dispatch through evaluateGpuLight") ||
        !requireTrue(
            sharedLighting.find("sampler2D uLocalShadowSpotAtlas") !=
                    std::string::npos &&
            sharedLighting.find(
                "samplerCubeArray uLocalShadowPointCubeArray") !=
                    std::string::npos &&
            sharedLighting.find("visibility / 9.0") != std::string::npos,
            "shared consumers must evaluate Spot and Point 3x3 PCF") ||
        !requireTrue(
            contributionEvaluation != std::string::npos &&
                shadowSampling != std::string::npos &&
                contributionEvaluation < shadowSampling,
            "surface contribution rejection must precede local-shadow sampling") ||
        !requireTrue(
            lightEvaluation.find("inversesqrt(distanceSquared)") !=
                    std::string::npos &&
                lightEvaluation.find(
                    "distanceSquared, normalizedDistanceSquared") !=
                    std::string::npos &&
                lightEvaluation.find("normalize(light.direction.xyz)") ==
                    std::string::npos,
            "local-light evaluation must reuse squared distance and normalized contract directions")) {
        return false;
    }

    const std::string normalizedShared = removeWhitespace(sharedLighting);
    const std::string normalizedPipeline = removeWhitespace(pipeline);
    constexpr const char* kBindings[] = {
        "layout(set=MECRAFT_CLUSTER_BIND_SET,binding=0,std430)",
        "layout(set=MECRAFT_CLUSTER_BIND_SET,binding=1,std430)",
        "layout(set=MECRAFT_CLUSTER_BIND_SET,binding=2,std430)",
        "layout(set=MECRAFT_CLUSTER_BIND_SET,binding=3,std430)",
        "layout(set=MECRAFT_CLUSTER_BIND_SET,binding=4,std430)",
        "layout(set=MECRAFT_CLUSTER_BIND_SET,binding=5)",
        "layout(set=MECRAFT_CLUSTER_BIND_SET,binding=6)"};
    for (const char* binding : kBindings) {
        if (!requireTrue(normalizedShared.find(binding) != std::string::npos,
                         "shared consumers must expose the complete fixed resource layout")) {
            return false;
        }
    }

    constexpr const char* kResources[] = {
        "lights", "records", "compactIndices", "stats"};
    for (const char* resource : kResources) {
        const std::string read =
            std::string("readBuffer(clusteredLightingResources.") + resource;
        const size_t firstRead = normalizedPipeline.find(read);
        const size_t secondRead = firstRead == std::string::npos
            ? std::string::npos
            : normalizedPipeline.find(read, firstRead + read.size());
        if (!requireTrue(
                firstRead != std::string::npos &&
                    secondRead != std::string::npos,
                "deferred and Forward+ graph passes must read the same clustered buffers")) {
            return false;
        }
    }
    constexpr const char* kLocalShadowResources[] = {
        "readBuffer(localShadowResources.metadata",
        "readTexture(localShadowResources.spotAtlas",
        "readTexture(localShadowResources.pointCubeArray"};
    for (const char* resource : kLocalShadowResources) {
        const size_t firstRead = normalizedPipeline.find(resource);
        const size_t secondRead = firstRead == std::string::npos
            ? std::string::npos
            : normalizedPipeline.find(
                  resource, firstRead + std::string(resource).size());
        if (!requireTrue(
                firstRead != std::string::npos &&
                    secondRead != std::string::npos,
                "deferred and Forward+ graph passes must read local shadows")) {
            return false;
        }
    }

    const size_t localShadowPass = normalizedPipeline.find(
        "m_localShadowPass->addGraphPasses(");
    const size_t clusteredPass = normalizedPipeline.find(
        "m_clusteredLightingPass->addGraphPasses(");
    return requireTrue(
               localShadowPass != std::string::npos &&
                   clusteredPass != std::string::npos &&
                   localShadowPass < clusteredPass,
               "local shadow rendering must complete before cluster build") &&
           requireTrue(
               deferred.find("uDeferredDebugMode == 24") !=
                       std::string::npos &&
                   deferred.find("uDeferredDebugMode == 25") !=
                       std::string::npos &&
                   deferred.find("uDeferredDebugMode == 26") !=
                       std::string::npos,
               "deferred lighting must expose local-shadow diagnostics");
}

} // namespace

int main() {
    if (!testGridAndLogarithmicSlices() || !testCoverageAndCapacity() ||
        !testComputeShaderContracts() || !testReadbackCompletionContract() ||
        !testEmptyLightingSteadyStateContract() ||
        !testSharedLightingConsumers()) {
        return 1;
    }
    std::cout << "[clustered_lighting_contract_test] PASS\n";
    return 0;
}
