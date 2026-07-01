#include "GameResourceBootstrapper.h"

#include "../Paths.h"
#include "../item/Item.h"
#include "../resource/AtmosphereLutProbe.h"
#include "../resource/ResourceMgr.h"
#include "../resource/ResourcePackArchiveExtractor.h"
#include "../ui/inventory/ContainerUiRegistry.h"
#include "../world/block/Block.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace app {

namespace {

bool directoryContainsPng(const std::filesystem::path& directory) {
    if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
        return false;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".png") {
            return true;
        }
    }
    return false;
}

bool directoryContainsProperties(const std::filesystem::path& directory) {
    if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
        return false;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".properties") {
            return true;
        }
    }
    return false;
}

void appendTextureDirectoryIfPresent(std::vector<std::string>& directories,
                                     const std::filesystem::path& directory) {
    if (directoryContainsPng(directory)) {
        directories.push_back(directory.string());
    }
}

void appendConnectedTextureDirectoryIfPresent(std::vector<std::string>& directories,
                                              const std::filesystem::path& directory) {
    if (directoryContainsProperties(directory)) {
        directories.push_back(directory.string());
    }
}

resource::BlockTextureSourceSet buildBlockTextureSourceSet() {
    resource::BlockTextureSourceSet sourceSet;
    sourceSet.textureDirectories.push_back(BLOCKS_TEXTURES_DIR);

    const std::filesystem::path packsRoot(RESOURCE_PACKS_DIR);
    if (!std::filesystem::exists(packsRoot) || !std::filesystem::is_directory(packsRoot)) {
        return sourceSet;
    }

    std::vector<std::filesystem::path> packRoots;
    for (const std::filesystem::path& extractedRoot : resource::prepareResourcePackArchives(packsRoot)) {
        if (std::filesystem::exists(extractedRoot) && std::filesystem::is_directory(extractedRoot)) {
            packRoots.push_back(extractedRoot);
        }
    }
    for (const auto& entry : std::filesystem::directory_iterator(packsRoot)) {
        if (entry.is_directory()) {
            packRoots.push_back(entry.path());
        }
    }
    std::sort(packRoots.begin(), packRoots.end(),
              [](const std::filesystem::path& a, const std::filesystem::path& b) {
                  return a.filename().string() < b.filename().string();
              });

    for (const std::filesystem::path& packRoot : packRoots) {
        appendTextureDirectoryIfPresent(sourceSet.textureDirectories, packRoot);
        appendTextureDirectoryIfPresent(sourceSet.textureDirectories, packRoot / "assets" / "minecraft" / "textures" / "block");
        appendTextureDirectoryIfPresent(sourceSet.textureDirectories, packRoot / "assets" / "minecraft" / "textures" / "blocks");
        appendTextureDirectoryIfPresent(sourceSet.textureDirectories, packRoot / "textures" / "block");
        appendTextureDirectoryIfPresent(sourceSet.textureDirectories, packRoot / "textures" / "blocks");
        appendConnectedTextureDirectoryIfPresent(sourceSet.connectedTextureDirectories,
                                                 packRoot / "assets" / "minecraft" / "optifine" / "ctm");
        appendConnectedTextureDirectoryIfPresent(sourceSet.connectedTextureDirectories,
                                                 packRoot / "assets" / "minecraft" / "mcpatcher" / "ctm");
        appendConnectedTextureDirectoryIfPresent(sourceSet.connectedTextureDirectories,
                                                 packRoot / "optifine" / "ctm");
        appendConnectedTextureDirectoryIfPresent(sourceSet.connectedTextureDirectories,
                                                 packRoot / "mcpatcher" / "ctm");
    }

    return sourceSet;
}

} // namespace

