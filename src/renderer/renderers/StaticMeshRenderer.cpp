#include "StaticMeshRenderer.h"
#include "GltfPunctualLightLoader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "cgltf/cgltf.h"
#include "mikktspace/mikktspace.h"
#include "stb/stb_image.h"

#include "../contracts/GpuMaterialContract.h"
#include "../contracts/SceneIdentityContract.h"
#include "../core/FrameContext.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiCommandListPool.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../../resource/ResourceMgr.h"
#include "../../world/IWorldView.h"

namespace {

template <typename Handle> [[nodiscard]] bool sameHandle(const Handle lhs, const Handle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

struct StaticMeshVertex {
    float position[3];
    float normal[3];
    float tangent[4];
    float uv[2];
};

struct StaticMeshGBufferPushConstants {
    glm::mat4 model{1.0f};
    glm::mat4 previousModel{1.0f};
};

struct StaticMeshTransparentPushConstants {
    glm::mat4 model{1.0f};
    glm::vec4 reflectionParams{1.0f, 0.0f, 0.0f, 0.0f};
};

struct StaticMeshFrameParams {
    glm::vec4 voxelLight{1.0f, 0.0f, 0.0f, 1.0f};
    glm::mat4 viewProjection{1.0f};
    glm::mat4 previousViewProjection{1.0f};
    glm::vec4 cameraPosition{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 sunDirection{0.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 sunColor{1.0f};
    glm::vec4 ambientColor{0.2f, 0.2f, 0.2f, 0.0f};
    glm::vec4 fogColor{0.0f};
    glm::vec4 fogParams{0.0f};
    glm::uvec4 clusterGrid{0u};
    glm::vec4 clusterDepth{0.0f};
    glm::uvec4 clusterRenderExtent{0u};
};

struct StaticMeshMaterialParams {
    renderer::contracts::GpuMaterial material;
    glm::uvec4 identity{0u};
};

struct StaticMeshPreviewPushConstants {
    glm::mat4 viewProj{1.0f};
    glm::mat4 model{1.0f};
};

struct StaticMeshProbeCaptureFrameParams {
    glm::mat4 viewProjection{1.0f};
    glm::vec4 probePosition{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 sunDirection{0.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 sunColor{1.0f};
    glm::vec4 ambientColor{0.2f, 0.2f, 0.2f, 0.0f};
    glm::uvec4 lightCount{0u};
};

struct TextureCacheKey {
    const cgltf_image* image = nullptr;
    bool srgb = false;

    bool operator==(const TextureCacheKey& other) const { return image == other.image && srgb == other.srgb; }
};

struct TextureCacheKeyHash {
    std::size_t operator()(const TextureCacheKey& key) const {
        const std::size_t pointerHash = std::hash<const cgltf_image*>{}(key.image);
        return pointerHash ^
               (static_cast<std::size_t>(key.srgb) + 0x9e3779b97f4a7c15ull + (pointerHash << 6u) + (pointerHash >> 2u));
    }
};

struct DecodedImage {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
};

struct TangentGenerationContext {
    std::vector<StaticMeshVertex>* vertices = nullptr;
};

static_assert(sizeof(StaticMeshGBufferPushConstants) == 128u,
              "Static mesh G-buffer push constants must fit the Vulkan minimum limit");
static_assert(sizeof(StaticMeshTransparentPushConstants) == 80u,
              "Static mesh transparent push constants must match the shader block");
static_assert(sizeof(StaticMeshFrameParams) == 288u, "Static mesh frame parameters must match the std140 shader block");
static_assert(sizeof(StaticMeshMaterialParams) == 272u,
              "Static mesh material parameters must match the std140 shader block");
static_assert(sizeof(StaticMeshPreviewPushConstants) == 128u,
              "Static mesh preview push constants must fit the Vulkan minimum limit");
static_assert(sizeof(StaticMeshProbeCaptureFrameParams) == 144u,
              "Static mesh probe-capture parameters must match the std140 block");

[[nodiscard]] bool finiteVector(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] uint32_t mipLevelCount(const uint32_t width, const uint32_t height) {
    uint32_t dimension = std::max(width, height);
    uint32_t levels = 1u;
    while (dimension > 1u) {
        dimension >>= 1u;
        ++levels;
    }
    return levels;
}

[[nodiscard]] std::optional<RhiAddressMode> convertWrapMode(const cgltf_wrap_mode mode) {
    switch (mode) {
    case cgltf_wrap_mode_clamp_to_edge: return RhiAddressMode::ClampToEdge;
    case cgltf_wrap_mode_mirrored_repeat: return RhiAddressMode::MirroredRepeat;
    case cgltf_wrap_mode_repeat: return RhiAddressMode::Repeat;
    }
    return std::nullopt;
}

[[nodiscard]] bool buildSamplerDesc(const cgltf_sampler* sampler, const RhiCapabilities& capabilities,
                                    RhiSamplerDesc& desc) {
    desc.minFilter = RhiFilter::Linear;
    desc.magFilter = RhiFilter::Linear;
    desc.mipmapMode = RhiMipmapMode::Linear;
    desc.addressU = RhiAddressMode::Repeat;
    desc.addressV = RhiAddressMode::Repeat;
    desc.addressW = RhiAddressMode::Repeat;
    if (sampler != nullptr) {
        switch (sampler->mag_filter) {
        case cgltf_filter_type_undefined:
        case cgltf_filter_type_linear: desc.magFilter = RhiFilter::Linear; break;
        case cgltf_filter_type_nearest: desc.magFilter = RhiFilter::Nearest; break;
        default: return false;
        }
        switch (sampler->min_filter) {
        case cgltf_filter_type_undefined:
        case cgltf_filter_type_linear_mipmap_linear:
            desc.minFilter = RhiFilter::Linear;
            desc.mipmapMode = RhiMipmapMode::Linear;
            break;
        case cgltf_filter_type_nearest:
        case cgltf_filter_type_nearest_mipmap_nearest:
            desc.minFilter = RhiFilter::Nearest;
            desc.mipmapMode = RhiMipmapMode::Nearest;
            break;
        case cgltf_filter_type_linear:
        case cgltf_filter_type_linear_mipmap_nearest:
            desc.minFilter = RhiFilter::Linear;
            desc.mipmapMode = RhiMipmapMode::Nearest;
            break;
        case cgltf_filter_type_nearest_mipmap_linear:
            desc.minFilter = RhiFilter::Nearest;
            desc.mipmapMode = RhiMipmapMode::Linear;
            break;
        default: return false;
        }
        const std::optional<RhiAddressMode> addressU = convertWrapMode(sampler->wrap_s);
        const std::optional<RhiAddressMode> addressV = convertWrapMode(sampler->wrap_t);
        if (!addressU.has_value() || !addressV.has_value()) {
            return false;
        }
        desc.addressU = *addressU;
        desc.addressV = *addressV;
    }
    desc.maxAnisotropy = capabilities.samplerAnisotropy && desc.minFilter == RhiFilter::Linear
                             ? capabilities.maxSamplerAnisotropy
                             : 1.0f;
    return true;
}

[[nodiscard]] const char* cgltfResultName(const cgltf_result result) {
    switch (result) {
    case cgltf_result_success: return "success";
    case cgltf_result_data_too_short: return "data too short";
    case cgltf_result_unknown_format: return "unknown format";
    case cgltf_result_invalid_json: return "invalid JSON";
    case cgltf_result_invalid_gltf: return "invalid glTF";
    case cgltf_result_invalid_options: return "invalid options";
    case cgltf_result_file_not_found: return "file not found";
    case cgltf_result_io_error: return "I/O error";
    case cgltf_result_out_of_memory: return "out of memory";
    case cgltf_result_legacy_gltf: return "legacy glTF";
    case cgltf_result_max_enum: break;
    }
    return "invalid cgltf result";
}

void appendContractIssue(std::string& error, const std::string& issue) {
    error += error.empty() ? "" : "; ";
    error += issue;
}

[[nodiscard]] bool supportedMaterialExtension(const std::string_view extension) {
    return extension == "KHR_materials_pbrSpecularGlossiness" || extension == "KHR_materials_emissive_strength" ||
           extension == "KHR_materials_specular" || extension == "KHR_materials_ior" ||
           extension == "KHR_materials_clearcoat" || extension == "KHR_materials_transmission" ||
           extension == "KHR_materials_volume" || extension == "KHR_lights_punctual";
}

template <typename Predicate>
void appendMaterialExtensionIssue(const cgltf_data& data, std::string& error, const char* extension,
                                  Predicate predicate) {
    std::string materials;
    std::size_t count = 0u;
    for (cgltf_size index = 0u; index < data.materials_count; ++index) {
        const cgltf_material& material = data.materials[index];
        if (!predicate(material)) {
            continue;
        }
        ++count;
        materials += materials.empty() ? "" : ", ";
        materials += material.name != nullptr ? material.name : ("#" + std::to_string(index));
    }
    if (count != 0u) {
        appendContractIssue(error, std::string(extension) + " on " + std::to_string(count) + " material(s) [" +
                                       materials + "]");
    }
}

[[nodiscard]] std::string staticAssetContractError(const cgltf_data& data) {
    std::string error;
    std::string unsupportedExtensions;
    for (cgltf_size index = 0u; index < data.extensions_required_count; ++index) {
        const std::string_view extension(data.extensions_required[index]);
        if (!supportedMaterialExtension(extension)) {
            unsupportedExtensions += unsupportedExtensions.empty() ? "" : ", ";
            unsupportedExtensions += extension;
        }
    }
    if (!unsupportedExtensions.empty()) {
        appendContractIssue(error, "unsupported required extensions [" + unsupportedExtensions + "]");
    }

    std::size_t conflictingWorkflows = 0u;
    std::size_t incompatibleLegacyExtensions = 0u;
    std::size_t volumeWithoutTransmission = 0u;
    std::size_t transmissionAlphaConflicts = 0u;
    std::size_t unlit = 0u;
    for (cgltf_size index = 0u; index < data.materials_count; ++index) {
        const cgltf_material& material = data.materials[index];
        const bool usesMetallicRoughness = material.has_pbr_metallic_roughness != 0;
        const bool usesSpecularGlossiness = material.has_pbr_specular_glossiness != 0;
        conflictingWorkflows += usesMetallicRoughness && usesSpecularGlossiness ? 1u : 0u;
        incompatibleLegacyExtensions +=
            usesSpecularGlossiness && (material.has_specular || material.has_ior || material.has_clearcoat ||
                                       material.has_transmission || material.has_volume)
                ? 1u
                : 0u;
        volumeWithoutTransmission += material.has_volume && !material.has_transmission ? 1u : 0u;
        transmissionAlphaConflicts +=
            material.has_transmission && material.alpha_mode != cgltf_alpha_mode_opaque ? 1u : 0u;
        unlit += material.unlit != 0 ? 1u : 0u;
    }
    if (conflictingWorkflows != 0u) {
        appendContractIssue(error, std::to_string(conflictingWorkflows) +
                                       " material(s) define both metallic-roughness and "
                                       "KHR_materials_pbrSpecularGlossiness");
    }
    if (incompatibleLegacyExtensions != 0u) {
        appendContractIssue(error, std::to_string(incompatibleLegacyExtensions) +
                                       " material(s) combine legacy specular-glossiness with "
                                       "metallic-roughness material extensions");
    }
    if (volumeWithoutTransmission != 0u) {
        appendContractIssue(error, std::to_string(volumeWithoutTransmission) +
                                       " material(s) use KHR_materials_volume without "
                                       "KHR_materials_transmission");
    }
    if (transmissionAlphaConflicts != 0u) {
        appendContractIssue(error, std::to_string(transmissionAlphaConflicts) +
                                       " transmission material(s) use non-opaque alpha coverage");
    }
    if (unlit != 0u) {
        appendContractIssue(error, std::to_string(unlit) + " material(s) use KHR_materials_unlit");
    }
    appendMaterialExtensionIssue(data, error, "unsupported KHR_materials_sheen",
                                 [](const cgltf_material& material) { return material.has_sheen != 0; });
    appendMaterialExtensionIssue(data, error, "unsupported KHR_materials_iridescence",
                                 [](const cgltf_material& material) { return material.has_iridescence != 0; });
    appendMaterialExtensionIssue(data, error, "unsupported KHR_materials_diffuse_transmission",
                                 [](const cgltf_material& material) { return material.has_diffuse_transmission != 0; });
    appendMaterialExtensionIssue(data, error, "unsupported KHR_materials_anisotropy",
                                 [](const cgltf_material& material) { return material.has_anisotropy != 0; });
    appendMaterialExtensionIssue(data, error, "unsupported KHR_materials_dispersion",
                                 [](const cgltf_material& material) { return material.has_dispersion != 0; });
    if (error.empty()) {
        return {};
    }
    return "glTF asset is incompatible with the static mesh asset contract: " + error +
           ". Supported material extensions are KHR_materials_specular, "
           "KHR_materials_ior, KHR_materials_clearcoat, "
           "KHR_materials_transmission, KHR_materials_volume, "
           "KHR_materials_emissive_strength, and "
           "KHR_materials_pbrSpecularGlossiness; supported scene extensions "
           "include KHR_lights_punctual";
}

[[nodiscard]] bool readBinaryFile(const std::filesystem::path& path, std::vector<unsigned char>& bytes) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        return false;
    }
    const std::streamoff length = stream.tellg();
    if (length <= 0 || static_cast<uint64_t>(length) > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    bytes.resize(static_cast<std::size_t>(length));
    stream.seekg(0, std::ios::beg);
    return static_cast<bool>(stream.read(reinterpret_cast<char*>(bytes.data()), length));
}

[[nodiscard]] bool decodeImage(const cgltf_image& image, const std::filesystem::path& modelDirectory,
                               DecodedImage& decoded, std::string& error) {
    const unsigned char* encodedData = nullptr;
    std::size_t encodedSize = 0u;
    std::vector<unsigned char> externalBytes;
    if (image.buffer_view != nullptr) {
        encodedData = cgltf_buffer_view_data(image.buffer_view);
        encodedSize = image.buffer_view->size;
    } else if (image.uri != nullptr) {
        std::string decodedUri(image.uri);
        cgltf_decode_uri(decodedUri.data());
        if (decodedUri.rfind("data:", 0u) == 0u) {
            error = "embedded image data URIs are outside the supported glTF asset contract";
            return false;
        }
        if (!readBinaryFile(modelDirectory / decodedUri, externalBytes)) {
            error = "failed to read external image: " + decodedUri;
            return false;
        }
        encodedData = externalBytes.data();
        encodedSize = externalBytes.size();
    } else {
        error = "glTF image has neither a buffer view nor a URI";
        return false;
    }
    if (encodedData == nullptr || encodedSize == 0u ||
        encodedSize > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        error = "glTF image payload has an invalid size";
        return false;
    }

    int channels = 0;
    unsigned char* pixels = stbi_load_from_memory(encodedData, static_cast<int>(encodedSize), &decoded.width,
                                                  &decoded.height, &channels, 4);
    if (pixels == nullptr || decoded.width <= 0 || decoded.height <= 0) {
        error = std::string("failed to decode glTF image: ") + stbi_failure_reason();
        if (pixels != nullptr) {
            stbi_image_free(pixels);
        }
        return false;
    }
    const std::size_t pixelBytes =
        static_cast<std::size_t>(decoded.width) * static_cast<std::size_t>(decoded.height) * 4u;
    decoded.pixels.assign(pixels, pixels + pixelBytes);
    stbi_image_free(pixels);
    return true;
}

[[nodiscard]] const cgltf_accessor* requireAttribute(const cgltf_primitive& primitive, const cgltf_attribute_type type,
                                                     const cgltf_int index) {
    return cgltf_find_accessor(&primitive, type, index);
}

[[nodiscard]] bool readAccessorVec(const cgltf_accessor& accessor, const cgltf_size index, float* output,
                                   const cgltf_size componentCount) {
    return !accessor.is_sparse && cgltf_accessor_read_float(&accessor, index, output, componentCount) != 0;
}

int tangentFaceCount(const SMikkTSpaceContext* context) {
    const auto* data = static_cast<const TangentGenerationContext*>(context->m_pUserData);
    return static_cast<int>(data->vertices->size() / 3u);
}

int tangentVerticesPerFace(const SMikkTSpaceContext*, int) {
    return 3;
}

const StaticMeshVertex& tangentVertex(const SMikkTSpaceContext* context, const int face, const int vertex) {
    const auto* data = static_cast<const TangentGenerationContext*>(context->m_pUserData);
    return (*data->vertices)[static_cast<std::size_t>(face * 3 + vertex)];
}

void tangentPosition(const SMikkTSpaceContext* context, float output[], const int face, const int vertex) {
    std::memcpy(output, tangentVertex(context, face, vertex).position, sizeof(float) * 3u);
}

void tangentNormal(const SMikkTSpaceContext* context, float output[], const int face, const int vertex) {
    std::memcpy(output, tangentVertex(context, face, vertex).normal, sizeof(float) * 3u);
}

void tangentUv(const SMikkTSpaceContext* context, float output[], const int face, const int vertex) {
    std::memcpy(output, tangentVertex(context, face, vertex).uv, sizeof(float) * 2u);
}

void setTangent(const SMikkTSpaceContext* context, const float tangent[], const float sign, const int face,
                const int vertex) {
    auto* data = static_cast<TangentGenerationContext*>(context->m_pUserData);
    StaticMeshVertex& output = (*data->vertices)[static_cast<std::size_t>(face * 3 + vertex)];
    output.tangent[0] = tangent[0];
    output.tangent[1] = tangent[1];
    output.tangent[2] = tangent[2];
    output.tangent[3] = sign;
}

[[nodiscard]] bool generateTangents(std::vector<StaticMeshVertex>& vertices, std::string& error) {
    if (vertices.empty() || vertices.size() % 3u != 0u ||
        vertices.size() / 3u > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    SMikkTSpaceInterface interface{};
    interface.m_getNumFaces = tangentFaceCount;
    interface.m_getNumVerticesOfFace = tangentVerticesPerFace;
    interface.m_getPosition = tangentPosition;
    interface.m_getNormal = tangentNormal;
    interface.m_getTexCoord = tangentUv;
    interface.m_setTSpaceBasic = setTangent;
    TangentGenerationContext userData{&vertices};
    SMikkTSpaceContext context{&interface, &userData};
    if (genTangSpaceDefault(&context) == 0) {
        error = "MikkTSpace tangent generation failed";
        return false;
    }
    const auto invalid = std::find_if(vertices.begin(), vertices.end(), [](const StaticMeshVertex& vertex) {
        const glm::vec3 tangent(vertex.tangent[0], vertex.tangent[1], vertex.tangent[2]);
        return !finiteVector(tangent) || glm::dot(tangent, tangent) <= 0.25f || !std::isfinite(vertex.tangent[3]);
    });
    if (invalid != vertices.end()) {
        const std::size_t vertexIndex = static_cast<std::size_t>(invalid - vertices.begin());
        const std::size_t triangleFirst = (vertexIndex / 3u) * 3u;
        const StaticMeshVertex& v0 = vertices[triangleFirst + 0u];
        const StaticMeshVertex& v1 = vertices[triangleFirst + 1u];
        const StaticMeshVertex& v2 = vertices[triangleFirst + 2u];
        const glm::vec3 p0 = glm::make_vec3(v0.position);
        const glm::vec3 p1 = glm::make_vec3(v1.position);
        const glm::vec3 p2 = glm::make_vec3(v2.position);
        const glm::vec2 uv0 = glm::make_vec2(v0.uv);
        const glm::vec2 uv1 = glm::make_vec2(v1.uv);
        const glm::vec2 uv2 = glm::make_vec2(v2.uv);
        const float geometricArea2 = glm::length(glm::cross(p1 - p0, p2 - p0));
        const glm::vec3 edge0 = p1 - p0;
        const glm::vec3 edge1 = p2 - p1;
        const glm::vec3 edge2 = p0 - p2;
        const float longestEdgeSq = std::max({glm::dot(edge0, edge0), glm::dot(edge1, edge1), glm::dot(edge2, edge2)});
        const glm::vec2 uvEdge1 = uv1 - uv0;
        const glm::vec2 uvEdge2 = uv2 - uv0;
        const float uvDeterminant = uvEdge1.x * uvEdge2.y - uvEdge1.y * uvEdge2.x;
        const glm::vec2 uvEdge12 = uv2 - uv1;
        const float longestUvEdgeSq =
            std::max({glm::dot(uvEdge1, uvEdge1), glm::dot(uvEdge2, uvEdge2), glm::dot(uvEdge12, uvEdge12)});
        const float uvQuality = longestUvEdgeSq > 0.0f ? std::abs(uvDeterminant) / longestUvEdgeSq : 0.0f;
        error = "MikkTSpace produced an invalid tangent at triangle " + std::to_string(vertexIndex / 3u) +
                " (geometric area2=" + std::to_string(geometricArea2) +
                ", longest edge squared=" + std::to_string(longestEdgeSq) +
                ", UV determinant=" + std::to_string(uvDeterminant) + ", UV quality=" + std::to_string(uvQuality) +
                ", tangent=" + std::to_string(invalid->tangent[0]) + "," + std::to_string(invalid->tangent[1]) + "," +
                std::to_string(invalid->tangent[2]) + ")";
        return false;
    }
    return true;
}

[[nodiscard]] std::size_t removeNonRasterizableTriangles(std::vector<StaticMeshVertex>& vertices) {
    std::vector<StaticMeshVertex> filtered;
    filtered.reserve(vertices.size());
    std::size_t removed = 0u;
    for (std::size_t first = 0u; first < vertices.size(); first += 3u) {
        const StaticMeshVertex& v0 = vertices[first + 0u];
        const StaticMeshVertex& v1 = vertices[first + 1u];
        const StaticMeshVertex& v2 = vertices[first + 2u];
        const glm::vec3 p0 = glm::make_vec3(v0.position);
        const glm::vec3 p1 = glm::make_vec3(v1.position);
        const glm::vec3 p2 = glm::make_vec3(v2.position);
        const glm::vec3 edge0 = p1 - p0;
        const glm::vec3 edge1 = p2 - p1;
        const glm::vec3 edge2 = p0 - p2;
        const float longestEdgeSq = std::max({glm::dot(edge0, edge0), glm::dot(edge1, edge1), glm::dot(edge2, edge2)});
        const float geometricArea2 = glm::length(glm::cross(edge0, p2 - p0));
        const float geometricQuality = longestEdgeSq > 0.0f ? geometricArea2 / longestEdgeSq : 0.0f;

        const glm::vec2 uv0 = glm::make_vec2(v0.uv);
        const glm::vec2 uv1 = glm::make_vec2(v1.uv);
        const glm::vec2 uv2 = glm::make_vec2(v2.uv);
        const glm::vec2 uvEdge0 = uv1 - uv0;
        const glm::vec2 uvEdge1 = uv2 - uv1;
        const glm::vec2 uvEdge2 = uv0 - uv2;
        const float longestUvEdgeSq =
            std::max({glm::dot(uvEdge0, uvEdge0), glm::dot(uvEdge1, uvEdge1), glm::dot(uvEdge2, uvEdge2)});
        const float uvArea2 = std::abs(uvEdge0.x * (uv2.y - uv0.y) - uvEdge0.y * (uv2.x - uv0.x));
        const float uvQuality = longestUvEdgeSq > 0.0f ? uvArea2 / longestUvEdgeSq : 0.0f;

        // A triangle that is nearly collinear in both object and UV space has
        // no stable raster footprint or tangent basis. Removing it preserves
        // every surface for which normal mapping is mathematically defined.
        if (geometricQuality <= 1e-4f && uvQuality <= 1e-4f) {
            ++removed;
            continue;
        }
        filtered.insert(filtered.end(), vertices.begin() + first, vertices.begin() + first + 3u);
    }
    vertices.swap(filtered);
    return removed;
}

[[nodiscard]] glm::vec2 decodePackedLight(const uint8_t packed) {
    return {static_cast<float>((packed >> 4u) & 0x0fu) / 15.0f, static_cast<float>(packed & 0x0fu) / 15.0f};
}

} // namespace

bool StaticMeshRenderer::init(ResourceMgr& resourceMgr, const std::string& modelPath) {
    shutdown();
    m_rhiDevice = &resourceMgr.rhiDevice();
    if (!m_staticBlasCache.init(m_rhiDevice)) {
        const std::string error = m_staticBlasCache.lastError();
        shutdown();
        m_lastError = error;
        return false;
    }
    const std::optional<renderer::contracts::StableObjectId> objectId =
        renderer::contracts::allocateStableSceneId<renderer::contracts::StableObjectIdTag>();
    if (!objectId.has_value()) {
        setError("stable static mesh object identity space is exhausted");
        m_rhiDevice = nullptr;
        return false;
    }
    m_objectId = *objectId;
    if (!createPipelineResources() || !loadAsset(modelPath, resourceMgr) ||
        !buildStaticBlas(resourceMgr.commandListPool())) {
        const std::string error = m_lastError;
        shutdown();
        m_lastError = error;
        return false;
    }
    return true;
}

void StaticMeshRenderer::setError(std::string message) {
    m_lastError = std::move(message);
}

bool StaticMeshRenderer::createPipelineResources() {
    const auto gbufferVertexSource = renderer::rhi::loadShaderSource("assets/shaders/static_mesh_gbuffer_rhi.vert");
    const auto gbufferFragmentSource = renderer::rhi::loadShaderSource("assets/shaders/static_mesh_gbuffer_rhi.frag");
    const auto shadowVertexSource = renderer::rhi::loadShaderSource("assets/shaders/static_mesh_shadow_rhi.vert");
    const auto shadowFragmentSource = renderer::rhi::loadShaderSource("assets/shaders/static_mesh_shadow_rhi.frag");
    const auto previewVertexSource = renderer::rhi::loadShaderSource("assets/shaders/static_mesh_preview_rhi.vert");
    const auto previewFragmentSource = renderer::rhi::loadShaderSource("assets/shaders/static_mesh_preview_rhi.frag");
    const auto probeCaptureVertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/static_mesh_probe_capture_rhi.vert");
    const auto probeCaptureFragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/static_mesh_probe_capture_rhi.frag");
    const auto transparentVertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/static_mesh_transparent_rhi.vert");
    renderer::rhi::RhiShaderSourceOptions transparentOptions;
    if (m_rhiDevice->backend() == RhiBackend::Vulkan) {
        transparentOptions.preprocessorDefinitions.emplace_back("MECRAFT_CLUSTERED_LIGHTING");
    }
    const auto transparentFragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/static_mesh_transparent_rhi.frag", transparentOptions);
    if (!gbufferVertexSource || !gbufferFragmentSource || !shadowVertexSource || !shadowFragmentSource ||
        !previewVertexSource || !previewFragmentSource || !probeCaptureVertexSource || !probeCaptureFragmentSource ||
        !transparentVertexSource || !transparentFragmentSource) {
        setError("failed to load static mesh shaders");
        return false;
    }
    const auto createShader = [this](const char* debugName, const RhiShaderStage stage, const std::string& source) {
        RhiShaderDesc desc;
        desc.debugName = debugName;
        desc.stage = stage;
        desc.source = source.c_str();
        desc.sourceSize = source.size();
        return m_rhiDevice->createShader(desc);
    };
    m_gbufferVertexShader = createShader("StaticMesh.GBuffer.Vertex", RhiShaderStage::Vertex, *gbufferVertexSource);
    m_gbufferFragmentShader =
        createShader("StaticMesh.GBuffer.Fragment", RhiShaderStage::Fragment, *gbufferFragmentSource);
    m_shadowVertexShader = createShader("StaticMesh.Shadow.Vertex", RhiShaderStage::Vertex, *shadowVertexSource);
    m_shadowFragmentShader =
        createShader("StaticMesh.Shadow.Fragment", RhiShaderStage::Fragment, *shadowFragmentSource);
    m_previewVertexShader = createShader("StaticMesh.Preview.Vertex", RhiShaderStage::Vertex, *previewVertexSource);
    m_previewFragmentShader =
        createShader("StaticMesh.Preview.Fragment", RhiShaderStage::Fragment, *previewFragmentSource);
    m_transparentVertexShader =
        createShader("StaticMesh.Transparent.Vertex", RhiShaderStage::Vertex, *transparentVertexSource);
    m_transparentFragmentShader =
        createShader("StaticMesh.Transparent.Fragment", RhiShaderStage::Fragment, *transparentFragmentSource);
    m_probeCaptureVertexShader =
        createShader("StaticMesh.ProbeCapture.Vertex", RhiShaderStage::Vertex, *probeCaptureVertexSource);
    m_probeCaptureFragmentShader =
        createShader("StaticMesh.ProbeCapture.Fragment", RhiShaderStage::Fragment, *probeCaptureFragmentSource);
    if (!m_gbufferVertexShader.isValid() || !m_gbufferFragmentShader.isValid() || !m_shadowVertexShader.isValid() ||
        !m_shadowFragmentShader.isValid() || !m_previewVertexShader.isValid() || !m_previewFragmentShader.isValid() ||
        !m_transparentVertexShader.isValid() || !m_transparentFragmentShader.isValid() ||
        !m_probeCaptureVertexShader.isValid() || !m_probeCaptureFragmentShader.isValid()) {
        setError("failed to compile static mesh shaders");
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "StaticMesh.Material.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 5u; ++binding) {
        bindGroupLayoutDesc.entries.push_back(
            {binding, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u});
    }
    for (uint32_t binding = 7u; binding <= 13u; ++binding) {
        bindGroupLayoutDesc.entries.push_back(
            {binding, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u});
    }
    bindGroupLayoutDesc.entries.push_back({5u, RhiBindingType::UniformBuffer, rhiFlag(RhiShaderStage::Fragment), 1u});
    bindGroupLayoutDesc.entries.push_back(
        {6u, RhiBindingType::UniformBuffer, rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment), 1u});
    m_bindGroupLayout = m_rhiDevice->createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_bindGroupLayout.isValid()) {
        setError("failed to create static mesh material bind group layout");
        return false;
    }

    RhiBindGroupLayoutDesc transparentSceneLayoutDesc;
    transparentSceneLayoutDesc.debugName = "StaticMesh.TransparentScene.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 3u; ++binding) {
        transparentSceneLayoutDesc.entries.push_back(
            {binding, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u});
    }
    m_transparentSceneBindGroupLayout = m_rhiDevice->createBindGroupLayout(transparentSceneLayoutDesc);
    RhiSamplerDesc linearSamplerDesc;
    linearSamplerDesc.minFilter = RhiFilter::Linear;
    linearSamplerDesc.magFilter = RhiFilter::Linear;
    linearSamplerDesc.mipmapMode = RhiMipmapMode::Linear;
    m_transparentSceneLinearSampler = m_rhiDevice->createSampler(linearSamplerDesc);
    RhiSamplerDesc depthSamplerDesc = linearSamplerDesc;
    depthSamplerDesc.minFilter = RhiFilter::Nearest;
    depthSamplerDesc.magFilter = RhiFilter::Nearest;
    m_transparentSceneDepthSampler = m_rhiDevice->createSampler(depthSamplerDesc);
    if (!m_transparentSceneBindGroupLayout.isValid() || !m_transparentSceneLinearSampler.isValid() ||
        !m_transparentSceneDepthSampler.isValid()) {
        setError("failed to create static mesh transparent scene resources");
        return false;
    }

    RhiBindGroupLayoutDesc probeCaptureLayoutDesc;
    probeCaptureLayoutDesc.debugName = "StaticMesh.ProbeCapture.BindGroupLayout";
    probeCaptureLayoutDesc.entries.push_back(
        {0u, RhiBindingType::UniformBuffer, rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment), 1u});
    probeCaptureLayoutDesc.entries.push_back(
        {1u, RhiBindingType::StorageBuffer, rhiFlag(RhiShaderStage::Fragment), 1u});
    m_probeCaptureBindGroupLayout = m_rhiDevice->createBindGroupLayout(probeCaptureLayoutDesc);
    if (!m_probeCaptureBindGroupLayout.isValid()) {
        setError("failed to create static mesh probe-capture bind group layout");
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "StaticMesh.GBuffer.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(StaticMeshGBufferPushConstants);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex);
    m_gbufferPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    pipelineLayoutDesc.debugName = "StaticMesh.Shadow.PipelineLayout";
    pipelineLayoutDesc.pushConstantBytes = sizeof(glm::mat4);
    m_shadowPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    pipelineLayoutDesc.debugName = "StaticMesh.Preview.PipelineLayout";
    pipelineLayoutDesc.pushConstantBytes = sizeof(StaticMeshPreviewPushConstants);
    m_previewPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    pipelineLayoutDesc.debugName = "StaticMesh.ProbeCapture.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts = {m_bindGroupLayout, m_probeCaptureBindGroupLayout};
    pipelineLayoutDesc.pushConstantBytes = sizeof(glm::mat4);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex);
    m_probeCapturePipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    if (!m_gbufferPipelineLayout.isValid() || !m_shadowPipelineLayout.isValid() || !m_previewPipelineLayout.isValid() ||
        !m_probeCapturePipelineLayout.isValid()) {
        setError("failed to create static mesh pipeline layouts");
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "StaticMesh.GBuffer.Pipeline";
    pipelineDesc.vertexShader = m_gbufferVertexShader;
    pipelineDesc.fragmentShader = m_gbufferFragmentShader;
    pipelineDesc.layout = m_gbufferPipelineLayout;
    pipelineDesc.vertexInput.bindings = {{0u, sizeof(StaticMeshVertex), RhiVertexInputRate::Vertex}};
    pipelineDesc.vertexInput.attributes = {{0u, 0u, RhiVertexFormat::Float3, offsetof(StaticMeshVertex, position)},
                                           {1u, 0u, RhiVertexFormat::Float3, offsetof(StaticMeshVertex, normal)},
                                           {2u, 0u, RhiVertexFormat::Float4, offsetof(StaticMeshVertex, tangent)},
                                           {3u, 0u, RhiVertexFormat::Float2, offsetof(StaticMeshVertex, uv)}};
    pipelineDesc.colorFormats = {RhiTextureFormat::Rgba8Unorm, RhiTextureFormat::Rgb10A2Unorm,
                                 RhiTextureFormat::Rg8Unorm,   RhiTextureFormat::Rgba8Unorm,
                                 RhiTextureFormat::Rgba8Unorm, RhiTextureFormat::Rgba8Unorm,
                                 RhiTextureFormat::Rg32Uint,   RhiTextureFormat::Rg16Float};
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    pipelineDesc.blend.attachments.resize(8u);
    m_gbufferPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    pipelineDesc.debugName = "StaticMesh.GBuffer.DoubleSidedPipeline";
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    m_gbufferDoubleSidedPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);

    pipelineDesc.debugName = "StaticMesh.Shadow.Pipeline";
    pipelineDesc.vertexShader = m_shadowVertexShader;
    pipelineDesc.fragmentShader = m_shadowFragmentShader;
    pipelineDesc.layout = m_shadowPipelineLayout;
    pipelineDesc.vertexInput.attributes = {{0u, 0u, RhiVertexFormat::Float3, offsetof(StaticMeshVertex, position)},
                                           {3u, 0u, RhiVertexFormat::Float2, offsetof(StaticMeshVertex, uv)}};
    pipelineDesc.raster.cullMode = RhiCullMode::Back;
    pipelineDesc.colorFormats.clear();
    pipelineDesc.blend.attachments.clear();
    m_shadowPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    pipelineDesc.debugName = "StaticMesh.Shadow.DoubleSidedPipeline";
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    m_shadowDoubleSidedPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    pipelineDesc.debugName = "StaticMesh.Preview.Pipeline";
    pipelineDesc.vertexShader = m_previewVertexShader;
    pipelineDesc.fragmentShader = m_previewFragmentShader;
    pipelineDesc.layout = m_previewPipelineLayout;
    pipelineDesc.vertexInput.attributes = {{0u, 0u, RhiVertexFormat::Float3, offsetof(StaticMeshVertex, position)},
                                           {1u, 0u, RhiVertexFormat::Float3, offsetof(StaticMeshVertex, normal)},
                                           {2u, 0u, RhiVertexFormat::Float4, offsetof(StaticMeshVertex, tangent)},
                                           {3u, 0u, RhiVertexFormat::Float2, offsetof(StaticMeshVertex, uv)}};
    pipelineDesc.raster.cullMode = RhiCullMode::Back;
    pipelineDesc.colorFormats = {RhiTextureFormat::Rgba8Unorm};
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::Less;
    pipelineDesc.blend.attachments.resize(1u);
    m_previewPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    pipelineDesc.debugName = "StaticMesh.Preview.DoubleSidedPipeline";
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    m_previewDoubleSidedPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);

