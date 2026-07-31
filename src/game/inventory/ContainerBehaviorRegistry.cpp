#include "ContainerBehaviorRegistry.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <utility>

#include <nlohmann/json.hpp>

#include "../../Paths.h"
#include "../../engine/registry/NamespacedId.h"

bool ContainerBehaviorRegistry::s_initialized = false;
std::unordered_map<std::string, ContainerBehaviorDef> ContainerBehaviorRegistry::s_defs;

namespace {
constexpr const char* kContainerBehaviorConfigDir = CONTAINER_BEHAVIOR_CONFIG_DIR;

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

bool readInteger(const nlohmann::json& owner, const std::string& ownerName, const char* fieldName, int& out,
                 std::string& error) {
    const nlohmann::json* value = findRequiredField(owner, ownerName, fieldName, error);
    if (value == nullptr) {
        return false;
    }
    if (!value->is_number_integer()) {
        return fail(error, ownerName + " requires integer field: " + fieldName);
    }
    const int64_t parsed = value->get<int64_t>();
    if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
        return fail(error, ownerName + " integer field is out of range: " + fieldName);
    }
    out = static_cast<int>(parsed);
    return true;
}

bool readNonNegativeInteger(const nlohmann::json& owner, const std::string& ownerName, const char* fieldName, int& out,
                            std::string& error) {
    if (!readInteger(owner, ownerName, fieldName, out, error)) {
        return false;
    }
    if (out < 0) {
        return fail(error, ownerName + " requires non-negative field: " + fieldName);
    }
    return true;
}

bool readPositiveInteger(const nlohmann::json& owner, const std::string& ownerName, const char* fieldName, int& out,
                         std::string& error) {
    if (!readInteger(owner, ownerName, fieldName, out, error)) {
        return false;
    }
    if (out <= 0) {
        return fail(error, ownerName + " requires positive field: " + fieldName);
    }
    return true;
}

