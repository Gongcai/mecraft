#pragma once

// 平台相关的资源根路径
// Linux:   可执行文件通常在项目根目录或 build/ 子目录，使用 "assets"
// Windows: 可执行文件在 build/ 子目录，需要 "../assets" 向上回溯
#ifdef __linux__
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

// 常用资源路径
#define BLOCKS_TEXTURES_DIR   TEXTURES_DIR "/blocks"
#define ITEMS_TEXTURES_DIR    TEXTURES_DIR "/items"
#define GUI_TEXTURES_DIR      TEXTURES_DIR "/gui"
#define FONT_TEXTURES_DIR     TEXTURES_DIR "/font"
#define LIGHTMAP_DIR          TEXTURES_DIR "/lightmap"
#define ENTITY_TEXTURE_DIR    TEXTURES_DIR "/entity"

#define MOBS_TEXTURE_DIR       ENTITY_TEXTURE_DIR "/mobs"
#define ICONS_TEXTURE_DIR      GUI_TEXTURES_DIR "/icons"



#define BLOCKS_CONFIG_PATH    CONFIG_DIR "/blocks.json"
#define FLUIDS_CONFIG_PATH    CONFIG_DIR "/fluids.json"
#define ITEMS_CONFIG_PATH     CONFIG_DIR "/items.json"
#define RECIPES_CONFIG_PATH   CONFIG_DIR "/recipes.json"
#define KEYBINDINGS_PATH      CONFIG_DIR "/keybindings.txt"

#define LIGHTMAP_DAY_PATH     LIGHTMAP_DIR "/lightmap_day.png"
#define LIGHTMAP_NIGHT_PATH   LIGHTMAP_DIR "/lightmap_night.png"
#define WIDGETS_TEXTURE_PATH  GUI_TEXTURES_DIR "/widgets.png"
#define INVENTORY_TEX_PATH    GUI_TEXTURES_DIR "/inventory.png"
#define FONT_ASCII_PATH       FONT_TEXTURES_DIR "/ascii.png"
#define TEST_TEXTURE_PATH     BLOCKS_TEXTURES_DIR "/test.png"
#define STEVE_TEXTURE_PATH    ENTITY_TEXTURE_DIR  "/steve.png"
#define ZOMBIE_TEXTURE_PATH    MOBS_TEXTURE_DIR  "/zombie.png"