    pipelineDesc.debugName = "StaticMesh.Preview.TransparentPipeline";
    pipelineDesc.raster.cullMode = RhiCullMode::Back;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::LessOrEqual;
    RhiBlendAttachmentState previewBlend;
    previewBlend.blendEnabled = true;
    previewBlend.srcColor = RhiBlendFactor::One;
    previewBlend.dstColor = RhiBlendFactor::OneMinusSrcAlpha;
    previewBlend.srcAlpha = RhiBlendFactor::One;
    previewBlend.dstAlpha = RhiBlendFactor::OneMinusSrcAlpha;
    pipelineDesc.blend.attachments = {previewBlend};
    m_previewTransparentPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    pipelineDesc.debugName = "StaticMesh.Preview.TransparentDoubleSidedPipeline";
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    m_previewTransparentDoubleSidedPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);

    pipelineDesc.debugName = "StaticMesh.ProbeCapture.Pipeline";
    pipelineDesc.vertexShader = m_probeCaptureVertexShader;
    pipelineDesc.fragmentShader = m_probeCaptureFragmentShader;
    pipelineDesc.layout = m_probeCapturePipelineLayout;
    pipelineDesc.raster.cullMode = RhiCullMode::Back;
    pipelineDesc.colorFormats = {RhiTextureFormat::Rgba16Float};
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::Less;
    pipelineDesc.blend.attachments.resize(1u);
    m_probeCapturePipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    pipelineDesc.debugName = "StaticMesh.ProbeCapture.DoubleSidedPipeline";
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    m_probeCaptureDoubleSidedPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);

    RhiBlendAttachmentState probeCaptureBlend;
    probeCaptureBlend.blendEnabled = true;
    probeCaptureBlend.srcColor = RhiBlendFactor::One;
    probeCaptureBlend.dstColor = RhiBlendFactor::OneMinusSrcAlpha;
    probeCaptureBlend.srcAlpha = RhiBlendFactor::One;
    probeCaptureBlend.dstAlpha = RhiBlendFactor::OneMinusSrcAlpha;
    pipelineDesc.debugName = "StaticMesh.ProbeCapture.TransparentPipeline";
    pipelineDesc.raster.cullMode = RhiCullMode::Back;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::LessOrEqual;
    pipelineDesc.blend.attachments = {probeCaptureBlend};
    m_probeCaptureTransparentPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    pipelineDesc.debugName = "StaticMesh.ProbeCapture.TransparentDoubleSidedPipeline";
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    m_probeCaptureTransparentDoubleSidedPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    if (m_rhiDevice->backend() != RhiBackend::Vulkan && !ensureTransparentPipelines({})) {
        setError("failed to create static mesh transparent pipelines");
        return false;
    }
    if (!m_gbufferPipeline.isValid() || !m_gbufferDoubleSidedPipeline.isValid() || !m_shadowPipeline.isValid() ||
        !m_shadowDoubleSidedPipeline.isValid() || !m_previewPipeline.isValid() ||
        !m_previewDoubleSidedPipeline.isValid() || !m_previewTransparentPipeline.isValid() ||
        !m_previewTransparentDoubleSidedPipeline.isValid() || !m_probeCapturePipeline.isValid() ||
        !m_probeCaptureDoubleSidedPipeline.isValid() || !m_probeCaptureTransparentPipeline.isValid() ||
        !m_probeCaptureTransparentDoubleSidedPipeline.isValid()) {
        setError("failed to create static mesh graphics pipelines");
        return false;
    }

    RhiBufferDesc frameBufferDesc;
    frameBufferDesc.debugName = "StaticMesh.FrameUniformBuffer";
    frameBufferDesc.size = sizeof(StaticMeshFrameParams);
    frameBufferDesc.usage = rhiFlag(RhiBufferUsage::Uniform) | rhiFlag(RhiBufferUsage::TransferDst);
    frameBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    frameBufferDesc.initialState = RhiResourceState::UniformBuffer;
    frameBufferDesc.memoryCategory = RhiMemoryCategory::Uniform;
    const StaticMeshFrameParams frameParams{};
    m_frameUniformBuffer = m_rhiDevice->createBuffer(frameBufferDesc, &frameParams, sizeof(frameParams));
    frameBufferDesc.debugName = "StaticMesh.ProbeCapture.FrameUniformBuffer";
    frameBufferDesc.size = sizeof(StaticMeshProbeCaptureFrameParams);
    const StaticMeshProbeCaptureFrameParams probeCaptureParams{};
    m_probeCaptureFrameUniformBuffer =
        m_rhiDevice->createBuffer(frameBufferDesc, &probeCaptureParams, sizeof(probeCaptureParams));
    if (!m_frameUniformBuffer.isValid() || !m_probeCaptureFrameUniformBuffer.isValid() ||
        !ensureReflectionProbeCaptureLightCapacity(1u)) {
        setError("failed to create static mesh frame uniform buffer");
        return false;
    }
    return true;
}

