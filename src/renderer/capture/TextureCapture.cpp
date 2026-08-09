#include "renderer/capture/TextureCapture.h"

#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiCommandListPool.h"
#include "renderer/rhi/RhiDevice.h"

#include <glm/gtc/packing.hpp>
#include <stb/stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <utility>

namespace renderer::capture {
namespace {

constexpr uint32_t kBytesPerPixel = 4u;
constexpr uint32_t kHalfBytesPerPixel = 8u;

[[nodiscard]] bool supportedFormat(const RhiTextureFormat format) {
    return format == RhiTextureFormat::Rgba8Unorm || format == RhiTextureFormat::Rgba8Srgb ||
           format == RhiTextureFormat::Bgra8Unorm || format == RhiTextureFormat::Bgra8Srgb;
}

[[nodiscard]] bool bgraFormat(const RhiTextureFormat format) {
    return format == RhiTextureFormat::Bgra8Unorm || format == RhiTextureFormat::Bgra8Srgb;
}

[[nodiscard]] bool halfFormat(const RhiTextureFormat format) {
    return format == RhiTextureFormat::Rgba16Float;
}

[[nodiscard]] TextureCaptureResult failure(const TextureCaptureError error, std::string detail) {
    TextureCaptureResult result;
    result.error = error;
    result.detail = std::move(detail);
    return result;
}

} // namespace

bool TextureCaptureResult::succeeded() const {
    return error == TextureCaptureError::None;
}

TextureCaptureError normalizeTextureCapturePixels(const uint8_t* source, const size_t sourceSize,
                                                  const uint32_t bytesPerRow, const uint32_t width,
                                                  const uint32_t height, const RhiTextureFormat format,
                                                  const TextureCaptureOrigin origin, std::vector<uint8_t>& rgba) {
    rgba.clear();
    if (source == nullptr || width == 0u || height == 0u || !supportedFormat(format)) {
        return !supportedFormat(format) ? TextureCaptureError::UnsupportedFormat : TextureCaptureError::InvalidRequest;
    }
    const uint64_t tightRowBytes = static_cast<uint64_t>(width) * kBytesPerPixel;
    if (tightRowBytes > bytesPerRow) {
        return TextureCaptureError::InvalidRequest;
    }
    const uint64_t requiredSourceSize = static_cast<uint64_t>(height - 1u) * bytesPerRow + tightRowBytes;
    const uint64_t outputSize = tightRowBytes * height;
    if (requiredSourceSize > sourceSize || outputSize > std::numeric_limits<size_t>::max()) {
        return TextureCaptureError::SizeOverflow;
    }

    rgba.resize(static_cast<size_t>(outputSize));
    const bool swizzleBgra = bgraFormat(format);
    for (uint32_t destinationY = 0u; destinationY < height; ++destinationY) {
        const uint32_t sourceY = origin == TextureCaptureOrigin::TopLeft ? destinationY : height - destinationY - 1u;
        const uint8_t* sourceRow = source + static_cast<uint64_t>(sourceY) * bytesPerRow;
        uint8_t* destinationRow = rgba.data() + static_cast<uint64_t>(destinationY) * tightRowBytes;
        for (uint32_t x = 0u; x < width; ++x) {
            const uint8_t* sourcePixel = sourceRow + x * kBytesPerPixel;
            uint8_t* destinationPixel = destinationRow + x * kBytesPerPixel;
            destinationPixel[0] = sourcePixel[swizzleBgra ? 2u : 0u];
            destinationPixel[1] = sourcePixel[1];
            destinationPixel[2] = sourcePixel[swizzleBgra ? 0u : 2u];
            destinationPixel[3] = sourcePixel[3];
        }
    }
    return TextureCaptureError::None;
}

TextureCaptureError normalizeTextureCaptureHalfPixels(const uint8_t* source, const size_t sourceSize,
                                                      const uint32_t bytesPerRow, const uint32_t width,
                                                      const uint32_t height, const RhiTextureFormat format,
                                                      const TextureCaptureOrigin origin,
                                                      std::vector<uint16_t>& rgba16f) {
    rgba16f.clear();
    if (source == nullptr || width == 0u || height == 0u || !halfFormat(format)) {
        return !halfFormat(format) ? TextureCaptureError::UnsupportedFormat : TextureCaptureError::InvalidRequest;
    }
    const uint64_t tightRowBytes = static_cast<uint64_t>(width) * kHalfBytesPerPixel;
    if (tightRowBytes > bytesPerRow) {
        return TextureCaptureError::InvalidRequest;
    }
    const uint64_t requiredSourceSize = static_cast<uint64_t>(height - 1u) * bytesPerRow + tightRowBytes;
    const uint64_t outputSize = tightRowBytes * height;
    if (requiredSourceSize > sourceSize || outputSize > std::numeric_limits<size_t>::max()) {
        return TextureCaptureError::SizeOverflow;
    }

    rgba16f.resize(static_cast<size_t>(width) * height * 4u);
    for (uint32_t destinationY = 0u; destinationY < height; ++destinationY) {
        const uint32_t sourceY = origin == TextureCaptureOrigin::TopLeft ? destinationY : height - destinationY - 1u;
        const uint8_t* sourceRow = source + static_cast<uint64_t>(sourceY) * bytesPerRow;
        uint16_t* destinationRow = rgba16f.data() + static_cast<size_t>(destinationY) * width * 4u;
        std::memcpy(destinationRow, sourceRow, static_cast<size_t>(tightRowBytes));
    }
    return TextureCaptureError::None;
}

TextureCaptureError scaleTextureCaptureHalfPixels(std::vector<uint16_t>& rgba16f, const float scale) {
    if (rgba16f.empty() || rgba16f.size() % 4u != 0u || !std::isfinite(scale) || scale <= 0.0f) {
        return TextureCaptureError::InvalidRequest;
    }
    for (size_t pixel = 0u; pixel < rgba16f.size() / 4u; ++pixel) {
        for (uint32_t channel = 0u; channel < 3u; ++channel) {
            const float source = glm::unpackHalf1x16(rgba16f[pixel * 4u + channel]);
            const float scaled = source * scale;
            if (!std::isfinite(source) || source < 0.0f || !std::isfinite(scaled) || scaled > 65504.0f) {
                return TextureCaptureError::RadianceScalingFailed;
            }
            rgba16f[pixel * 4u + channel] = glm::packHalf1x16(scaled);
        }
    }
    return TextureCaptureError::None;
}

namespace {

void writeUint32(std::ofstream& output, const uint32_t value) {
    const std::array<uint8_t, 4u> bytes{static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8u),
                                        static_cast<uint8_t>(value >> 16u), static_cast<uint8_t>(value >> 24u)};
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void writeUint64(std::ofstream& output, const uint64_t value) {
    const std::array<uint8_t, 8u> bytes{static_cast<uint8_t>(value),        static_cast<uint8_t>(value >> 8u),
                                        static_cast<uint8_t>(value >> 16u), static_cast<uint8_t>(value >> 24u),
                                        static_cast<uint8_t>(value >> 32u), static_cast<uint8_t>(value >> 40u),
                                        static_cast<uint8_t>(value >> 48u), static_cast<uint8_t>(value >> 56u)};
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void writeFloat(std::ofstream& output, const float value) {
    static_assert(sizeof(float) == sizeof(uint32_t));
    uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    writeUint32(output, bits);
}

void writeString(std::ofstream& output, const std::string_view value) {
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    output.put('\0');
}

void writeAttribute(std::ofstream& output, const std::string_view name, const std::string_view type,
                    const uint32_t size) {
    writeString(output, name);
    writeString(output, type);
    writeUint32(output, size);
}

uint64_t exrHeaderSize() {
    constexpr uint64_t kMagicAndVersionBytes = 8u;
    constexpr uint64_t kChannelsPayloadBytes = 55u;
    constexpr uint64_t kChannelsAttributeBytes = 9u + 7u + 4u + kChannelsPayloadBytes;
    constexpr uint64_t kCompressionAttributeBytes = 12u + 12u + 4u + 1u;
    constexpr uint64_t kDataWindowAttributeBytes = 11u + 6u + 4u + 16u;
    constexpr uint64_t kDisplayWindowAttributeBytes = 14u + 6u + 4u + 16u;
    constexpr uint64_t kLineOrderAttributeBytes = 10u + 10u + 4u + 1u;
    constexpr uint64_t kPixelAspectAttributeBytes = 17u + 6u + 4u + 4u;
    constexpr uint64_t kScreenCenterAttributeBytes = 19u + 4u + 4u + 8u;
    constexpr uint64_t kScreenWidthAttributeBytes = 18u + 6u + 4u + 4u;
    constexpr uint64_t kHeaderTerminatorBytes = 1u;
    return kMagicAndVersionBytes + kChannelsAttributeBytes + kCompressionAttributeBytes + kDataWindowAttributeBytes +
           kDisplayWindowAttributeBytes + kLineOrderAttributeBytes + kPixelAspectAttributeBytes +
           kScreenCenterAttributeBytes + kScreenWidthAttributeBytes + kHeaderTerminatorBytes;
}

void writeExrHeader(std::ofstream& output, const uint32_t width, const uint32_t height) {
    writeUint32(output, 20000630u);
    writeUint32(output, 2u);

    writeAttribute(output, "channels", "chlist", 55u);
    for (const std::string_view channel : {std::string_view{"B"}, std::string_view{"G"}, std::string_view{"R"}}) {
        writeString(output, channel);
        writeUint32(output, 1u);
        output.put('\0');
        output.put('\0');
        output.put('\0');
        output.put('\0');
        writeUint32(output, 1u);
        writeUint32(output, 1u);
    }
    output.put('\0');

    writeAttribute(output, "compression", "compression", 1u);
    output.put('\0');
    const auto writeBox = [&output, width, height]() {
        writeUint32(output, 0u);
        writeUint32(output, 0u);
        writeUint32(output, width - 1u);
        writeUint32(output, height - 1u);
    };
    writeAttribute(output, "dataWindow", "box2i", 16u);
    writeBox();
    writeAttribute(output, "displayWindow", "box2i", 16u);
    writeBox();
    writeAttribute(output, "lineOrder", "lineOrder", 1u);
    output.put('\0');
    writeAttribute(output, "pixelAspectRatio", "float", 4u);
    writeFloat(output, 1.0f);
    writeAttribute(output, "screenWindowCenter", "v2f", 8u);
    writeFloat(output, 0.0f);
    writeFloat(output, 0.0f);
    writeAttribute(output, "screenWindowWidth", "float", 4u);
    writeFloat(output, 1.0f);
    output.put('\0');
}

class ExrByteReader final {
public:
    explicit ExrByteReader(const std::vector<uint8_t>& bytes) : m_bytes(bytes) {}

    [[nodiscard]] bool readByte(uint8_t& value) {
        if (m_offset >= m_bytes.size()) {
            return false;
        }
        value = m_bytes[m_offset++];
        return true;
    }

    [[nodiscard]] bool readUint16(uint16_t& value) {
        uint8_t bytes[2u]{};
        if (!readByte(bytes[0u]) || !readByte(bytes[1u])) {
            return false;
        }
        value = static_cast<uint16_t>(bytes[0u]) | (static_cast<uint16_t>(bytes[1u]) << 8u);
        return true;
    }

    [[nodiscard]] bool readUint32(uint32_t& value) {
        uint8_t bytes[4u]{};
        for (uint32_t index = 0u; index < 4u; ++index) {
            if (!readByte(bytes[index])) {
                return false;
            }
        }
        value = static_cast<uint32_t>(bytes[0u]) | (static_cast<uint32_t>(bytes[1u]) << 8u) |
                (static_cast<uint32_t>(bytes[2u]) << 16u) | (static_cast<uint32_t>(bytes[3u]) << 24u);
        return true;
    }

    [[nodiscard]] bool readUint64(uint64_t& value) {
        value = 0u;
        for (uint32_t index = 0u; index < 8u; ++index) {
            uint8_t byte = 0u;
            if (!readByte(byte)) {
                return false;
            }
            value |= static_cast<uint64_t>(byte) << (index * 8u);
        }
        return true;
    }

    [[nodiscard]] bool readCString(std::string& value) {
        value.clear();
        while (m_offset < m_bytes.size()) {
            const uint8_t byte = m_bytes[m_offset++];
            if (byte == 0u) {
                return true;
            }
            value.push_back(static_cast<char>(byte));
        }
        return false;
    }

    [[nodiscard]] size_t offset() const { return m_offset; }

private:
    const std::vector<uint8_t>& m_bytes;
    size_t m_offset = 0u;
};

[[nodiscard]] bool readExrAttribute(ExrByteReader& reader, const char* expectedName, const char* expectedType,
                                    const uint32_t expectedSize) {
    std::string name;
    std::string type;
    uint32_t size = 0u;
    return reader.readCString(name) && name == expectedName && reader.readCString(type) && type == expectedType &&
           reader.readUint32(size) && size == expectedSize;
}

[[nodiscard]] bool readExrChannel(ExrByteReader& reader, const char* expectedName) {
    std::string name;
    uint32_t pixelType = 0u;
    uint8_t reserved = 0u;
    uint32_t xSampling = 0u;
    uint32_t ySampling = 0u;
    return reader.readCString(name) && name == expectedName && reader.readUint32(pixelType) && pixelType == 1u &&
           reader.readByte(reserved) && reserved == 0u && reader.readByte(reserved) && reserved == 0u &&
           reader.readByte(reserved) && reserved == 0u && reader.readByte(reserved) && reserved == 0u &&
           reader.readUint32(xSampling) && xSampling == 1u && reader.readUint32(ySampling) && ySampling == 1u;
}

[[nodiscard]] bool readExrBox(ExrByteReader& reader, uint32_t& width, uint32_t& height) {
    uint32_t minX = 0u;
    uint32_t minY = 0u;
    uint32_t maxX = 0u;
    uint32_t maxY = 0u;
    if (!reader.readUint32(minX) || !reader.readUint32(minY) || !reader.readUint32(maxX) || !reader.readUint32(maxY) ||
        minX != 0u || minY != 0u || maxX == std::numeric_limits<uint32_t>::max() ||
        maxY == std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    width = maxX + 1u;
    height = maxY + 1u;
    return width != 0u && height != 0u;
}

} // namespace

TextureCaptureResult writeLinearExr(const std::filesystem::path& outputPath, const uint32_t width,
                                    const uint32_t height, const std::vector<uint16_t>& rgba16f) {
    const uint64_t pixelCount = static_cast<uint64_t>(width) * height;
    if (outputPath.empty() || outputPath.extension() != ".exr" || width == 0u || height == 0u ||
        pixelCount > std::numeric_limits<size_t>::max() / 4u || rgba16f.size() != pixelCount * 4u) {
        return failure(TextureCaptureError::InvalidRequest,
                       "lowercase .exr path and exact RGBA16F pixels are required");
    }
    const uint64_t scanlineBytes = static_cast<uint64_t>(width) * 3u * sizeof(uint16_t);
    const uint64_t offsetTableBytes = static_cast<uint64_t>(height) * sizeof(uint64_t);
    if (scanlineBytes > std::numeric_limits<uint32_t>::max() ||
        exrHeaderSize() > std::numeric_limits<uint64_t>::max() - offsetTableBytes ||
        scanlineBytes > std::numeric_limits<uint64_t>::max() - sizeof(uint32_t) * 2u) {
        return failure(TextureCaptureError::SizeOverflow, "EXR scanline or offset table size overflowed");
    }
    const std::filesystem::path parent = outputPath.parent_path();
    if (!parent.empty()) {
        std::error_code directoryError;
        std::filesystem::create_directories(parent, directoryError);
        if (directoryError) {
            return failure(TextureCaptureError::OutputDirectoryFailed, directoryError.message());
        }
    }

    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        return failure(TextureCaptureError::ExrWriteFailed, outputPath.generic_u8string());
    }
    writeExrHeader(output, width, height);
    const uint64_t firstChunkOffset = exrHeaderSize() + offsetTableBytes;
    const uint64_t chunkSize = sizeof(uint32_t) * 2u + scanlineBytes;
    for (uint32_t y = 0u; y < height; ++y) {
        writeUint64(output, firstChunkOffset + static_cast<uint64_t>(y) * chunkSize);
    }
    for (uint32_t y = 0u; y < height; ++y) {
        writeUint32(output, y);
        writeUint32(output, static_cast<uint32_t>(scanlineBytes));
        const uint16_t* row = rgba16f.data() + static_cast<size_t>(y) * width * 4u;
        for (const uint32_t channel : {2u, 1u, 0u}) {
            for (uint32_t x = 0u; x < width; ++x) {
                const uint16_t half = row[x * 4u + channel];
                output.put(static_cast<char>(half & 0xffu));
                output.put(static_cast<char>(half >> 8u));
            }
        }
    }
    output.flush();
    if (!output) {
        return failure(TextureCaptureError::ExrWriteFailed, outputPath.generic_u8string());
    }
    return {};
}

TextureCaptureResult readLinearExr(const std::filesystem::path& inputPath, LinearExrImage& image) {
    image = {};
    if (inputPath.empty() || inputPath.extension() != ".exr") {
        return failure(TextureCaptureError::InvalidRequest, "a lowercase .exr input path is required");
    }
    std::ifstream input(inputPath, std::ios::binary | std::ios::ate);
    if (!input) {
        return failure(TextureCaptureError::ExrReadFailed, inputPath.generic_u8string());
    }
    const std::streamoff size = input.tellg();
    if (size <= 0 || static_cast<uint64_t>(size) > std::numeric_limits<size_t>::max()) {
        return failure(TextureCaptureError::ExrReadFailed, "EXR size is invalid");
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (input.gcount() != size) {
        return failure(TextureCaptureError::ExrReadFailed, "EXR file could not be read completely");
    }

    ExrByteReader reader(bytes);
    uint32_t magic = 0u;
    uint32_t version = 0u;
    uint8_t headerTerminator = 0u;
    uint8_t compression = 0u;
    uint8_t lineOrder = 0u;
    uint32_t dataWidth = 0u;
    uint32_t dataHeight = 0u;
    uint32_t displayWidth = 0u;
    uint32_t displayHeight = 0u;
    uint32_t floatBits = 0u;
    if (!reader.readUint32(magic) || magic != 20000630u || !reader.readUint32(version) || version != 2u ||
        !readExrAttribute(reader, "channels", "chlist", 55u) || !readExrChannel(reader, "B") ||
        !readExrChannel(reader, "G") || !readExrChannel(reader, "R") || !reader.readByte(headerTerminator) ||
        headerTerminator != 0u || !readExrAttribute(reader, "compression", "compression", 1u) ||
        !reader.readByte(compression) || compression != 0u || !readExrAttribute(reader, "dataWindow", "box2i", 16u) ||
        !readExrBox(reader, dataWidth, dataHeight) || !readExrAttribute(reader, "displayWindow", "box2i", 16u) ||
        !readExrBox(reader, displayWidth, displayHeight) || dataWidth != displayWidth || dataHeight != displayHeight ||
        !readExrAttribute(reader, "lineOrder", "lineOrder", 1u) || !reader.readByte(lineOrder) || lineOrder != 0u ||
        !readExrAttribute(reader, "pixelAspectRatio", "float", 4u) || !reader.readUint32(floatBits) ||
        floatBits != 0x3f800000u || !readExrAttribute(reader, "screenWindowCenter", "v2f", 8u) ||
        !reader.readUint32(floatBits) || floatBits != 0u || !reader.readUint32(floatBits) || floatBits != 0u ||
        !readExrAttribute(reader, "screenWindowWidth", "float", 4u) || !reader.readUint32(floatBits) ||
        floatBits != 0x3f800000u || !reader.readByte(headerTerminator) || headerTerminator != 0u) {
        return failure(TextureCaptureError::ExrReadFailed, "EXR header does not match the linear RGB capture contract");
    }

    const uint64_t pixelCount = static_cast<uint64_t>(dataWidth) * dataHeight;
    const uint64_t scanlineBytes = static_cast<uint64_t>(dataWidth) * 3u * sizeof(uint16_t);
    const uint64_t offsetTableBytes = static_cast<uint64_t>(dataHeight) * sizeof(uint64_t);
    if (pixelCount > std::numeric_limits<size_t>::max() / 3u || scanlineBytes > std::numeric_limits<uint32_t>::max() ||
        reader.offset() > std::numeric_limits<uint64_t>::max() - offsetTableBytes) {
        return failure(TextureCaptureError::ExrReadFailed, "EXR dimensions exceed the linear capture contract");
    }
    const uint64_t firstChunkOffset = static_cast<uint64_t>(reader.offset()) + offsetTableBytes;
    const uint64_t chunkBytes = sizeof(uint32_t) * 2u + scanlineBytes;
    if (chunkBytes == 0u || dataHeight > (std::numeric_limits<uint64_t>::max() - firstChunkOffset) / chunkBytes ||
        firstChunkOffset + static_cast<uint64_t>(dataHeight) * chunkBytes != bytes.size()) {
        return failure(TextureCaptureError::ExrReadFailed, "EXR chunk table size is invalid");
    }
    for (uint32_t y = 0u; y < dataHeight; ++y) {
        uint64_t offset = 0u;
        const uint64_t expectedOffset = firstChunkOffset + static_cast<uint64_t>(y) * chunkBytes;
        if (!reader.readUint64(offset) || offset != expectedOffset) {
            return failure(TextureCaptureError::ExrReadFailed,
                           "EXR scanline offset is invalid:" + std::to_string(offset) +
                               "!=" + std::to_string(expectedOffset));
        }
    }

    image.width = dataWidth;
    image.height = dataHeight;
    image.rgb16f.resize(static_cast<size_t>(pixelCount) * 3u);
    for (uint32_t y = 0u; y < dataHeight; ++y) {
        uint32_t scanlineY = 0u;
        uint32_t storedBytes = 0u;
        if (!reader.readUint32(scanlineY) || scanlineY != y || !reader.readUint32(storedBytes) ||
            storedBytes != scanlineBytes) {
            image = {};
            return failure(TextureCaptureError::ExrReadFailed, "EXR scanline header is invalid");
        }
        for (const uint32_t channel : {2u, 1u, 0u}) {
            for (uint32_t x = 0u; x < dataWidth; ++x) {
                uint16_t half = 0u;
                if (!reader.readUint16(half)) {
                    image = {};
                    return failure(TextureCaptureError::ExrReadFailed, "EXR scanline payload is truncated");
                }
                image.rgb16f[(static_cast<size_t>(y) * dataWidth + x) * 3u + channel] = half;
            }
        }
    }
    if (reader.offset() != bytes.size()) {
        image = {};
        return failure(TextureCaptureError::ExrReadFailed, "EXR contains trailing data");
    }
    return {};
}

TextureCaptureResult captureTextureToPng(RhiDevice& rhiDevice, RhiCommandListPool& commandListPool,
                                         const TextureCaptureRequest& request) {
    if (!request.sourceTexture.isValid() || request.width == 0u || request.height == 0u ||
        request.sourceState == RhiResourceState::Undefined || request.outputPath.empty() ||
        request.outputPath.extension() != ".png") {
        return failure(TextureCaptureError::InvalidRequest,
                       "source texture, state, extent, and lowercase .png path are required");
    }
    if (!supportedFormat(request.sourceFormat)) {
        return failure(TextureCaptureError::UnsupportedFormat, "only 8-bit RGBA and BGRA textures can be captured");
    }

    const uint64_t tightRowBytes = static_cast<uint64_t>(request.width) * kBytesPerPixel;
    if (request.width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        request.height > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        tightRowBytes > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        return failure(TextureCaptureError::SizeOverflow, "PNG dimensions or stride exceed the writer contract");
    }
    const uint64_t alignment = std::max<uint64_t>(1u, rhiDevice.capabilities().textureBufferCopyRowPitchAlignment);
    if (tightRowBytes > std::numeric_limits<uint64_t>::max() - (alignment - 1u)) {
        return failure(TextureCaptureError::SizeOverflow, "aligned capture row size overflowed");
    }
    const uint64_t bytesPerRow64 = ((tightRowBytes + alignment - 1u) / alignment) * alignment;
    if (bytesPerRow64 > std::numeric_limits<uint32_t>::max() ||
        request.height > std::numeric_limits<uint64_t>::max() / bytesPerRow64) {
        return failure(TextureCaptureError::SizeOverflow, "capture readback size overflowed");
    }
    const uint32_t bytesPerRow = static_cast<uint32_t>(bytesPerRow64);
    const uint64_t readbackSize = bytesPerRow64 * request.height;
    if (readbackSize > std::numeric_limits<size_t>::max()) {
        return failure(TextureCaptureError::SizeOverflow, "capture readback exceeds the host address space");
    }

    RhiBufferDesc readbackDesc;
    readbackDesc.debugName = "ValidationCapture.Readback";
    readbackDesc.size = readbackSize;
    readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::MapRead);
    readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    readbackDesc.initialState = RhiResourceState::TransferDst;
    readbackDesc.memoryCategory = RhiMemoryCategory::Readback;
    const RhiBufferHandle readback = rhiDevice.createBuffer(readbackDesc, nullptr, 0u);
    if (!readback.isValid()) {
        return failure(TextureCaptureError::ReadbackAllocationFailed,
                       "failed to create the GPU-to-CPU readback buffer");
    }

    RhiCommandList* commandList = commandListPool.acquire(RhiCommandListType::Graphics);
    if (commandList == nullptr) {
        rhiDevice.destroyBuffer(readback);
        return failure(TextureCaptureError::CommandListAcquireFailed, "failed to acquire a graphics command list");
    }
    if (!commandList->begin({"ValidationCapture.Commands", RhiCommandListType::Graphics})) {
        rhiDevice.destroyBuffer(readback);
        return failure(TextureCaptureError::CommandListBeginFailed, "failed to begin the capture command list");
    }

    commandList->textureBarrier({request.sourceTexture, request.sourceState, RhiResourceState::TransferSrc});
    RhiTextureBufferCopy copy;
    copy.srcTexture = request.sourceTexture;
    copy.dstBuffer = readback;
    copy.bytesPerRow = bytesPerRow;
    copy.rowsPerImage = request.height;
    copy.width = request.width;
    copy.height = request.height;
    commandList->copyTextureToBuffer(copy);
    commandList->bufferBarrier({readback, RhiResourceState::TransferDst, RhiResourceState::HostRead});
    commandList->textureBarrier({request.sourceTexture, RhiResourceState::TransferSrc, request.sourceState});
    if (!commandList->end()) {
        rhiDevice.destroyBuffer(readback);
        return failure(TextureCaptureError::CommandListEndFailed, "capture command validation failed");
    }

    RhiCommandList* submitted[] = {commandList};
    RhiSubmissionToken token;
    if (!rhiDevice.submit({"ValidationCapture.Submit", submitted, 1u, RhiQueueType::Graphics}, &token)) {
        rhiDevice.destroyBuffer(readback);
        return failure(TextureCaptureError::SubmissionFailed, "capture copy submission was rejected");
    }
    if (!rhiDevice.waitForSubmission(token)) {
        rhiDevice.destroyBuffer(readback);
        return failure(TextureCaptureError::SubmissionWaitFailed, "capture copy submission did not complete");
    }

    const auto* mapped = static_cast<const uint8_t*>(rhiDevice.mapBuffer(readback, 0u, readbackSize));
    if (mapped == nullptr) {
        rhiDevice.destroyBuffer(readback);
        return failure(TextureCaptureError::BufferMapFailed, "capture readback buffer mapping failed");
    }
    std::vector<uint8_t> rgba;
    const TextureCaptureError normalizationError =
        normalizeTextureCapturePixels(mapped, static_cast<size_t>(readbackSize), bytesPerRow, request.width,
                                      request.height, request.sourceFormat, request.origin, rgba);
    rhiDevice.unmapBuffer(readback);
    rhiDevice.destroyBuffer(readback);
    if (normalizationError != TextureCaptureError::None) {
        return failure(TextureCaptureError::PixelNormalizationFailed, textureCaptureErrorStableId(normalizationError));
    }

    const std::filesystem::path parent = request.outputPath.parent_path();
    if (!parent.empty()) {
        std::error_code directoryError;
        std::filesystem::create_directories(parent, directoryError);
        if (directoryError) {
            return failure(TextureCaptureError::OutputDirectoryFailed, directoryError.message());
        }
    }
    const std::string outputPath = request.outputPath.generic_u8string();
    const int writeResult =
        stbi_write_png(outputPath.c_str(), static_cast<int>(request.width), static_cast<int>(request.height), 4,
                       rgba.data(), static_cast<int>(request.width * kBytesPerPixel));
    if (writeResult == 0) {
        return failure(TextureCaptureError::PngWriteFailed, outputPath);
    }
    return {};
}

TextureCaptureResult captureTextureToExr(RhiDevice& rhiDevice, RhiCommandListPool& commandListPool,
                                         const TextureCaptureRequest& request) {
    if (!request.sourceTexture.isValid() || request.width == 0u || request.height == 0u ||
        request.sourceState == RhiResourceState::Undefined || request.sourceFormat != RhiTextureFormat::Rgba16Float ||
        !std::isfinite(request.linearRgbScale) || request.linearRgbScale <= 0.0f || request.outputPath.empty() ||
        request.outputPath.extension() != ".exr") {
        return failure(TextureCaptureError::InvalidRequest,
                       "source texture, state, RGBA16F extent, and lowercase .exr path are required");
    }
    const uint64_t tightRowBytes = static_cast<uint64_t>(request.width) * kHalfBytesPerPixel;
    if (request.width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        request.height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        return failure(TextureCaptureError::SizeOverflow, "EXR dimensions exceed the writer contract");
    }
    const uint64_t alignment = std::max<uint64_t>(1u, rhiDevice.capabilities().textureBufferCopyRowPitchAlignment);
    if (tightRowBytes > std::numeric_limits<uint64_t>::max() - (alignment - 1u)) {
        return failure(TextureCaptureError::SizeOverflow, "aligned EXR capture row size overflowed");
    }
    const uint64_t bytesPerRow64 = ((tightRowBytes + alignment - 1u) / alignment) * alignment;
    if (bytesPerRow64 > std::numeric_limits<uint32_t>::max() ||
        request.height > std::numeric_limits<uint64_t>::max() / bytesPerRow64) {
        return failure(TextureCaptureError::SizeOverflow, "EXR capture readback size overflowed");
    }
    const uint32_t bytesPerRow = static_cast<uint32_t>(bytesPerRow64);
    const uint64_t readbackSize = bytesPerRow64 * request.height;
    if (readbackSize > std::numeric_limits<size_t>::max()) {
        return failure(TextureCaptureError::SizeOverflow, "EXR capture readback exceeds the host address space");
    }

    RhiBufferDesc readbackDesc;
    readbackDesc.debugName = "ValidationCapture.ExrReadback";
    readbackDesc.size = readbackSize;
    readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::MapRead);
    readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    readbackDesc.initialState = RhiResourceState::TransferDst;
    readbackDesc.memoryCategory = RhiMemoryCategory::Readback;
    const RhiBufferHandle readback = rhiDevice.createBuffer(readbackDesc, nullptr, 0u);
    if (!readback.isValid()) {
        return failure(TextureCaptureError::ReadbackAllocationFailed,
                       "failed to create the GPU-to-CPU EXR readback buffer");
    }
    RhiCommandList* commandList = commandListPool.acquire(RhiCommandListType::Graphics);
    if (commandList == nullptr ||
        !commandList->begin({"ValidationCapture.ExrCommands", RhiCommandListType::Graphics})) {
        rhiDevice.destroyBuffer(readback);
        return failure(TextureCaptureError::CommandListBeginFailed, "failed to begin the EXR capture command list");
    }
    commandList->textureBarrier({request.sourceTexture, request.sourceState, RhiResourceState::TransferSrc});
    RhiTextureBufferCopy copy;
    copy.srcTexture = request.sourceTexture;
    copy.dstBuffer = readback;
    copy.bytesPerRow = bytesPerRow;
    copy.rowsPerImage = request.height;
    copy.width = request.width;
    copy.height = request.height;
    commandList->copyTextureToBuffer(copy);
    commandList->bufferBarrier({readback, RhiResourceState::TransferDst, RhiResourceState::HostRead});
    commandList->textureBarrier({request.sourceTexture, RhiResourceState::TransferSrc, request.sourceState});
    if (!commandList->end()) {
        rhiDevice.destroyBuffer(readback);
        return failure(TextureCaptureError::CommandListEndFailed, "EXR capture command validation failed");
    }
    RhiCommandList* submitted[] = {commandList};
    RhiSubmissionToken token;
    if (!rhiDevice.submit({"ValidationCapture.ExrSubmit", submitted, 1u, RhiQueueType::Graphics}, &token) ||
        !rhiDevice.waitForSubmission(token)) {
        rhiDevice.destroyBuffer(readback);
        return failure(TextureCaptureError::SubmissionFailed, "EXR capture submission did not complete");
    }
    const auto* mapped = static_cast<const uint8_t*>(rhiDevice.mapBuffer(readback, 0u, readbackSize));
    if (mapped == nullptr) {
        rhiDevice.destroyBuffer(readback);
        return failure(TextureCaptureError::BufferMapFailed, "EXR capture readback mapping failed");
    }
    std::vector<uint16_t> rgba16f;
    const TextureCaptureError normalizationError =
        normalizeTextureCaptureHalfPixels(mapped, static_cast<size_t>(readbackSize), bytesPerRow, request.width,
                                          request.height, request.sourceFormat, request.origin, rgba16f);
    rhiDevice.unmapBuffer(readback);
    rhiDevice.destroyBuffer(readback);
    if (normalizationError != TextureCaptureError::None) {
        return failure(TextureCaptureError::PixelNormalizationFailed, textureCaptureErrorStableId(normalizationError));
    }
    const TextureCaptureError scalingError = scaleTextureCaptureHalfPixels(rgba16f, request.linearRgbScale);
    if (scalingError != TextureCaptureError::None) {
        return failure(TextureCaptureError::RadianceScalingFailed, textureCaptureErrorStableId(scalingError));
    }
    return writeLinearExr(request.outputPath, request.width, request.height, rgba16f);
}

const char* textureCaptureErrorStableId(const TextureCaptureError error) {
    switch (error) {
    case TextureCaptureError::None: return "None";
    case TextureCaptureError::InvalidRequest: return "InvalidRequest";
    case TextureCaptureError::UnsupportedFormat: return "UnsupportedFormat";
    case TextureCaptureError::SizeOverflow: return "SizeOverflow";
    case TextureCaptureError::ReadbackAllocationFailed: return "ReadbackAllocationFailed";
    case TextureCaptureError::CommandListAcquireFailed: return "CommandListAcquireFailed";
    case TextureCaptureError::CommandListBeginFailed: return "CommandListBeginFailed";
    case TextureCaptureError::CommandListEndFailed: return "CommandListEndFailed";
    case TextureCaptureError::SubmissionFailed: return "SubmissionFailed";
    case TextureCaptureError::SubmissionWaitFailed: return "SubmissionWaitFailed";
    case TextureCaptureError::BufferMapFailed: return "BufferMapFailed";
    case TextureCaptureError::PixelNormalizationFailed: return "PixelNormalizationFailed";
    case TextureCaptureError::RadianceScalingFailed: return "RadianceScalingFailed";
    case TextureCaptureError::OutputDirectoryFailed: return "OutputDirectoryFailed";
    case TextureCaptureError::PngWriteFailed: return "PngWriteFailed";
    case TextureCaptureError::ExrWriteFailed: return "ExrWriteFailed";
    case TextureCaptureError::ExrReadFailed: return "ExrReadFailed";
    }
    std::abort();
}

} // namespace renderer::capture
