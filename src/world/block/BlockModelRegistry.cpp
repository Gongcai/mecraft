#include "BlockModelRegistry.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <system_error>
#include <unordered_set>

#include "../../resource/ResourceMgr.h"
#include "Paths.h"

std::unordered_map<std::string, std::unique_ptr<BlockModel>> BlockModelRegistry::s_models{};
ResourceMgr* BlockModelRegistry::s_resourceMgr = nullptr;

namespace {
constexpr const char* kModelFormat = "mecraft_model_v1";
constexpr const char* kBlockModelsDir = ASSETS_DIR "/models/blocks";

bool directionBitFromName(const std::string& name, uint8_t& out, std::string& error) {
    if (name == "up") {
        out = static_cast<uint8_t>(1u << 0u);
        return true;
    }
    if (name == "down") {
        out = static_cast<uint8_t>(1u << 1u);
        return true;
    }
    if (name == "south") {
        out = static_cast<uint8_t>(1u << 2u);
        return true;
    }
    if (name == "north") {
        out = static_cast<uint8_t>(1u << 3u);
        return true;
    }
    if (name == "west") {
        out = static_cast<uint8_t>(1u << 4u);
        return true;
    }
    if (name == "east") {
        out = static_cast<uint8_t>(1u << 5u);
        return true;
    }
    error = "Unknown model face direction: " + name;
    return false;
}

bool directionIndexFromName(const std::string& name, int& out, std::string& error) {
    if (name == "up") {
        out = 0;
        return true;
    }
    if (name == "down") {
        out = 1;
        return true;
    }
    if (name == "south") {
        out = 2;
        return true;
    }
    if (name == "north") {
        out = 3;
        return true;
    }
    if (name == "west") {
        out = 4;
        return true;
    }
    if (name == "east") {
        out = 5;
        return true;
    }
    error = "Unknown model face direction: " + name;
    return false;
}

bool readVec3(const nlohmann::json& json, const char* key, std::array<float, 3>& out, std::string& error) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_array() || it->size() != 3) {
        error = std::string("Model element requires vec3 field: ") + key;
        return false;
    }

    for (size_t i = 0; i < out.size(); ++i) {
        if (!(*it)[i].is_number()) {
            error = std::string("Model vec3 field contains non-number value: ") + key;
            return false;
        }
        out[i] = (*it)[i].get<float>();
    }
    return true;
}

bool readUv(const nlohmann::json& json, std::array<float, 4>& out, std::string& error) {
    const auto it = json.find("uv");
    if (it == json.end() || !it->is_array() || it->size() != 4) {
        error = "Model face requires uv with four numbers";
        return false;
    }

    for (size_t i = 0; i < out.size(); ++i) {
        if (!(*it)[i].is_number()) {
            error = "Model face uv contains non-number value";
            return false;
        }
        out[i] = (*it)[i].get<float>();
    }
    return true;
}

std::string makeModelName(const std::filesystem::path& base, const std::filesystem::path& file) {
    std::filesystem::path relative = file.lexically_relative(base);
    relative.replace_extension();
    std::string path = relative.generic_string();
    return "block/" + path;
}

bool resolveTextureVariable(const std::unordered_map<std::string, std::string>& textures, const std::string& key,
                            std::unordered_set<std::string>& visiting, std::string& outTexture, std::string& error) {
    const auto it = textures.find(key);
    if (it == textures.end()) {
        error = "Unknown model texture variable: " + key;
        return false;
    }

    const std::string& value = it->second;
    if (value.empty() || value.front() != '#') {
        outTexture = value;
        return true;
    }

    const std::string nextKey = value.substr(1);
    if (!visiting.insert(nextKey).second) {
        error = "Cyclic model texture variable reference: " + key;
        return false;
    }
    std::string resolved;
    if (!resolveTextureVariable(textures, nextKey, visiting, resolved, error)) {
        return false;
    }
    visiting.erase(nextKey);
    outTexture = std::move(resolved);
    return true;
}
} // namespace