bool StaticMeshRenderer::ensureTransparentPipelines(const RhiBindGroupLayoutHandle clusteredLightingLayout) {
    if (m_rhiDevice == nullptr || !m_bindGroupLayout.isValid() || !m_transparentSceneBindGroupLayout.isValid() ||
        !m_transparentVertexShader.isValid() || !m_transparentFragmentShader.isValid() ||
        (m_rhiDevice->backend() == RhiBackend::Vulkan && !clusteredLightingLayout.isValid())) {
        return false;
    }
    if (m_transparentPipeline.isValid() && m_transparentDoubleSidedPipeline.isValid() &&
        (m_rhiDevice->backend() != RhiBackend::Vulkan ||
         sameHandle(m_transparentClusterBindGroupLayout, clusteredLightingLayout))) {
        return true;
    }

    destroyTransparentPipelines();
    m_transparentClusterBindGroupLayout = clusteredLightingLayout;

    RhiPipelineLayoutDesc layoutDesc;
    layoutDesc.debugName = "StaticMesh.Transparent.PipelineLayout";
    layoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout);
    layoutDesc.bindGroupLayouts.push_back(m_transparentSceneBindGroupLayout);
    if (m_rhiDevice->backend() == RhiBackend::Vulkan) {
        layoutDesc.bindGroupLayouts.push_back(clusteredLightingLayout);
    }
    layoutDesc.pushConstantBytes = sizeof(StaticMeshTransparentPushConstants);
    layoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    m_transparentPipelineLayout = m_rhiDevice->createPipelineLayout(layoutDesc);
    if (!m_transparentPipelineLayout.isValid()) {
        destroyTransparentPipelines();
        return false;
    }

    RhiGraphicsPipelineDesc desc;
    desc.debugName = "StaticMesh.Transparent.Pipeline";
    desc.vertexShader = m_transparentVertexShader;
    desc.fragmentShader = m_transparentFragmentShader;
    desc.layout = m_transparentPipelineLayout;
    desc.vertexInput.bindings = {{0u, sizeof(StaticMeshVertex), RhiVertexInputRate::Vertex}};
    desc.vertexInput.attributes = {{0u, 0u, RhiVertexFormat::Float3, offsetof(StaticMeshVertex, position)},
                                   {1u, 0u, RhiVertexFormat::Float3, offsetof(StaticMeshVertex, normal)},
                                   {2u, 0u, RhiVertexFormat::Float4, offsetof(StaticMeshVertex, tangent)},
                                   {3u, 0u, RhiVertexFormat::Float2, offsetof(StaticMeshVertex, uv)}};
    desc.raster.cullMode = RhiCullMode::Back;
    desc.depthStencil.depthTestEnabled = true;
    desc.depthStencil.depthWriteEnabled = false;
    desc.depthStencil.depthCompare = RhiCompareOp::LessOrEqual;
    desc.colorFormats = {RhiTextureFormat::Rgba16Float, RhiTextureFormat::R8Unorm, RhiTextureFormat::R8Unorm};
    desc.depthFormat = RhiTextureFormat::Depth32Float;
    RhiBlendAttachmentState colorBlend;
    colorBlend.blendEnabled = true;
    colorBlend.srcColor = RhiBlendFactor::One;
    colorBlend.dstColor = RhiBlendFactor::OneMinusSrcAlpha;
    colorBlend.srcAlpha = RhiBlendFactor::One;
    colorBlend.dstAlpha = RhiBlendFactor::OneMinusSrcAlpha;
    RhiBlendAttachmentState maskBlend;
    maskBlend.blendEnabled = true;
    maskBlend.srcColor = RhiBlendFactor::One;
    maskBlend.dstColor = RhiBlendFactor::One;
    maskBlend.colorOp = RhiBlendOp::Max;
    maskBlend.srcAlpha = RhiBlendFactor::One;
    maskBlend.dstAlpha = RhiBlendFactor::One;
    maskBlend.alphaOp = RhiBlendOp::Max;
    desc.blend.attachments = {colorBlend, maskBlend, maskBlend};
    m_transparentPipeline = m_rhiDevice->createGraphicsPipeline(desc);
    desc.debugName = "StaticMesh.Transparent.DoubleSidedPipeline";
    desc.raster.cullMode = RhiCullMode::None;
    m_transparentDoubleSidedPipeline = m_rhiDevice->createGraphicsPipeline(desc);
    if (!m_transparentPipeline.isValid() || !m_transparentDoubleSidedPipeline.isValid()) {
        destroyTransparentPipelines();
        return false;
    }
    return true;
}