void bootstrapGameResources(ResourceMgr& resourceMgr) {
    resourceMgr.init();
    resourceMgr.loadBlockTextureCatalog(BLOCK_TEXTURES_CONFIG_PATH);
    resourceMgr.buildBlockTextureResources(buildBlockTextureSourceSet(), 0);
    resourceMgr.loadLightmapTextures(LIGHTMAP_DAY_PATH, LIGHTMAP_NIGHT_PATH);
    resourceMgr.loadColormapTextures(GRASS_TEXTURE_PATH, FOLIAGE_TEXTURE_PATH);
    resourceMgr.loadTexture2D("shader_noise2d", SHADERPACK_NOISE2D_PATH, false, true, true, false);
    resourceMgr.loadTexture2D("shader_bayer256", SHADERPACK_BAYER256_PATH, false, true, false, false);
    // DerivativeMain/texture/RippleNormal.png.mcmeta uses blur=true, clamp=false.
    resourceMgr.loadTexture2D("shader_ripple_normal", SHADERPACK_RIPPLE_NORMAL_PATH, false, true, true, false);
    resourceMgr.loadTexture2D("shader_ldr_lut", SHADERPACK_LDR_LUT_PATH, false, false, true, false);
    resourceMgr.loadTexture2D("rain", RAIN_TEXTURE_PATH, false, false, false, false);
    resourceMgr.loadTexture2D("snow", SNOW_TEXTURE_PATH, false, false, false, false);

    resource::probeAtmosphereLut("Transmittance", SHADERPACK_TRANSMITTANCE_LUT_PATH, 256U * 64U * 16U);
    resource::probeAtmosphereLut("Scattering", SHADERPACK_SCATTERING_LUT_PATH, 32U * 128U * 32U * 8U * 16U);
    resource::probeAtmosphereLut("Irradiance", SHADERPACK_IRRADIANCE_LUT_PATH, 64U * 16U * 16U);
    resource::probeAtmosphereLut("Final", SHADERPACK_FINAL_LUT_PATH);

    resourceMgr.buildItemTextureAtlas(ITEMS_TEXTURES_DIR, 16);
    resourceMgr.loadGuiTexture("widgets", WIDGETS_TEXTURE_PATH, true);
    resourceMgr.loadGuiTexture("inventory", INVENTORY_TEX_PATH, true);

    ui::ContainerUiRegistry::init();
    for (const auto& [id, def] : ui::ContainerUiRegistry::all()) {
        const std::string texturePath = std::string(ASSETS_DIR) + "/" + def.backgroundTexturePath;
        if (resourceMgr.loadGuiTexture(def.backgroundTexture, texturePath, true) == 0) {
            throw std::runtime_error("Failed to load container UI texture for " + id + ": " + texturePath);
        }
    }

    resourceMgr.loadGuiTexture("creative_tab_inventory", CREATIVE_INVENTORY_PATH, true);
    resourceMgr.loadGuiTexture("creative_tab_items", CREATIVE_TAB_ITEMS_PATH, true);
    for (int i = 1; i <= 7; ++i) {
        const std::string suffix = std::to_string(i) + ".png";
        resourceMgr.loadGuiTexture("creative_tab_top_selected_" + std::to_string(i),
                                   std::string(CREATIVE_TABS_PATH) + "/tab_top_selected_" + suffix,
                                   true);
        resourceMgr.loadGuiTexture("creative_tab_top_unselected_" + std::to_string(i),
                                   std::string(CREATIVE_TABS_PATH) + "/tab_top_unselected_" + suffix,
                                   true);
        resourceMgr.loadGuiTexture("creative_tab_bottom_selected_" + std::to_string(i),
                                   std::string(CREATIVE_TABS_PATH) + "/tab_bottom_selected_" + suffix,
                                   true);
        resourceMgr.loadGuiTexture("creative_tab_bottom_unselected_" + std::to_string(i),
                                   std::string(CREATIVE_TABS_PATH) + "/tab_bottom_unselected_" + suffix,
                                   true);
    }

    resourceMgr.loadGuiTexture("creative_scroller", std::string(CREATIVE_TABS_PATH) + "/scroller.png", true);
    resourceMgr.loadGuiTexture("creative_scroller_disabled", std::string(CREATIVE_TABS_PATH) + "/scroller_disabled.png", true);
    resourceMgr.loadGuiTexture("steve", STEVE_TEXTURE_PATH, true);
    resourceMgr.loadGuiTexture("chest", CHEST_ENTITY_TEXTURE_PATH, true);
    resourceMgr.preloadEntityTexturesFromConfig(ENTITIES_CONFIG_PATH);

    resourceMgr.buildHudIconAtlas(ICONS_TEXTURE_DIR, 8);

    BlockRegistry::init(&resourceMgr);
    ItemRegistry::init();
    resourceMgr.buildBlockIconAtlas(64);
}

} // namespace app