bool readBoolean(const nlohmann::json& owner, const std::string& ownerName, const char* fieldName, bool& out,
                 std::string& error) {
    const nlohmann::json* value = findRequiredField(owner, ownerName, fieldName, error);
    if (value == nullptr) {
        return false;
    }
    if (!value->is_boolean()) {
        return fail(error, ownerName + " requires boolean field: " + fieldName);
    }
    out = value->get<bool>();
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

bool parseStorageKind(const std::string& value, const std::string& ownerName, ContainerStorageKind& out,
                      std::string& error) {
    if (value == "block_entity") {
        out = ContainerStorageKind::BlockEntity;
        return true;
    }
    if (value == "transient") {
        out = ContainerStorageKind::Transient;
        return true;
    }
    return fail(error, ownerName + " has unknown storage kind: " + value);
}

bool validateKnownHandler(const std::string& handler, const std::string& ownerName, std::string& error) {
    if (handler == "storage" || handler == "smelting" || handler == "crafting") {
        return true;
    }
    return fail(error, ownerName + " has unknown container behavior handler: " + handler);
}

bool validateKnownAccepts(const std::string& accepts, const std::string& ownerName, std::string& error) {
    if (accepts == "any" || accepts == "smelting_input" || accepts == "fuel") {
        return true;
    }
    return fail(error, ownerName + " has unknown slot accepts rule: " + accepts);
}

bool parseStorage(const nlohmann::json& storageJson, const std::string& behaviorId, ContainerStorageDef& storage,
                  std::string& error) {
    if (!storageJson.is_object()) {
        return fail(error, behaviorId + ".storage must be an object");
    }

    std::string kind;
    return readString(storageJson, behaviorId + ".storage", "kind", kind, error) &&
           parseStorageKind(kind, behaviorId + ".storage", storage.kind, error) &&
           readPositiveInteger(storageJson, behaviorId + ".storage", "slots", storage.slots, error);
}

bool parseSlotRule(const nlohmann::json& ruleJson, const std::string& behaviorId, const int index,
                   const int storageSlots, ContainerSlotRuleDef& rule, std::string& error) {
    if (!ruleJson.is_object()) {
        return fail(error, behaviorId + ".slotRules[" + std::to_string(index) + "] must be an object");
    }
    const std::string ownerName = behaviorId + ".slotRules[" + std::to_string(index) + "]";

    if (!readString(ruleJson, ownerName, "id", rule.id, error) ||
        !readNonNegativeInteger(ruleJson, ownerName, "slot", rule.slot, error)) {
        return false;
    }
    if (rule.slot >= storageSlots) {
        return fail(error, ownerName + " slot exceeds storage slot count");
    }
    return readString(ruleJson, ownerName, "accepts", rule.accepts, error) &&
           validateKnownAccepts(rule.accepts, ownerName, error) &&
           readBoolean(ruleJson, ownerName, "outputOnly", rule.outputOnly, error);
}

bool parseProcessor(const nlohmann::json& processorJson, const std::string& behaviorId, const int index,
                    const int storageSlots, ContainerProcessorDef& processor, std::string& error) {
    if (!processorJson.is_object()) {
        return fail(error, behaviorId + ".processors[" + std::to_string(index) + "] must be an object");
    }
    const std::string ownerName = behaviorId + ".processors[" + std::to_string(index) + "]";

    if (!readString(processorJson, ownerName, "id", processor.id, error) ||
        !readString(processorJson, ownerName, "type", processor.type, error)) {
        return false;
    }
    if (processor.type == "smelting") {
        if (!readNonNegativeInteger(processorJson, ownerName, "inputSlot", processor.inputSlot, error) ||
            !readNonNegativeInteger(processorJson, ownerName, "fuelSlot", processor.fuelSlot, error) ||
            !readNonNegativeInteger(processorJson, ownerName, "outputSlot", processor.outputSlot, error)) {
            return false;
        }
        if (processor.inputSlot >= storageSlots || processor.fuelSlot >= storageSlots ||
            processor.outputSlot >= storageSlots) {
            return fail(error, ownerName + " references a slot outside storage");
        }
        return true;
    }
    if (processor.type == "crafting") {
        if (!readPositiveInteger(processorJson, ownerName, "gridSize", processor.gridSize, error) ||
            !readNonNegativeInteger(processorJson, ownerName, "resultSlot", processor.resultSlot, error)) {
            return false;
        }
        if (processor.resultSlot >= storageSlots) {
            return fail(error, ownerName + " resultSlot references a slot outside storage");
        }
        return true;
    }
    return fail(error, ownerName + " has unknown processor type: " + processor.type);
}

bool parseContainerBehaviorDef(const nlohmann::json& root, const std::string& sourceName, ContainerBehaviorDef& def,
                               std::string& error) {
    if (!root.is_object()) {
        return fail(error, "Container behavior file must contain an object: " + sourceName);
    }

    def = ContainerBehaviorDef{};
    if (!readNamespacedField(root, sourceName, "id", def.id, error) ||
        !readString(root, def.id, "handler", def.handler, error) || !validateKnownHandler(def.handler, def.id, error)) {
        return false;
    }
    const nlohmann::json* storage = findRequiredField(root, def.id, "storage", error);
    if (storage == nullptr || !parseStorage(*storage, def.id, def.storage, error)) {
        return false;
    }
    const auto comparatorSignalIt = root.find("comparatorSignal");
    if (comparatorSignalIt != root.end()) {
        if (!comparatorSignalIt->is_boolean()) {
            return fail(error, def.id + " requires boolean field: comparatorSignal");
        }
        def.comparatorSignal = comparatorSignalIt->get<bool>();
        if (def.comparatorSignal && def.storage.kind != ContainerStorageKind::BlockEntity) {
            return fail(error, def.id + " comparatorSignal requires block_entity storage");
        }
    }

    const nlohmann::json* slotRules = findRequiredField(root, def.id, "slotRules", error);
    if (slotRules == nullptr) {
        return false;
    }
    if (!slotRules->is_array()) {
        return fail(error, def.id + " requires array field: slotRules");
    }
    for (int i = 0; i < static_cast<int>(slotRules->size()); ++i) {
        ContainerSlotRuleDef rule;
        if (!parseSlotRule((*slotRules)[static_cast<std::size_t>(i)], def.id, i, def.storage.slots, rule, error)) {
            return false;
        }
        def.slotRules.push_back(std::move(rule));
    }

    const nlohmann::json* processors = findRequiredField(root, def.id, "processors", error);
    if (processors == nullptr) {
        return false;
    }
    if (!processors->is_array()) {
        return fail(error, def.id + " requires array field: processors");
    }
    for (int i = 0; i < static_cast<int>(processors->size()); ++i) {
        ContainerProcessorDef processor;
        if (!parseProcessor((*processors)[static_cast<std::size_t>(i)], def.id, i, def.storage.slots, processor,
                            error)) {
            return false;
        }
        def.processors.push_back(std::move(processor));
    }

    return true;
}

bool loadContainerBehaviorFile(const std::filesystem::path& path,
                               std::unordered_map<std::string, ContainerBehaviorDef>& defs, std::string& error) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return fail(error, "Failed to open container behavior config: " + path.string());
    }

    nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded()) {
        return fail(error, "Failed to parse container behavior config " + path.string() + ": invalid JSON");
    }

    ContainerBehaviorDef def;
    if (!parseContainerBehaviorDef(root, path.string(), def, error)) {
        return false;
    }
    if (defs.find(def.id) != defs.end()) {
        return fail(error, "Duplicate container behavior id: " + def.id);
    }
    defs.emplace(def.id, std::move(def));
    return true;
}
} // namespace

