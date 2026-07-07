#include "renderer/rhi/RhiHandleAllocator.h"
#include "renderer/rhi/RhiHash.h"
#include "renderer/rhi/gl/GlRhiDevice.h"
#include "renderer/rhi/gl/GlRhiTextureRegistry.h"
#include "resource/RhiTextureResourceUtils.h"

#include <cstdint>
#include <iostream>

namespace {
bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

bool testHandleGeneration() {
    RhiHandleAllocator<RhiTextureHandle> allocator;

    const RhiTextureHandle first = allocator.allocate();
    if (!requireTrue(first.isValid(), "allocated texture handle must be valid")) {
        return false;
    }
    if (!requireTrue(allocator.isAlive(first), "allocated texture handle must be alive")) {
        return false;
    }
    if (!requireTrue(allocator.release(first), "release must accept a live texture handle")) {
        return false;
    }
    if (!requireTrue(!allocator.isAlive(first), "released texture handle must not stay alive")) {
        return false;
    }

    const RhiTextureHandle second = allocator.allocate();
    if (!requireTrue(second.index == first.index, "allocator must reuse released handle slots")) {
        return false;
    }
    if (!requireTrue(second.generation != first.generation, "reused handle generation must change")) {
        return false;
    }
    return requireTrue(!allocator.release(first), "release must reject stale texture handles");
}

bool testDescHashStability() {
    RhiTextureDesc desc;
    desc.dimension = RhiTextureDimension::Texture2DArray;
    desc.format = RhiTextureFormat::Rgba8Unorm;
    desc.width = 16;
    desc.height = 16;
    desc.depthOrLayers = 128;
    desc.mipLevels = 5;
    desc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::TransferDst);

    const uint64_t firstHash = rhiHashTextureDesc(desc);
    const uint64_t secondHash = rhiHashTextureDesc(desc);
    if (!requireTrue(firstHash == secondHash, "texture desc hash must be stable")) {
        return false;
    }

    desc.mipLevels = 1;
    return requireTrue(firstHash != rhiHashTextureDesc(desc),
                       "texture desc hash must change when semantic fields change");
}

bool testGlTextureRegistry() {
    renderer::rhi::gl::GlRhiTextureRegistration registration;
    registration.textureId = 42;
    registration.dimension = RhiTextureDimension::Texture2D;
    registration.format = RhiTextureFormat::Rgba8Unorm;
    registration.width = 16;
    registration.height = 16;
    registration.depthOrLayers = 1;
    registration.mipLevels = 1;
    registration.sampleCount = 1;
    registration.usage = rhiFlag(RhiTextureUsage::Sampled);

    const RhiTextureHandle handle = renderer::rhi::gl::registerTexture(registration);
    if (!requireTrue(handle.isValid(), "registered GL texture must return a valid RHI handle")) {
        return false;
    }
    if (!requireTrue(renderer::rhi::gl::isTextureRegistered(handle),
                     "registered GL texture handle must be alive")) {
        return false;
    }
    if (!requireTrue(renderer::rhi::gl::textureId(handle) == registration.textureId,
                     "registered GL texture handle must resolve to the native texture id")) {
        return false;
    }

    renderer::rhi::gl::GlRhiTextureRegistration resolved;
    if (!requireTrue(renderer::rhi::gl::textureRegistration(handle, resolved),
                     "registered GL texture handle must expose its copied metadata")) {
        return false;
    }
    if (!requireTrue(resolved.width == registration.width &&
                         resolved.height == registration.height &&
                         resolved.format == registration.format,
                     "registered GL texture metadata must round-trip")) {
        return false;
    }

    renderer::rhi::gl::unregisterTexture(handle);
    return requireTrue(!renderer::rhi::gl::isTextureRegistered(handle),
                       "unregistered GL texture handle must not stay alive");
}

bool testResourceTextureRegistration() {
    TextureAtlas atlas;
    const uint32_t atlasTextureId = 77;
    atlas.atlasWidth = 32;
    atlas.atlasHeight = 16;
    if (!requireTrue(resource::registerTextureAtlas(atlas, atlasTextureId),
                     "texture atlas registration must create an RHI texture handle")) {
        return false;
    }
    if (!requireTrue(renderer::rhi::gl::textureId(atlas.texture) == atlasTextureId,
                     "texture atlas handle must resolve to its native texture id")) {
        return false;
    }
    resource::unregisterTextureAtlas(atlas);
    if (!requireTrue(!atlas.texture.isValid(),
                     "texture atlas unregister must clear the RHI texture handle")) {
        return false;
    }

    TextureArray textureArray;
    const uint32_t textureArrayId = 88;
    textureArray.tileSize = 16;
    textureArray.layerCount = 4;
    if (!requireTrue(resource::registerTextureArray(textureArray, textureArrayId),
                     "texture array registration must create an RHI texture handle")) {
        return false;
    }
    if (!requireTrue(renderer::rhi::gl::textureId(textureArray.texture) == textureArrayId,
                     "texture array handle must resolve to its native texture id")) {
        return false;
    }
    resource::unregisterTextureArray(textureArray);
    return requireTrue(!textureArray.texture.isValid(),
                       "texture array unregister must clear the RHI texture handle");
}

bool testGlRhiDeviceHandles() {
    GlRhiDevice device;
    RhiDeviceDesc deviceDesc;
    deviceDesc.debugName = "rhi_core_test";
    if (!requireTrue(device.init(deviceDesc), "OpenGL RHI device must initialize")) {
        return false;
    }

    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "test-buffer";
    bufferDesc.size = 256;
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex) | rhiFlag(RhiBufferUsage::TransferDst);
    const RhiBufferHandle buffer = device.createBuffer(bufferDesc, nullptr, 0);
    if (!requireTrue(buffer.isValid(), "OpenGL RHI device must create buffer handles")) {
        return false;
    }

    RhiTextureDesc textureDesc;
    textureDesc.debugName = "test-texture";
    textureDesc.width = 4;
    textureDesc.height = 4;
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::TransferDst);
    const RhiTextureHandle texture = device.createTexture(textureDesc, nullptr);
    if (!requireTrue(texture.isValid(), "OpenGL RHI device must create texture handles")) {
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = texture;
    const RhiTextureViewHandle view = device.createTextureView(viewDesc);
    if (!requireTrue(view.isValid(), "OpenGL RHI device must create texture view handles")) {
        return false;
    }

    device.destroyTextureView(view);
    device.destroyTexture(texture);
    device.destroyBuffer(buffer);
    device.shutdown();
    return true;
}
} // namespace

int main() {
    if (!testHandleGeneration()) {
        return 1;
    }
    if (!testDescHashStability()) {
        return 1;
    }
    if (!testGlTextureRegistry()) {
        return 1;
    }
    if (!testResourceTextureRegistration()) {
        return 1;
    }
    if (!testGlRhiDeviceHandles()) {
        return 1;
    }
    return 0;
}
