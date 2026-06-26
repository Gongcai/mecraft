#include "ContainerBehaviorRegistry.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "../../Paths.h"
#include "../../engine/registry/NamespacedId.h"

bool ContainerBehaviorRegistry::s_initialized = false;
std::unordered_map<std::string, ContainerBehaviorDef> ContainerBehaviorRegistry::s_defs;

namespace {
constexpr const char* kContainerBehaviorConfigDir = CONTAINER_BEHAVIOR_CONFIG_DIR;

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

int requireInteger(const nlohmann::json& owner,
                   const std::string& ownerName,
                   const char* fieldName) {
    const nlohmann::json& value = requireField(owner, ownerName, fieldName);
    if (!value.is_number_integer()) {
        throw std::runtime_error(ownerName + " requires integer field: " + fieldName);
    }
    return value.get<int>();
}

int requireNonNegativeInteger(const nlohmann::json& owner,
                              const std::string& ownerName,
                              const char* fieldName) {
    const int parsed = requireInteger(owner, ownerName, fieldName);
    if (parsed < 0) {
        throw std::runtime_error(ownerName + " requires non-negative field: " + fieldName);
    }
    return parsed;
}

int requirePositiveInteger(const nlohmann::json& owner,
                           const std::string& ownerName,
                           const char* fieldName) {
    const int parsed = requireInteger(owner, ownerName, fieldName);
    if (parsed <= 0) {
        throw std::runtime_error(ownerName + " requires positive field: " + fieldName);
    }
    return parsed;
}

bool requireBoolean(const nlohmann::json& owner,
                    const std::string& ownerName,
                    const char* fieldName) {
    const nlohmann::json& value = requireField(owner, ownerName, fieldName);
    if (!value.is_boolean()) {
        throw std::runtime_error(ownerName + " requires boolean field: " + fieldName);
    }
    return value.get<bool>();
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

ContainerStorageKind parseStorageKind(const std::string& value, const std::string& ownerName) {
    if (value == "block_entity") {
        return ContainerStorageKind::BlockEntity;
    }
    if (value == "transient") {
        return ContainerStorageKind::Transient;
    }
    throw std::runtime_error(ownerName + " has unknown storage kind: " + value);
}

void requireKnownHandler(const std::string& handler, const std::string& ownerName) {
    if (handler == "storage" || handler == "smelting" || handler == "crafting") {
        return;
    }
    throw std::runtime_error(ownerName + " has unknown container behavior handler: " + handler);
}

void requireKnownAccepts(const std::string& accepts, const std::string& ownerName) {
    if (accepts == "any" || accepts == "smelting_input" || accepts == "fuel") {
        return;
    }
    throw std::runtime_error(ownerName + " has unknown slot accepts rule: " + accepts);
}

ContainerStorageDef parseStorage(const nlohmann::json& storageJson, const std::string& behaviorId) {
    if (!storageJson.is_object()) {
        throw std::runtime_error(behaviorId + ".storage must be an object");
    }

    ContainerStorageDef storage;
    storage.kind = parseStorageKind(requireString(storageJson, behaviorId + ".storage", "kind"),
                                    behaviorId + ".storage");
    storage.slots = requirePositiveInteger(storageJson, behaviorId + ".storage", "slots");
    return storage;
}

ContainerSlotRuleDef parseSlotRule(const nlohmann::json& ruleJson,
                                   const std::string& behaviorId,
                                   const int index,
                                   const int storageSlots) {
    if (!ruleJson.is_object()) {
        throw std::runtime_error(behaviorId + ".slotRules[" + std::to_string(index) + "] must be an object");
    }
    const std::string ownerName = behaviorId + ".slotRules[" + std::to_string(index) + "]";

    ContainerSlotRuleDef rule;
    rule.id = requireString(ruleJson, ownerName, "id");
    rule.slot = requireNonNegativeInteger(ruleJson, ownerName, "slot");
    if (rule.slot >= storageSlots) {
        throw std::runtime_error(ownerName + " slot exceeds storage slot count");
    }
    rule.accepts = requireString(ruleJson, ownerName, "accepts");
    requireKnownAccepts(rule.accepts, ownerName);
    rule.outputOnly = requireBoolean(ruleJson, ownerName, "outputOnly");
    return rule;
}

ContainerProcessorDef parseProcessor(const nlohmann::json& processorJson,
                                     const std::string& behaviorId,
                                     const int index,
                                     const int storageSlots) {
    if (!processorJson.is_object()) {
        throw std::runtime_error(behaviorId + ".processors[" + std::to_string(index) + "] must be an object");
    }
    const std::string ownerName = behaviorId + ".processors[" + std::to_string(index) + "]";

    ContainerProcessorDef processor;
    processor.id = requireString(processorJson, ownerName, "id");
    processor.type = requireString(processorJson, ownerName, "type");
    if (processor.type == "smelting") {
        processor.inputSlot = requireNonNegativeInteger(processorJson, ownerName, "inputSlot");
        processor.fuelSlot = requireNonNegativeInteger(processorJson, ownerName, "fuelSlot");
        processor.outputSlot = requireNonNegativeInteger(processorJson, ownerName, "outputSlot");
        if (processor.inputSlot >= storageSlots ||
            processor.fuelSlot >= storageSlots ||
            processor.outputSlot >= storageSlots) {
            throw std::runtime_error(ownerName + " references a slot outside storage");
        }
        return processor;
    }
    if (processor.type == "crafting") {
        processor.gridSize = requirePositiveInteger(processorJson, ownerName, "gridSize");
        processor.resultSlot = requireNonNegativeInteger(processorJson, ownerName, "resultSlot");
        if (processor.resultSlot >= storageSlots) {
            throw std::runtime_error(ownerName + " resultSlot references a slot outside storage");
        }
        return processor;
    }
    throw std::runtime_error(ownerName + " has unknown processor type: " + processor.type);
}

ContainerBehaviorDef parseContainerBehaviorDef(const nlohmann::json& root, const std::string& sourceName) {
    if (!root.is_object()) {
        throw std::runtime_error("Container behavior file must contain an object: " + sourceName);
    }

    ContainerBehaviorDef def;
    def.id = parseNamespacedField(root, sourceName, "id");
    def.handler = requireString(root, def.id, "handler");
    requireKnownHandler(def.handler, def.id);
    def.storage = parseStorage(requireField(root, def.id, "storage"), def.id);
    const auto comparatorSignalIt = root.find("comparatorSignal");
    if (comparatorSignalIt != root.end()) {
        if (!comparatorSignalIt->is_boolean()) {
            throw std::runtime_error(def.id + " requires boolean field: comparatorSignal");
        }
        def.comparatorSignal = comparatorSignalIt->get<bool>();
        if (def.comparatorSignal &&
            def.storage.kind != ContainerStorageKind::BlockEntity) {
            throw std::runtime_error(def.id + " comparatorSignal requires block_entity storage");
        }
    }

    const nlohmann::json& slotRules = requireField(root, def.id, "slotRules");
    if (!slotRules.is_array()) {
        throw std::runtime_error(def.id + " requires array field: slotRules");
    }
    for (int i = 0; i < static_cast<int>(slotRules.size()); ++i) {
        def.slotRules.push_back(parseSlotRule(slotRules[static_cast<std::size_t>(i)],
                                              def.id,
                                              i,
                                              def.storage.slots));
    }

    const nlohmann::json& processors = requireField(root, def.id, "processors");
    if (!processors.is_array()) {
        throw std::runtime_error(def.id + " requires array field: processors");
    }
    for (int i = 0; i < static_cast<int>(processors.size()); ++i) {
        def.processors.push_back(parseProcessor(processors[static_cast<std::size_t>(i)],
                                                def.id,
                                                i,
                                                def.storage.slots));
    }

    return def;
}

void loadContainerBehaviorFile(const std::filesystem::path& path,
                               std::unordered_map<std::string, ContainerBehaviorDef>& defs) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open container behavior config: " + path.string());
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse container behavior config " + path.string() + ": " + e.what());
    }

