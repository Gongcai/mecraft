#include "BlockInteractionRegistry.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>

#include <nlohmann/json.hpp>

#include "../../Paths.h"
#include "../../engine/registry/NamespacedId.h"

bool BlockInteractionRegistry::s_initialized = false;
std::unordered_map<std::string, BlockInteractionDef> BlockInteractionRegistry::s_defs;

namespace {
constexpr const char* kBlockInteractionConfigDir = BLOCK_INTERACTION_CONFIG_DIR;

bool fail(std::string& error, std::string message) {
    error = std::move(message);
    return false;
}

const nlohmann::json* findRequiredField(const nlohmann::json& owner, const std::string& ownerName,
                                        const char* fieldName, std::string& error) {
    const auto it = owner.find(fieldName);
    if (it == owner.end()) {
        fail(error, ownerName + " is missing required field: " + fieldName);
        return nullptr;
    }
    return &(*it);
}

bool readString(const nlohmann::json& owner, const std::string& ownerName, const char* fieldName, std::string& out,
                std::string& error) {
    const nlohmann::json* value = findRequiredField(owner, ownerName, fieldName, error);
    if (value == nullptr) {
        return false;
    }
    if (!value->is_string()) {
        return fail(error, ownerName + " requires string field: " + fieldName);
    }
    const std::string parsed = value->get<std::string>();
    if (parsed.empty()) {
        return fail(error, ownerName + " requires non-empty field: " + fieldName);
    }
    out = parsed;
    return true;
}

bool readNamespacedField(const nlohmann::json& owner, const std::string& ownerName, const char* fieldName,
                         std::string& out, std::string& error) {
    std::string raw;
    if (!readString(owner, ownerName, fieldName, raw, error)) {
        return false;
    }
    const NamespacedId id(raw);
    if (id.namespaceStr().empty() || id.path().empty()) {
        return fail(error, ownerName + " requires valid namespaced field: " + fieldName);
    }
    out = id.full();
    return true;
}

bool parseActionKind(const std::string& value, const std::string& ownerName, BlockInteractionActionKind& out,
                     std::string& error) {
    if (value == "toggle_boolean_property") {
        out = BlockInteractionActionKind::ToggleBooleanProperty;
        return true;
    }
    if (value == "set_property_once") {
        out = BlockInteractionActionKind::SetPropertyOnce;
        return true;
    }
    if (value == "cycle_property") {
        out = BlockInteractionActionKind::CycleProperty;
        return true;
    }
    return fail(error, ownerName + " has unknown block interaction action: " + value);
}

bool parsePartnerSync(const std::string& value, const std::string& ownerName, BlockInteractionPartnerSync& out,
                      std::string& error) {
    if (value == "none") {
        out = BlockInteractionPartnerSync::None;
        return true;
    }
    if (value == "door_open") {
        out = BlockInteractionPartnerSync::DoorOpen;
        return true;
    }
    return fail(error, ownerName + " has unknown partner sync mode: " + value);
}

bool readStringArray(const nlohmann::json& owner, const std::string& ownerName, const char* fieldName,
                     std::vector<std::string>& out, std::string& error) {
    const nlohmann::json* values = findRequiredField(owner, ownerName, fieldName, error);
    if (values == nullptr) {
        return false;
    }
    if (!values->is_array()) {
        return fail(error, ownerName + " requires array field: " + fieldName);
    }

    std::vector<std::string> parsed;
    parsed.reserve(values->size());
    for (int i = 0; i < static_cast<int>(values->size()); ++i) {
        const nlohmann::json& value = (*values)[static_cast<std::size_t>(i)];
        if (!value.is_string()) {
            return fail(error, ownerName + "." + fieldName + "[" + std::to_string(i) + "] must be a string");
        }
        const std::string text = value.get<std::string>();
        if (text.empty()) {
            return fail(error, ownerName + "." + fieldName + "[" + std::to_string(i) + "] must not be empty");
        }
        parsed.push_back(text);
    }
    if (parsed.empty()) {
        return fail(error, ownerName + " requires non-empty array field: " + fieldName);
    }
    out = std::move(parsed);
    return true;
}

bool parseBlockInteractionDef(const nlohmann::json& root, const std::string& sourceName, BlockInteractionDef& def,
                              std::string& error) {
    if (!root.is_object()) {
        return fail(error, "Block interaction file must contain an object: " + sourceName);
    }

    def = BlockInteractionDef{};
    std::string action;
    if (!readNamespacedField(root, sourceName, "id", def.id, error) ||
        !readString(root, def.id, "action", action, error) || !parseActionKind(action, def.id, def.action, error) ||
        !readString(root, def.id, "property", def.property, error)) {
        return false;
    }

    switch (def.action) {
    case BlockInteractionActionKind::ToggleBooleanProperty:
        if (!readString(root, def.id, "falseValue", def.falseValue, error) ||
            !readString(root, def.id, "trueValue", def.trueValue, error)) {
            return false;
        }
        break;
    case BlockInteractionActionKind::SetPropertyOnce:
        if (!readString(root, def.id, "value", def.setValue, error)) {
            return false;
        }
        break;
    case BlockInteractionActionKind::CycleProperty:
        if (!readStringArray(root, def.id, "values", def.cycleValues, error)) {
            return false;
        }
        if (def.cycleValues.size() < 2) {
            return fail(error, def.id + " requires at least two cycle values");
        }
        break;
    }

    const auto syncIt = root.find("partnerSync");
    if (syncIt != root.end()) {
        if (!syncIt->is_string()) {
            return fail(error, def.id + " requires string field: partnerSync");
        }
        if (!parsePartnerSync(syncIt->get<std::string>(), def.id, def.partnerSync, error)) {
            return false;
        }
    }

    return true;
}

bool loadBlockInteractionFile(const std::filesystem::path& path,
                              std::unordered_map<std::string, BlockInteractionDef>& defs, std::string& error) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return fail(error, "Failed to open block interaction config: " + path.string());
    }

    nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded()) {
        return fail(error, "Failed to parse block interaction config " + path.string() + ": invalid JSON");
    }

    BlockInteractionDef def;
    if (!parseBlockInteractionDef(root, path.string(), def, error)) {
        return false;
    }
    if (defs.find(def.id) != defs.end()) {
        return fail(error, "Duplicate block interaction id: " + def.id);
    }
    defs.emplace(def.id, std::move(def));
    return true;
}
} // namespace

