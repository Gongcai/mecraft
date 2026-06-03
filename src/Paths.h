#pragma once

// CMake builds inject the runtime assets directory. Packaged builds use the
// assets folder next to the executable; local dev can opt into source assets.
#ifdef MECRAFT_ASSETS_DIR
    #define ASSETS_DIR MECRAFT_ASSETS_DIR
#elif defined(__linux__)
    #define ASSETS_DIR "assets"
#else
    #define ASSETS_DIR "../assets"
#endif

// 资源子目录路径
#define SHADERS_DIR       ASSETS_DIR "/shaders"
#define TEXTURES_DIR      ASSETS_DIR "/textures"
#define SOUNDS_DIR        ASSETS_DIR "/sounds"
#define BGM_DIR           ASSETS_DIR "/bgm"
#define CONFIG_DIR        ASSETS_DIR "/config"
#define FONTS_DIR          ASSETS_DIR "/fonts"
#define THEMES_DIR         ASSETS_DIR "/themes"

// 常用资源路径
#define BLOCKS_TEXTURES_DIR   TEXTURES_DIR "/blocks"
#define ITEMS_TEXTURES_DIR    TEXTURES_DIR "/items"
#define GUI_TEXTURES_DIR      TEXTURES_DIR "/gui"
#define FONT_TEXTURES_DIR     TEXTURES_DIR "/font"
#define LIGHTMAP_DIR          TEXTURES_DIR "/lightmap"
#define COLORMAP_DIR          TEXTURES_DIR "/colormap"
#define ENTITY_TEXTURE_DIR    TEXTURES_DIR "/entity"
#define SKYBOX_TEXTURES_DIR   TEXTURES_DIR "/skybox"
#define ENVIRONMENT_TEXTURES_DIR TEXTURES_DIR "/environment"
#define SHADERPACK_TEXTURES_DIR TEXTURES_DIR "/shaderpacks"
#define SHADERPACK_ATMOSPHERE_DIR SHADERPACK_TEXTURES_DIR "/Atmosphere"

#define MOBS_TEXTURE_DIR       ENTITY_TEXTURE_DIR "/mobs"
#define ICONS_TEXTURE_DIR      GUI_TEXTURES_DIR "/hud"
#define CREATIVE_TEXTURE_DIR   GUI_TEXTURES_DIR "/creative_inventory"


#define BLOCKS_CONFIG_PATH    CONFIG_DIR "/blocks.json"
#define FLUIDS_CONFIG_PATH    CONFIG_DIR "/fluids.json"
#define ITEMS_CONFIG_PATH     CONFIG_DIR "/items.json"
#define RECIPES_CONFIG_PATH   CONFIG_DIR "/recipes.json"
#define KEYBINDINGS_PATH      CONFIG_DIR "/keybindings.txt"
#define LOCALE_DIR            CONFIG_DIR "/locale"
#define SETTINGS_PATH         CONFIG_DIR "/settings.json"
#define FIRST_PERSON_HELD_ITEM_CONFIG_PATH CONFIG_DIR "/first_person_held_item.json"

#define LIGHTMAP_DAY_PATH     LIGHTMAP_DIR "/lightmap_day.png"
#define LIGHTMAP_NIGHT_PATH   LIGHTMAP_DIR "/lightmap_night.png"
#define WIDGETS_TEXTURE_PATH  GUI_TEXTURES_DIR "/widgets.png"
#define INVENTORY_TEX_PATH    GUI_TEXTURES_DIR "/inventory.png"
#define FONT_ASCII_PATH       FONT_TEXTURES_DIR "/ascii.png"
#define TEST_TEXTURE_PATH     BLOCKS_TEXTURES_DIR "/test.png"
#define STEVE_TEXTURE_PATH    ENTITY_TEXTURE_DIR  "/steve.png"
#define ZOMBIE_TEXTURE_PATH    MOBS_TEXTURE_DIR  "/zombie.png"
#define TITLE_TEXTURE_PATH     GUI_TEXTURES_DIR "/title.png"
#define SUN_TEXTURE_PATH       ENVIRONMENT_TEXTURES_DIR "/sun.png"
#define MOON_TEXTURE_PATH      ENVIRONMENT_TEXTURES_DIR "/moon_phases.png"
#define CLOUD_TEXTURE_PATH     ENVIRONMENT_TEXTURES_DIR "/clouds.png"
#define RAIN_TEXTURE_PATH       ENVIRONMENT_TEXTURES_DIR "/rain.png"
#define SNOW_TEXTURE_PATH       ENVIRONMENT_TEXTURES_DIR "/snow.png"
#define SHADERPACK_NOISE2D_PATH SHADERPACK_TEXTURES_DIR "/noise2D.png"
#define SHADERPACK_BAYER256_PATH SHADERPACK_TEXTURES_DIR "/Bayer256.png"
#define SHADERPACK_RIPPLE_NORMAL_PATH SHADERPACK_TEXTURES_DIR "/RippleNormal.png"
#define SHADERPACK_LDR_LUT_PATH SHADERPACK_TEXTURES_DIR "/LDR_LLL1_0.png"
#define SHADERPACK_TRANSMITTANCE_LUT_PATH SHADERPACK_ATMOSPHERE_DIR "/Transmittance.lut"
#define SHADERPACK_SCATTERING_LUT_PATH SHADERPACK_ATMOSPHERE_DIR "/Scattering.lut"
#define SHADERPACK_IRRADIANCE_LUT_PATH SHADERPACK_ATMOSPHERE_DIR "/Irradiance.lut"
#define SHADERPACK_FINAL_LUT_PATH SHADERPACK_ATMOSPHERE_DIR "/Final.lut"
#define CREATIVE_INVENTORY_PATH CREATIVE_TEXTURE_DIR "/tab_inventory.png"
#define CREATIVE_TABS_PATH     CREATIVE_TEXTURE_DIR "/tabs"
#define CREATIVE_TAB_ITEMS_PATH    CREATIVE_TEXTURE_DIR "/tab_items.png"
#define DEFAULT_FONT_PATH     FONTS_DIR "/msyhbd.ttc"
#define FOLIAGE_TEXTURE_PATH    COLORMAP_DIR "/foliage.png"
#define GRASS_TEXTURE_PATH      COLORMAP_DIR "/grass.png"