bool StaticMeshRenderer::loadAsset(const std::string& modelPath, ResourceMgr& resourceMgr) {
    cgltf_options options{};
    cgltf_data* rawData = nullptr;
    cgltf_result result = cgltf_parse_file(&options, modelPath.c_str(), &rawData);
    if (result != cgltf_result_success || rawData == nullptr) {
        setError(std::string("failed to parse glTF asset: ") + cgltfResultName(result));
        return false;
    }
    const auto freeData = [](cgltf_data* data) {
        cgltf_free(data);
    };
    std::unique_ptr<cgltf_data, decltype(freeData)> data(rawData, freeData);
    result = cgltf_load_buffers(&options, data.get(), modelPath.c_str());
    if (result != cgltf_result_success) {
        setError(std::string("failed to load glTF buffers: ") + cgltfResultName(result));
        return false;
    }
    result = cgltf_validate(data.get());
    if (result != cgltf_result_success) {
        setError(std::string("glTF validation failed: ") + cgltfResultName(result));
        return false;
    }
    if (data->asset.version == nullptr || std::strcmp(data->asset.version, "2.0") != 0) {
        setError("static mesh assets must declare glTF version 2.0");
        return false;
    }
    if (data->scene == nullptr || data->scene->nodes_count == 0u || data->animations_count != 0u ||
        data->skins_count != 0u) {
        setError("static mesh assets require a non-empty default scene without skins or animations");
        return false;
    }
    if (data->materials_count == 0u) {
        setError("static mesh assets require explicit metallic-roughness materials");
        return false;
    }
    const std::string contractError = staticAssetContractError(*data);
    if (!contractError.empty()) {
        setError(contractError);
        return false;
    }

    const std::filesystem::path modelDirectory = std::filesystem::path(modelPath).parent_path();
    std::unordered_map<TextureCacheKey, uint32_t, TextureCacheKeyHash> textureCache;
    std::unordered_map<uint64_t, uint32_t> solidTextureCache;

    const auto uploadTexture = [this](const unsigned char* pixels, const std::size_t sizeBytes, const uint32_t width,
                                      const uint32_t height, const bool srgb, uint32_t& textureIndex) -> bool {
        const uint32_t mipLevels = mipLevelCount(width, height);
        RhiTextureDesc textureDesc;
        textureDesc.debugName = srgb ? "StaticMesh.ColorTexture" : "StaticMesh.DataTexture";
        textureDesc.format = srgb ? RhiTextureFormat::Rgba8Srgb : RhiTextureFormat::Rgba8Unorm;
        textureDesc.width = width;
        textureDesc.height = height;
        textureDesc.mipLevels = mipLevels;
        textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::TransferSrc) |
                            rhiFlag(RhiTextureUsage::TransferDst);
        textureDesc.memoryCategory = RhiMemoryCategory::Texture;
        RhiTextureInitialData initialData;
        initialData.pixels = pixels;
        initialData.sizeBytes = sizeBytes;
        initialData.finalState = RhiResourceState::TransferDst;
        TextureResource resource;
        resource.texture = m_rhiDevice->createTexture(textureDesc, &initialData);
        if (!resource.texture.isValid()) {
            setError("failed to upload static mesh texture");
            return false;
        }
        RhiTextureViewDesc viewDesc;
        viewDesc.texture = resource.texture;
        viewDesc.viewType = RhiTextureViewType::Texture2D;
        viewDesc.mipCount = mipLevels;
        resource.view = m_rhiDevice->createTextureView(viewDesc);
        if (!resource.view.isValid()) {
            m_rhiDevice->destroyTexture(resource.texture);
            setError("failed to create static mesh texture view");
            return false;
        }
        resource.mipLevels = mipLevels;
        textureIndex = static_cast<uint32_t>(m_textures.size());
        m_textures.push_back(resource);
        return true;
    };

    const auto loadTexture = [&](const cgltf_texture_view& textureView, const bool srgb,
                                 const std::array<unsigned char, 4>& defaultPixel, uint32_t& textureIndex,
                                 RhiSamplerHandle& samplerHandle) -> bool {
        const cgltf_sampler* sampler = nullptr;
        if (textureView.texture == nullptr) {
            const uint64_t solidKey =
                static_cast<uint64_t>(defaultPixel[0]) | (static_cast<uint64_t>(defaultPixel[1]) << 8u) |
                (static_cast<uint64_t>(defaultPixel[2]) << 16u) | (static_cast<uint64_t>(defaultPixel[3]) << 24u) |
                (static_cast<uint64_t>(srgb) << 32u);
            const auto cached = solidTextureCache.find(solidKey);
            if (cached != solidTextureCache.end()) {
                textureIndex = cached->second;
            } else {
                if (!uploadTexture(defaultPixel.data(), defaultPixel.size(), 1u, 1u, srgb, textureIndex)) {
                    return false;
                }
                solidTextureCache.emplace(solidKey, textureIndex);
            }
        } else {
            if (textureView.texture->image == nullptr) {
                setError("glTF texture does not reference a 2D image");
                return false;
            }
            if (textureView.texcoord != 0 || textureView.has_transform) {
                setError("static mesh materials require untransformed TEXCOORD_0");
                return false;
            }
            if (textureView.texture->has_basisu || textureView.texture->has_webp) {
                setError("compressed and WebP glTF texture extensions are not supported");
                return false;
            }
            const TextureCacheKey cacheKey{textureView.texture->image, srgb};
            const auto cached = textureCache.find(cacheKey);
            if (cached != textureCache.end()) {
                textureIndex = cached->second;
            } else {
                DecodedImage decoded;
                std::string decodeError;
                if (!decodeImage(*textureView.texture->image, modelDirectory, decoded, decodeError)) {
                    setError(std::move(decodeError));
                    return false;
                }
                if (!uploadTexture(decoded.pixels.data(), decoded.pixels.size(), static_cast<uint32_t>(decoded.width),
                                   static_cast<uint32_t>(decoded.height), srgb, textureIndex)) {
                    return false;
                }
                textureCache.emplace(cacheKey, textureIndex);
            }
            sampler = textureView.texture->sampler;
        }

        const RhiCapabilities& capabilities = m_rhiDevice->capabilities();
        RhiSamplerDesc samplerDesc;
        if (!buildSamplerDesc(sampler, capabilities, samplerDesc)) {
            setError("glTF texture uses an invalid sampler contract");
            return false;
        }
        samplerHandle = m_rhiDevice->createSampler(samplerDesc);
        if (!samplerHandle.isValid()) {
            setError("failed to create static mesh anisotropic sampler");
            return false;
        }
        m_samplers.push_back(samplerHandle);
        return true;
    };

    m_materials.reserve(data->materials_count);
    for (cgltf_size materialIndex = 0u; materialIndex < data->materials_count; ++materialIndex) {
        const cgltf_material& material = data->materials[materialIndex];
        const bool specularGlossiness = material.has_pbr_specular_glossiness != 0;
        const cgltf_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;
        const cgltf_pbr_specular_glossiness& specularGloss = material.pbr_specular_glossiness;
        const std::array<const cgltf_texture_view*, 12> textureViews = {
            specularGlossiness ? &specularGloss.diffuse_texture : &pbr.base_color_texture,
            specularGlossiness ? &specularGloss.specular_glossiness_texture : &pbr.metallic_roughness_texture,
            &material.normal_texture,
            &material.occlusion_texture,
            &material.emissive_texture,
            &material.specular.specular_texture,
            &material.specular.specular_color_texture,
            &material.clearcoat.clearcoat_texture,
            &material.clearcoat.clearcoat_roughness_texture,
            &material.clearcoat.clearcoat_normal_texture,
            &material.transmission.transmission_texture,
            &material.volume.thickness_texture};
        std::array<uint32_t, 12> textureIndices{};
        std::array<RhiSamplerHandle, 12> samplers{};
        const renderer::contracts::GpuMaterialWorkflow workflow =
            specularGlossiness ? renderer::contracts::GpuMaterialWorkflow::SpecularGlossiness
                               : renderer::contracts::GpuMaterialWorkflow::MetallicRoughness;
        for (std::size_t channel = 0u; channel < textureViews.size(); ++channel) {
            const auto semantic = static_cast<renderer::contracts::GpuMaterialTextureSemantic>(channel);
            if (!loadTexture(*textureViews[channel],
                             renderer::contracts::gpuMaterialTextureUsesSrgb(semantic, workflow),
                             renderer::contracts::gpuMaterialDefaultTexturePixel(semantic), textureIndices[channel],
                             samplers[channel])) {
                return false;
            }
        }

        const std::string materialName =
            material.name != nullptr ? material.name : ("#" + std::to_string(materialIndex));
        renderer::contracts::GltfMaterialNormalizationInput materialInput;
        materialInput.workflow = workflow;
        materialInput.baseColorFactor =
            specularGlossiness ? glm::make_vec4(specularGloss.diffuse_factor) : glm::make_vec4(pbr.base_color_factor);
        materialInput.emissiveFactor = glm::make_vec3(material.emissive_factor);
        materialInput.emissiveStrength =
            material.has_emissive_strength ? material.emissive_strength.emissive_strength : 1.0f;
        materialInput.metallicFactor = pbr.metallic_factor;
        materialInput.perceptualRoughnessFactor = pbr.roughness_factor;
        materialInput.normalScale = material.normal_texture.scale;
        materialInput.occlusionStrength = material.occlusion_texture.scale;
        if (specularGlossiness) {
            materialInput.specularGlossinessFactor = glm::make_vec3(specularGloss.specular_factor);
            materialInput.glossinessFactor = specularGloss.glossiness_factor;
        }
        if (material.has_specular) {
            materialInput.dielectricSpecularColorFactor = glm::make_vec3(material.specular.specular_color_factor);
            materialInput.dielectricSpecularWeightFactor = material.specular.specular_factor;
        }
        if (material.has_clearcoat) {
            materialInput.clearcoatFactor = material.clearcoat.clearcoat_factor;
            materialInput.clearcoatPerceptualRoughnessFactor = material.clearcoat.clearcoat_roughness_factor;
            materialInput.clearcoatNormalScale = material.clearcoat.clearcoat_normal_texture.scale;
        }
        materialInput.transmissionFactor = material.has_transmission ? material.transmission.transmission_factor : 0.0f;
        materialInput.thicknessFactor = material.has_volume ? material.volume.thickness_factor : 0.0f;
        materialInput.alphaCutoff = material.alpha_cutoff;
        materialInput.ior = material.has_ior ? material.ior.ior : 1.5f;
        bool infiniteAttenuationDistance = false;
        if (material.has_volume) {
            materialInput.attenuationColor = glm::make_vec3(material.volume.attenuation_color);
            const float encodedDistance = material.volume.attenuation_distance;
            if (encodedDistance == std::numeric_limits<float>::max()) {
                infiniteAttenuationDistance = true;
            } else if (std::isfinite(encodedDistance) && encodedDistance > 0.0f) {
                materialInput.attenuationDistance = encodedDistance;
            } else {
                setError("glTF volume attenuation distance is invalid [material=" + materialName + "]");
                return false;
            }
        }

        using renderer::contracts::GpuMaterialAlphaMode;
        if (material.has_transmission) {
            materialInput.alphaMode = GpuMaterialAlphaMode::Transmission;
        } else {
            switch (material.alpha_mode) {
            case cgltf_alpha_mode_opaque: materialInput.alphaMode = GpuMaterialAlphaMode::Opaque; break;
            case cgltf_alpha_mode_mask: materialInput.alphaMode = GpuMaterialAlphaMode::Mask; break;
            case cgltf_alpha_mode_blend: materialInput.alphaMode = GpuMaterialAlphaMode::Blend; break;
            case cgltf_alpha_mode_max_enum: setError("glTF material alpha mode is invalid"); return false;
            }
        }

        using renderer::contracts::GpuMaterialFlag;
        using renderer::contracts::gpuMaterialFlagBit;
        materialInput.flags =
            (material.double_sided ? gpuMaterialFlagBit(GpuMaterialFlag::DoubleSided) : 0u) |
            (material.has_specular ? gpuMaterialFlagBit(GpuMaterialFlag::Specular) : 0u) |
            (material.has_ior ? gpuMaterialFlagBit(GpuMaterialFlag::Ior) : 0u) |
            (material.has_clearcoat ? gpuMaterialFlagBit(GpuMaterialFlag::Clearcoat) : 0u) |
            (material.has_transmission ? gpuMaterialFlagBit(GpuMaterialFlag::Transmission) : 0u) |
            (material.has_volume ? gpuMaterialFlagBit(GpuMaterialFlag::Volume) : 0u) |
            (infiniteAttenuationDistance ? gpuMaterialFlagBit(GpuMaterialFlag::InfiniteAttenuationDistance) : 0u);
        for (size_t channel = 0u; channel < textureIndices.size(); ++channel) {
            materialInput.textureBindings[channel] = {textureIndices[channel], samplers[channel].index};
        }

        const renderer::contracts::GpuMaterialNormalizationResult normalization =
            renderer::contracts::normalizeGltfMaterial(materialInput);
        if (!normalization.succeeded()) {
            setError(std::string("glTF material normalization failed [material=") + materialName +
                     ", error=" + renderer::contracts::gpuMaterialNormalizationErrorStableId(normalization.error) +
                     ", field=" + renderer::contracts::gpuMaterialFieldStableId(normalization.field) + "]");
            return false;
        }
        const std::optional<renderer::contracts::StableMaterialId> materialId =
            renderer::contracts::allocateStableSceneId<renderer::contracts::StableMaterialIdTag>();
        if (!materialId.has_value()) {
            setError("stable static mesh material identity space is exhausted");
            return false;
        }
        const StaticMeshMaterialParams params{normalization.material, glm::uvec4(materialId->value, 0u, 0u, 0u)};

        RhiBufferDesc materialBufferDesc;
        materialBufferDesc.debugName = "StaticMesh.MaterialUniformBuffer";
        materialBufferDesc.size = sizeof(params);
        materialBufferDesc.usage = rhiFlag(RhiBufferUsage::Uniform) | rhiFlag(RhiBufferUsage::TransferDst);
        materialBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
        materialBufferDesc.initialState = RhiResourceState::UniformBuffer;
        materialBufferDesc.memoryCategory = RhiMemoryCategory::Uniform;
        MaterialResource resource;
        resource.materialId = *materialId;
        resource.uniformBuffer = m_rhiDevice->createBuffer(materialBufferDesc, &params, sizeof(params));
        if (!resource.uniformBuffer.isValid()) {
            setError("failed to create static mesh material uniform buffer");
            return false;
        }
        resource.doubleSided = material.double_sided != 0;
        resource.alphaMasked = materialInput.alphaMode == renderer::contracts::GpuMaterialAlphaMode::Mask;
        resource.alphaBlended = materialInput.alphaMode == renderer::contracts::GpuMaterialAlphaMode::Blend;
        resource.transmissive = materialInput.alphaMode == renderer::contracts::GpuMaterialAlphaMode::Transmission;
        resource.forwardOpticalLayer = resource.alphaBlended || resource.transmissive || material.has_clearcoat;

        RhiBindGroupDesc bindGroupDesc;
        bindGroupDesc.layout = m_bindGroupLayout;
        for (uint32_t channel = 0u; channel < 5u; ++channel) {
            RhiBindGroupEntry entry;
            entry.binding = channel;
            entry.resource.combinedTextureSampler = {m_textures[textureIndices[channel]].view, samplers[channel]};
            bindGroupDesc.entries.push_back(entry);
        }
        for (uint32_t channel = 5u; channel < 12u; ++channel) {
            RhiBindGroupEntry entry;
            entry.binding = channel + 2u;
            entry.resource.combinedTextureSampler = {m_textures[textureIndices[channel]].view, samplers[channel]};
            bindGroupDesc.entries.push_back(entry);
        }
        RhiBindGroupEntry materialEntry;
        materialEntry.binding = 5u;
        materialEntry.resource.buffer = {resource.uniformBuffer, 0u, sizeof(params)};
        bindGroupDesc.entries.push_back(materialEntry);
        RhiBindGroupEntry frameEntry;
        frameEntry.binding = 6u;
        frameEntry.resource.buffer = {m_frameUniformBuffer, 0u, sizeof(StaticMeshFrameParams)};
        bindGroupDesc.entries.push_back(frameEntry);
        resource.bindGroup = m_rhiDevice->createBindGroup(bindGroupDesc);
        if (!resource.bindGroup.isValid()) {
            m_rhiDevice->destroyBuffer(resource.uniformBuffer);
            setError("failed to create static mesh material bind group");
            return false;
        }
        m_materials.push_back(resource);
    }

    bool boundsInitialized = false;
    const auto appendPrimitive = [&](const cgltf_node& node, const cgltf_primitive& primitive) -> bool {
        if (primitive.type != cgltf_primitive_type_triangles || primitive.indices == nullptr ||
            primitive.material == nullptr || primitive.targets_count != 0u || primitive.has_draco_mesh_compression) {
            setError("mesh primitive must be indexed triangles without morph or Draco data");
            return false;
        }
        const cgltf_accessor* positions = requireAttribute(primitive, cgltf_attribute_type_position, 0);
        const cgltf_accessor* normals = requireAttribute(primitive, cgltf_attribute_type_normal, 0);
        const cgltf_accessor* tangents = requireAttribute(primitive, cgltf_attribute_type_tangent, 0);
        const cgltf_accessor* uvs = requireAttribute(primitive, cgltf_attribute_type_texcoord, 0);
        const cgltf_material& primitiveMaterial = *primitive.material;
        const cgltf_pbr_metallic_roughness& primitiveMetallicRoughness = primitiveMaterial.pbr_metallic_roughness;
        const cgltf_pbr_specular_glossiness& primitiveSpecularGlossiness = primitiveMaterial.pbr_specular_glossiness;
        const bool materialUsesTangentSpaceNormals =
            primitiveMaterial.normal_texture.texture != nullptr ||
            primitiveMaterial.clearcoat.clearcoat_normal_texture.texture != nullptr;
        const bool materialUsesTextures = primitiveMetallicRoughness.base_color_texture.texture != nullptr ||
                                          primitiveMetallicRoughness.metallic_roughness_texture.texture != nullptr ||
                                          primitiveSpecularGlossiness.diffuse_texture.texture != nullptr ||
                                          primitiveSpecularGlossiness.specular_glossiness_texture.texture != nullptr ||
                                          primitiveMaterial.normal_texture.texture != nullptr ||
                                          primitiveMaterial.occlusion_texture.texture != nullptr ||
                                          primitiveMaterial.emissive_texture.texture != nullptr ||
                                          primitiveMaterial.specular.specular_texture.texture != nullptr ||
                                          primitiveMaterial.specular.specular_color_texture.texture != nullptr ||
                                          primitiveMaterial.clearcoat.clearcoat_texture.texture != nullptr ||
                                          primitiveMaterial.clearcoat.clearcoat_roughness_texture.texture != nullptr ||
                                          primitiveMaterial.clearcoat.clearcoat_normal_texture.texture != nullptr ||
                                          primitiveMaterial.transmission.transmission_texture.texture != nullptr ||
                                          primitiveMaterial.volume.thickness_texture.texture != nullptr;
        if (positions == nullptr || normals == nullptr || (uvs == nullptr && materialUsesTextures) ||
            positions->type != cgltf_type_vec3 || normals->type != cgltf_type_vec3 ||
            (uvs != nullptr && (uvs->type != cgltf_type_vec2 || positions->count != uvs->count)) ||
            positions->count != normals->count || primitive.indices->count == 0u ||
            primitive.indices->count % 3u != 0u ||
            (materialUsesTangentSpaceNormals && tangents != nullptr &&
             (tangents->type != cgltf_type_vec4 || tangents->count != positions->count))) {
            setError("mesh primitive has an invalid POSITION/NORMAL/TANGENT/TEXCOORD_0 contract");
            return false;
        }

        cgltf_float worldValues[16];
        cgltf_node_transform_world(&node, worldValues);
        const glm::mat4 world = glm::make_mat4(worldValues);
        const glm::mat3 linear(world);
        const float determinant = glm::determinant(linear);
        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-8f) {
            setError("mesh node has a singular world transform");
            return false;
        }
        const glm::mat3 normalMatrix = glm::inverseTranspose(linear);
        std::vector<StaticMeshVertex> vertices;
        vertices.reserve(primitive.indices->count);
        std::vector<uint32_t> indices;
        indices.reserve(primitive.indices->count);
        glm::vec3 primitiveBoundsMin{0.0f};
        glm::vec3 primitiveBoundsMax{0.0f};
        bool primitiveBoundsInitialized = false;
        for (cgltf_size corner = 0u; corner < primitive.indices->count; ++corner) {
            const cgltf_size sourceIndex = cgltf_accessor_read_index(primitive.indices, corner);
            if (sourceIndex >= positions->count) {
                setError("mesh primitive index exceeds its vertex accessors");
                return false;
            }
            float positionValues[3];
            float normalValues[3];
            float uvValues[2] = {0.0f, 0.0f};
            if (!readAccessorVec(*positions, sourceIndex, positionValues, 3u) ||
                !readAccessorVec(*normals, sourceIndex, normalValues, 3u) ||
                (uvs != nullptr && !readAccessorVec(*uvs, sourceIndex, uvValues, 2u))) {
                setError("failed to decode mesh vertex accessors");
                return false;
            }
            const glm::vec3 position = glm::vec3(world * glm::vec4(glm::make_vec3(positionValues), 1.0f));
            const glm::vec3 transformedNormal = normalMatrix * glm::make_vec3(normalValues);
            const float normalLengthSq = glm::dot(transformedNormal, transformedNormal);
            if (!finiteVector(position) || !finiteVector(transformedNormal) || normalLengthSq < 1e-12f) {
                setError("mesh vertex contains a non-finite position or normal");
                return false;
            }
            const glm::vec3 normal = transformedNormal / std::sqrt(normalLengthSq);
            if (!primitiveBoundsInitialized) {
                primitiveBoundsMin = position;
                primitiveBoundsMax = position;
                primitiveBoundsInitialized = true;
            } else {
                primitiveBoundsMin = glm::min(primitiveBoundsMin, position);
                primitiveBoundsMax = glm::max(primitiveBoundsMax, position);
            }
            StaticMeshVertex vertex{};
            std::memcpy(vertex.position, glm::value_ptr(position), sizeof(vertex.position));
            std::memcpy(vertex.normal, glm::value_ptr(normal), sizeof(vertex.normal));
            vertex.uv[0] = uvValues[0];
            vertex.uv[1] = uvValues[1];
            if (materialUsesTangentSpaceNormals && tangents != nullptr) {
                float tangentValues[4];
                if (!readAccessorVec(*tangents, sourceIndex, tangentValues, 4u)) {
                    setError("failed to decode mesh tangent accessor");
                    return false;
                }
                const glm::vec3 transformedTangent = linear * glm::make_vec3(tangentValues);
                const float tangentLengthSq = glm::dot(transformedTangent, transformedTangent);
                if (!finiteVector(transformedTangent) || tangentLengthSq < 1e-12f) {
                    setError("mesh vertex contains an invalid tangent");
                    return false;
                }
                const glm::vec3 tangent = transformedTangent / std::sqrt(tangentLengthSq);
                std::memcpy(vertex.tangent, glm::value_ptr(tangent), sizeof(float) * 3u);
                vertex.tangent[3] = tangentValues[3] * (determinant < 0.0f ? -1.0f : 1.0f);
            } else if (!materialUsesTangentSpaceNormals) {
                const glm::vec3 tangentReference =
                    std::abs(normal.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
                const glm::vec3 tangent = glm::normalize(glm::cross(tangentReference, normal));
                std::memcpy(vertex.tangent, glm::value_ptr(tangent), sizeof(float) * 3u);
                vertex.tangent[3] = 1.0f;
            }
            if (!boundsInitialized) {
                m_assetBoundsMin = position;
                m_assetBoundsMax = position;
                boundsInitialized = true;
            } else {
                m_assetBoundsMin = glm::min(m_assetBoundsMin, position);
                m_assetBoundsMax = glm::max(m_assetBoundsMax, position);
            }
            vertices.push_back(vertex);
            indices.push_back(static_cast<uint32_t>(indices.size()));
        }
        if (determinant < 0.0f) {
            for (std::size_t first = 0u; first < vertices.size(); first += 3u) {
                std::swap(vertices[first + 1u], vertices[first + 2u]);
            }
        }
        const std::size_t removedTriangles = removeNonRasterizableTriangles(vertices);
        if (removedTriangles != 0u) {
            indices.resize(vertices.size());
            for (std::size_t index = 0u; index < indices.size(); ++index) {
                indices[index] = static_cast<uint32_t>(index);
            }
        }
        if (vertices.empty()) {
            setError("mesh primitive contains only non-rasterizable triangles");
            return false;
        }
        if (materialUsesTangentSpaceNormals && tangents == nullptr) {
            std::string tangentError;
            if (!generateTangents(vertices, tangentError)) {
                setError(std::move(tangentError));
                return false;
            }
        }
        if (vertices.size() > static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())) {
            setError("mesh primitive exceeds the 32-bit index contract");
            return false;
        }

        PrimitiveResource resource;
        const std::optional<renderer::contracts::StableGeometryId> geometryId =
            renderer::contracts::allocateStableSceneId<renderer::contracts::StableGeometryIdTag>();
        if (!geometryId.has_value()) {
            setError("stable static mesh geometry identity space is exhausted");
            return false;
        }
        resource.geometryId = *geometryId;
        resource.vertexCount = static_cast<uint32_t>(vertices.size());
        resource.indexCount = static_cast<uint32_t>(indices.size());
        resource.materialIndex = static_cast<uint32_t>(primitive.material - data->materials);
        resource.boundsCenter = (primitiveBoundsMin + primitiveBoundsMax) * 0.5f;
        const MaterialResource& material = m_materials[resource.materialIndex];
        const bool solidGeometry = !material.alphaBlended && !material.transmissive;
        RhiBufferDesc bufferDesc;
        bufferDesc.debugName = "StaticMesh.VertexBuffer";
        bufferDesc.size = vertices.size() * sizeof(StaticMeshVertex);
        bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex) | rhiFlag(RhiBufferUsage::TransferDst);
        if (solidGeometry && m_staticBlasCache.supported()) {
            bufferDesc.usage |= rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::DeviceAddress) |
                                rhiFlag(RhiBufferUsage::AccelerationStructureBuildInput);
        }
        bufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
        bufferDesc.initialState = RhiResourceState::VertexBuffer;
        bufferDesc.memoryCategory = RhiMemoryCategory::Geometry;
        resource.vertexBuffer = m_rhiDevice->createBuffer(bufferDesc, vertices.data(), bufferDesc.size);
        bufferDesc.debugName = "StaticMesh.IndexBuffer";
        bufferDesc.size = indices.size() * sizeof(uint32_t);
        bufferDesc.usage = rhiFlag(RhiBufferUsage::Index) | rhiFlag(RhiBufferUsage::TransferDst);
        if (solidGeometry && m_staticBlasCache.supported()) {
            bufferDesc.usage |= rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::DeviceAddress) |
                                rhiFlag(RhiBufferUsage::AccelerationStructureBuildInput);
        }
        bufferDesc.initialState = RhiResourceState::IndexBuffer;
        resource.indexBuffer = m_rhiDevice->createBuffer(bufferDesc, indices.data(), bufferDesc.size);
        if (!resource.vertexBuffer.isValid() || !resource.indexBuffer.isValid()) {
            if (resource.vertexBuffer.isValid()) {
                m_rhiDevice->destroyBuffer(resource.vertexBuffer);
            }
            if (resource.indexBuffer.isValid()) {
                m_rhiDevice->destroyBuffer(resource.indexBuffer);
            }
            setError("failed to upload static mesh primitive buffers");
            return false;
        }
        m_primitives.push_back(resource);
        return true;
    };

    std::vector<const cgltf_node*> nodes;
    nodes.reserve(data->nodes_count);
    m_punctualLights.reserve(data->lights_count);
    for (cgltf_size rootIndex = 0u; rootIndex < data->scene->nodes_count; ++rootIndex) {
        nodes.push_back(data->scene->nodes[rootIndex]);
    }
    for (std::size_t nodeIndex = 0u; nodeIndex < nodes.size(); ++nodeIndex) {
        const cgltf_node* node = nodes[nodeIndex];
        if (node == nullptr || node->skin != nullptr || node->has_mesh_gpu_instancing) {
            setError("default scene contains an unsupported node contract");
            return false;
        }
        if (node->light != nullptr) {
            const renderer::assets::GltfPunctualLightDecodeResult decoded =
                renderer::assets::decodeGltfPunctualLight(*node);
            if (!decoded.succeeded()) {
                const std::string lightName =
                    node->light->name != nullptr ? node->light->name : (node->name != nullptr ? node->name : "unnamed");
                setError("glTF punctual light decode failed [light=" + lightName + ", error=" +
                         renderer::assets::gltfPunctualLightDecodeErrorStableId(decoded.error) + ", instantiation=" +
                         renderer::contracts::analyticLightInstantiationErrorStableId(decoded.instantiationError) +
                         ", normalization=" +
                         renderer::contracts::gpuLightNormalizationErrorStableId(decoded.normalizationError) +
                         ", field=" + renderer::contracts::gpuLightFieldStableId(decoded.normalizationField) + "]");
                return false;
            }
            m_punctualLights.push_back(decoded.source);
        }
        if (node->mesh != nullptr) {
            for (cgltf_size primitiveIndex = 0u; primitiveIndex < node->mesh->primitives_count; ++primitiveIndex) {
                if (!appendPrimitive(*node, node->mesh->primitives[primitiveIndex])) {
                    return false;
                }
            }
        }
        for (cgltf_size childIndex = 0u; childIndex < node->children_count; ++childIndex) {
            nodes.push_back(node->children[childIndex]);
        }
    }
    if (m_primitives.empty() || !boundsInitialized) {
        setError("default glTF scene contains no renderable mesh primitives");
        return false;
    }
    const glm::vec3 boundsExtent = m_assetBoundsMax - m_assetBoundsMin;
    const float largestExtent = std::max({boundsExtent.x, boundsExtent.y, boundsExtent.z});
    if (!std::isfinite(largestExtent) || largestExtent <= 1e-6f) {
        setError("static mesh bounds are degenerate");
        return false;
    }

    RhiCommandList* commandList = resourceMgr.commandListPool().acquire(RhiCommandListType::Graphics);
    if (commandList == nullptr ||
        !commandList->begin({"StaticMesh.TextureMipCommands", RhiCommandListType::Graphics})) {
        setError("failed to begin static mesh texture mip commands");
        return false;
    }
    for (const TextureResource& texture : m_textures) {
        if (texture.mipLevels > 1u) {
            commandList->generateMipmaps(texture.texture);
        }
        commandList->textureBarrier({texture.texture, RhiResourceState::TransferDst, RhiResourceState::ShaderRead});
    }
    if (!commandList->end()) {
        setError("failed to end static mesh texture mip commands");
        return false;
    }
    RhiCommandList* submitted[] = {commandList};
    if (!m_rhiDevice->submit({"StaticMesh.TextureMipSubmit", submitted, 1u, RhiQueueType::Graphics})) {
        setError("failed to submit static mesh texture mip commands");
        return false;
    }
    return true;
}