    ContainerBehaviorDef def = parseContainerBehaviorDef(root, path.string());
    if (defs.find(def.id) != defs.end()) {
        throw std::runtime_error("Duplicate container behavior id: " + def.id);
    }
    defs.emplace(def.id, std::move(def));
}
}

void ContainerBehaviorRegistry::init() {
    if (s_initialized) {
        return;
    }

    const std::filesystem::path configDir(kContainerBehaviorConfigDir);
    if (!std::filesystem::exists(configDir) || !std::filesystem::is_directory(configDir)) {
        throw std::runtime_error("Container behavior config directory is missing: " + configDir.string());
    }

    std::unordered_map<std::string, ContainerBehaviorDef> defs;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(configDir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        loadContainerBehaviorFile(entry.path(), defs);
    }

    if (defs.empty()) {
        throw std::runtime_error("Container behavior config directory contains no JSON definitions: " +
                                 configDir.string());
    }

    s_defs = std::move(defs);
    s_initialized = true;
}

void ContainerBehaviorRegistry::ensureInitialized() {
    if (!s_initialized) {
        init();
    }
}

const ContainerBehaviorDef& ContainerBehaviorRegistry::require(const std::string& id) {
    ensureInitialized();
    const NamespacedId namespacedId(id);
    const std::string canonicalId = namespacedId.full();
    const auto it = s_defs.find(canonicalId);
    if (it == s_defs.end()) {
        throw std::runtime_error("Container behavior is not registered: " + canonicalId);
    }
    return it->second;
}

const std::unordered_map<std::string, ContainerBehaviorDef>& ContainerBehaviorRegistry::all() {
    ensureInitialized();
    return s_defs;
}