bool BlockInteractionRegistry::init() {
    if (s_initialized) {
        return true;
    }

    const std::filesystem::path configDir(kBlockInteractionConfigDir);
    std::error_code fsError;
    if (!std::filesystem::exists(configDir, fsError) || fsError || !std::filesystem::is_directory(configDir, fsError) ||
        fsError) {
        std::cerr << "Block interaction config directory is missing: " << configDir.string() << '\n';
        return false;
    }

    std::unordered_map<std::string, BlockInteractionDef> defs;
    std::filesystem::directory_iterator it(configDir, fsError);
    if (fsError) {
        std::cerr << "Failed to enumerate block interaction config directory: " << configDir.string() << ": "
                  << fsError.message() << '\n';
        return false;
    }
    const std::filesystem::directory_iterator end;
    for (; it != end; it.increment(fsError)) {
        if (fsError) {
            std::cerr << "Failed to advance block interaction config iterator: " << configDir.string() << ": "
                      << fsError.message() << '\n';
            return false;
        }
        const std::filesystem::directory_entry& entry = *it;
        if (!entry.is_regular_file(fsError) || fsError || entry.path().extension() != ".json") {
            fsError.clear();
            continue;
        }
        std::string error;
        if (!loadBlockInteractionFile(entry.path(), defs, error)) {
            std::cerr << error << '\n';
            return false;
        }
    }

    if (defs.empty()) {
        std::cerr << "Block interaction config directory contains no JSON definitions: " << configDir.string() << '\n';
        return false;
    }

    s_defs = std::move(defs);
    s_initialized = true;
    return true;
}

bool BlockInteractionRegistry::ensureInitialized() {
    return s_initialized || init();
}

const BlockInteractionDef& BlockInteractionRegistry::require(const std::string& id) {
    if (!ensureInitialized()) {
        std::cerr << "Block interaction registry failed to initialize\n";
        std::abort();
    }
    const NamespacedId namespacedId(id);
    const std::string canonicalId = namespacedId.full();
    const auto it = s_defs.find(canonicalId);
    if (it == s_defs.end()) {
        std::cerr << "Block interaction is not registered: " << canonicalId << '\n';
        std::abort();
    }
    return it->second;
}

const std::unordered_map<std::string, BlockInteractionDef>& BlockInteractionRegistry::all() {
    if (!ensureInitialized()) {
        std::cerr << "Block interaction registry failed to initialize\n";
        std::abort();
    }
    return s_defs;
}
