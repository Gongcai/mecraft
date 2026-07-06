#include "renderer/rhi/gl/GlRhiTextureRegistry.h"

#include "renderer/rhi/RhiHandleAllocator.h"

#include <mutex>
#include <vector>

namespace renderer::rhi::gl {
namespace {

struct TextureRecord {
    GlRhiTextureRegistration registration;
    bool active = false;
};

RhiHandleAllocator<RhiTextureHandle> g_textureHandles;
std::vector<TextureRecord> g_textures;
std::mutex g_textureMutex;

[[nodiscard]] TextureRecord* recordForHandle(const RhiTextureHandle handle) {
    if (!g_textureHandles.isAlive(handle)) {
        return nullptr;
    }

    const uint32_t slot = handle.index - 1;
    if (slot >= g_textures.size() || !g_textures[slot].active) {
        return nullptr;
    }
    return &g_textures[slot];
}

} // namespace

RhiTextureHandle registerTexture(const GlRhiTextureRegistration& registration) {
    if (registration.textureId == 0 || registration.width == 0 || registration.height == 0 ||
        registration.depthOrLayers == 0 || registration.mipLevels == 0 ||
        registration.sampleCount == 0 || registration.usage == 0) {
        return {};
    }

    std::lock_guard<std::mutex> lock(g_textureMutex);

    const RhiTextureHandle handle = g_textureHandles.allocate();
    const uint32_t slot = handle.index - 1;
    if (slot >= g_textures.size()) {
        g_textures.resize(slot + 1);
    }

    g_textures[slot].registration = registration;
    g_textures[slot].active = true;
    return handle;
}

void unregisterTexture(const RhiTextureHandle handle) {
    std::lock_guard<std::mutex> lock(g_textureMutex);

    TextureRecord* record = recordForHandle(handle);
    if (record == nullptr) {
        return;
    }

    *record = {};
    (void) g_textureHandles.release(handle);
}

void unregisterTextureAndReset(RhiTextureHandle& handle) {
    unregisterTexture(static_cast<const RhiTextureHandle>(handle));
    handle = {};
}

bool isTextureRegistered(const RhiTextureHandle handle) {
    std::lock_guard<std::mutex> lock(g_textureMutex);
    return recordForHandle(handle) != nullptr;
}

uint32_t textureId(const RhiTextureHandle handle) {
    std::lock_guard<std::mutex> lock(g_textureMutex);

    const TextureRecord* record = recordForHandle(handle);
    return record != nullptr ? record->registration.textureId : 0;
}

bool textureRegistration(const RhiTextureHandle handle,
                         GlRhiTextureRegistration& outRegistration) {
    std::lock_guard<std::mutex> lock(g_textureMutex);
    const TextureRecord* record = recordForHandle(handle);
    if (record == nullptr) {
        outRegistration = {};
        return false;
    }

    outRegistration = record->registration;
    return true;
}

} // namespace renderer::rhi::gl