bool BlockModelRegistry::init(ResourceMgr* resourceMgr) {
    s_resourceMgr = resourceMgr;
    s_models.clear();

    const std::filesystem::path modelDir(kBlockModelsDir);
    std::error_code fsError;
    if (!std::filesystem::exists(modelDir, fsError)) {
        if (fsError) {
            std::cerr << "Failed to inspect block model directory: " << modelDir.string() << ": " << fsError.message()
                      << '\n';
        } else {
            std::cerr << "Block model directory does not exist: " << modelDir.string() << '\n';
        }
        return false;
    }
    fsError.clear();
    if (!std::filesystem::is_directory(modelDir, fsError)) {
        if (fsError) {
            std::cerr << "Failed to inspect block model path: " << modelDir.string() << ": " << fsError.message()
                      << '\n';
        } else {
            std::cerr << "Block model path is not a directory: " << modelDir.string() << '\n';
        }
        return false;
    }

    std::filesystem::recursive_directory_iterator it(modelDir, std::filesystem::directory_options::none, fsError);
    if (fsError) {
        std::cerr << "Failed to iterate block model directory: " << modelDir.string() << ": " << fsError.message()
                  << '\n';
        return false;
    }
    const std::filesystem::recursive_directory_iterator end;
    while (it != end) {
        const std::filesystem::directory_entry& entry = *it;
        fsError.clear();
        const bool regularFile = entry.is_regular_file(fsError);
        if (fsError) {
            std::cerr << "Failed to inspect block model path: " << entry.path().string() << ": " << fsError.message()
                      << '\n';
            return false;
        }
        if (!regularFile || entry.path().extension() != ".json") {
            it.increment(fsError);
            if (fsError) {
                std::cerr << "Failed to continue iterating block model directory: " << modelDir.string() << ": "
                          << fsError.message() << '\n';
                return false;
            }
            continue;
        }

        std::ifstream file(entry.path());
        if (!file.is_open()) {
            std::cerr << "Failed to open block model file: " << entry.path().string() << '\n';
            return false;
        }

        nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
        if (root.is_discarded()) {
            std::cerr << "Failed to parse block model file: " << entry.path().string() << '\n';
            return false;
        }
        const std::string name = makeModelName(modelDir, entry.path());
        std::unique_ptr<BlockModel> model;
        std::string error;
        if (!parseModel(root, name, model, error)) {
            std::cerr << "Failed to parse block model " << name << ": " << error << '\n';
            return false;
        }
        s_models[name] = std::move(model);

        it.increment(fsError);
        if (fsError) {
            std::cerr << "Failed to continue iterating block model directory: " << modelDir.string() << ": "
                      << fsError.message() << '\n';
            return false;
        }
    }
    return true;
}

const BlockModel* BlockModelRegistry::get(const std::string& name) {
    const auto it = s_models.find(name);
    return it != s_models.end() ? it->second.get() : nullptr;
}

AnimatedTextureRef BlockModelRegistry::resolveTextureRef(const std::string& textureName) {
    if (s_resourceMgr == nullptr) {
        std::cerr << "Cannot resolve model texture without ResourceMgr: " << textureName << '\n';
        std::abort();
    }

    const TextureAnimationInfo info = s_resourceMgr->getTextureAnimation(textureName);
    AnimatedTextureRef ref;
    ref.firstLayer = info.firstLayer;
    ref.frameCount = static_cast<uint16_t>(std::max(1, info.frameCount));
    ref.fps = info.fps;
    ref.isAnimated = info.isAnimated;
    return ref;
}

bool BlockModelRegistry::parseModel(const nlohmann::json& json, const std::string& name,
                                    std::unique_ptr<BlockModel>& outModel, std::string& error) {
    if (!json.is_object()) {
        error = "Block model root must be an object: " + name;
        return false;
    }
    const auto formatIt = json.find("format");
    if (formatIt == json.end() || !formatIt->is_string() || formatIt->get<std::string>() != kModelFormat) {
        error = "Block model has unsupported format: " + name;
        return false;
    }

    auto model = std::make_unique<BlockModel>();
    model->name = name;
    if (const auto aoIt = json.find("ambientocclusion"); aoIt != json.end()) {
        if (!aoIt->is_boolean()) {
            error = "Block model ambientocclusion must be boolean: " + name;
            return false;
        }
        model->ambientOcclusion = aoIt->get<bool>();
    }

    const auto texturesIt = json.find("textures");
    if (texturesIt == json.end() || !texturesIt->is_object()) {
        error = "Block model requires textures object: " + name;
        return false;
    }
    for (auto it = texturesIt->begin(); it != texturesIt->end(); ++it) {
        if (!it.value().is_string()) {
            error = "Block model texture value must be string: " + name + "." + it.key();
            return false;
        }
        model->textures[it.key()] = it.value().get<std::string>();
    }
    if (!resolveTextureVariables(*model, error)) {
        return false;
    }

    const auto elementsIt = json.find("elements");
    if (elementsIt == json.end() || !elementsIt->is_array()) {
        error = "Block model requires elements array: " + name;
        return false;
    }
    model->elements.reserve(elementsIt->size());
    for (const nlohmann::json& elementJson : *elementsIt) {
        ModelElement element;
        if (!parseElement(elementJson, element, error)) {
            return false;
        }
        model->elements.push_back(std::move(element));
    }
    if (!validateFaceTextureVariables(*model, error)) {
        return false;
    }
    outModel = std::move(model);
    return true;
}

