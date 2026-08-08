#include "app/validation/ValidationSceneContract.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using Json = nlohmann::json;

constexpr uint32_t kExpectedWidth = 1280u;
constexpr uint32_t kExpectedHeight = 720u;
constexpr uint32_t kExpectedWarmupFrames = 300u;
constexpr uint32_t kExpectedSampleFrames = 3u;

struct ExpectedCapture {
    const char* scene;
    const char* backend;
    const char* sceneSource;
    const char* sceneId;
    uint32_t sceneVersion;
    const char* sceneHash;
    const char* cameraHash;
    const char* renderSettingsId;
    const char* renderSettingsHash;
    const char* imageSource;
    uintmax_t byteSize;
    const char* fnv1a64;
    const char* sha256;
    uint32_t renderSettingsVersion = app::validation::kValidationRenderSettingsVersion;
    const char* cameraId = nullptr;
};

constexpr std::array<ExpectedCapture, 20u> kExpectedCaptures{
    {{"voxel", "vulkan", "../scenes/m0_voxel_baseline.json", "m0_voxel_baseline",
      app::validation::kValidationSceneContractVersion1, "a9ed907345d91471", "93b8b518406d50f9",
      "m0_voxel_render_settings", "dd51efecb1ce4c75", "m0_voxel_baseline_vulkan_1280x720.png", 1511309u,
      "f07952a77f7a1b80", "e57d401434e60e967f37543721c0a753dcc02eb1103af05cdc62c2bf283d4467"},
     {"model", "vulkan", "../scenes/m0_model_damaged_helmet.json", "m0_model_damaged_helmet",
      app::validation::kValidationSceneContractVersion1, "5a561448631fdaba", "6f33df94f7766d10",
      "m0_model_render_settings", "b8bb5b3347e46551", "m0_model_damaged_helmet_vulkan_1280x720.png", 1234823u,
      "0acff06e5a4b407b", "c7c20bb8820f0413dd0e4e418b9bb04d6b47689f1f07de69bf8a2896c7e25e66"},
     {"voxel", "opengl", "../scenes/m0_voxel_baseline.json", "m0_voxel_baseline",
      app::validation::kValidationSceneContractVersion1, "a9ed907345d91471", "93b8b518406d50f9",
      "m0_voxel_render_settings", "dd51efecb1ce4c75", "m0_voxel_baseline_opengl_1280x720.png", 1559880u,
      "c0ab8dfe38860049", "83491e3140815b3471cd5b1a25f576f1c727da1dc9c263dd95649bc4b4315079"},
     {"model", "opengl", "../scenes/m0_model_damaged_helmet.json", "m0_model_damaged_helmet",
      app::validation::kValidationSceneContractVersion1, "5a561448631fdaba", "6f33df94f7766d10",
      "m0_model_render_settings", "b8bb5b3347e46551", "m0_model_damaged_helmet_opengl_1280x720.png", 1254074u,
      "fcd624333380366c", "4fe801feffe55134f413fcc0e512b9fc4b5fc4f61d263250914ae3e754a30278"},
     {"voxel", "vulkan", "../scenes/v02_cave_turn.json", "v02_cave_turn",
      app::validation::kValidationSceneContractVersion, "38e52e1b43c36514", "87e07b85195fdded",
      "m0_voxel_render_settings", "dd51efecb1ce4c75", "v02_cave_turn_vulkan_1280x720.png", 1605634u, "bb63c81b1fe7717c",
      "d83fa8c171838803a5c6dad6af65b257f034b6ffd6ee8478cbd937e2032c486d"},
     {"voxel", "opengl", "../scenes/v02_cave_turn.json", "v02_cave_turn",
      app::validation::kValidationSceneContractVersion, "38e52e1b43c36514", "87e07b85195fdded",
      "m0_voxel_render_settings", "dd51efecb1ce4c75", "v02_cave_turn_opengl_1280x720.png", 1599216u, "292b8f0614f94eba",
      "cd79cc926df9cdf97ae600804ab80282ee92b2fcd398dbf88c63cad75ed84de5"},
     {"voxel", "vulkan", "../scenes/v07_local_light_village.json", "v07_local_light_village",
      app::validation::kValidationSceneContractVersion, "185cc198a75902e7", "cc6a83ed668b12b5",
      "m0_voxel_render_settings", "dd51efecb1ce4c75", "v07_local_light_village_vulkan_1280x720.png", 1927321u,
      "889cf2097fb6fc08", "5989b0b55ebacbd79e8565d93ef32423c901e36ba0c2cb5c923d318f98fcf8b8"},
     {"voxel", "opengl", "../scenes/v07_local_light_village.json", "v07_local_light_village",
      app::validation::kValidationSceneContractVersion, "185cc198a75902e7", "cc6a83ed668b12b5",
      "m0_voxel_render_settings", "dd51efecb1ce4c75", "v07_local_light_village_opengl_1280x720.png", 1910247u,
      "e9646d908c65f697", "134a8c474b1dc8db84f06b6b196d8ee1f857da8fb1e5ca87420a81e50eeca306"},
     {"model", "vulkan", "../scenes/m07_probe_interior.json", "m07_probe_interior",
      app::validation::kValidationSceneContractVersion, "868a3adbe426276d", "c17f7838a2a58df0",
      "m0_model_render_settings", "b8bb5b3347e46551", "m07_probe_interior_vulkan_1280x720.png", 1004315u,
      "c7f23f26f530cdb2", "f8a8a97988b8d751b1e5832b73e15ed1db5799d7535fbcf02b909e5d94858f42"},
     {"model", "opengl", "../scenes/m07_probe_interior.json", "m07_probe_interior",
      app::validation::kValidationSceneContractVersion, "868a3adbe426276d", "c17f7838a2a58df0",
      "m0_model_render_settings", "b8bb5b3347e46551", "m07_probe_interior_opengl_1280x720.png", 827902u,
      "c7608d3044394b57", "c48ec38200458ccccd3ac1eba27240e3ab4d93941405dc144858ba6ecae4c97e"},
     {"voxel", "vulkan", "../scenes/v01_window_room.json", "v01_window_room",
      app::validation::kValidationSceneContractVersion, "d21cff463ded7fd2", "926b9e7d3f02af9b",
      "m0_voxel_render_settings", "dd51efecb1ce4c75", "v01_window_room_vulkan_1280x720.png", 1195689u,
      "1b1adfdece4b0eea", "c831311d0871c8b965abb5f4926711dc6425c083cece6cde4ac48ee52033bbd1"},
     {"voxel", "opengl", "../scenes/v01_window_room.json", "v01_window_room",
      app::validation::kValidationSceneContractVersion, "d21cff463ded7fd2", "926b9e7d3f02af9b",
      "m0_voxel_render_settings", "dd51efecb1ce4c75", "v01_window_room_opengl_1280x720.png", 1276106u,
      "94f5235554296eb6", "49125a03106f72fa4030527eedb7b73c496127f2b06ed6f244929bee17a7ea91"},
     {"model", "vulkan", "../scenes/m01_material_grid.json", "m01_material_grid",
      app::validation::kValidationSceneContractVersion, "85b426376ec6af54", "39d7d82f9711135e",
      "m0_model_render_settings", "b8bb5b3347e46551", "m01_material_grid_vulkan_1280x720.png", 1234187u,
      "01c08d699686d08e", "e3d5df1041d96cb6b1072e7d34b57b54647da243157441c2eac31caddfed25d1"},
     {"model", "opengl", "../scenes/m01_material_grid.json", "m01_material_grid",
      app::validation::kValidationSceneContractVersion, "85b426376ec6af54", "39d7d82f9711135e",
      "m0_model_render_settings", "b8bb5b3347e46551", "m01_material_grid_opengl_1280x720.png", 1251175u,
      "e169ca32efd110d5", "15212af0c0a59bbad3c2e6c8aa0f4d20e028ccc3cf2271c6332a70a86c0635a6"},
     {"model", "vulkan", "../scenes/m02_damaged_helmet.json", "m02_damaged_helmet",
      app::validation::kValidationSceneContractVersion, "e1bb945558de619e", "1c0a7939ab2fcdf6",
      "m0_model_render_settings", "b8bb5b3347e46551", "m02_damaged_helmet_vulkan_1280x720.png", 1455565u,
      "a28244b3a09e2e5f", "5412c95876e47e599bbe0c6304a1c1b46f9a83a7facad0ecea30492c562a8182"},
     {"model", "opengl", "../scenes/m02_damaged_helmet.json", "m02_damaged_helmet",
      app::validation::kValidationSceneContractVersion, "e1bb945558de619e", "1c0a7939ab2fcdf6",
      "m0_model_render_settings", "b8bb5b3347e46551", "m02_damaged_helmet_opengl_1280x720.png", 1490078u,
      "49bd90e1de0eeeda", "5fa9609c73ea05c3205267ed9d63cd38b81064df874ed5d3fbaec20309da9e41"},
     {"model", "vulkan", "../scenes/m03_sponza_atrium.json", "m03_sponza_atrium",
      app::validation::kValidationSceneContractVersion, "72769566d3e17f6c", "0bb11dd13d10c191",
      "m0_model_render_settings", "b8bb5b3347e46551", "m03_sponza_atrium_vulkan_1280x720.png", 1452716u,
      "08f27649deb46570", "bd676fe71d6236abb522f83652cd8e8724effe2645b117d160f11a3cfec28b12"},
     {"model", "opengl", "../scenes/m03_sponza_atrium.json", "m03_sponza_atrium",
      app::validation::kValidationSceneContractVersion, "72769566d3e17f6c", "0bb11dd13d10c191",
      "m0_model_render_settings", "b8bb5b3347e46551", "m03_sponza_atrium_opengl_1280x720.png", 1180902u,
      "b35c719c29555d77", "ebb937340e9fb4044781ff524d3c4c11475e778e39513ea7270c37c65c0467f3"},
     {"voxel", "vulkan", "../scenes/m3_voxel_rtgi_cave.json", "m3_voxel_rtgi_cave",
      app::validation::kValidationSceneContractVersion, "be46e44f90e35451", "87e07b85195fdded",
      app::validation::kValidationRtgiVoxelRenderSettingsId, "5aae41a7d7957a2d",
      "m3_voxel_rtgi_cave_vulkan_1280x720.png", 1579895u, "d31c3dd96de31cf2",
      "30c8088215694f4be538e31cd09211c30eca9518c8aa3a2312052f501700d061",
      app::validation::kValidationRtgiVoxelRenderSettingsVersion, "v02_cave_turn"},
     {"model", "vulkan", "../scenes/m3_model_rtgi_sponza.json", "m3_model_rtgi_sponza",
      app::validation::kValidationSceneContractVersion, "0376d1804439f51f", "0bb11dd13d10c191",
      app::validation::kValidationRtgiModelRenderSettingsId, "a01010a43cdb7781",
      "m3_model_rtgi_sponza_vulkan_1280x720.png", 1120476u, "77a66296c93dea3a",
      "dd8e12534a84a0aba6323d165172016e27e6a10879276f7260e9bc0cbd444776",
      app::validation::kValidationRtgiModelRenderSettingsVersion, "m03_sponza_atrium"}}};

