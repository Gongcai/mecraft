#pragma once

#include <string>
#include <string_view>
#include <functional>
#include <cstdint>

class NamespacedId {
public:
    // Default construct (yields minecraft:air)
    NamespacedId();

    // Construct from "namespace:path" string
    explicit NamespacedId(std::string_view full);

    // Construct from separate namespace and path
    NamespacedId(std::string_view ns, std::string_view path);

    [[nodiscard]] std::string_view namespaceStr() const { return m_ns; }
    [[nodiscard]] std::string_view path() const { return m_path; }
    [[nodiscard]] std::string full() const;
    [[nodiscard]] uint64_t hash() const { return m_hash; }

    bool operator==(const NamespacedId& other) const;
    bool operator!=(const NamespacedId& other) const;
    bool operator<(const NamespacedId& other) const; // for ordered containers

private:
    std::string m_ns; // namespace, e.g. "minecraft"
    std::string m_path; // path, e.g. "stone"
    uint64_t m_hash; // precomputed hash

    static uint64_t computeHash(std::string_view ns, std::string_view path);
};

// Hash support for unordered_map/unordered_set
namespace std {
template <> struct hash<NamespacedId> {
    size_t operator()(const NamespacedId& id) const noexcept { return static_cast<size_t>(id.hash()); }
};
} // namespace std