bool StaticMeshRenderer::buildStaticBlas(RhiCommandListPool& commandListPool) {
    std::vector<renderer::rt::StaticMeshBlasGeometry> geometries;
    std::vector<std::size_t> primitiveIndices;
    geometries.reserve(m_primitives.size());
    primitiveIndices.reserve(m_primitives.size());
    for (std::size_t primitiveIndex = 0u; primitiveIndex < m_primitives.size(); ++primitiveIndex) {
        const PrimitiveResource& primitive = m_primitives[primitiveIndex];
        const MaterialResource& material = m_materials[primitive.materialIndex];
        if (material.alphaBlended || material.transmissive) {
            continue;
        }
        geometries.push_back({primitive.geometryId, primitive.vertexBuffer, primitive.indexBuffer,
                              offsetof(StaticMeshVertex, position), sizeof(StaticMeshVertex), primitive.vertexCount,
                              primitive.indexCount, !material.alphaMasked, material.doubleSided});
        primitiveIndices.push_back(primitiveIndex);
    }

    const renderer::rt::StaticMeshBlasBuildResult result = m_staticBlasCache.build(commandListPool, geometries);
    switch (result) {
    case renderer::rt::StaticMeshBlasBuildResult::Built:
        for (const std::size_t primitiveIndex : primitiveIndices) {
            m_primitives[primitiveIndex].retainedByBlas = true;
        }
        return true;
    case renderer::rt::StaticMeshBlasBuildResult::Empty:
    case renderer::rt::StaticMeshBlasBuildResult::Unsupported: return true;
    case renderer::rt::StaticMeshBlasBuildResult::InvalidGeometry:
    case renderer::rt::StaticMeshBlasBuildResult::Failed: setError(m_staticBlasCache.lastError()); return false;
    }
    setError("static mesh BLAS build returned an invalid result");
    return false;
}

