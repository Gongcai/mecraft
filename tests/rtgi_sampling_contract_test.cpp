#include "renderer/contracts/RtgiSamplingContract.h"
#include "renderer/core/RenderSettings.h"

#if defined(MECRAFT_ENABLE_NRD)
#include <NRD.h>
#endif

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>

namespace {
[[nodiscard]] bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] bool validateShaderMirror() {
    const std::string samplingPath = std::string(MECRAFT_TEST_SOURCE_DIR) + "/assets/shaders/rtgi_sampling.glsl";
    const std::string tracePath = std::string(MECRAFT_TEST_SOURCE_DIR) + "/assets/shaders/rtgi_trace.comp";
    const std::string counterPath =
        std::string(MECRAFT_TEST_SOURCE_DIR) + "/assets/shaders/rtgi_trace_counter_reduce.comp";
    const std::string pipelinePath = std::string(MECRAFT_TEST_SOURCE_DIR) + "/src/renderer/core/DeferredPipeline.cpp";
    std::ifstream samplingFile(samplingPath, std::ios::binary);
    std::ifstream traceFile(tracePath, std::ios::binary);
    std::ifstream counterFile(counterPath, std::ios::binary);
    std::ifstream pipelineFile(pipelinePath, std::ios::binary);
    if (!samplingFile.is_open() || !traceFile.is_open() || !counterFile.is_open() || !pipelineFile.is_open()) {
        return false;
    }
    const std::string samplingSource{std::istreambuf_iterator<char>(samplingFile), std::istreambuf_iterator<char>()};
    const std::string traceSource{std::istreambuf_iterator<char>(traceFile), std::istreambuf_iterator<char>()};
    const std::string counterSource{std::istreambuf_iterator<char>(counterFile), std::istreambuf_iterator<char>()};
    const std::string pipelineSource{std::istreambuf_iterator<char>(pipelineFile), std::istreambuf_iterator<char>()};
    return samplingSource.find("const uint RTGI_SECONDARY_LIGHTING_TERRAIN_NORMAL_MAP_BIT = 1u << 0u;") !=
               std::string::npos &&
           samplingSource.find("const uint RTGI_SECONDARY_LIGHTING_TERRAIN_SPECULAR_MAP_BIT = 1u << 1u;") !=
               std::string::npos &&
           samplingSource.find("const vec2 RTGI_R2_INCREMENT = vec2(0.7548776662466927, 0.5698402909980532);") !=
               std::string::npos &&
           samplingSource.find("const float RTGI_MINIMUM_RAY_ORIGIN_BIAS = RTGI_VOXEL_SURFACE_EXPANSION * 2.0;") !=
               std::string::npos &&
           samplingSource.find(
               "uint rtgiTerrainHitIdentityHash(uint revisionLow, uint revisionHigh, uvec2 vertexAddressWords)") !=
               std::string::npos &&
           traceSource.find("layout(std140, set = 1, binding = 16) uniform RtgiSecondaryLightingParams") !=
               std::string::npos &&
           traceSource.find("vec4 traceAndEmissionScales;") != std::string::npos &&
           traceSource.find("vec4 terrainLightScales;") != std::string::npos &&
           traceSource.find("uvec4 flags;") != std::string::npos &&
           traceSource.find("bool rasterPolicy = policy == GPU_LIGHT_SHADOW_RASTER_DYNAMIC") != std::string::npos &&
           traceSource.find("policy != GPU_LIGHT_SHADOW_RAY_QUERY") != std::string::npos &&
           traceSource.find("gpuLightShadowIndex(light) != GPU_LIGHT_INVALID_RESOURCE_INDEX") != std::string::npos &&
           traceSource.find("localShadowVisibility(light, cameraRelativeSurface") == std::string::npos &&
           traceSource.find("rtgiTraceVisibility(surfacePosition, originNormal, direction, maximumDistance") !=
               std::string::npos &&
           traceSource.find("float endpointDistance = maximumDistance - dot(originOffset, unitDirection);") !=
               std::string::npos &&
           traceSource.find("candidateDistance >= endpointDistance - endpointExclusionDistance") != std::string::npos &&
           traceSource.find("gpuLightPointSelfShadowRadius(light)") != std::string::npos &&
           traceSource.find("const float RTGI_METALLIC_DIFFUSE_TRANSPORT_FLOOR = 0.35;") != std::string::npos &&
           traceSource.find("const float RTGI_RADIANCE_FIREFLY_CLAMP = 8.0;") != std::string::npos &&
           traceSource.find("vec3 rtgiSuppressSolarSkyLobe(vec3 skyRadiance, vec3 worldDirection)") !=
               std::string::npos &&
           traceSource.find("rtgiSuppressSolarSkyLobe(sampleSkyRadiance(uSkyCapture, rayDirection), rayDirection)") !=
               std::string::npos &&
           traceSource.find("ivec2 noiseTexel = ivec2(uvec2(texel) % uvec2(noiseExtent));") != std::string::npos &&
           traceSource.find("rtgiPixelScrambledCranleyPattersonRotation(pc.frameMaskAndFlags.x, uvec2(texel))") !=
               std::string::npos &&
           traceSource.find("frameOffset") == std::string::npos &&
           traceSource.find("layout(set = 1, binding = 17) uniform sampler2D uVoxelLightTexture;") !=
               std::string::npos &&
           traceSource.find("rtgiPrimarySkyVisibility") != std::string::npos &&
           traceSource.find("vec3 rtgiVoxelGeometricNormal(vec3 shadingNormal)") != std::string::npos &&
           traceSource.find("materialKindId(materialAux.materialKind) <= MATERIAL_NETHER_ORE") != std::string::npos &&
           traceSource.find("if (!voxelPrimarySurface && adjacentDepthValid") != std::string::npos &&
           traceSource.find("vec3 samplingNormal = voxelPrimarySurface ? geometricNormal : normal;") !=
               std::string::npos &&
           traceSource.find("radiance += rtgiDiffuseTransportAlbedo(surface) * contribution.diffuse") !=
               std::string::npos &&
           traceSource.find("rtgiTerrainBlockLightIncident") == std::string::npos &&
           traceSource.find("surface.geometricNormal") != std::string::npos &&
           traceSource.find("rtgiAccumulateSkyAmbient") != std::string::npos &&
           traceSource.find("reflect(rayDirection, geometricNormal)") != std::string::npos &&
           traceSource.find("surface.albedo * (1.0 - surface.metalness)") == std::string::npos &&
           traceSource.find("optional local-light shadow resource must not erase") != std::string::npos &&
           traceSource.find("Local lights are optional secondary transport") != std::string::npos &&
           counterSource.find("const uint RTGI_TRACE_COUNTER_CONTRACT_VERSION = 1u;") != std::string::npos &&
           counterSource.find("void rtgiTraceCounterAtomicAdd64(uint lowWord, uint highWord, uint value)") !=
               std::string::npos &&
           counterSource.find("atomicMax(uCounters.words[RTGI_TRACE_COUNTER_PEAK_CANDIDATE]") != std::string::npos &&
           counterSource.find("any(notEqual(pc.renderExtentAndContract.xy, uvec2(imageSize(uValidation))))") !=
               std::string::npos &&
           pipelineSource.find(
               "const bool rtgiTraceInspection = isRtgiTraceInspectionView(settings.debug.viewMode);") !=
               std::string::npos &&
           pipelineSource.find("traceSettings.temporalSamplingEnabled = nrdEnabled && !rtgiTraceInspection;") !=
               std::string::npos &&
           pipelineSource.find("traceSettings.temporalSampleIndex = m_rtgiTemporalSampleIndex;") != std::string::npos &&
           pipelineSource.find("++m_rtgiTemporalSampleIndex;") != std::string::npos &&
           pipelineSource.find("m_lastNrdSceneTlasRevision") == std::string::npos &&
           pipelineSource.find("relaxSettings.atrousIterationNum =") != std::string::npos &&
           pipelineSource.find("nrdAccumulationFrameCount(::nrd::RELAX_DEFAULT_ACCUMULATION_TIME") !=
               std::string::npos &&
           pipelineSource.find("relaxSettings.antilagSettings.accelerationAmount = 0.0f;") == std::string::npos &&
           pipelineSource.find("relaxSettings.antilagSettings.resetAmount = 0.0f;") == std::string::npos &&
           pipelineSource.find("relaxSettings.enableAntiFirefly = true;") != std::string::npos &&
           pipelineSource.find("reblurSettings.enableAntiFirefly = true;") != std::string::npos &&
           pipelineSource.find("const bool nrdEnabled = rtgiEnabled && settings.nrd.enabled;") != std::string::npos &&
           pipelineSource.find(
               "traceSettings.celestialRadianceScale = settings.postProcess.directSunStrength * 64.0f;") !=
               std::string::npos &&
           pipelineSource.find("void DeferredPipeline::invalidateHistory() {\n"
                               "    m_hasPreviousFrameData = false;\n"
                               "    m_rtgiTemporalSampleIndex = 0u;\n"
                               "#if defined(MECRAFT_ENABLE_NRD)\n"
                               "    m_nrdClearHistory = true;") != std::string::npos;
}
} // namespace