bool BlockModelRegistry::parseElement(const nlohmann::json& json, ModelElement& outElement, std::string& error) {
    if (!json.is_object()) {
        error = "Model element must be an object";
        return false;
    }

    ModelElement element;
    if (!readVec3(json, "from", element.from, error)) {
        return false;
    }
    if (!readVec3(json, "to", element.to, error)) {
        return false;
    }

    const auto facesIt = json.find("faces");
    if (facesIt == json.end() || !facesIt->is_object()) {
        error = "Model element requires faces object";
        return false;
    }

    for (auto it = facesIt->begin(); it != facesIt->end(); ++it) {
        int direction = 0;
        if (!directionIndexFromName(it.key(), direction, error)) {
            return false;
        }
        std::unique_ptr<ModelFace> face;
        if (!parseFace(it.value(), face, error)) {
            return false;
        }
        element.faces[static_cast<size_t>(direction)] = std::move(face);
    }
    outElement = std::move(element);
    return true;
}

bool BlockModelRegistry::parseFace(const nlohmann::json& json, std::unique_ptr<ModelFace>& outFace,
                                   std::string& error) {
    if (!json.is_object()) {
        error = "Model face must be an object";
        return false;
    }

    const auto textureIt = json.find("texture");
    if (textureIt == json.end() || !textureIt->is_string()) {
        error = "Model face requires texture string";
        return false;
    }
    const std::string textureVar = textureIt->get<std::string>();
    if (textureVar.empty() || textureVar.front() != '#') {
        error = "Model face texture must reference a texture variable";
        return false;
    }

    auto face = std::make_unique<ModelFace>();
    face->textureVar = textureVar;
    if (!readUv(json, face->uv, error)) {
        return false;
    }

    if (const auto cullIt = json.find("cullface"); cullIt != json.end()) {
        if (!cullIt->is_string()) {
            error = "Model face cullface must be string";
            return false;
        }
        if (!directionBitFromName(cullIt->get<std::string>(), face->cullfaceBits, error)) {
            return false;
        }
    }

    if (const auto rotationIt = json.find("rotation"); rotationIt != json.end()) {
        if (!rotationIt->is_number_integer()) {
            error = "Model face rotation must be integer";
            return false;
        }
        const int rotation = rotationIt->get<int>();
        if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) {
            error = "Model face rotation must be 0, 90, 180, or 270";
            return false;
        }
        face->uvRotation = static_cast<uint16_t>(rotation);
    }

    if (const auto tintIt = json.find("tintindex"); tintIt != json.end()) {
        if (!tintIt->is_number_integer()) {
            error = "Model face tintindex must be integer";
            return false;
        }
        face->tintIndex = static_cast<int8_t>(std::clamp(tintIt->get<int>(), -1, 127));
    }
    outFace = std::move(face);
    return true;
}

bool BlockModelRegistry::resolveTextureVariables(BlockModel& model, std::string& error) {
    std::unordered_map<std::string, std::string> resolved;
    resolved.reserve(model.textures.size());
    for (const auto& [key, value] : model.textures) {
        std::unordered_set<std::string> visiting;
        visiting.insert(key);
        std::string texture;
        if (!resolveTextureVariable(model.textures, key, visiting, texture, error)) {
            return false;
        }
        resolved[key] = std::move(texture);
    }
    model.textures = std::move(resolved);
    return true;
}

bool BlockModelRegistry::validateFaceTextureVariables(const BlockModel& model, std::string& error) {
    for (const ModelElement& element : model.elements) {
        for (const std::unique_ptr<ModelFace>& face : element.faces) {
            if (!face) {
                continue;
            }
            const std::string textureKey = face->textureVar.substr(1);
            if (model.textures.find(textureKey) == model.textures.end()) {
                error = "Model face references undefined texture variable: " + model.name + "." + textureKey;
                return false;
            }
        }
    }
    return true;
}