void StaticMeshRenderer::prepareFrame(const FrameContext& ctx, const IWorldView& worldView) {
    if (!m_instancePlaced) {
        const glm::mat4 inverseView = glm::inverse(ctx.camera.view);
        const glm::vec3 cameraForward = -glm::normalize(glm::vec3(inverseView[2]));
        const glm::vec3 boundsCenter = (m_assetBoundsMin + m_assetBoundsMax) * 0.5f;
        const glm::vec3 boundsExtent = m_assetBoundsMax - m_assetBoundsMin;
        const float largestExtent = std::max({boundsExtent.x, boundsExtent.y, boundsExtent.z});
        const float displayScale = 1.8f / largestExtent;
        const glm::vec3 anchor = ctx.camera.position + cameraForward * 3.0f;
        m_modelMatrix = glm::translate(glm::mat4(1.0f), anchor) * glm::scale(glm::mat4(1.0f), glm::vec3(displayScale)) *
                        glm::translate(glm::mat4(1.0f), -boundsCenter);
        m_previousModelMatrix = m_modelMatrix;
        m_instancePlaced = true;
    }
    const glm::vec3 worldCenter =
        glm::vec3(m_modelMatrix * glm::vec4((m_assetBoundsMin + m_assetBoundsMax) * 0.5f, 1.0f));
    const glm::ivec3 lightPosition = glm::ivec3(glm::floor(worldCenter));
    const glm::vec2 light =
        decodePackedLight(worldView.getPackedLight(lightPosition.x, lightPosition.y, lightPosition.z));
    m_voxelLight = glm::vec4(light, 0.0f, 1.0f);
    m_framePrepared = true;
}

void StaticMeshRenderer::setInstanceTransform(const glm::mat4& model, const glm::mat4& previousModel) {
    m_modelMatrix = model;
    m_previousModelMatrix = previousModel;
    m_instancePlaced = true;
}

bool StaticMeshRenderer::setStableObjectId(const renderer::contracts::StableObjectId objectId) {
    if (!objectId.isValid()) {
        return false;
    }
    m_objectId = objectId;
    return true;
}

void StaticMeshRenderer::prepareStandaloneFrame() {
    m_voxelLight = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    m_framePrepared = true;
}

void StaticMeshRenderer::assetBounds(glm::vec3& minimum, glm::vec3& maximum) const {
    minimum = m_assetBoundsMin;
    maximum = m_assetBoundsMax;
}

bool StaticMeshRenderer::appendPunctualLights(const glm::mat4& model, const glm::vec3& cameraPosition,
                                              const std::vector<renderer::contracts::StableLightId>& lightIds,
                                              std::vector<renderer::contracts::SceneLight>& lights,
                                              std::string& error) const {
    using namespace renderer::contracts;

    if (lightIds.size() != m_punctualLights.size()) {
        error = "static mesh punctual-light identity count does not match the asset definition";
        return false;
    }
    const std::size_t initialLightCount = lights.size();
    for (std::size_t index = 0u; index < m_punctualLights.size(); ++index) {
        const AnalyticLightInstantiationResult result =
            instantiateAnalyticLight(m_punctualLights[index], lightIds[index], model, cameraPosition);
        if (!result.succeeded()) {
            lights.resize(initialLightCount);
            error = "static mesh punctual-light instantiation failed [index=" + std::to_string(index) +
                    ", error=" + analyticLightInstantiationErrorStableId(result.error) +
                    ", normalization=" + gpuLightNormalizationErrorStableId(result.normalizationError) +
                    ", field=" + gpuLightFieldStableId(result.normalizationField) + "]";
            return false;
        }
        lights.push_back(result.sceneLight);
    }
    error.clear();
    return true;
}

bool StaticMeshRenderer::prepareGBuffer(RhiCommandList& commandList, const glm::mat4& viewProjection,
                                        const glm::mat4& previousViewProjection, const FrameContext& context) const {
    if (!m_framePrepared || !m_frameUniformBuffer.isValid()) {
        return false;
    }
    commandList.bufferBarrier({m_frameUniformBuffer, RhiResourceState::UniformBuffer, RhiResourceState::TransferDst});
    StaticMeshFrameParams frameParams;
    frameParams.voxelLight = m_voxelLight;
    frameParams.viewProjection = viewProjection;
    frameParams.previousViewProjection = previousViewProjection;
    frameParams.cameraPosition = glm::vec4(context.camera.position, 1.0f);
    const bool moonDominant = context.skyColors.moonVisibility > context.skyColors.sunVisibility;
    frameParams.sunDirection =
        glm::vec4(moonDominant ? context.skyColors.moonDirection : context.skyColors.sunDirection, 0.0f);
    frameParams.sunColor = glm::vec4(
        (moonDominant ? context.skyColors.moonLightColor : context.skyColors.sunLightColor) * context.skyIntensity,
        1.0f);
    frameParams.ambientColor = glm::vec4(context.skyColors.skyAmbientColor * context.skyIntensity, 0.0f);
    frameParams.fogColor = glm::vec4(context.fog.color, 0.0f);
    frameParams.fogParams = glm::vec4(context.fog.startDistance, context.fog.endDistance, context.fog.density,
                                      context.fog.enabled ? static_cast<float>(context.fog.mode + 1) : 0.0f);
    if (m_rhiDevice->backend() == RhiBackend::Vulkan) {
        if (!m_transparentClusterBindGroup.isValid() || m_transparentClusterGrid.clusterCount == 0u) {
            return false;
        }
        frameParams.clusterGrid = {m_transparentClusterGrid.tileCountX, m_transparentClusterGrid.tileCountY,
                                   m_transparentClusterGrid.depthSliceCount, renderer::contracts::kClusterTileWidth};
        frameParams.clusterDepth = {m_transparentClusterGrid.nearPlane, m_transparentClusterGrid.farPlane,
                                    m_transparentClusterGrid.depthLogScale, m_transparentClusterGrid.depthLogBias};
        frameParams.clusterRenderExtent = {m_transparentClusterGrid.renderWidth, m_transparentClusterGrid.renderHeight,
                                           0u, 0u};
    }
    commandList.updateBuffer(m_frameUniformBuffer, 0u, &frameParams, sizeof(frameParams));
    commandList.bufferBarrier({m_frameUniformBuffer, RhiResourceState::TransferDst, RhiResourceState::UniformBuffer});
    return true;
}

