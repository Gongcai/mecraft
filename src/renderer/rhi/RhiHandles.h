#ifndef MECRAFT_RHI_HANDLES_H
#define MECRAFT_RHI_HANDLES_H

#include <cstdint>

struct RhiBufferHandle {
    uint32_t index = 0;
    uint32_t generation = 0;

    [[nodiscard]] constexpr bool isValid() const {
        return index != 0 && generation != 0;
    }
};

struct RhiTextureHandle {
    uint32_t index = 0;
    uint32_t generation = 0;

    [[nodiscard]] constexpr bool isValid() const {
        return index != 0 && generation != 0;
    }
};

struct RhiTextureViewHandle {
    uint32_t index = 0;
    uint32_t generation = 0;

    [[nodiscard]] constexpr bool isValid() const {
        return index != 0 && generation != 0;
    }
};

struct RhiSamplerHandle {
    uint32_t index = 0;
    uint32_t generation = 0;

    [[nodiscard]] constexpr bool isValid() const {
        return index != 0 && generation != 0;
    }
};

struct RhiShaderHandle {
    uint32_t index = 0;
    uint32_t generation = 0;

    [[nodiscard]] constexpr bool isValid() const {
        return index != 0 && generation != 0;
    }
};

struct RhiPipelineLayoutHandle {
    uint32_t index = 0;
    uint32_t generation = 0;

    [[nodiscard]] constexpr bool isValid() const {
        return index != 0 && generation != 0;
    }
};

struct RhiPipelineHandle {
    uint32_t index = 0;
    uint32_t generation = 0;

    [[nodiscard]] constexpr bool isValid() const {
        return index != 0 && generation != 0;
    }
};

struct RhiBindGroupLayoutHandle {
    uint32_t index = 0;
    uint32_t generation = 0;

    [[nodiscard]] constexpr bool isValid() const {
        return index != 0 && generation != 0;
    }
};

struct RhiBindGroupHandle {
    uint32_t index = 0;
    uint32_t generation = 0;

    [[nodiscard]] constexpr bool isValid() const {
        return index != 0 && generation != 0;
    }
};

struct RhiQueryPoolHandle {
    uint32_t index = 0;
    uint32_t generation = 0;

    [[nodiscard]] constexpr bool isValid() const {
        return index != 0 && generation != 0;
    }
};

#endif // MECRAFT_RHI_HANDLES_H