bool requireTrue(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "[reference_capture_manifest_test] FAIL: " << message << '\n';
        return false;
    }
    return true;
}

bool hasExactFields(const Json& object, const std::initializer_list<const char*> fields) {
    if (!object.is_object() || object.size() != fields.size()) {
        return false;
    }
    for (const char* field : fields) {
        if (object.find(field) == object.end()) {
            return false;
        }
    }
    return true;
}

bool hasString(const Json& object, const char* field, const char* expected) {
    const auto value = object.find(field);
    return value != object.end() && value->is_string() && value->get_ref<const std::string&>() == expected;
}

bool readUint32(const Json& object, const char* field, uint32_t& output) {
    const auto value = object.find(field);
    if (value == object.end() || !value->is_number_unsigned()) {
        return false;
    }
    const uint64_t parsed = value->get<uint64_t>();
    if (parsed > UINT32_MAX) {
        return false;
    }
    output = static_cast<uint32_t>(parsed);
    return true;
}

bool isLowercaseHex(const std::string_view value, const size_t length) {
    if (value.size() != length) {
        return false;
    }
    for (const char character : value) {
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

uint32_t readBigEndianUint32(const std::array<uint8_t, 29u>& bytes, const size_t offset) {
    return (static_cast<uint32_t>(bytes[offset]) << 24u) | (static_cast<uint32_t>(bytes[offset + 1u]) << 16u) |
           (static_cast<uint32_t>(bytes[offset + 2u]) << 8u) | static_cast<uint32_t>(bytes[offset + 3u]);
}

bool validatePngHeader(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!requireTrue(static_cast<bool>(input), "reference PNG must open")) {
        return false;
    }
    std::array<uint8_t, 29u> bytes{};
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    constexpr std::array<uint8_t, 8u> kPngSignature{0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au};
    if (!requireTrue(input.gcount() == static_cast<std::streamsize>(bytes.size()),
                     "reference PNG must contain a complete IHDR") ||
        !requireTrue(std::equal(kPngSignature.begin(), kPngSignature.end(), bytes.begin()),
                     "reference image must have a PNG signature") ||
        !requireTrue(readBigEndianUint32(bytes, 8u) == 13u && bytes[12u] == 'I' && bytes[13u] == 'H' &&
                         bytes[14u] == 'D' && bytes[15u] == 'R',
                     "the first PNG chunk must be a complete IHDR") ||
        !requireTrue(readBigEndianUint32(bytes, 16u) == kExpectedWidth &&
                         readBigEndianUint32(bytes, 20u) == kExpectedHeight,
                     "reference PNG dimensions must match the capture profile") ||
        !requireTrue(bytes[24u] == 8u && bytes[25u] == 6u && bytes[26u] == 0u && bytes[27u] == 0u && bytes[28u] == 0u,
                     "reference PNG must remain non-interlaced RGBA8")) {
        return false;
    }
    return true;
}

const ExpectedCapture* findExpectedCapture(const std::string_view sceneId, const std::string_view backend,
                                           size_t& index) {
    for (size_t candidateIndex = 0u; candidateIndex < kExpectedCaptures.size(); ++candidateIndex) {
        const ExpectedCapture& candidate = kExpectedCaptures[candidateIndex];
        if (sceneId == candidate.sceneId && backend == candidate.backend) {
            index = candidateIndex;
            return &candidate;
        }
    }
    return nullptr;
}

bool validateCapture(const Json& capture, const std::filesystem::path& manifestDirectory,
                     std::array<bool, kExpectedCaptures.size()>& seen) {
    if (!requireTrue(hasExactFields(capture, {"scene", "rhi_backend", "scene_contract", "camera_path",
                                              "render_settings", "image"}),
                     "capture entries must use the exact version 1 field set")) {
        return false;
    }
    const Json& sceneContract = *capture.find("scene_contract");
    const Json& cameraPath = *capture.find("camera_path");
    const Json& renderSettings = *capture.find("render_settings");
    const Json& image = *capture.find("image");
    if (!requireTrue(hasExactFields(sceneContract, {"source", "id", "version", "content_hash", "hash_algorithm"}),
                     "scene identity must use the exact version 1 field set") ||
        !requireTrue(hasExactFields(cameraPath, {"id", "content_hash"}),
                     "Camera Path identity must use the exact field set") ||
        !requireTrue(hasExactFields(renderSettings, {"id", "version", "content_hash"}),
                     "render settings identity must use the exact field set") ||
        !requireTrue(hasExactFields(image, {"source", "format", "byte_size", "fnv1a64", "sha256"}),
                     "image identity must use the exact version 1 field set")) {
        return false;
    }

    const auto sceneValue = capture.find("scene");
    const auto backendValue = capture.find("rhi_backend");
    const auto sceneIdValue = sceneContract.find("id");
    if (!requireTrue(sceneValue->is_string() && backendValue->is_string() && sceneIdValue->is_string(),
                     "capture scene, scene contract ID, and backend must be strings")) {
        return false;
    }
    const std::string& scene = sceneValue->get_ref<const std::string&>();
    const std::string& backend = backendValue->get_ref<const std::string&>();
    const std::string& sceneId = sceneIdValue->get_ref<const std::string&>();
    size_t expectedIndex = 0u;
    const ExpectedCapture* expected = findExpectedCapture(sceneId, backend, expectedIndex);
    if (!requireTrue(expected != nullptr, "capture scene contract/backend pair must belong to the validation matrix") ||
        !requireTrue(!seen[expectedIndex], "capture scene contract/backend pairs must be unique")) {
        return false;
    }
    seen[expectedIndex] = true;

    uint32_t sceneVersion = 0u;
    uint32_t renderSettingsVersion = 0u;
    if (!requireTrue(scene == expected->scene && hasString(sceneContract, "source", expected->sceneSource) &&
                         hasString(sceneContract, "id", expected->sceneId) &&
                         hasString(sceneContract, "content_hash", expected->sceneHash) &&
                         hasString(sceneContract, "hash_algorithm", renderer::contracts::kStableContentHashAlgorithm) &&
                         readUint32(sceneContract, "version", sceneVersion) && sceneVersion == expected->sceneVersion,
                     "scene identity must match its versioned descriptor") ||
        !requireTrue(hasString(cameraPath, "id", expected->cameraId != nullptr ? expected->cameraId : expected->sceneId) &&
                         hasString(cameraPath, "content_hash", expected->cameraHash),
                     "Camera Path identity must match the selected scene") ||
        !requireTrue(hasString(renderSettings, "id", expected->renderSettingsId) &&
                         hasString(renderSettings, "content_hash", expected->renderSettingsHash) &&
                         readUint32(renderSettings, "version", renderSettingsVersion) &&
                         renderSettingsVersion == expected->renderSettingsVersion,
                     "render settings identity must match the selected scene")) {
        return false;
    }

    const std::filesystem::path scenePath = (manifestDirectory / expected->sceneSource).lexically_normal();
    const app::validation::ValidationSceneContractLoadResult loadedScene =
        app::validation::loadValidationSceneContract(scenePath);
    if (!requireTrue(loadedScene.succeeded(), "manifest scene descriptors must verify") ||
        !requireTrue(loadedScene.contract.version == expected->sceneVersion &&
                         std::string_view(validationSceneStableId(loadedScene.contract.scene)) == expected->scene &&
                         loadedScene.contract.id == expected->sceneId &&
                         renderer::contracts::stableContentHashHex(loadedScene.contract.contentHash) ==
                             expected->sceneHash &&
                         renderer::contracts::stableContentHashHex(loadedScene.contract.cameraPath.contentHash) ==
                             expected->cameraHash &&
                         loadedScene.contract.renderSettings.id == expected->renderSettingsId &&
                         renderer::contracts::stableContentHashHex(loadedScene.contract.renderSettings.contentHash) ==
                             expected->renderSettingsHash,
                     "manifest identities must equal the parsed scene contract")) {
        return false;
    }

    const auto byteSizeValue = image.find("byte_size");
    const auto fnvValue = image.find("fnv1a64");
    const auto shaValue = image.find("sha256");
    if (!requireTrue(hasString(image, "source", expected->imageSource) && hasString(image, "format", "png_rgba8") &&
                         byteSizeValue->is_number_unsigned() && byteSizeValue->get<uintmax_t>() == expected->byteSize &&
                         fnvValue->is_string() && fnvValue->get_ref<const std::string&>() == expected->fnv1a64 &&
                         shaValue->is_string() && shaValue->get_ref<const std::string&>() == expected->sha256 &&
                         isLowercaseHex(shaValue->get_ref<const std::string&>(), 64u),
                     "image metadata must match the locked validation reference")) {
        return false;
    }

    const std::filesystem::path imagePath = manifestDirectory / expected->imageSource;
    std::error_code fileError;
    const uintmax_t byteSize = std::filesystem::file_size(imagePath, fileError);
    renderer::contracts::StableContentHash expectedHash = 0u;
    const renderer::contracts::FileContentHashResult actualHash = renderer::contracts::stableFileContentHash(imagePath);
    if (!requireTrue(!fileError && byteSize == expected->byteSize, "reference PNG byte size must match the manifest") ||
        !requireTrue(renderer::contracts::parseStableContentHashHex(expected->fnv1a64, expectedHash),
                     "reference PNG FNV-1a must use canonical hexadecimal text") ||
        !requireTrue(actualHash.succeeded() && actualHash.hash == expectedHash,
                     "reference PNG bytes must match the manifest hash") ||
        !validatePngHeader(imagePath)) {
        return false;
    }
    return true;
}

} // namespace