void StaticMeshRenderer::renderToGBuffer(RhiCommandList& commandList) const {
    const StaticMeshGBufferPushConstants pushConstants{m_modelMatrix, m_previousModelMatrix};
    for (const PrimitiveResource& primitive : m_primitives) {
        const MaterialResource& material = m_materials[primitive.materialIndex];
        if (material.alphaBlended || material.transmissive) {
            continue;
        }
        commandList.setGraphicsPipeline(material.doubleSided ? m_gbufferDoubleSidedPipeline : m_gbufferPipeline);
        commandList.setBindGroup(0u, material.bindGroup);
        commandList.setVertexBuffer(0u, primitive.vertexBuffer, 0u);
        commandList.setIndexBuffer(primitive.indexBuffer, RhiIndexFormat::Uint32, 0u);
        commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Vertex));
        commandList.drawIndexed(primitive.indexCount, 1u, 0u, 0, m_objectId.value);
    }
}

void StaticMeshRenderer::appendTransparentDraws(const glm::mat4& model, const glm::vec3& cameraPosition,
                                                std::vector<TransparentDraw>& draws) const {
    for (std::size_t primitiveIndex = 0u; primitiveIndex < m_primitives.size(); ++primitiveIndex) {
        const PrimitiveResource& primitive = m_primitives[primitiveIndex];
        if (!m_materials[primitive.materialIndex].forwardOpticalLayer) {
            continue;
        }
        const glm::vec3 worldCenter = glm::vec3(model * glm::vec4(primitive.boundsCenter, 1.0f));
        const glm::vec3 toCamera = worldCenter - cameraPosition;
        draws.push_back({primitiveIndex, model, glm::dot(toCamera, toCamera)});
    }
}

void StaticMeshRenderer::appendPreparedTransparentDraws(const glm::vec3& cameraPosition,
                                                        std::vector<TransparentDraw>& draws) const {
    appendTransparentDraws(m_modelMatrix, cameraPosition, draws);
}

bool StaticMeshRenderer::prepareTransparentResources(const RhiTextureViewHandle sceneColor,
                                                     const RhiTextureViewHandle opaqueDepth,
                                                     const RhiTextureViewHandle skyCapture) {
    const std::array<RhiTextureViewHandle, 3> views = {sceneColor, opaqueDepth, skyCapture};
    if (m_rhiDevice == nullptr || !m_transparentSceneBindGroupLayout.isValid() ||
        !m_transparentSceneLinearSampler.isValid() || !m_transparentSceneDepthSampler.isValid() ||
        std::any_of(views.begin(), views.end(), [](const RhiTextureViewHandle view) { return !view.isValid(); })) {
        setError("static mesh transparent reflection resources are invalid");
        return false;
    }
    const bool viewsMatch = std::equal(views.begin(), views.end(), m_transparentSceneViews.begin(),
                                       [](const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
                                           return lhs.index == rhs.index && lhs.generation == rhs.generation;
                                       });
    if (m_transparentSceneBindGroup.isValid() && viewsMatch) {
        return true;
    }
    if (m_transparentSceneBindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(m_transparentSceneBindGroup);
        m_transparentSceneBindGroup = {};
        m_transparentSceneViews = {};
    }

    const std::array<RhiSamplerHandle, 3> samplers = {m_transparentSceneLinearSampler, m_transparentSceneDepthSampler,
                                                      m_transparentSceneLinearSampler};
    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_transparentSceneBindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler = {views[binding], samplers[binding]};
        bindGroupDesc.entries.push_back(entry);
    }
    m_transparentSceneBindGroup = m_rhiDevice->createBindGroup(bindGroupDesc);
    if (!m_transparentSceneBindGroup.isValid()) {
        setError("failed to bind static mesh transparent reflection resources");
        return false;
    }
    m_transparentSceneViews = views;
    return true;
}

bool StaticMeshRenderer::configureClusteredLighting(const RhiBindGroupLayoutHandle bindGroupLayout,
                                                    const RhiBindGroupHandle bindGroup,
                                                    const renderer::contracts::ClusterGrid& grid) {
    if (m_rhiDevice == nullptr || m_rhiDevice->backend() != RhiBackend::Vulkan || !bindGroupLayout.isValid() ||
        !bindGroup.isValid() || grid.clusterCount == 0u || grid.tileCountX == 0u || grid.tileCountY == 0u ||
        grid.depthSliceCount != renderer::contracts::kClusterDepthSliceCount) {
        setError("static mesh clustered-light resources are invalid");
        return false;
    }
    if (!ensureTransparentPipelines(bindGroupLayout)) {
        setError("failed to create shared-layout static mesh Forward+ pipelines");
        return false;
    }
    m_transparentClusterBindGroup = bindGroup;
    m_transparentClusterGrid = grid;
    return true;
}

void StaticMeshRenderer::renderTransparentDraw(RhiCommandList& commandList, const TransparentDraw& draw,
                                               const float reflectionCompositeStrength) const {
    if (draw.primitiveIndex >= m_primitives.size()) {
        std::abort();
    }
    const PrimitiveResource& primitive = m_primitives[draw.primitiveIndex];
    const MaterialResource& material = m_materials[primitive.materialIndex];
    if (!material.forwardOpticalLayer) {
        std::abort();
    }
    if (!m_transparentSceneBindGroup.isValid()) {
        std::abort();
    }
    commandList.setGraphicsPipeline(material.doubleSided ? m_transparentDoubleSidedPipeline : m_transparentPipeline);
    commandList.setBindGroup(0u, material.bindGroup);
    commandList.setBindGroup(1u, m_transparentSceneBindGroup);
    if (m_rhiDevice->backend() == RhiBackend::Vulkan) {
        if (!m_transparentClusterBindGroup.isValid()) {
            std::abort();
        }
        commandList.setBindGroup(2u, m_transparentClusterBindGroup);
    }
    commandList.setVertexBuffer(0u, primitive.vertexBuffer, 0u);
    commandList.setIndexBuffer(primitive.indexBuffer, RhiIndexFormat::Uint32, 0u);
    const StaticMeshTransparentPushConstants pushConstants{
        draw.model, glm::vec4(std::clamp(reflectionCompositeStrength, 0.0f, 1.0f), 0.0f, 0.0f, 0.0f)};
    commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                              rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
    commandList.drawIndexed(primitive.indexCount, 1u, 0u, 0, 0u);
}

bool StaticMeshRenderer::hasTransparentPrimitives() const {
    return std::any_of(m_primitives.begin(), m_primitives.end(), [this](const PrimitiveResource& primitive) {
        return m_materials[primitive.materialIndex].forwardOpticalLayer;
    });
}

bool StaticMeshRenderer::rebuildReflectionProbeCaptureBindGroup() {
    if (m_rhiDevice == nullptr || !m_probeCaptureBindGroupLayout.isValid() ||
        !m_probeCaptureFrameUniformBuffer.isValid() || !m_probeCaptureLightBuffer.isValid() ||
        m_probeCaptureLightCapacity == 0u) {
        return false;
    }
    RhiBindGroupDesc desc;
    desc.layout = m_probeCaptureBindGroupLayout;
    RhiBindGroupEntry frameEntry;
    frameEntry.binding = 0u;
    frameEntry.resource.buffer = {m_probeCaptureFrameUniformBuffer, 0u, sizeof(StaticMeshProbeCaptureFrameParams)};
    desc.entries.push_back(frameEntry);
    RhiBindGroupEntry lightEntry;
    lightEntry.binding = 1u;
    lightEntry.resource.buffer = {m_probeCaptureLightBuffer, 0u,
                                  static_cast<uint64_t>(m_probeCaptureLightCapacity) *
                                      sizeof(renderer::contracts::GpuLight)};
    desc.entries.push_back(lightEntry);
    const RhiBindGroupHandle bindGroup = m_rhiDevice->createBindGroup(desc);
    if (!bindGroup.isValid()) {
        return false;
    }
    if (m_probeCaptureBindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(m_probeCaptureBindGroup);
    }
    m_probeCaptureBindGroup = bindGroup;
    return true;
}

bool StaticMeshRenderer::ensureReflectionProbeCaptureLightCapacity(const uint32_t lightCount) {
    if (m_rhiDevice == nullptr || lightCount > renderer::contracts::kClusterMaxLightCount) {
        return false;
    }
    const uint32_t requiredCapacity = std::max(lightCount, 1u);
    if (m_probeCaptureLightCapacity >= requiredCapacity && m_probeCaptureLightBuffer.isValid() &&
        m_probeCaptureBindGroup.isValid()) {
        return true;
    }
    RhiBufferDesc desc;
    desc.debugName = "StaticMesh.ProbeCapture.LightBuffer";
    desc.size = static_cast<uint64_t>(requiredCapacity) * sizeof(renderer::contracts::GpuLight);
    desc.usage = rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::TransferDst);
    desc.memoryUsage = RhiMemoryUsage::GpuOnly;
    desc.initialState = RhiResourceState::StorageBuffer;
    desc.memoryCategory = RhiMemoryCategory::SceneData;
    const RhiBufferHandle previousBuffer = m_probeCaptureLightBuffer;
    const uint32_t previousCapacity = m_probeCaptureLightCapacity;
    m_probeCaptureLightBuffer = m_rhiDevice->createBuffer(desc, nullptr, 0u);
    m_probeCaptureLightCapacity = requiredCapacity;
    if (!m_probeCaptureLightBuffer.isValid() || !rebuildReflectionProbeCaptureBindGroup()) {
        if (m_probeCaptureLightBuffer.isValid()) {
            m_rhiDevice->destroyBuffer(m_probeCaptureLightBuffer);
        }
        m_probeCaptureLightBuffer = previousBuffer;
        m_probeCaptureLightCapacity = previousCapacity;
        return false;
    }
    if (previousBuffer.isValid()) {
        m_rhiDevice->destroyBuffer(previousBuffer);
    }
    return true;
}

bool StaticMeshRenderer::prepareReflectionProbeCapture(RhiCommandList& commandList, const glm::mat4& viewProjection,
                                                       const glm::vec3& probePosition, const FrameContext& context,
                                                       const std::vector<renderer::contracts::SceneLight>& lights) {
    if (!m_framePrepared || !m_probeCaptureFrameUniformBuffer.isValid() || !m_probeCaptureLightBuffer.isValid() ||
        !m_probeCaptureBindGroup.isValid() || lights.size() > m_probeCaptureLightCapacity) {
        return false;
    }
    StaticMeshProbeCaptureFrameParams params;
    params.viewProjection = viewProjection;
    params.probePosition = glm::vec4(probePosition, 1.0f);
    const bool moonDominant = context.skyColors.moonVisibility > context.skyColors.sunVisibility;
    params.sunDirection =
        glm::vec4(moonDominant ? context.skyColors.moonDirection : context.skyColors.sunDirection, 0.0f);
    params.sunColor = glm::vec4((moonDominant ? context.skyColors.moonLightColor : context.skyColors.sunLightColor) *
                                    context.skyIntensity,
                                1.0f);
    params.ambientColor = glm::vec4(context.skyColors.skyAmbientColor * context.skyIntensity, 0.0f);
    params.lightCount.x = static_cast<uint32_t>(lights.size());
    commandList.bufferBarrier(
        {m_probeCaptureFrameUniformBuffer, RhiResourceState::UniformBuffer, RhiResourceState::TransferDst});
    commandList.updateBuffer(m_probeCaptureFrameUniformBuffer, 0u, &params, sizeof(params));
    commandList.bufferBarrier(
        {m_probeCaptureFrameUniformBuffer, RhiResourceState::TransferDst, RhiResourceState::UniformBuffer});
    if (!lights.empty()) {
        std::vector<renderer::contracts::GpuLight> gpuLights;
        gpuLights.reserve(lights.size());
        for (const renderer::contracts::SceneLight& light : lights) {
            gpuLights.push_back(light.light);
        }
        commandList.bufferBarrier(
            {m_probeCaptureLightBuffer, RhiResourceState::StorageBuffer, RhiResourceState::TransferDst});
        commandList.updateBuffer(m_probeCaptureLightBuffer, 0u, gpuLights.data(),
                                 gpuLights.size() * sizeof(gpuLights.front()));
        commandList.bufferBarrier(
            {m_probeCaptureLightBuffer, RhiResourceState::TransferDst, RhiResourceState::StorageBuffer});
    }
    return true;
}

void StaticMeshRenderer::renderReflectionProbeCaptureOpaque(RhiCommandList& commandList) const {
    if (!m_probeCaptureBindGroup.isValid()) {
        std::abort();
    }
    for (const PrimitiveResource& primitive : m_primitives) {
        const MaterialResource& material = m_materials[primitive.materialIndex];
        if (material.alphaBlended || material.transmissive) {
            continue;
        }
        commandList.setGraphicsPipeline(material.doubleSided ? m_probeCaptureDoubleSidedPipeline
                                                             : m_probeCapturePipeline);
        commandList.setBindGroup(0u, material.bindGroup);
        commandList.setBindGroup(1u, m_probeCaptureBindGroup);
        commandList.setVertexBuffer(0u, primitive.vertexBuffer, 0u);
        commandList.setIndexBuffer(primitive.indexBuffer, RhiIndexFormat::Uint32, 0u);
        commandList.pushConstants(&m_modelMatrix, sizeof(m_modelMatrix), rhiFlag(RhiShaderStage::Vertex));
        commandList.drawIndexed(primitive.indexCount, 1u, 0u, 0, 0u);
    }
}

