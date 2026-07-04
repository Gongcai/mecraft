#include "BlockInteractionRegistry.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "../../Paths.h"
#include "../../engine/registry/NamespacedId.h"

bool BlockInteractionRegistry::s_initialized = false;
std::unordered_map<std::string, BlockInteractionDef> BlockInteractionRegistry::s_defs;

namespace {
constexpr const char* kBlockInteractionConfigDir = BLOCK_INTERACTION_CONFIG_DIR;

const nlohmann::json& requireField(const nlohmann::json& owner,
                                   const std::string& ownerName,
                                   const char* fieldName) {
    if (!owner.contains(fieldName)) {
        throw std::runtime_error(ownerName + " is missing required field: " + fieldName);
    }
    return owner[fieldName];
}

std::string requireString(const nlohmann::json& owner,
                          const std::string& ownerName,
                          const char* fieldName) {
    const nlohmann::json& value = requireField(owner, ownerName, fieldName);
    if (!value.is_string()) {
        throw std::runtime_error(ownerName + " requires string field: " + fieldName);
    }
    const std::string parsed = value.get<std::string>();
    if (parsed.empty()) {
        throw std::runtime_error(ownerName + " requires non-empty field: " + fieldName);
    }
    return parsed;
}

std::string parseNamespacedField(const nlohmann::json& owner,
                                 const std::string& ownerName,
                                 const char* fieldName) {
    const NamespacedId id(requireString(owner, ownerName, fieldName));
    if (id.namespaceStr().empty() || id.path().empty()) {
        throw std::runtime_error(ownerName + " requires valid namespaced field: " + fieldName);
    }
    return id.full();
}

BlockInteractionActionKind parseActionKind(const std::string& value, const std::string& ownerName) {
    if (value == "toggle_boolean_property") {
        return BlockInteractionActionKind::ToggleBooleanProperty;
    }
    if (value == "set_property_once") {
        return BlockInteractionActionKind::SetPropertyOnce;
    }
    if (value == "cycle_property") {
        return BlockInteractionActionKind::CycleProperty;
    }
    throw std::runtime_error(ownerName + " has unknown block interaction action: " + value);
}

BlockInteractionPartnerSync parsePartnerSync(const std::string& value, const std::string& ownerName) {
    if (value == "none") {
        return BlockInteractionPartnerSync::None;
    }
    if (value == "door_open") {
        return BlockInteractionPartnerSync::DoorOpen;
    }
    throw std::runtime_error(ownerName + " has unknown partner sync mode: " + value);
}

std::vector<std::string> requireStringArray(const nlohmann::json& owner,
                                            const std::string& ownerName,
                                            const char* fieldName) {
    const nlohmann::json& values = requireField(owner, ownerName, fieldName);
    if (!values.is_array()) {
        throw std::runtime_error(ownerName + " requires array field: " + fieldName);
    }

    std::vector<std::string> parsed;
    parsed.reserve(values.size());
    for (int i = 0; i < static_cast<int>(values.size()); ++i) {
        const nlohmann::json& value = values[static_cast<std::size_t>(i)];
        if (!value.is_string()) {
            throw std::runtime_error(ownerName + "." + fieldName + "[" + std::to_string(i) +
                                     "] must be a string");
        }
        const std::string text = value.get<std::string>();
        if (text.empty()) {
            throw std::runtime_error(ownerName + "." + fieldName + "[" + std::to_string(i) +
                                     "] must not be empty");
        }
        parsed.push_back(text);
    }
    if (parsed.empty()) {
        throw std::runtime_error(ownerName + " requires non-empty array field: " + fieldName);
    }
    return parsed;
}

BlockInteractionDef parseBlockInteractionDef(const nlohmann::json& root, const std::string& sourceName) {
    if (!root.is_object()) {
        throw std::runtime_error("Block interaction file must contain an object: " + sourceName);
    }

    BlockInteractionDef def;
    def.id = parseNamespacedField(root, sourceName, "id");
    def.action = parseActionKind(requireString(root, def.id, "action"), def.id);
    def.property = requireString(root, def.id, "property");

    switch (def.action) {
        case BlockInteractionActionKind::ToggleBooleanProperty:
            def.falseValue = requireString(root, def.id, "falseValue");
            def.trueValue = requireString(root, def.id, "trueValue");
            break;
        case BlockInteractionActionKind::SetPropertyOnce:
            def.setValue = requireString(root, def.id, "value");
            break;
        case BlockInteractionActionKind::CycleProperty:
            def.cycleValues = requireStringArray(root, def.id, "values");
            if (def.cycleValues.size() < 2) {
                throw std::runtime_error(def.id + " requires at least two cycle values");
            }
            break;
    }

    const auto syncIt = root.find("partnerSync");
    if (syncIt != root.end()) {
        if (!syncIt->is_string()) {
            throw std::runtime_error(def.id + " requires string field: partnerSync");
        }
        def.partnerSync = parsePartnerSync(syncIt->get<std::string>(), def.id);
    }

    return def;
}

void loadBlockInteractionFile(const std::filesystem::path& path,
                              std::unordered_map<std::string, BlockInteractionDef>& defs) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open block interaction config: " + path.string());
    }

    nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded()) {
        throw std::runtime_error("Failed to parse block interaction config " + path.string() + ": invalid JSON");
    }

    BlockInteractionDef def = parseBlockInteractionDef(root, path.string());
    if (defs.find(def.id) != defs.end()) {
        throw std::runtime_error("Duplicate block interaction id: " + def.id);
    }
    defs.emplace(def.id, std::move(def));
}
}

void BlockInteractionRegistry::init() {
    if (s_initialized) {
        return;
    }

    const std::filesystem::path configDir(kBlockInteractionConfigDir);
    if (!std::filesystem::exists(configDir) || !std::filesystem::is_directory(configDir)) {
        throw std::runtime_error("Block interaction config directory is missing: " + configDir.string());
    }

    std::unordered_map<std::string, BlockInteractionDef> defs;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(configDir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        loadBlockInteractionFile(entry.path(), defs);
    }

    if (defs.empty()) {
        throw std::runtime_error("Block interaction config directory contains no JSON definitions: " +
                                 configDir.string());
    }

    s_defs = std::move(defs);
    s_initialized = true;
}

void BlockInteractionRegistry::ensureInitialized() {
    if (!s_initialized) {
        init();
    }
}

const BlockInteractionDef& BlockInteractionRegistry::require(const std::string& id) {
    ensureInitialized();
    const NamespacedId namespacedId(id);
    const std::string canonicalId = namespacedId.full();
    const auto it = s_defs.find(canonicalId);
    if (it == s_defs.end()) {
        throw std::runtime_error("Block interaction is not registered: " + canonicalId);
    }
    return it->second;
}

const std::unordered_map<std::string, BlockInteractionDef>& BlockInteractionRegistry::all() {
    ensureInitialized();
    return s_defs;
}
