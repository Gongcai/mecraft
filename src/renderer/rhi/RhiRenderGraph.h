#ifndef MECRAFT_RHI_RENDER_GRAPH_H
#define MECRAFT_RHI_RENDER_GRAPH_H

#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiResources.h"
#include "renderer/rhi/RhiTypes.h"

#include <cstdint>

struct RgTextureHandle {
    uint32_t index = 0;

    [[nodiscard]] constexpr bool isValid() const {
        return index != 0;
    }
};

enum class RgPassType {
    Graphics,
    Compute,
    Copy
};

struct RgImportedTexture {
    const char* name = nullptr;
    RhiTextureHandle texture;
};

struct RgTextureDesc {
    const char* name = nullptr;
    RhiTextureDesc desc;
};

#endif // MECRAFT_RHI_RENDER_GRAPH_H