int main() {
    using namespace renderer::contracts;

    bool valid = true;
#if defined(MECRAFT_ENABLE_NRD)
    const nrd::RelaxSettings defaultRelaxSettings{};
    valid = requireTrue(defaultRelaxSettings.antilagSettings.accelerationAmount > 0.0f &&
                            defaultRelaxSettings.antilagSettings.resetAmount > 0.0f,
                        "RELAX anti-lag must remain responsive to stale luminance history") &&
            valid;
#endif
    valid = requireTrue(kRtgiVoxelSurfaceExpansion == 1.0f / 2048.0f && kRtgiMinimumRayOriginBias == 1.0f / 1024.0f &&
                            RtgiSettings{}.minimumRayOriginBias == kRtgiMinimumRayOriginBias,
                        "RTGI ray-origin bias must remain larger than the sealed voxel BLAS shell") &&
            valid;
    valid = requireTrue(!isRtgiTraceInspectionView(88) && isRtgiTraceInspectionView(89) &&
                            isRtgiTraceInspectionView(90) && isRtgiTraceInspectionView(91) &&
                            isRtgiTraceInspectionView(92) && isRtgiTraceInspectionView(93) &&
                            isRtgiTraceInspectionView(94) && !isRtgiTraceInspectionView(95),
                        "RTGI trace inspection views must remain the exact 89-94 range") &&
            valid;
    valid = requireTrue(rtgiSampleHash(0u) == 0u && rtgiSampleHash(1u) == 1753845952u &&
                            rtgiSampleHash(0xffffffffu) == 1734902346u,
                        "RTGI sample hash must remain bit-exact") &&
            valid;
    valid = requireTrue(rtgiStableHitIdentityHash(601u, 501u) == 1366735474u &&
                            rtgiStableHitIdentityHash(602u, 502u) == 1027311900u &&
                            rtgiTerrainHitIdentityHash(1u, 0u) == 1753845952u &&
                            rtgiTerrainHitIdentityHash(1u, 1u) == 3875847014u && sizeof(RtgiTracePushConstants) == 128u,
                        "RTGI stable hit identity and push-constant contracts must remain bit-exact") &&
            valid;
    valid = requireTrue(
                sizeof(RtgiSecondaryLightingParams) == 128u && alignof(RtgiSecondaryLightingParams) == 16u &&
                    offsetof(RtgiSecondaryLightingParams, sunDirectionAndVisibility) == 0u &&
                    offsetof(RtgiSecondaryLightingParams, traceAndEmissionScales) == 80u &&
                    offsetof(RtgiSecondaryLightingParams, terrainLightScales) == 96u &&
                    offsetof(RtgiSecondaryLightingParams, flags) == 112u &&
                    RtgiSecondaryLightingParams{}.traceAndEmissionScales.w == 1.0f &&
                    RtgiSecondaryLightingParams{}.terrainLightScales == glm::vec4(0.0f) &&
                    kRtgiSecondaryLightingTerrainNormalMapBit == 1u &&
                    kRtgiSecondaryLightingTerrainSpecularMapBit == 2u && kRtgiMetallicDiffuseTransportFloor == 0.35f &&
                    (kRtgiSecondaryLightingTerrainNormalMapBit | kRtgiSecondaryLightingTerrainSpecularMapBit) == 3u,
                "RTGI secondary-lighting UBO, analytic-light ownership, terrain-map flags, and metallic transport "
                "must remain fixed") &&
            valid;

    const glm::vec2 firstRotation = rtgiCranleyPattersonRotation(0u);
    const glm::vec2 repeatedRotation = rtgiCranleyPattersonRotation(0u);
    const glm::vec2 nextRotation = rtgiCranleyPattersonRotation(1u);
    valid = requireTrue(firstRotation == repeatedRotation && firstRotation.x >= 0.0f && firstRotation.x < 1.0f &&
                            firstRotation.y >= 0.0f && firstRotation.y < 1.0f && firstRotation != nextRotation,
                        "RTGI Cranley-Patterson rotation must be deterministic and frame-varying") &&
            valid;
    constexpr glm::vec2 kExpectedR2Step{0.7548776662466927f, 0.5698402909980532f};
    const glm::vec2 wrappedStep = glm::mod(nextRotation - firstRotation + glm::vec2(1.0f), glm::vec2(1.0f));
    valid = requireTrue(glm::length(wrappedStep - kExpectedR2Step) <= 1.0e-6f,
                        "RTGI frame rotation must advance by the low-discrepancy R2 step") &&
            valid;

    const glm::vec2 scrambled = rtgiPixelScrambledCranleyPattersonRotation(19u, glm::uvec2(7u, 11u));
    const glm::vec2 repeatedScramble = rtgiPixelScrambledCranleyPattersonRotation(19u, glm::uvec2(7u, 11u));
    const glm::vec2 adjacentScramble = rtgiPixelScrambledCranleyPattersonRotation(19u, glm::uvec2(8u, 11u));
    const glm::vec2 nextScramble = rtgiPixelScrambledCranleyPattersonRotation(20u, glm::uvec2(7u, 11u));
    valid = requireTrue(scrambled == repeatedScramble && scrambled != adjacentScramble && scrambled != nextScramble &&
                            glm::all(glm::greaterThanEqual(scrambled, glm::vec2(0.0f))) &&
                            glm::all(glm::lessThan(scrambled, glm::vec2(1.0f))),
                        "RTGI pixel scrambling must be deterministic, spatially decorrelated, and frame-varying") &&
            valid;

    const std::optional<glm::vec3> pole = rtgiCosineHemisphereDirection(glm::vec2(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    valid = requireTrue(pole.has_value() && glm::length(*pole - glm::vec3(0.0f, 1.0f, 0.0f)) <= 1.0e-6f,
                        "Zero radial RTGI sample must map to the surface normal") &&
            valid;

    const std::optional<glm::vec3> voxelPositiveX =
        rtgiVoxelGeometricNormal(glm::normalize(glm::vec3(0.91f, 0.35f, -0.22f)));
    const std::optional<glm::vec3> voxelNegativeY =
        rtgiVoxelGeometricNormal(glm::normalize(glm::vec3(0.18f, -0.94f, 0.29f)));
    const std::optional<glm::vec3> voxelPositiveZ =
        rtgiVoxelGeometricNormal(glm::normalize(glm::vec3(-0.31f, 0.42f, 0.85f)));
    valid = requireTrue(voxelPositiveX.has_value() && voxelNegativeY.has_value() && voxelPositiveZ.has_value() &&
                            glm::length(*voxelPositiveX - glm::vec3(1.0f, 0.0f, 0.0f)) <= 1.0e-6f &&
                            glm::length(*voxelNegativeY - glm::vec3(0.0f, -1.0f, 0.0f)) <= 1.0e-6f &&
                            glm::length(*voxelPositiveZ - glm::vec3(0.0f, 0.0f, 1.0f)) <= 1.0e-6f &&
                            !rtgiVoxelGeometricNormal(glm::vec3(0.0f)).has_value(),
                        "RTGI voxel geometric normals must remain stable under terrain normal mapping") &&
            valid;

    const std::optional<glm::vec3> known =
        rtgiCosineHemisphereDirection(glm::vec2(0.25f, 0.75f), glm::vec3(0.0f, 1.0f, 0.0f));
    valid = requireTrue(known.has_value() && std::abs(glm::length(*known) - 1.0f) <= 1.0e-5f &&
                            std::abs(glm::dot(*known, glm::vec3(0.0f, 1.0f, 0.0f)) - 0.5f) <= 1.0e-5f,
                        "RTGI cosine sample must preserve unit length and the analytic cosine") &&
            valid;

    valid =
        requireTrue(!rtgiCosineHemisphereDirection(glm::vec2(-0.1f, 0.5f), glm::vec3(0.0f, 1.0f, 0.0f)).has_value() &&
                        !rtgiCosineHemisphereDirection(glm::vec2(0.5f), glm::vec3(0.0f)).has_value(),
                    "RTGI cosine sampling must reject invalid input contracts") &&
        valid;

    const glm::vec3 cameraPosition{1000000.0f, 96.0f, -2000000.0f};
    const glm::vec3 sceneOrigin{999936.0f, 0.0f, -2000000.0f};
    const glm::mat4 projection = glm::perspective(glm::radians(70.0f), 16.0f / 9.0f, 0.1f, 500.0f);
    const glm::mat4 view =
        glm::lookAt(cameraPosition, cameraPosition + glm::vec3(0.2f, -0.1f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 cameraRelativeInverseViewProjection;
    const bool cameraRelativeMatrix = makeRtgiCameraRelativeInverseViewProjection(
        projection, view, cameraPosition, sceneOrigin, cameraRelativeInverseViewProjection);
    const glm::mat4 viewRotation = glm::mat4(glm::mat3(view));
    const glm::vec3 scenePoint{7.0f, 3.0f, -11.0f};
    const glm::vec3 cameraRelativePoint = scenePoint - (cameraPosition - sceneOrigin);
    const glm::vec4 clipPoint = projection * viewRotation * glm::vec4(cameraRelativePoint, 1.0f);
    const glm::vec4 reconstructedPointH = cameraRelativeInverseViewProjection * clipPoint;
    const glm::vec3 reconstructedPoint = glm::vec3(reconstructedPointH) / reconstructedPointH.w;
    valid = requireTrue(
                cameraRelativeMatrix && std::abs(reconstructedPointH.w) > 1.0e-6f &&
                    glm::length(reconstructedPoint - scenePoint) <= 2.0e-3f &&
                    !makeRtgiCameraRelativeInverseViewProjection(projection, view, glm::vec3(NAN), sceneOrigin,
                                                                 cameraRelativeInverseViewProjection),
                "RTGI camera-relative reconstruction must preserve large-world positions and reject invalid input") &&
            valid;

    constexpr uint32_t kSampleCount = 4096u;
    double cosineSum = 0.0;
    for (uint32_t index = 0u; index < kSampleCount; ++index) {
        const glm::vec2 sample{static_cast<float>(rtgiSampleHash(index * 2u) & 0x00ffffffu) / 16777216.0f,
                               static_cast<float>(rtgiSampleHash(index * 2u + 1u) & 0x00ffffffu) / 16777216.0f};
        const std::optional<glm::vec3> direction = rtgiCosineHemisphereDirection(sample, glm::vec3(0.0f, 0.0f, 1.0f));
        if (!direction.has_value() || std::abs(glm::length(*direction) - 1.0f) > 1.0e-5f || direction->z < 0.0f) {
            valid = false;
            break;
        }
        cosineSum += direction->z;
    }
    const double meanCosine = cosineSum / static_cast<double>(kSampleCount);
    valid = requireTrue(valid && std::abs(meanCosine - (2.0 / 3.0)) <= 0.02,
                        "RTGI sample distribution must remain cosine weighted") &&
            valid;

    valid = requireTrue(static_cast<uint32_t>(RtgiTraceClassification::Sky) == 0u &&
                            static_cast<uint32_t>(RtgiTraceClassification::Translucent) == 1u &&
                            static_cast<uint32_t>(RtgiTraceClassification::Miss) == 2u &&
                            static_cast<uint32_t>(RtgiTraceClassification::Hit) == 3u &&
                            static_cast<uint32_t>(RtgiTraceClassification::NonFinite) == 4u,
                        "RTGI validation classifications must remain stable") &&
            valid;

    const std::optional<uint32_t> packedValidation = encodeRtgiTraceValidation(RtgiTraceClassification::Hit, 17u, 1u);
    valid =
        requireTrue(
            packedValidation.has_value() &&
                rtgiTraceValidationClassification(*packedValidation) == RtgiTraceClassification::Hit &&
                rtgiTraceValidationCandidateCount(*packedValidation) == 17u &&
                rtgiTraceValidationConfirmedCount(*packedValidation) == 1u &&
                !encodeRtgiTraceValidation(RtgiTraceClassification::Miss, kRtgiTraceValidationCandidateMask + 1u, 0u)
                     .has_value() &&
                !encodeRtgiTraceValidation(RtgiTraceClassification::Miss, 0u, kRtgiTraceValidationConfirmedMask + 1u)
                     .has_value(),
            "RTGI validation packing must preserve classification and Cutout counters") &&
        valid;

    std::array<uint32_t, kRtgiTraceCounterWordCount> counterWords{};
    counterWords[static_cast<size_t>(RtgiTraceCounterWord::CandidateLow)] = 4u;
    counterWords[static_cast<size_t>(RtgiTraceCounterWord::CandidateHigh)] = 1u;
    counterWords[static_cast<size_t>(RtgiTraceCounterWord::ConfirmedLow)] = 2u;
    counterWords[static_cast<size_t>(RtgiTraceCounterWord::ConfirmedHigh)] = 1u;
    counterWords[static_cast<size_t>(RtgiTraceCounterWord::PeakCandidatePerPixel)] = 4u;
    counterWords[static_cast<size_t>(RtgiTraceCounterWord::PeakConfirmedPerPixel)] = 2u;
    counterWords[static_cast<size_t>(RtgiTraceCounterWord::PixelHigh)] = 1u;
    counterWords[static_cast<size_t>(RtgiTraceCounterWord::ContractVersion)] = kRtgiTraceCounterContractVersion;
    const std::optional<RtgiTraceCounterFrameStats> decodedCounters =
        decodeRtgiTraceCounterReadback(counterWords, 7u, 91u, 65536u, 65536u);
    counterWords[static_cast<size_t>(RtgiTraceCounterWord::InvariantError)] = 1u;
    valid =
        requireTrue(decodedCounters.has_value() && decodedCounters->sequence == 7u &&
                        decodedCounters->frameIndex == 91u && decodedCounters->pixelCount == (uint64_t{1u} << 32u) &&
                        decodedCounters->candidateCount == (uint64_t{1u} << 32u) + 4u &&
                        decodedCounters->confirmedCount == (uint64_t{1u} << 32u) + 2u &&
                        !decodeRtgiTraceCounterReadback(counterWords, 8u, 92u, 65536u, 65536u).has_value(),
                    "RTGI counter readback must preserve 64-bit totals and reject GPU invariant errors") &&
        valid;
    valid = requireTrue(validateShaderMirror(), "RTGI GLSL flags and secondary-lighting UBO must mirror C++") && valid;
    return valid ? 0 : 1;
}
