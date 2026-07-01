#include "../src/resource/BlockTextureManifest.h"
#include "../src/resource/BlockTextureSourceSet.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

int fail(const char* message) {
    std::cerr << "[block_texture_manifest_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void touchPng(const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary);
    file << "png";
}

} // namespace

int main() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "mecraft_block_texture_manifest_test";
    const std::filesystem::path baseDir = root / "base";
    const std::filesystem::path packDir = root / "pack";

    std::filesystem::remove_all(root);
    std::filesystem::create_directories(baseDir);
    std::filesystem::create_directories(packDir);

    touchPng(baseDir / "dirt.png");
    touchPng(baseDir / "stone.png");
    touchPng(packDir / "stone_n.png");
    touchPng(packDir / "stone_s.png");
    touchPng(packDir / "orphan_n.png");
    touchPng(packDir / "orphan_s.png");

    resource::BlockTextureSourceSet sourceSet;
    sourceSet.textureDirectories.push_back(baseDir.string());
    sourceSet.textureDirectories.push_back(packDir.string());

    const resource::BlockTextureManifest manifest = resource::buildBlockTextureManifest(sourceSet);
    if (manifest.entries().size() != 2U) {
        std::filesystem::remove_all(root);
        return fail("manifest should contain only entries with albedo textures");
    }

    const resource::BlockTextureManifestEntry* stone = manifest.find("stone");
    if (stone == nullptr || !stone->normalPath.has_value() || !stone->specularPath.has_value()) {
        std::filesystem::remove_all(root);
        return fail("matching material maps should attach to the albedo entry");
    }
    if (manifest.find("orphan") != nullptr) {
        std::filesystem::remove_all(root);
        return fail("standalone material maps should not create renderable entries");
    }

    std::filesystem::remove_all(root);
    std::cout << "[block_texture_manifest_test] PASS\n";
    return EXIT_SUCCESS;
}