void StaticMeshRenderer::renderReflectionProbeCaptureTransparent(RhiCommandList& commandList,
                                                                 const TransparentDraw& draw) const {
    if (draw.primitiveIndex >= m_primitives.size() || !m_probeCaptureBindGroup.isValid()) {
        std::abort();
    }
    const PrimitiveResource& primitive = m_primitives[draw.primitiveIndex];
    const MaterialResource& material = m_materials[primitive.materialIndex];
    if (!material.forwardOpticalLayer) {
        std::abort();
    }
    commandList.setGraphicsPipeline(material.doubleSided ? m_probeCaptureTransparentDoubleSidedPipeline
                                                         : m_probeCaptureTransparentPipeline);
    commandList.setBindGroup(0u, material.bindGroup);
    commandList.setBindGroup(1u, m_probeCaptureBindGroup);
    commandList.setVertexBuffer(0u, primitive.vertexBuffer, 0u);
    commandList.setIndexBuffer(primitive.indexBuffer, RhiIndexFormat::Uint32, 0u);
    commandList.pushConstants(&draw.model, sizeof(draw.model), rhiFlag(RhiShaderStage::Vertex));
    commandList.drawIndexed(primitive.indexCount, 1u, 0u, 0, 0u);
}

void StaticMeshRenderer::renderPreview(RhiCommandList& commandList, const glm::mat4& viewProj) const {
    if (!m_framePrepared) {
        return;
    }
    const StaticMeshPreviewPushConstants constants{viewProj, m_modelMatrix};
    const auto drawPrimitive = [this, &commandList, &constants](const PrimitiveResource& primitive,
                                                                const bool transparent) {
        const MaterialResource& material = m_materials[primitive.materialIndex];
        RhiPipelineHandle pipeline;
        if (transparent) {
            pipeline = material.doubleSided ? m_previewTransparentDoubleSidedPipeline : m_previewTransparentPipeline;
        } else {
            pipeline = material.doubleSided ? m_previewDoubleSidedPipeline : m_previewPipeline;
        }
        commandList.setGraphicsPipeline(pipeline);
        commandList.setBindGroup(0u, material.bindGroup);
        commandList.setVertexBuffer(0u, primitive.vertexBuffer, 0u);
        commandList.setIndexBuffer(primitive.indexBuffer, RhiIndexFormat::Uint32, 0u);
        commandList.pushConstants(&constants, sizeof(constants), rhiFlag(RhiShaderStage::Vertex));
        commandList.drawIndexed(primitive.indexCount, 1u, 0u, 0, 0u);
    };
    for (const PrimitiveResource& primitive : m_primitives) {
        const MaterialResource& material = m_materials[primitive.materialIndex];
        if (material.alphaBlended || material.transmissive) {
            continue;
        }
        drawPrimitive(primitive, false);
    }
    for (const PrimitiveResource& primitive : m_primitives) {
        const MaterialResource& material = m_materials[primitive.materialIndex];
        if (!material.alphaBlended && !material.transmissive) {
            continue;
        }
        drawPrimitive(primitive, true);
    }
}

void StaticMeshRenderer::renderToShadowMap(RhiCommandList& commandList, const glm::mat4& shadowViewProj) const {
    if (!m_framePrepared) {
        return;
    }
    const glm::mat4 modelViewProj = shadowViewProj * m_modelMatrix;
    for (const PrimitiveResource& primitive : m_primitives) {
        const MaterialResource& material = m_materials[primitive.materialIndex];
        if (material.alphaBlended || material.transmissive) {
            continue;
        }
        commandList.setGraphicsPipeline(material.doubleSided ? m_shadowDoubleSidedPipeline : m_shadowPipeline);
        commandList.setBindGroup(0u, material.bindGroup);
        commandList.setVertexBuffer(0u, primitive.vertexBuffer, 0u);
        commandList.setIndexBuffer(primitive.indexBuffer, RhiIndexFormat::Uint32, 0u);
        commandList.pushConstants(&modelViewProj, sizeof(modelViewProj), rhiFlag(RhiShaderStage::Vertex));
        commandList.drawIndexed(primitive.indexCount, 1u, 0u, 0, 0u);
    }
}

void StaticMeshRenderer::shutdown() {
    if (m_rhiDevice != nullptr) {
        for (PrimitiveResource& primitive : m_primitives) {
            if (!primitive.retainedByBlas && primitive.indexBuffer.isValid()) {
                m_rhiDevice->destroyBuffer(primitive.indexBuffer);
            }
            if (!primitive.retainedByBlas && primitive.vertexBuffer.isValid()) {
                m_rhiDevice->destroyBuffer(primitive.vertexBuffer);
            }
        }
        for (MaterialResource& material : m_materials) {
            if (material.bindGroup.isValid()) {
                m_rhiDevice->destroyBindGroup(material.bindGroup);
            }
            if (material.uniformBuffer.isValid()) {
                m_rhiDevice->destroyBuffer(material.uniformBuffer);
            }
        }
        for (TextureResource& texture : m_textures) {
            if (texture.view.isValid()) {
                m_rhiDevice->destroyTextureView(texture.view);
            }
            if (texture.texture.isValid()) {
                m_rhiDevice->destroyTexture(texture.texture);
            }
        }
        for (const RhiSamplerHandle sampler : m_samplers) {
            if (sampler.isValid()) {
                m_rhiDevice->destroySampler(sampler);
            }
        }
        destroyPipelineResources();
    }
    m_staticBlasCache.shutdown();
    m_primitives.clear();
    m_materials.clear();
    m_textures.clear();
    m_samplers.clear();
    m_punctualLights.clear();
    m_rhiDevice = nullptr;
    m_assetBoundsMin = glm::vec3(0.0f);
    m_assetBoundsMax = glm::vec3(0.0f);
    m_modelMatrix = glm::mat4(1.0f);
    m_previousModelMatrix = glm::mat4(1.0f);
    m_voxelLight = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    m_objectId = {};
    m_instancePlaced = false;
    m_framePrepared = false;
    m_lastError.clear();
}

void StaticMeshRenderer::destroyPipelineResources() {
    if (m_transparentSceneBindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(m_transparentSceneBindGroup);
    }
    if (m_probeCaptureBindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(m_probeCaptureBindGroup);
    }
    destroyTransparentPipelines();
    if (m_probeCaptureTransparentDoubleSidedPipeline.isValid()) {
        m_rhiDevice->destroyPipeline(m_probeCaptureTransparentDoubleSidedPipeline);
    }
    if (m_probeCaptureTransparentPipeline.isValid()) {
        m_rhiDevice->destroyPipeline(m_probeCaptureTransparentPipeline);
    }
    if (m_probeCaptureDoubleSidedPipeline.isValid()) {
        m_rhiDevice->destroyPipeline(m_probeCaptureDoubleSidedPipeline);
    }
    if (m_probeCapturePipeline.isValid()) {
        m_rhiDevice->destroyPipeline(m_probeCapturePipeline);
    }
    if (m_previewDoubleSidedPipeline.isValid()) {
        m_rhiDevice->destroyPipeline(m_previewDoubleSidedPipeline);
    }
    if (m_previewTransparentDoubleSidedPipeline.isValid()) {
        m_rhiDevice->destroyPipeline(m_previewTransparentDoubleSidedPipeline);
    }
    if (m_previewTransparentPipeline.isValid()) {
        m_rhiDevice->destroyPipeline(m_previewTransparentPipeline);
    }
    if (m_previewPipeline.isValid()) {
        m_rhiDevice->destroyPipeline(m_previewPipeline);
    }
    if (m_shadowDoubleSidedPipeline.isValid()) {
        m_rhiDevice->destroyPipeline(m_shadowDoubleSidedPipeline);
    }
    if (m_shadowPipeline.isValid()) {
        m_rhiDevice->destroyPipeline(m_shadowPipeline);
    }
    if (m_gbufferDoubleSidedPipeline.isValid()) {
        m_rhiDevice->destroyPipeline(m_gbufferDoubleSidedPipeline);
    }
    if (m_gbufferPipeline.isValid()) {
        m_rhiDevice->destroyPipeline(m_gbufferPipeline);
    }
    if (m_probeCapturePipelineLayout.isValid()) {
        m_rhiDevice->destroyPipelineLayout(m_probeCapturePipelineLayout);
    }
    if (m_shadowPipelineLayout.isValid()) {
        m_rhiDevice->destroyPipelineLayout(m_shadowPipelineLayout);
    }
    if (m_previewPipelineLayout.isValid()) {
        m_rhiDevice->destroyPipelineLayout(m_previewPipelineLayout);
    }
    if (m_gbufferPipelineLayout.isValid()) {
        m_rhiDevice->destroyPipelineLayout(m_gbufferPipelineLayout);
    }
    if (m_bindGroupLayout.isValid()) {
        m_rhiDevice->destroyBindGroupLayout(m_bindGroupLayout);
    }
    if (m_transparentSceneBindGroupLayout.isValid()) {
        m_rhiDevice->destroyBindGroupLayout(m_transparentSceneBindGroupLayout);
    }
    if (m_probeCaptureBindGroupLayout.isValid()) {
        m_rhiDevice->destroyBindGroupLayout(m_probeCaptureBindGroupLayout);
    }
    if (m_transparentSceneDepthSampler.isValid()) {
        m_rhiDevice->destroySampler(m_transparentSceneDepthSampler);
    }
    if (m_transparentSceneLinearSampler.isValid()) {
        m_rhiDevice->destroySampler(m_transparentSceneLinearSampler);
    }
    if (m_shadowFragmentShader.isValid()) {
        m_rhiDevice->destroyShader(m_shadowFragmentShader);
    }
    if (m_probeCaptureFragmentShader.isValid()) {
        m_rhiDevice->destroyShader(m_probeCaptureFragmentShader);
    }
    if (m_probeCaptureVertexShader.isValid()) {
        m_rhiDevice->destroyShader(m_probeCaptureVertexShader);
    }
    if (m_transparentFragmentShader.isValid()) {
        m_rhiDevice->destroyShader(m_transparentFragmentShader);
    }
    if (m_transparentVertexShader.isValid()) {
        m_rhiDevice->destroyShader(m_transparentVertexShader);
    }
    if (m_shadowVertexShader.isValid()) {
        m_rhiDevice->destroyShader(m_shadowVertexShader);
    }
    if (m_previewFragmentShader.isValid()) {
        m_rhiDevice->destroyShader(m_previewFragmentShader);
    }
    if (m_previewVertexShader.isValid()) {
        m_rhiDevice->destroyShader(m_previewVertexShader);
    }
    if (m_gbufferFragmentShader.isValid()) {
        m_rhiDevice->destroyShader(m_gbufferFragmentShader);
    }
    if (m_gbufferVertexShader.isValid()) {
        m_rhiDevice->destroyShader(m_gbufferVertexShader);
    }
    if (m_frameUniformBuffer.isValid()) {
        m_rhiDevice->destroyBuffer(m_frameUniformBuffer);
    }
    if (m_probeCaptureFrameUniformBuffer.isValid()) {
        m_rhiDevice->destroyBuffer(m_probeCaptureFrameUniformBuffer);
    }
    if (m_probeCaptureLightBuffer.isValid()) {
        m_rhiDevice->destroyBuffer(m_probeCaptureLightBuffer);
    }
    m_probeCaptureTransparentDoubleSidedPipeline = {};
    m_probeCaptureTransparentPipeline = {};
    m_probeCaptureDoubleSidedPipeline = {};
    m_probeCapturePipeline = {};
    m_shadowDoubleSidedPipeline = {};
    m_shadowPipeline = {};
    m_transparentDoubleSidedPipeline = {};
    m_transparentPipeline = {};
    m_gbufferDoubleSidedPipeline = {};
    m_gbufferPipeline = {};
    m_previewDoubleSidedPipeline = {};
    m_previewPipeline = {};
    m_previewTransparentDoubleSidedPipeline = {};
    m_previewTransparentPipeline = {};
    m_probeCapturePipelineLayout = {};
    m_shadowPipelineLayout = {};
    m_transparentPipelineLayout = {};
    m_gbufferPipelineLayout = {};
    m_previewPipelineLayout = {};
    m_bindGroupLayout = {};
    m_transparentSceneBindGroupLayout = {};
    m_transparentClusterBindGroupLayout = {};
    m_probeCaptureBindGroupLayout = {};
    m_transparentSceneBindGroup = {};
    m_transparentClusterBindGroup = {};
    m_probeCaptureBindGroup = {};
    m_transparentSceneLinearSampler = {};
    m_transparentSceneDepthSampler = {};
    m_transparentSceneViews = {};
    m_transparentClusterGrid = {};
    m_shadowFragmentShader = {};
    m_shadowVertexShader = {};
    m_transparentFragmentShader = {};
    m_transparentVertexShader = {};
    m_probeCaptureFragmentShader = {};
    m_probeCaptureVertexShader = {};
    m_gbufferFragmentShader = {};
    m_gbufferVertexShader = {};
    m_previewFragmentShader = {};
    m_previewVertexShader = {};
    m_frameUniformBuffer = {};
    m_probeCaptureFrameUniformBuffer = {};
    m_probeCaptureLightBuffer = {};
    m_probeCaptureLightCapacity = 0u;
}

void StaticMeshRenderer::destroyTransparentPipelines() {
    if (m_rhiDevice != nullptr) {
        if (m_transparentDoubleSidedPipeline.isValid()) {
            m_rhiDevice->destroyPipeline(m_transparentDoubleSidedPipeline);
        }
        if (m_transparentPipeline.isValid()) {
            m_rhiDevice->destroyPipeline(m_transparentPipeline);
        }
        if (m_transparentPipelineLayout.isValid()) {
            m_rhiDevice->destroyPipelineLayout(m_transparentPipelineLayout);
        }
    }
    m_transparentDoubleSidedPipeline = {};
    m_transparentPipeline = {};
    m_transparentPipelineLayout = {};
    m_transparentClusterBindGroupLayout = {};
}