int main() {
    const std::filesystem::path manifestPath =
        std::filesystem::path(MECRAFT_TEST_SOURCE_DIR) / "assets/validation/reference_captures/manifest.json";
    std::ifstream input(manifestPath);
    const Json manifest = Json::parse(input, nullptr, false);
    if (!requireTrue(static_cast<bool>(input), "reference capture manifest must open") ||
        !requireTrue(!manifest.is_discarded(), "reference capture manifest must contain valid JSON") ||
        !requireTrue(hasExactFields(manifest, {"kind", "version", "capture_profile", "captures"}),
                     "manifest root must use the exact version 1 field set") ||
        !requireTrue(hasString(manifest, "kind", "mecraft.reference_capture_manifest"),
                     "manifest kind must remain versioned")) {
        return 1;
    }

    uint32_t manifestVersion = 0u;
    if (!requireTrue(readUint32(manifest, "version", manifestVersion) && manifestVersion == 1u,
                     "only reference capture manifest version 1 is supported")) {
        return 1;
    }

    const Json& profile = *manifest.find("capture_profile");
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint32_t warmupFrames = 0u;
    uint32_t sampleFrames = 0u;
    if (!requireTrue(hasExactFields(profile, {"width", "height", "warmup_frame_count", "sample_frame_count"}),
                     "capture profile must use the exact version 1 field set") ||
        !requireTrue(readUint32(profile, "width", width) && readUint32(profile, "height", height) &&
                         readUint32(profile, "warmup_frame_count", warmupFrames) &&
                         readUint32(profile, "sample_frame_count", sampleFrames) && width == kExpectedWidth &&
                         height == kExpectedHeight && warmupFrames == kExpectedWarmupFrames &&
                         sampleFrames == kExpectedSampleFrames,
                     "capture profile must remain 1280x720 with 300/3 frames")) {
        return 1;
    }

    const Json& captures = *manifest.find("captures");
    if (!requireTrue(captures.is_array() && captures.size() == kExpectedCaptures.size(),
                     "manifest must contain exactly twenty versioned captures")) {
        return 1;
    }
    std::array<bool, kExpectedCaptures.size()> seen{};
    for (const Json& capture : captures) {
        if (!validateCapture(capture, manifestPath.parent_path(), seen)) {
            return 1;
        }
    }
    for (const bool captureSeen : seen) {
        if (!requireTrue(captureSeen, "every scene contract/backend pair must be present")) {
            return 1;
        }
    }

    std::cout << "[reference_capture_manifest_test] PASS\n";
    return 0;
}
