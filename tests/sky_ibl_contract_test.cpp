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
             "runtime GGX convolution must retain the fixed sample budget");
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
  return requireTrue(pass.find("SkyIbl.Radiance") != std::string::npos &&
                         pass.find("SkyIbl.GgxPrefilter") !=
                             std::string::npos &&
                         pass.find("SkyIbl.DfgLut") != std::string::npos,
                     "the render graph must expose all three IBL products") &&
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
         requireTrue(reflection.find("uReflectionDebugMode == 31") !=
                             std::string::npos &&
                         reflection.find("uReflectionDebugMode == 32") !=
                             std::string::npos,
                     "roughness mip and DFG must remain directly observable");
}
} // namespace

int main() {
  return testProductContract() && testRoughnessMipMapping() &&
                 testGenerationAndConsumptionContract()
             ? 0
             : 1;
}
