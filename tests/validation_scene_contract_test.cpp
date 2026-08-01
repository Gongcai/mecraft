#include "app/validation/ValidationSceneContract.h"
#include "app/validation/ValidationVoxelFixture.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[validation_scene_contract_test] FAIL: " << message << '\n';
        return false;
    }
    return true;
}

bool requireLoaded(const app::validation::ValidationSceneContractLoadResult& result, const char* sceneId) {
    if (result.succeeded()) {
        return true;
    }
    std::cerr << "[validation_scene_contract_test] FAIL: " << sceneId
              << " error=" << app::validation::validationSceneContractErrorStableId(result.error)
              << " detail=" << result.detail << '\n';
    return false;
}

} // namespace

int main() {
    using renderer::contracts::stableContentHashBytes;
    using renderer::contracts::stableContentHashHex;
    constexpr char kHello[] = "hello";
    if (!requireTrue(stableContentHashHex(stableContentHashBytes(kHello, 5u)) == "a430d84680aabd0b",
                     "raw byte hashing must match the FNV-1a 64-bit reference vector")) {
        return 1;
    }

    const std::filesystem::path sourceRoot = MECRAFT_TEST_SOURCE_DIR;
    const std::filesystem::path sceneRoot = sourceRoot / "assets/validation/scenes";
    const std::filesystem::path voxelPath = sceneRoot / "m0_voxel_baseline.json";
    const std::filesystem::path modelPath = sceneRoot / "m0_model_damaged_helmet.json";
    const std::filesystem::path windowRoomPath = sceneRoot / "v01_window_room.json";
    const std::filesystem::path cavePath = sceneRoot / "v02_cave_turn.json";
    const std::filesystem::path villagePath = sceneRoot / "v07_local_light_village.json";
    const std::filesystem::path materialGridPath = sceneRoot / "m01_material_grid.json";
    const std::filesystem::path damagedHelmetPath = sceneRoot / "m02_damaged_helmet.json";
    const std::filesystem::path probeInteriorPath = sceneRoot / "m07_probe_interior.json";
    const app::validation::ValidationSceneContractLoadResult voxel =
        app::validation::loadValidationSceneContract(voxelPath);
    const app::validation::ValidationSceneContractLoadResult model =
        app::validation::loadValidationSceneContract(modelPath);
    const app::validation::ValidationSceneContractLoadResult windowRoom =
        app::validation::loadValidationSceneContract(windowRoomPath);
    const app::validation::ValidationSceneContractLoadResult cave =
        app::validation::loadValidationSceneContract(cavePath);
    const app::validation::ValidationSceneContractLoadResult village =
        app::validation::loadValidationSceneContract(villagePath);
    const app::validation::ValidationSceneContractLoadResult materialGrid =
        app::validation::loadValidationSceneContract(materialGridPath);
    const app::validation::ValidationSceneContractLoadResult damagedHelmet =
        app::validation::loadValidationSceneContract(damagedHelmetPath);
    const app::validation::ValidationSceneContractLoadResult probeInterior =
        app::validation::loadValidationSceneContract(probeInteriorPath);
    const bool voxelLoaded = requireLoaded(voxel, "m0_voxel_baseline");
    const bool modelLoaded = requireLoaded(model, "m0_model_damaged_helmet");
    const bool windowRoomLoaded = requireLoaded(windowRoom, "v01_window_room");
    const bool caveLoaded = requireLoaded(cave, "v02_cave_turn");
    const bool villageLoaded = requireLoaded(village, "v07_local_light_village");
    const bool materialGridLoaded = requireLoaded(materialGrid, "m01_material_grid");
    const bool damagedHelmetLoaded = requireLoaded(damagedHelmet, "m02_damaged_helmet");
    const bool probeInteriorLoaded = requireLoaded(probeInterior, "m07_probe_interior");
    if (!voxelLoaded || !modelLoaded || !windowRoomLoaded || !caveLoaded || !villageLoaded || !materialGridLoaded ||
        !damagedHelmetLoaded || !probeInteriorLoaded) {
        return 1;
    }
    if (!requireTrue(voxel.succeeded() && model.succeeded(), "both M0 scene descriptors must verify") ||
        !requireTrue(voxel.contract.scene == ValidationScene::Voxel && voxel.contract.voxelWorld.has_value() &&
                         !voxel.contract.modelAsset.has_value() &&
                         stableContentHashHex(voxel.contract.contentHash) == "ecb85fb88ef6aeef" &&
                         stableContentHashHex(voxel.contract.renderSettings.contentHash) == "511c4e7a0e2e2de9" &&
                         stableContentHashHex(voxel.contract.voxelWorld->contentHash) == "10ac44b930335e6b",
                     "the voxel scene, renderer, and world identities must remain locked") ||
        !requireTrue(model.contract.scene == ValidationScene::Model && model.contract.modelAsset.has_value() &&
                         !model.contract.voxelWorld.has_value() &&
                         stableContentHashHex(model.contract.contentHash) == "bde98b1cf0faca9a" &&
                         stableContentHashHex(model.contract.renderSettings.contentHash) == "9a8940b4590c9585" &&
                         stableContentHashHex(model.contract.modelAsset->contentHash) == "f67fb46e0033d3dd",
                     "the model scene, renderer, and asset identities must remain locked")) {
        return 1;
    }
    if (!requireTrue(
            windowRoom.contract.version == app::validation::kValidationSceneContractVersion &&
                windowRoom.contract.scene == ValidationScene::Voxel && windowRoom.contract.voxelWorld.has_value() &&
                windowRoom.contract.voxelWorld->fixture.has_value() &&
                windowRoom.contract.voxelWorld->fixture->id == app::validation::kValidationVoxelFixtureWindowRoomId &&
                stableContentHashHex(windowRoom.contract.voxelWorld->fixture->contentHash) == "323aa669c63c427a" &&
                stableContentHashHex(windowRoom.contract.voxelWorld->contentHash) == "d38f66e8e2469348" &&
                stableContentHashHex(windowRoom.contract.cameraPath.contentHash) == "926b9e7d3f02af9b" &&
                stableContentHashHex(windowRoom.contract.contentHash) == "4f8717cfe9b48270",
            "V01 scene, Camera Path, world, and fixture identities must remain locked") ||
        !requireTrue(cave.contract.version == app::validation::kValidationSceneContractVersion &&
                         cave.contract.scene == ValidationScene::Voxel && cave.contract.voxelWorld.has_value() &&
                         cave.contract.voxelWorld->fixture.has_value() &&
                         cave.contract.voxelWorld->fixture->id == app::validation::kValidationVoxelFixtureCaveTurnId &&
                         stableContentHashHex(cave.contract.voxelWorld->fixture->contentHash) == "8e8834253081af88" &&
                         stableContentHashHex(cave.contract.voxelWorld->contentHash) == "b58b9504e7ed54b3" &&
                         stableContentHashHex(cave.contract.cameraPath.contentHash) == "87e07b85195fdded" &&
                         stableContentHashHex(cave.contract.contentHash) == "4bb4409c20ca21c6",
                     "V02 scene, Camera Path, world, and fixture identities must remain locked") ||
        !requireTrue(village.contract.version == app::validation::kValidationSceneContractVersion &&
                         village.contract.scene == ValidationScene::Voxel && village.contract.voxelWorld.has_value() &&
                         village.contract.voxelWorld->fixture.has_value() &&
                         village.contract.voxelWorld->fixture->id ==
                             app::validation::kValidationVoxelFixtureLocalLightVillageId &&
                         stableContentHashHex(village.contract.voxelWorld->fixture->contentHash) ==
                             "b34bf75d177ebb41" &&
                         stableContentHashHex(village.contract.voxelWorld->contentHash) == "500831ce59abbfd5" &&
                         stableContentHashHex(village.contract.cameraPath.contentHash) == "cc6a83ed668b12b5" &&
                         stableContentHashHex(village.contract.contentHash) == "5be2746ff4c4b81d",
                     "V07 scene, Camera Path, world, and fixture identities must remain locked") ||
        !requireTrue(materialGrid.contract.version == app::validation::kValidationSceneContractVersion &&
                         materialGrid.contract.scene == ValidationScene::Model &&
                         materialGrid.contract.modelAsset.has_value() &&
                         materialGrid.contract.modelProbeGrid.has_value() &&
                         materialGrid.contract.modelProbeGrid->spacingMeters == 0.8 &&
                         materialGrid.contract.modelProbeGrid->boundsPaddingMeters == 0.1 &&
                         stableContentHashHex(materialGrid.contract.modelAsset->contentHash) == "e2dbc94ec6365711" &&
                         stableContentHashHex(materialGrid.contract.cameraPath.contentHash) == "39d7d82f9711135e" &&
                         stableContentHashHex(materialGrid.contract.contentHash) == "2eb15aeceefd92a4",
                     "M01 scene, Camera Path, asset, and Probe grid identities must remain locked") ||
        !requireTrue(damagedHelmet.contract.version == app::validation::kValidationSceneContractVersion &&
                         damagedHelmet.contract.scene == ValidationScene::Model &&
                         damagedHelmet.contract.modelAsset.has_value() &&
                         damagedHelmet.contract.modelProbeGrid.has_value() &&
                         damagedHelmet.contract.modelProbeGrid->spacingMeters == 1.2 &&
                         damagedHelmet.contract.modelProbeGrid->boundsPaddingMeters == 0.0 &&
                         stableContentHashHex(damagedHelmet.contract.modelAsset->contentHash) == "f67fb46e0033d3dd" &&
                         stableContentHashHex(damagedHelmet.contract.cameraPath.contentHash) == "1c0a7939ab2fcdf6" &&
                         stableContentHashHex(damagedHelmet.contract.contentHash) == "a930eac94f635702",
                     "M02 scene, Camera Path, asset, and Probe grid identities must remain locked") ||
        !requireTrue(probeInterior.contract.version == app::validation::kValidationSceneContractVersion &&
                         probeInterior.contract.scene == ValidationScene::Model &&
                         probeInterior.contract.modelAsset.has_value() &&
                         probeInterior.contract.modelProbeGrid.has_value() &&
                         probeInterior.contract.modelProbeGrid->spacingMeters == 0.8 &&
                         probeInterior.contract.modelProbeGrid->boundsPaddingMeters == 0.0 &&
                         stableContentHashHex(probeInterior.contract.modelAsset->contentHash) == "097b196adca0e388" &&
                         stableContentHashHex(probeInterior.contract.cameraPath.contentHash) == "c17f7838a2a58df0" &&
                         stableContentHashHex(probeInterior.contract.contentHash) == "e7cfecd188549085",
                     "M07 scene, Camera Path, asset, and Probe grid identities must remain locked")) {
        return 1;
    }

    std::ifstream voxelInput(voxelPath);
    nlohmann::json voxelJson = nlohmann::json::parse(voxelInput, nullptr, false);
    if (!requireTrue(!voxelJson.is_discarded(), "the voxel fixture must remain valid JSON")) {
        return 1;
    }
    voxelJson["voxel_world"]["seed"] = 1235;
    const auto changedWorld = app::validation::parseValidationSceneContractJson(voxelJson.dump(), voxelPath);
    if (!requireTrue(changedWorld.error == app::validation::ValidationSceneContractError::WorldHashMismatch,
                     "world recipe changes must invalidate the versioned world hash")) {
        return 1;
    }

    voxelJson = nlohmann::json::parse(std::ifstream(voxelPath), nullptr, false);
    voxelJson["camera_path"]["content_hash"] = "0000000000000001";
    const auto changedCamera = app::validation::parseValidationSceneContractJson(voxelJson.dump(), voxelPath);
    if (!requireTrue(changedCamera.error == app::validation::ValidationSceneContractError::CameraPathIdentityMismatch,
                     "Camera Path identity drift must fail before rendering")) {
        return 1;
    }

    nlohmann::json caveJson = nlohmann::json::parse(std::ifstream(cavePath), nullptr, false);
    voxelJson = nlohmann::json::parse(std::ifstream(voxelPath), nullptr, false);
    voxelJson["voxel_world"]["fixture"] = caveJson["voxel_world"]["fixture"];
    const auto version1WithFixture = app::validation::parseValidationSceneContractJson(voxelJson.dump(), voxelPath);
    if (!requireTrue(version1WithFixture.error == app::validation::ValidationSceneContractError::UnexpectedField,
                     "version 1 voxel scenes must reject Fixture identities")) {
        return 1;
    }

    caveJson.erase("content_hash");
    caveJson["content_hash"] = "4bb4409c20ca21c6";
    caveJson["voxel_world"].erase("fixture");
    const auto version2WithoutFixture = app::validation::parseValidationSceneContractJson(caveJson.dump(), cavePath);
    if (!requireTrue(version2WithoutFixture.error == app::validation::ValidationSceneContractError::MissingField,
                     "version 2 voxel scenes must require Fixture identities")) {
        return 1;
    }

    caveJson = nlohmann::json::parse(std::ifstream(cavePath), nullptr, false);
    caveJson["voxel_world"]["fixture"]["content_hash"] = "0000000000000001";
    const auto changedFixture = app::validation::parseValidationSceneContractJson(caveJson.dump(), cavePath);
    if (!requireTrue(changedFixture.error == app::validation::ValidationSceneContractError::FixtureHashMismatch,
                     "Fixture recipe drift must fail before world synchronization")) {
        return 1;
    }

    nlohmann::json modelJson = nlohmann::json::parse(std::ifstream(modelPath), nullptr, false);
    modelJson["reflection_probe_grid"] = {{"spacing_meters", 0.8}, {"bounds_padding_meters", 0.0}};
    const auto version1WithProbeGrid = app::validation::parseValidationSceneContractJson(modelJson.dump(), modelPath);
    if (!requireTrue(version1WithProbeGrid.error == app::validation::ValidationSceneContractError::UnexpectedField,
                     "version 1 model scenes must reject Probe grid inputs")) {
        return 1;
    }

    nlohmann::json probeInteriorJson = nlohmann::json::parse(std::ifstream(probeInteriorPath), nullptr, false);
    probeInteriorJson.erase("reflection_probe_grid");
    const auto version2WithoutProbeGrid =
        app::validation::parseValidationSceneContractJson(probeInteriorJson.dump(), probeInteriorPath);
    if (!requireTrue(version2WithoutProbeGrid.error == app::validation::ValidationSceneContractError::MissingField,
                     "version 2 model scenes must require Probe grid inputs")) {
        return 1;
    }

    probeInteriorJson = nlohmann::json::parse(std::ifstream(probeInteriorPath), nullptr, false);
    probeInteriorJson["reflection_probe_grid"]["spacing_meters"] = 0.0;
    const auto invalidProbeGrid =
        app::validation::parseValidationSceneContractJson(probeInteriorJson.dump(), probeInteriorPath);
    if (!requireTrue(invalidProbeGrid.error == app::validation::ValidationSceneContractError::InvalidProbeGrid,
                     "Probe grid spacing must be positive")) {
        return 1;
    }

    probeInteriorJson = nlohmann::json::parse(std::ifstream(probeInteriorPath), nullptr, false);
    probeInteriorJson["reflection_probe_grid"]["spacing_meters"] = 0.9;
    const auto changedProbeGrid =
        app::validation::parseValidationSceneContractJson(probeInteriorJson.dump(), probeInteriorPath);
    if (!requireTrue(changedProbeGrid.error == app::validation::ValidationSceneContractError::SceneHashMismatch,
                     "Probe grid changes must invalidate the versioned scene hash")) {
        return 1;
    }

    renderer::contracts::StableContentHash parsedHash = 0u;
    if (!requireTrue(renderer::contracts::parseStableContentHashHex("0123456789abcdef", parsedHash) &&
                         stableContentHashHex(parsedHash) == "0123456789abcdef" &&
                         !renderer::contracts::parseStableContentHashHex("0123456789abcdeF", parsedHash),
                     "manifest hashes must use canonical lowercase hexadecimal text")) {
        return 1;
    }

    std::cout << "[validation_scene_contract_test] PASS\n";
    return 0;
}
