#include "app/validation/ValidationSceneContract.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[validation_scene_contract_test] FAIL: "
                  << message << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    using renderer::contracts::stableContentHashBytes;
    using renderer::contracts::stableContentHashHex;
    constexpr char kHello[] = "hello";
    if (!requireTrue(
            stableContentHashHex(stableContentHashBytes(kHello, 5u)) ==
                "a430d84680aabd0b",
            "raw byte hashing must match the FNV-1a 64-bit reference vector")) {
        return 1;
    }

    const std::filesystem::path sourceRoot = MECRAFT_TEST_SOURCE_DIR;
    const std::filesystem::path sceneRoot =
        sourceRoot / "assets/validation/scenes";
    const std::filesystem::path voxelPath =
        sceneRoot / "m0_voxel_baseline.json";
    const std::filesystem::path modelPath =
        sceneRoot / "m0_model_damaged_helmet.json";
    const app::validation::ValidationSceneContractLoadResult voxel =
        app::validation::loadValidationSceneContract(voxelPath);
    const app::validation::ValidationSceneContractLoadResult model =
        app::validation::loadValidationSceneContract(modelPath);
    if (!requireTrue(voxel.succeeded() && model.succeeded(),
                     "both M0 scene descriptors must verify") ||
        !requireTrue(
            voxel.contract.scene == ValidationScene::Voxel &&
                voxel.contract.voxelWorld.has_value() &&
                !voxel.contract.modelAsset.has_value() &&
                stableContentHashHex(voxel.contract.contentHash) ==
                    "ecb85fb88ef6aeef" &&
                stableContentHashHex(
                    voxel.contract.renderSettings.contentHash) ==
                    "511c4e7a0e2e2de9" &&
                stableContentHashHex(
                    voxel.contract.voxelWorld->contentHash) ==
                    "10ac44b930335e6b",
            "the voxel scene, renderer, and world identities must remain locked") ||
        !requireTrue(
            model.contract.scene == ValidationScene::Model &&
                model.contract.modelAsset.has_value() &&
                !model.contract.voxelWorld.has_value() &&
                stableContentHashHex(model.contract.contentHash) ==
                    "bde98b1cf0faca9a" &&
                stableContentHashHex(
                    model.contract.renderSettings.contentHash) ==
                    "9a8940b4590c9585" &&
                stableContentHashHex(
                    model.contract.modelAsset->contentHash) ==
                    "f67fb46e0033d3dd",
            "the model scene, renderer, and asset identities must remain locked")) {
        return 1;
    }

    std::ifstream voxelInput(voxelPath);
    nlohmann::json voxelJson = nlohmann::json::parse(
        voxelInput, nullptr, false);
    if (!requireTrue(!voxelJson.is_discarded(),
                     "the voxel fixture must remain valid JSON")) {
        return 1;
    }
    voxelJson["voxel_world"]["seed"] = 1235;
    const auto changedWorld =
        app::validation::parseValidationSceneContractJson(
            voxelJson.dump(), voxelPath);
    if (!requireTrue(
            changedWorld.error ==
                app::validation::ValidationSceneContractError::WorldHashMismatch,
            "world recipe changes must invalidate the versioned world hash")) {
        return 1;
    }

    voxelJson = nlohmann::json::parse(
        std::ifstream(voxelPath), nullptr, false);
    voxelJson["camera_path"]["content_hash"] = "0000000000000001";
    const auto changedCamera =
        app::validation::parseValidationSceneContractJson(
            voxelJson.dump(), voxelPath);
    if (!requireTrue(
            changedCamera.error == app::validation::
                ValidationSceneContractError::CameraPathIdentityMismatch,
            "Camera Path identity drift must fail before rendering")) {
        return 1;
    }

    renderer::contracts::StableContentHash parsedHash = 0u;
    if (!requireTrue(
            renderer::contracts::parseStableContentHashHex(
                "0123456789abcdef", parsedHash) &&
                stableContentHashHex(parsedHash) == "0123456789abcdef" &&
                !renderer::contracts::parseStableContentHashHex(
                    "0123456789abcdeF", parsedHash),
            "manifest hashes must use canonical lowercase hexadecimal text")) {
        return 1;
    }

    std::cout << "[validation_scene_contract_test] PASS\n";
    return 0;
}
