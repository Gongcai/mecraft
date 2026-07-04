#include "GameResourceBootstrapper.h"

#include "../Paths.h"
#include "../game/interaction/BlockInteractionRegistry.h"
#include "../game/inventory/ContainerBehaviorRegistry.h"
#include "../item/Item.h"
#include "../resource/AtmosphereLutProbe.h"
#include "../resource/ResourceMgr.h"
#include "../ui/inventory/ContainerUiRegistry.h"
#include "../world/block/Block.h"

#include <iostream>
#include <string>

namespace app {

bool bootstrapGameResources(ResourceMgr& resourceMgr) {
    resourceMgr.init();
    if (!resourceMgr.loadBlockTextureCatalog(BLOCK_TEXTURES_CONFIG_PATH)) {
        return false;
    }
    resourceMgr.buildBlockTextureResources(BLOCKS_TEXTURES_DIR, 16);
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

    if (!ContainerBehaviorRegistry::init() ||
        !BlockInteractionRegistry::init() ||
        !ui::ContainerUiRegistry::init()) {
        return false;
    }
    for (const auto& [id, def] : ui::ContainerUiRegistry::all()) {
        const std::string texturePath = std::string(ASSETS_DIR) + "/" + def.backgroundTexturePath;
        if (resourceMgr.loadGuiTexture(def.backgroundTexture, texturePath, true) == 0) {
            std::cerr << "Failed to load container UI texture for " << id << ": " << texturePath << '\n';
            return false;
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
    if (!resourceMgr.preloadEntityTexturesFromConfig(ENTITIES_CONFIG_PATH)) {
        return false;
    }

    resourceMgr.buildHudIconAtlas(ICONS_TEXTURE_DIR, 8);

    BlockRegistry::init(&resourceMgr);
    ItemRegistry::init();
    resourceMgr.buildBlockIconAtlas(64);
    return true;
}

} // namespace app
