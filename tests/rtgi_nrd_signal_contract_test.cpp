#include "renderer/contracts/RtgiNrdSignalContract.h"

#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>

namespace {
[[nodiscard]] bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] bool near(const float lhs, const float rhs, const float tolerance = 1.0e-6f) {
    return std::abs(lhs - rhs) <= tolerance;
}

[[nodiscard]] bool validateShaderMirror() {
    const std::string signalPath = std::string(MECRAFT_TEST_SOURCE_DIR) + "/assets/shaders/rtgi_nrd_signal.glsl";
    const std::string packPath = std::string(MECRAFT_TEST_SOURCE_DIR) + "/assets/shaders/rtgi_nrd_signal_pack.comp";
    const std::string tracePath = std::string(MECRAFT_TEST_SOURCE_DIR) + "/assets/shaders/rtgi_trace.comp";
    const std::string guidePath = std::string(MECRAFT_TEST_SOURCE_DIR) + "/assets/shaders/nrd_guide_prep.comp";
    const std::string lightingPath = std::string(MECRAFT_TEST_SOURCE_DIR) + "/assets/shaders/deferred_lighting.frag";
    const std::string pipelinePath = std::string(MECRAFT_TEST_SOURCE_DIR) + "/src/renderer/core/DeferredPipeline.cpp";
    const std::string targetsPath =
        std::string(MECRAFT_TEST_SOURCE_DIR) + "/src/renderer/targets/DeferredRenderTargets.h";
    const std::string debugPassPath = std::string(MECRAFT_TEST_SOURCE_DIR) + "/src/renderer/passes/DebugPass.cpp";
    const std::string debugShaderPath = std::string(MECRAFT_TEST_SOURCE_DIR) + "/assets/shaders/deferred_debug.frag";
    const std::string scenePath = std::string(MECRAFT_TEST_SOURCE_DIR) + "/src/renderer/core/RenderScene.cpp";
    const std::string modelScenePath =
        std::string(MECRAFT_TEST_SOURCE_DIR) + "/src/scene/ModelSceneDeferredRenderer.cpp";
    std::ifstream signalFile(signalPath, std::ios::binary);
    std::ifstream packFile(packPath, std::ios::binary);
    std::ifstream traceFile(tracePath, std::ios::binary);
    std::ifstream guideFile(guidePath, std::ios::binary);
    std::ifstream lightingFile(lightingPath, std::ios::binary);
    std::ifstream pipelineFile(pipelinePath, std::ios::binary);
    std::ifstream targetsFile(targetsPath, std::ios::binary);
    std::ifstream debugPassFile(debugPassPath, std::ios::binary);
    std::ifstream debugShaderFile(debugShaderPath, std::ios::binary);
    std::ifstream sceneFile(scenePath, std::ios::binary);
    std::ifstream modelSceneFile(modelScenePath, std::ios::binary);
    if (!signalFile.is_open() || !packFile.is_open() || !traceFile.is_open() || !guideFile.is_open() ||
        !lightingFile.is_open() || !pipelineFile.is_open() || !targetsFile.is_open() || !debugPassFile.is_open() ||
        !debugShaderFile.is_open() || !sceneFile.is_open() || !modelSceneFile.is_open()) {
        return false;
    }
    const std::string signalSource{std::istreambuf_iterator<char>(signalFile), std::istreambuf_iterator<char>()};
    const std::string packSource{std::istreambuf_iterator<char>(packFile), std::istreambuf_iterator<char>()};
    const std::string traceSource{std::istreambuf_iterator<char>(traceFile), std::istreambuf_iterator<char>()};
    const std::string guideSource{std::istreambuf_iterator<char>(guideFile), std::istreambuf_iterator<char>()};
    const std::string lightingSource{std::istreambuf_iterator<char>(lightingFile), std::istreambuf_iterator<char>()};
    const std::string pipelineSource{std::istreambuf_iterator<char>(pipelineFile), std::istreambuf_iterator<char>()};
    const std::string targetsSource{std::istreambuf_iterator<char>(targetsFile), std::istreambuf_iterator<char>()};
    const std::string debugPassSource{std::istreambuf_iterator<char>(debugPassFile), std::istreambuf_iterator<char>()};
    const std::string debugShaderSource{std::istreambuf_iterator<char>(debugShaderFile),
                                        std::istreambuf_iterator<char>()};
    const std::string sceneSource{std::istreambuf_iterator<char>(sceneFile), std::istreambuf_iterator<char>()};
    const std::string modelSceneSource{std::istreambuf_iterator<char>(modelSceneFile),
                                       std::istreambuf_iterator<char>()};
    return signalSource.find("const float RTGI_NRD_FP16_MAX = 65504.0;") != std::string::npos &&
           signalSource.find("const float RTGI_NRD_EPSILON = 1.0e-6;") != std::string::npos &&
           signalSource.find("dot(color, vec3(0.25, 0.5, 0.25))") != std::string::npos &&
           signalSource.find("exp2(-200.0 * roughness * roughness)") != std::string::npos &&
           packSource.find("layout(set = 0, binding = 4, rg32ui) uniform uimage2D uValidation;") != std::string::npos &&
           packSource.find("MECRAFT_RTGI_SIGNAL_PACK_RELAX") != std::string::npos &&
           packSource.find("MECRAFT_RTGI_SIGNAL_PACK_REBLUR") != std::string::npos &&
           packSource.find("classification != RTGI_TRACE_CLASS_HIT && classification != RTGI_TRACE_CLASS_MISS") !=
               std::string::npos &&
           packSource.find("vec3 sceneRadiance = rawSignal.rgb * pc.preExposureAndInverse.y;") != std::string::npos &&
           packSource.find("uvec4(validationWord, validation.y, 0u, 0u)") != std::string::npos &&
           traceSource.find("radiance * uSecondaryLighting.traceAndEmissionScales.w") != std::string::npos &&
           traceSource.find("missRadiance, RTGI_NRD_FP16_MAX, RTGI_TRACE_CLASS_MISS") != std::string::npos &&
           traceSource.find("rtgiOffsetSurfaceOrigin") != std::string::npos &&
           traceSource.find("dot(geometricNormal, outgoingDirection) < 0.0 ? -1.0 : 1.0") != std::string::npos &&
           guideSource.find("layout(binding = 4, rgba16f) uniform writeonly image2D uMotion;") != std::string::npos &&
           guideSource.find("layout(binding = 7, r8) uniform writeonly image2D uReprojectionCoverage;") !=
               std::string::npos &&
           guideSource.find("uValidationTexture") == std::string::npos &&
           guideSource.find("uPreviousValidationTexture") == std::string::npos &&
           guideSource.find("uValidationHistory") == std::string::npos &&
           guideSource.find("vec2 motion = -texelFetch(uVelocityTexture, texel, 0).rg;") != std::string::npos &&
           guideSource.find("float nrdGuideMaterialId(SurfaceMaterial material, float roughness)") !=
               std::string::npos &&
           guideSource.find("vec3 nrdGuideVoxelGeometricNormal(vec3 shadingNormal)") != std::string::npos &&
           guideSource.find("vec3 leakageNormal = pc.flags.x > 0.5 ? nrdGuideVoxelGeometricNormal(normal) : normal;") !=
               std::string::npos &&
           guideSource.find("imageStore(uNormalRoughness, texel, nrdGuidePackNormalRoughness(normal, roughness, materialId));") !=
               std::string::npos &&
           guideSource.find("packed.w = clamp(materialId / 3.0, 0.0, 1.0);") != std::string::npos &&
           guideSource.find("nrdGuideReconstructPositiveViewZ") != std::string::npos &&
           guideSource.find("float motionViewZ = 0.0;") != std::string::npos &&
           guideSource.find("pc.currentClipToPreviousView, screenUv, depth") != std::string::npos &&
           guideSource.find("motionViewZ = previousPositiveViewZ - currentPositiveViewZ;") != std::string::npos &&
           guideSource.find("uHistoryDepthTexture") == std::string::npos &&
           guideSource.find("float signedViewZ = currentPositiveViewZ < invalidViewZ ? -currentPositiveViewZ : "
                            "-invalidViewZ;") != std::string::npos &&
           guideSource.find("viewPosition.z >= -1.0e-7") != std::string::npos &&
           guideSource.find("positiveViewZ = -viewPosition.z;") != std::string::npos &&
           guideSource.find("imageStore(uReprojectionCoverage, texel, vec4(reprojectionCoverage, 0.0, 0.0, 0.0));") !=
               std::string::npos &&
           guideSource.find("It is not NRD history confidence") != std::string::npos &&
           pipelineSource.find("glm::vec2 nrdCameraJitterPixels(const TemporalJitter& jitter)") != std::string::npos &&
           pipelineSource.find("return -jitter.pixels;") != std::string::npos &&
           pipelineSource.find("commonSettings.motionVectorScale[2] = 1.0f;") != std::string::npos &&
           pipelineSource.find("relaxSettings.minMaterialForDiffuse = 4.0f;") != std::string::npos &&
           pipelineSource.find("relaxSettings.enableAntiFirefly = false;") != std::string::npos &&
           pipelineSource.find("reblurSettings.minMaterialForDiffuse = 4.0f;") != std::string::npos &&
           pipelineSource.find("guideSettings.historyValid = !m_nrdClearHistory && !nrdTemporalReset;") !=
               std::string::npos &&
           pipelineSource.find("guideSettings.useJitteredProjection = traceSettings.useJitteredProjection;") !=
               std::string::npos &&
           pipelineSource.find("guideSettings.useVoxelGeometricNormal = m_shared->terrain != nullptr;") !=
               std::string::npos &&
           pipelineSource.find("IN_DIFF_CONFIDENCE") == std::string::npos &&
           pipelineSource.find("commonSettings.isHistoryConfidenceAvailable = false;") != std::string::npos &&
           pipelineSource.find("guideResources.reprojectionCoverage = nrdReprojectionCoverage;") != std::string::npos &&
           pipelineSource.find(
               "const RhiTextureUsageFlags sampledStorage = rhiFlag(RhiTextureUsage::Sampled) |\n"
               "                                                    rhiFlag(RhiTextureUsage::Storage) |\n"
               "                                                    rhiFlag(RhiTextureUsage::TransferSrc);") !=
               std::string::npos &&
           pipelineSource.find("const RhiTextureUsageFlags nrdOutputUsage =") != std::string::npos &&
           pipelineSource.find("sampledStorage | rhiFlag(RhiTextureUsage::ColorAttachment)") != std::string::npos &&
           pipelineSource.find("NRD.OutputInit") != std::string::npos &&
           pipelineSource.find("RTGI.RawDiffuseValidationCopy") != std::string::npos &&
           pipelineSource.find("NRD.DiffuseValidationCopy") != std::string::npos &&
           pipelineSource.find("ensureRtgiValidationOutputTextures") != std::string::npos &&
           pipelineSource.find("m_rtgiRawDiffuseValidationTexture") != std::string::npos &&
           pipelineSource.find("m_nrdDiffuseValidationTexture") != std::string::npos &&
           pipelineSource.find("writeTexture(nrdOutputDiffuse, RhiResourceState::RenderTarget)") != std::string::npos &&
           pipelineSource.find("NRD.ValidationDescriptorInit") != std::string::npos &&
           pipelineSource.find("writeTexture(nrdValidation, RhiResourceState::RenderTarget)") != std::string::npos &&
           pipelineSource.find("writeTexture(historyDepthCurrent, RhiResourceState::TransferDst)") !=
               std::string::npos &&
           pipelineSource.find("targets.swapHistory();") != std::string::npos &&
           targetsSource.find("historyDepthTexturePrevHandle() const") != std::string::npos &&
           targetsSource.find("m_historyDepthHandle[1 - m_currentHistoryIndex]") != std::string::npos &&
           targetsSource.find("nrdReprojectionCoverageTextureHandle() const") != std::string::npos &&
           targetsSource.find("historyConfidenceTexture") == std::string::npos &&
           targetsSource.find("historyRtgiValidationTexture") == std::string::npos &&
           debugPassSource.find("!isNrdValidationView(settings.debug.viewMode)") == std::string::npos &&
           debugPassSource.find("params.rtgiParams = glm::vec4(nrdEncoding, ctx.preExposure") != std::string::npos &&
           debugShaderSource.find("vec3 debugRawRtgiSceneRadiance(vec2 textureUv)") != std::string::npos &&
           debugShaderSource.find("/ uRtgiPreExposure;") != std::string::npos &&
           debugShaderSource.find("vec3 debugNrdSceneRadiance(vec2 textureUv)") != std::string::npos &&
           debugShaderSource.find("uNrdDiffuseEncoding == 2.0") != std::string::npos &&
           debugShaderSource.find("radiance = nrdYCoCgToLinear(radiance);") != std::string::npos &&
           debugShaderSource.find("vec3 raw = debugRawRtgiSceneRadiance(textureUv);") != std::string::npos &&
           debugShaderSource.find("vec3 denoised = debugNrdSceneRadiance(textureUv);") != std::string::npos &&
           sceneSource.find("ctx.jitter.pixels.x = frameX * 0.5f;") != std::string::npos &&
           sceneSource.find("ctx.jitter.pixels.y = -frameY * 0.5f;") != std::string::npos &&
           sceneSource.find("m_previousContext.camera.projection != ctx.camera.projection") == std::string::npos &&
           modelSceneSource.find("previousContext.camera.projection != context.camera.projection") ==
               std::string::npos &&
           lightingSource.find("uRtgiRadianceScale pRtgi.z") != std::string::npos &&
           lightingSource.find("* uRtgiRadianceScale;") != std::string::npos;
}
} // namespace

