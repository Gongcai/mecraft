#ifndef MECRAFT_SAVE_FORMAT_H
#define MECRAFT_SAVE_FORMAT_H

// MCHK binary chunk file format definitions.
// MCHK = "Mecraft Chunk" — a self-contained binary format for a single chunk column.
//
// File layout:
//   MchkHeader (24 bytes, fixed)
//   payload[]  (payloadSize bytes)
//
// Payload layout:
//   uint8_t   encoding (MCHK_ENCODING_PALLETIZED = 1)
//   uint16_t  subChunkMask (bit N = 1 if subchunk N is present)
//   For each present subchunk:
//     uint8_t scy
//     LayerPayload blockLayer
//     LayerPayload fluidLayer
//
// LayerPayload:
//   varuint   paletteCount (0 = all-air, skip packed data)
//   For each palette entry:
//     varuint runtimeId
//   uint8_t   bitsPerEntry
//   varuint   packedDataSize (bytes)
//   uint8_t[packedDataSize] packedIndices

#include <cstdint>
#include <cstddef>

namespace save {

// MCHK file magic: 'M' 'C' 'H' 'K' = 0x4D43484B
constexpr uint32_t MCHK_MAGIC = 0x4D43484Bu;
constexpr uint16_t MCHK_VERSION = 2;

// Encoding identifiers
constexpr uint8_t MCHK_ENCODING_PALLETIZED = 1;

// MCHK file header — 24 bytes, tightly packed.
#pragma pack(push, 1)
struct MchkHeader {
    uint32_t magic;        // Must be MCHK_MAGIC
    uint16_t version;      // Must be MCHK_VERSION
    uint16_t flags;        // Reserved, set to 0
    int32_t  chunkX;
    int32_t  chunkZ;
    uint32_t payloadSize;  // Byte count of payload following this header
    uint32_t payloadCrc32; // CRC-32 of the payload bytes
};
#pragma pack(pop)

static_assert(sizeof(MchkHeader) == 24, "MchkHeader must be exactly 24 bytes");

// --- CRC-32 (IEEE 802.3 polynomial) ---

namespace detail {

inline uint32_t crc32Lookup(uint32_t crc, const uint8_t* data, size_t length) {
    static constexpr uint32_t TABLE[256] = {
        0x00000000u, 0x77073096u, 0xEE0E612Cu, 0x990951BAu,
        0x076DC419u, 0x706AF48Fu, 0xE963A535u, 0x9E6495A3u,
        0x0EDB8832u, 0x79DCB8A4u, 0xE0D5E91Bu, 0x97D2D988u,
        0x09B64C2Bu, 0x7EB17CBDu, 0xE7B82D09u, 0x90BF1D9Fu,
        0x1DB71064u, 0x6AB020F2u, 0xF3B97148u, 0x84BE41DEu,
        0x1ADAD47Du, 0x6DDDE4EBu, 0xF4D4B551u, 0x83D385C7u,
        0x136C9856u, 0x646BA8C0u, 0xFD62F97Au, 0x8A65C9ECu,
        0x14015C4Fu, 0x63066CD9u, 0xFA0F3D63u, 0x8D080DF5u,
        0x3B6E20C8u, 0x4C69105Eu, 0xD56041E4u, 0xA2677172u,
        0x3C03E4D1u, 0x4B04D447u, 0xD20D85FDu, 0xA50AB56Bu,
        0x35B5A8FAu, 0x42B2986Cu, 0xDBBBB9D6u, 0xACBCB940u,
        0x32D86CE3u, 0x45DF5C75u, 0xDCD60DCFu, 0xABD13D59u,
        0x26D930ACu, 0x51DE003Au, 0xC8D75180u, 0xBFD06116u,
        0x21B4F6B5u, 0x56B3C423u, 0xCFBA9599u, 0xB8BDA50Fu,
        0x2802B89Eu, 0x5F058808u, 0xC60CD9B2u, 0xB10BE924u,
        0x2F6F7C87u, 0x58684C11u, 0xC1611DABu, 0xB6662D3Du,
        0x76DC4190u, 0x01DB7106u, 0x98D220BCu, 0xEFD5102Au,
        0x71B18589u, 0x06B6B51Fu, 0x9FBFE4A5u, 0xE8B8D433u,
        0x7807C9A2u, 0x0F00F934u, 0x9609A88Eu, 0xE10E9818u,
        0x7F6A0DBBu, 0x086D3D2Du, 0x91646C97u, 0xE6635C01u,
        0x6B6B51F4u, 0x1C6C6162u, 0x856530D8u, 0xF262004Eu,
        0x6C0695EDu, 0x1B01A57Bu, 0x8208F4C1u, 0xF50FC457u,
        0x65B0D9C6u, 0x12B7E950u, 0x8BBEB8EAu, 0xFCB9887Cu,
        0x62DD1DDFu, 0x15DA2D49u, 0x8CD37CF3u, 0xFBD44C65u,
        0x4DB26158u, 0x3AB551CEu, 0xA3BC0074u, 0xD4BB30E2u,
        0x4ADFA541u, 0x3DD895D7u, 0xA4D1C46Du, 0xD3D6F4FBu,
        0x4369E96Au, 0x346ED9FCu, 0xAD678846u, 0xDA60B8D0u,
        0x44042D73u, 0x33031DE5u, 0xAA0A4C5Fu, 0xDD0D7AC9u,
        0x5005713Cu, 0x270241AAu, 0xBE0B1010u, 0xC90C2086u,
        0x5768B525u, 0x206F85B3u, 0xB966D409u, 0xCE61E49Fu,
        0x5EDEF90Eu, 0x29D9C998u, 0xB0D09822u, 0xC7D7A8B4u,
        0x59B33D17u, 0x2EB40D81u, 0xB7BD5C3Bu, 0xC0BA6CADu,
        0xEDB88320u, 0x9ABFB3B6u, 0x03B6E20Cu, 0x74B1D29Au,
        0xEAD54739u, 0x9DD277AFu, 0x04DB2615u, 0x73DC1683u,
        0xE3630B12u, 0x94643B84u, 0x0D6D6A3Eu, 0x7A6A5AA8u,
        0xE40ECF0Bu, 0x9309FF9Du, 0x0A00AE27u, 0x7D079EB1u,
        0xF00F9344u, 0x8708A3D2u, 0x1E01F268u, 0x6906C2FEu,
        0xF762575Du, 0x806567CBu, 0x196C3671u, 0x6E6B06E7u,
        0xFED41B76u, 0x89D32BE0u, 0x10DA7A5Au, 0x67DD4ACCu,
        0xF9B9DF6Fu, 0x8EBEEFF9u, 0x17B7BE43u, 0x60B08ED5u,
        0xD6D6A3E8u, 0xA1D1937Eu, 0x38D8C2C4u, 0x4FDFF252u,
        0xD1BB67F1u, 0xA6BC5767u, 0x3FB506DDu, 0x48B2364Bu,
        0xD80D2BDAu, 0xAF0A1B4Cu, 0x36034AF6u, 0x41047A60u,
        0xDF60EFC3u, 0xA8670455u, 0x31684DD1u, 0x466B3D09u,
        0xCF692166u, 0xB86E11F0u, 0x21674044u, 0x566070D2u,
        0xC8047571u, 0xBF0345E7u, 0x260A145Du, 0x510D24CBu,
        0xC1B2395Au, 0xB6B509CCu, 0x2FBC5876u, 0x58BB68E0u,
        0xC6DD1743u, 0xB1DA07D5u, 0x28D3566Fu, 0x5FD466F9u,
        0xE3B88B0Cu, 0x94BFBB9Au, 0x0DB6EA20u, 0x7AB1DAB6u,
        0xE4D54F15u, 0x93D27F83u, 0x0ADB2E39u, 0x7DDC1EAFu,
        0xE760F01Eu, 0x9067C088u, 0x096E9132u, 0x7E69A1A4u,
        0xE60D5607u, 0x910A6691u, 0x0803372Bu, 0x7F0407BDu,
        0xF2081A48u, 0x850F2ADEu, 0x1C067B64u, 0x6B014BF2u,
        0xF56F5E51u, 0x82686EC7u, 0x1B613F7Du, 0x6C660FEBu,
        0xF6D9107Au, 0x81DE20ECu, 0x18D77156u, 0x6FD041C0u,
        0xF1B4D463u, 0x86B3E4F5u, 0x1FBAB54Fu, 0x68BD85D9u,
    };
    for (size_t i = 0; i < length; ++i) {
        crc = TABLE[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

inline uint32_t crc32(const uint8_t* data, size_t length) {
    return crc32Lookup(0xFFFFFFFFu, data, length) ^ 0xFFFFFFFFu;
}

} // namespace detail

} // namespace save

#endif // MECRAFT_SAVE_FORMAT_H
