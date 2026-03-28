//
// Created by Caiwe on 2026/3/24.
//

#include "Block.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>

#include "../resource/ResourceMgr.h"

std::array<BlockDef, BlockType::COUNT> BlockRegistry::s_blocks{};

namespace {
constexpr const char* kBlocksConfigPath = "../assets/config/blocks.json";
std::array<std::string, BlockType::COUNT> s_blockNames;
bool s_initialized = false;

void setAllFaces(BlockDef& def, int tex) {
    def.texTop = tex;
    def.texBottom = tex;
    def.texLeft = tex;
    def.texRight = tex;
    def.texFront = tex;
    def.texBack = tex;
}
}

void BlockRegistry::init(ResourceMgr* resourceMgr) {
    if (s_initialized) {
        return;
    }

    // 先建立一份稳定的默认表，读取配置失败时仍然可用。
    for (size_t i = 0; i < s_blocks.size(); ++i) {
        s_blockNames[i] = "unknown";
        s_blocks[i].name = s_blockNames[i].c_str();
        s_blocks[i].isSolid = true;
        s_blocks[i].isTransparent = false;
        s_blocks[i].isLightSource = false;
        s_blocks[i].lightLevel = 0;
        setAllFaces(s_blocks[i], 0);
    }

    s_blockNames[BlockType::AIR] = "air";
    s_blocks[BlockType::AIR].name = s_blockNames[BlockType::AIR].c_str();
    s_blocks[BlockType::AIR].isSolid = false;
    s_blocks[BlockType::AIR].isTransparent = true;
    setAllFaces(s_blocks[BlockType::AIR], -1);

    std::ifstream file(kBlocksConfigPath);
    if (!file.is_open()) {
        std::cerr << "[BlockRegistry] Failed to open config: " << kBlocksConfigPath << std::endl;
        s_initialized = true;
        return;
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (const std::exception& e) {
        std::cerr << "[BlockRegistry] Failed to parse blocks.json: " << e.what() << std::endl;
        s_initialized = true;
        return;
    }

    if (!root.contains("blocks") || !root["blocks"].is_array()) {
        std::cerr << "[BlockRegistry] Invalid blocks.json: missing 'blocks' array." << std::endl;
        s_initialized = true;
        return;
    }

    for (const auto& blockJson : root["blocks"]) {
        if (!blockJson.contains("id") || !blockJson["id"].is_number_integer()) {
            continue;
        }

        const int idInt = blockJson["id"].get<int>();
        if (idInt < 0 || idInt >= static_cast<int>(BlockType::COUNT)) {
            continue;
        }

        const auto id = static_cast<BlockID>(idInt);
        BlockDef def = s_blocks[id];

        if (blockJson.contains("name") && blockJson["name"].is_string()) {
            s_blockNames[id] = blockJson["name"].get<std::string>();
            def.name = s_blockNames[id].c_str();
        }

        if (blockJson.contains("isSolid") && blockJson["isSolid"].is_boolean()) {
            def.isSolid = blockJson["isSolid"].get<bool>();
        }
        if (blockJson.contains("isTransparent") && blockJson["isTransparent"].is_boolean()) {
            def.isTransparent = blockJson["isTransparent"].get<bool>();
        }
        if (blockJson.contains("isLightSource") && blockJson["isLightSource"].is_boolean()) {
            def.isLightSource = blockJson["isLightSource"].get<bool>();
        }
        if (blockJson.contains("lightLevel") && blockJson["lightLevel"].is_number_integer()) {
            const int light = blockJson["lightLevel"].get<int>();
            def.lightLevel = static_cast<uint8_t>(std::clamp(light, 0, 15));
        }

        if (blockJson.contains("textures") && blockJson["textures"].is_object()) {
            const auto& tex = blockJson["textures"];

            auto resolveTexName = [&](const char* key) -> int {
#ifdef MECRAFT_NO_TEXTURES
                return 0;
#else
                if (!tex.contains(key) || !tex[key].is_string() || resourceMgr == nullptr) {
                    return 0;
                }
                const std::string name = tex[key].get<std::string>();
                return static_cast<int>(resourceMgr->getTexture(name));
#endif
            };

            // 新格式：textures 下的字段全部是字符串贴图名
            if (tex.contains("all")) {
                const int idx = resolveTexName("all");
                setAllFaces(def, idx);
            }
            if (tex.contains("top")) {
                def.texTop = resolveTexName("top");
            }
            if (tex.contains("bottom")) {
                def.texBottom = resolveTexName("bottom");
            }
            if (tex.contains("side")) {
                const int idx = resolveTexName("side");
                def.texLeft  = idx;
                def.texRight = idx;
                def.texFront = idx;
                def.texBack  = idx;
            }
            if (tex.contains("left")) {
                def.texLeft = resolveTexName("left");
            }
            if (tex.contains("right")) {
                def.texRight = resolveTexName("right");
            }
            if (tex.contains("front")) {
                def.texFront = resolveTexName("front");
            }
            if (tex.contains("back")) {
                def.texBack = resolveTexName("back");
            }
        }

        s_blocks[id] = def;
    }

    s_initialized = true;
}

const BlockDef& BlockRegistry::get(BlockID id) {
    if (!s_initialized) {
        init(nullptr);
    }

    if (id >= BlockType::COUNT) {
        return s_blocks[BlockType::AIR];
    }

    return s_blocks[id];
}

void BlockRegistry::printAllBlocks() {
    for (const auto& block : s_blocks) {
        std::cout << block.name << std::endl;

    }
}
