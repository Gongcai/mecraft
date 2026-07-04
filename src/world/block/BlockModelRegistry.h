#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "Block.h"
#include "BlockModel.h"

class ResourceMgr;

class BlockModelRegistry {
public:
    [[nodiscard]] static bool init(ResourceMgr* resourceMgr);
    [[nodiscard]] static const BlockModel* get(const std::string& name);
    [[nodiscard]] static AnimatedTextureRef resolveTextureRef(const std::string& textureName);

private:
    static bool parseModel(const nlohmann::json& json,
                           const std::string& name,
                           std::unique_ptr<BlockModel>& outModel,
                           std::string& error);
    static bool parseElement(const nlohmann::json& json, ModelElement& outElement, std::string& error);
    static bool parseFace(const nlohmann::json& json, std::unique_ptr<ModelFace>& outFace, std::string& error);
    static bool resolveTextureVariables(BlockModel& model, std::string& error);
    static bool validateFaceTextureVariables(const BlockModel& model, std::string& error);

    static std::unordered_map<std::string, std::unique_ptr<BlockModel>> s_models;
    static ResourceMgr* s_resourceMgr;
};