int main() {
    using namespace renderer::contracts;

    bool valid = true;
    valid = requireTrue(kRtgiNrdFp16Max == 65504.0f && kRtgiNrdEpsilon == 1.0e-6f,
                        "RTGI NRD FP16 and epsilon constants must remain exact") &&
            valid;
    valid = requireTrue(sizeof(RtgiSignalPackPushConstants) == 112u && alignof(RtgiSignalPackPushConstants) == 16u &&
                            offsetof(RtgiSignalPackPushConstants, inverseProjection) == 0u &&
                            offsetof(RtgiSignalPackPushConstants, renderExtentAndInverse) == 64u &&
                            offsetof(RtgiSignalPackPushConstants, reblurParametersAndDiffuseRoughness) == 80u &&
                            offsetof(RtgiSignalPackPushConstants, preExposureAndInverse) == 96u,
                        "RTGI signal-pack push constants must remain Vulkan-minimum compatible") &&
            valid;

    const std::optional<glm::vec3> sceneRadiance = rtgiRemovePreExposure(glm::vec3(4.0f, 8.0f, 12.0f), 4.0f);
    const std::optional<glm::vec3> restoredRadiance =
        sceneRadiance.has_value() ? rtgiApplyPreExposure(*sceneRadiance, 4.0f) : std::nullopt;
    valid = requireTrue(sceneRadiance == std::optional(glm::vec3(1.0f, 2.0f, 3.0f)) &&
                            restoredRadiance == std::optional(glm::vec3(4.0f, 8.0f, 12.0f)) &&
                            !rtgiRemovePreExposure(glm::vec3(1.0f), 0.0f).has_value() &&
                            !rtgiApplyPreExposure(glm::vec3(1.0f), std::numeric_limits<float>::infinity()).has_value(),
                        "RTGI/NRD pre-exposure conversion must be finite, positive, and reversible") &&
            valid;

    const RtgiReblurHitDistanceParameters parameters;
    const std::optional<float> normalized = rtgiReblurNormalizedHitDistance(2.0f, -10.0f, parameters, 1.0f);
    const std::optional<float> glossyNormalized = rtgiReblurNormalizedHitDistance(2.0f, -10.0f, parameters, 0.0f);
    const std::optional<float> missNormalized =
        rtgiReblurNormalizedHitDistance(kRtgiNrdFp16Max, -10.0f, parameters, 1.0f);
    const std::optional<float> contactNormalized = rtgiReblurNormalizedHitDistance(0.0f, -10.0f, parameters, 1.0f);
    valid =
        requireTrue(normalized.has_value() && near(*normalized, 0.5f) && glossyNormalized.has_value() &&
                        near(*glossyNormalized, 0.025f) && missNormalized.has_value() && near(*missNormalized, 1.0f) &&
                        contactNormalized.has_value() && near(*contactNormalized, kRtgiNrdEpsilon),
                    "REBLUR normalized hit distance must match NRD 4.17") &&
        valid;

    const std::optional<glm::vec4> relax = rtgiPackRelaxRadianceAndHitDistance(glm::vec3(1.0f, 2.0f, 3.0f), 2.0f);
    const std::optional<glm::vec4> reblur =
        normalized.has_value()
            ? rtgiPackReblurRadianceAndNormalizedHitDistance(glm::vec3(1.0f, 2.0f, 3.0f), *normalized)
            : std::nullopt;
    valid = requireTrue(relax.has_value() && *relax == glm::vec4(1.0f, 2.0f, 3.0f, 2.0f) && reblur.has_value() &&
                            near(reblur->x, 2.0f) && near(reblur->y, -1.0f) && near(reblur->z, 0.0f) &&
                            near(reblur->w, 0.5f),
                        "RELAX raw distance and REBLUR YCoCg packing must remain method specific") &&
            valid;

    const std::optional<glm::vec4> bounded = rtgiPackRelaxRadianceAndHitDistance(glm::vec3(70000.0f), 70000.0f);
    RtgiReblurHitDistanceParameters invalidParameters = parameters;
    invalidParameters.viewZScale = 0.0f;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    valid = requireTrue(bounded.has_value() && *bounded == glm::vec4(kRtgiNrdFp16Max) &&
                            !rtgiReblurHitDistanceParametersValid(invalidParameters) &&
                            !rtgiReblurNormalizedHitDistance(1.0f, 1.0f, invalidParameters, 1.0f).has_value() &&
                            !rtgiReblurNormalizedHitDistance(-1.0f, 1.0f, parameters, 1.0f).has_value() &&
                            !rtgiPackRelaxRadianceAndHitDistance(glm::vec3(nan), 1.0f).has_value() &&
                            !rtgiPackRelaxRadianceAndHitDistance(glm::vec3(-1.0f), 1.0f).has_value() &&
                            !rtgiPackReblurRadianceAndNormalizedHitDistance(glm::vec3(1.0f), 1.1f).has_value(),
                        "RTGI NRD packing must reject invalid raw signal contracts") &&
            valid;

    valid = requireTrue(validateShaderMirror(), "RTGI NRD GLSL and raw-trace output must mirror C++") && valid;
    return valid ? 0 : 1;
}
