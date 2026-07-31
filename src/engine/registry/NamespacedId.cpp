#include "NamespacedId.h"

#include <algorithm>

NamespacedId::NamespacedId() : m_ns("minecraft"), m_path("air") {
    m_hash = computeHash(m_ns, m_path);
}

NamespacedId::NamespacedId(std::string_view full) {
    auto colonPos = full.find(':');
    if (colonPos != std::string_view::npos) {
        m_ns = full.substr(0, colonPos);
        m_path = full.substr(colonPos + 1);
    } else {
        // No colon: default namespace is "minecraft"
        m_ns = "minecraft";
        m_path = full;
    }
    m_hash = computeHash(m_ns, m_path);
}

NamespacedId::NamespacedId(std::string_view ns, std::string_view path) : m_ns(ns), m_path(path) {
    m_hash = computeHash(m_ns, m_path);
}

std::string NamespacedId::full() const {
    return m_ns + ":" + m_path;
}

bool NamespacedId::operator==(const NamespacedId& other) const {
    if (m_hash != other.m_hash)
        return false;
    return m_ns == other.m_ns && m_path == other.m_path;
}

bool NamespacedId::operator!=(const NamespacedId& other) const {
    return !(*this == other);
}

bool NamespacedId::operator<(const NamespacedId& other) const {
    if (m_ns != other.m_ns)
        return m_ns < other.m_ns;
    return m_path < other.m_path;
}

uint64_t NamespacedId::computeHash(std::string_view ns, std::string_view path) {
    // FNV-1a style combined hash
    uint64_t h = 14695981039346656037ULL;
    for (char c : ns) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        h *= 1099511628211ULL;
    }
    h ^= static_cast<uint64_t>(':');
    h *= 1099511628211ULL;
    for (char c : path) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        h *= 1099511628211ULL;
    }
    return h;
}
