#include "renderer/contracts/SkyIblContract.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
bool requireTrue(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "[sky_ibl_contract_test] FAIL: " << message << '\n';
    return false;
  }
  return true;
}

bool readProjectFile(const char *relativePath, std::string &source) {
  const std::string path =
      std::string(MECRAFT_TEST_SOURCE_DIR) + "/" + relativePath;
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open())
    return false;
  source.assign(std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>());
  return true;
}

bool testProductContract() {
  using namespace renderer::contracts;
  return requireTrue(kSkyIblCubeExtent == 128u,
                     "sky radiance must use the fixed 128-pixel cube extent") &&
         requireTrue(kSkyIblCubeMipCount == 8u,
                     "the GGX chain must cover eight complete mip levels") &&
         requireTrue(kSkyIblDfgExtent == 256u,
                     "the split-sum DFG LUT must use a 256-pixel extent") &&
         requireTrue(
             kSkyIblGgxSampleCount == 128u,
             "runtime GGX convolution must retain the fixed sample budget") &&
         requireTrue(kSkyIblGenerationCount == 2u,
                     "sky IBL must retain exactly two atomic generations") &&
         requireTrue(kSkyIblPrefilterWorkItemCount == 48u,
                     "the distributed build must cover every face and mip") &&
         requireTrue(kSkyIblRevisionIntervalFrames == 48u,
                     "sky revisions must match the complete build cadence");
}

bool testRevisionAndWorkMapping() {
  using namespace renderer::contracts;
  return requireTrue(skyIblRevisionForFrame(0u) == 1u &&
                         skyIblRevisionForFrame(47u) == 1u &&
                         skyIblRevisionForFrame(48u) == 2u,
                     "sky revisions must advance only at complete-build "
                     "boundaries") &&
         requireTrue(skyIblMipForWorkItem(0u) == 0u &&
                         skyIblFaceForWorkItem(0u) == 0u &&
                         skyIblMipForWorkItem(5u) == 0u &&
                         skyIblFaceForWorkItem(5u) == 5u &&
                         skyIblMipForWorkItem(6u) == 1u &&
                         skyIblFaceForWorkItem(6u) == 0u &&
                         skyIblMipForWorkItem(47u) == 7u &&
                         skyIblFaceForWorkItem(47u) == 5u,
                     "linear work items must cover every mip and face exactly "
                     "once");
}

bool testRoughnessMipMapping() {
  using namespace renderer::contracts;
  constexpr float epsilon = 1e-6f;
  return requireTrue(std::abs(skyIblRoughnessForMip(0u)) < epsilon,
                     "mip zero must represent a perfectly smooth surface") &&
         requireTrue(std::abs(skyIblRoughnessForMip(7u) - 1.0f) < epsilon,
                     "the final mip must represent roughness one") &&
         requireTrue(std::abs(skyIblMipForRoughness(0.5f) - 3.5f) < epsilon,
                     "roughness must select a continuous fractional mip") &&
         requireTrue(
             std::abs(skyIblMipForRoughness(-1.0f)) < epsilon &&
                 std::abs(skyIblMipForRoughness(2.0f) - 7.0f) < epsilon,
             "roughness-to-mip conversion must clamp to the product range");
}

bool testGenerationAndConsumptionContract() {
  std::string pass;
  std::string prefilter;
  std::string dfg;
  std::string reflection;
  if (!requireTrue(readProjectFile("src/renderer/passes/SkyIblPass.cpp", pass),
                   "sky IBL pass source must be readable") ||
      !requireTrue(
          readProjectFile("assets/shaders/sky_ibl_prefilter.frag", prefilter),
          "GGX prefilter shader must be readable") ||
      !requireTrue(readProjectFile("assets/shaders/sky_ibl_dfg.frag", dfg),
                   "DFG integration shader must be readable") ||
      !requireTrue(
          readProjectFile("assets/shaders/reflection_probe.frag", reflection),
          "reflection shader must be readable")) {
    return false;
  }
  const size_t reflectionMask =
      reflection.find("if (!hasDerivativeReflection)");
  const size_t mipDebug =
      reflection.find("uReflectionDebugMode == 31");
  const size_t dfgDebug =
      reflection.find("uReflectionDebugMode == 32");
  return requireTrue(pass.find("SkyIbl.Radiance") != std::string::npos &&
                         pass.find("SkyIbl.GgxPrefilter") !=
                             std::string::npos &&
                         pass.find("SkyIbl.DfgLut") != std::string::npos,
                     "the render graph must expose all three IBL products") &&
         requireTrue(
             pass.find("SkyIbl.Radiance.Generation0") != std::string::npos &&
                 pass.find("SkyIbl.Radiance.Generation1") !=
                     std::string::npos &&
                 pass.find("m_bootstrapBuild ? remaining : 1u") !=
                     std::string::npos,
             "generation resources must distribute one face/mip after the "
             "complete bootstrap") &&
         requireTrue(
             pass.find("m_activeGeneration = m_buildGeneration") !=
                     std::string::npos &&
                 pass.find("m_consumerGeneration = m_activeGeneration") !=
                     std::string::npos &&
                 pass.find("m_nextPrefilterWorkItem ==") !=
                     std::string::npos,
             "the consumer generation must switch only after every work item "
             "commits") &&
         requireTrue(prefilter.find("skyIblImportanceSampleGgx") !=
                             std::string::npos &&
                         prefilter.find("uSampleCount") != std::string::npos,
                     "specular prefiltering must use bounded GGX importance "
                     "sampling") &&
         requireTrue(
             dfg.find("integrateDfg") != std::string::npos &&
                 dfg.find("skyIblGeometrySmith") != std::string::npos,
             "the DFG LUT must integrate the split-sum visibility term") &&
         requireTrue(reflection.find("samplerCube uSkySpecularPrefilter") !=
                             std::string::npos &&
                         reflection.find("textureLod(") != std::string::npos &&
                         reflection.find("sampler2D uSkyDfgLut") !=
                             std::string::npos,
                     "reflection composition must consume roughness mip and "
                     "DFG products") &&
         requireTrue(mipDebug != std::string::npos &&
                         dfgDebug != std::string::npos &&
                         reflectionMask != std::string::npos &&
                         mipDebug < reflectionMask && dfgDebug < reflectionMask,
                     "roughness mip and DFG must cover every geometry pixel") &&
         requireTrue(
             reflection.find("skyReflection * skyLightRaw01") !=
                     std::string::npos &&
                 reflection.find(
                     "environmentReflection = environmentRadiance * materialAo") !=
                     std::string::npos &&
                 reflection.find("probeReflection * skyLightRaw01") ==
                     std::string::npos,
             "sky visibility must attenuate sky IBL without suppressing local probes");
}
} // namespace

int main() {
  return testProductContract() && testRoughnessMipMapping() &&
                 testRevisionAndWorkMapping() &&
                 testGenerationAndConsumptionContract()
             ? 0
             : 1;
}