bool ContainerBehaviorRegistry::init() {
    if (s_initialized) {
        return true;
    }

    const std::filesystem::path configDir(kContainerBehaviorConfigDir);
    std::error_code fsError;
    if (!std::filesystem::exists(configDir, fsError) || fsError || !std::filesystem::is_directory(configDir, fsError) ||
        fsError) {
        std::cerr << "Container behavior config directory is missing: " << configDir.string() << '\n';
        return false;
    }

    std::unordered_map<std::string, ContainerBehaviorDef> defs;
    std::filesystem::directory_iterator it(configDir, fsError);
    if (fsError) {
        std::cerr << "Failed to enumerate container behavior config directory: " << configDir.string() << ": "
                  << fsError.message() << '\n';
        return false;
    }
    const std::filesystem::directory_iterator end;
    for (; it != end; it.increment(fsError)) {
        if (fsError) {
            std::cerr << "Failed to advance container behavior config iterator: " << configDir.string() << ": "
                      << fsError.message() << '\n';
            return false;
        }
        const std::filesystem::directory_entry& entry = *it;
        if (!entry.is_regular_file(fsError) || fsError || entry.path().extension() != ".json") {
            fsError.clear();
            continue;
        }
        std::string error;
        if (!loadContainerBehaviorFile(entry.path(), defs, error)) {
            std::cerr << error << '\n';
            return false;
        }
    }

    if (defs.empty()) {
        std::cerr << "Container behavior config directory contains no JSON definitions: " << configDir.string() << '\n';
        return false;
    }

    s_defs = std::move(defs);
    s_initialized = true;
    return true;
}

bool ContainerBehaviorRegistry::ensureInitialized() {
    return s_initialized || init();
}

const ContainerBehaviorDef& ContainerBehaviorRegistry::require(const std::string& id) {
    if (!ensureInitialized()) {
        std::cerr << "Container behavior registry failed to initialize\n";
        std::abort();
    }
    const NamespacedId namespacedId(id);
    const std::string canonicalId = namespacedId.full();
    const auto it = s_defs.find(canonicalId);
    if (it == s_defs.end()) {
        std::cerr << "Container behavior is not registered: " << canonicalId << '\n';
        std::abort();
    }
    return it->second;
}

const std::unordered_map<std::string, ContainerBehaviorDef>& ContainerBehaviorRegistry::all() {
    if (!ensureInitialized()) {
        std::cerr << "Container behavior registry failed to initialize\n";
        std::abort();
    }
    return s_defs;
}
