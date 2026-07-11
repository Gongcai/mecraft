#include "BlockEntityRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include "../core/Shader.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../../resource/ResourceMgr.h"
#include "../../world/IWorldView.h"
#include "../../world/block/Block.h"
#include "../../world/block/BlockStateRegistry.h"
#include "../../world/block/PropIndices.h"
#include "../../world/chunk/Chunk.h"
#include "../../world/chunk/SubChunk.h"

namespace {

[[noreturn]] void failBlockEntityRenderer(const std::string& message) {
    std::cerr << message << '\n';
    std::abort();
}

constexpr unsigned int kQuadIndices[] = {0, 1, 2, 0, 2, 3};
constexpr float kPixel = 1.0f / 16.0f;

struct BlockEntityVertex {
    float x, y, z;
    float u, v;
    float nx, ny, nz;
};

struct FaceUvRect {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
};

constexpr std::array<glm::vec3, 6> kFaceNormals = {{
    {0.0f, 1.0f, 0.0f},
    {0.0f, -1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, -1.0f},
    {-1.0f, 0.0f, 0.0f},
    {1.0f, 0.0f, 0.0f}
}};

FaceUvRect pixelRectToUv(const float x0,
                         const float y0,
                         const float x1,
                         const float y1,
                         const float textureWidth,
                         const float textureHeight) {
    return {
        x0 / textureWidth,
        1.0f - y1 / textureHeight,
        x1 / textureWidth,
        1.0f - y0 / textureHeight
    };
}

std::array<FaceUvRect, 6> buildBoxUvs(const BlockEntityRenderer::CuboidDefinition& cuboid,
                                      const float textureWidth,
                                      const float textureHeight) {
    const float width = cuboid.toPixels.x - cuboid.fromPixels.x;
    const float height = cuboid.toPixels.y - cuboid.fromPixels.y;
    const float depth = cuboid.toPixels.z - cuboid.fromPixels.z;
    const float u = cuboid.textureU;
    const float v = cuboid.textureV;

    return {{
        pixelRectToUv(u + depth + width, v, u + depth + width + width, v + depth, textureWidth, textureHeight),
        pixelRectToUv(u + depth, v, u + depth + width, v + depth, textureWidth, textureHeight),
        pixelRectToUv(u + depth, v + depth, u + depth + width, v + depth + height, textureWidth, textureHeight),
        pixelRectToUv(u + depth + width + depth, v + depth, u + depth + width + depth + width, v + depth + height, textureWidth, textureHeight),
        pixelRectToUv(u, v + depth, u + depth, v + depth + height, textureWidth, textureHeight),
        pixelRectToUv(u + depth + width, v + depth, u + depth + width + depth, v + depth + height, textureWidth, textureHeight)
    }};
}

void appendCuboidVertices(std::vector<BlockEntityVertex>& vertices,
                          const BlockEntityRenderer::CuboidDefinition& cuboid,
                          const float textureWidth,
                          const float textureHeight) {
    const std::array<FaceUvRect, 6> uv = buildBoxUvs(cuboid, textureWidth, textureHeight);

    const float xmin = cuboid.fromPixels.x * kPixel;
    const float ymin = cuboid.fromPixels.y * kPixel;
    const float zmin = cuboid.fromPixels.z * kPixel;
    const float xmax = cuboid.toPixels.x * kPixel;
    const float ymax = cuboid.toPixels.y * kPixel;
    const float zmax = cuboid.toPixels.z * kPixel;

    struct FaceCorners {
        glm::vec3 pos[4];
    };

    const FaceCorners faces[6] = {
        {{{xmin, ymax, zmax}, {xmax, ymax, zmax}, {xmax, ymax, zmin}, {xmin, ymax, zmin}}},
        {{{xmin, ymin, zmin}, {xmax, ymin, zmin}, {xmax, ymin, zmax}, {xmin, ymin, zmax}}},
        {{{xmin, ymin, zmax}, {xmax, ymin, zmax}, {xmax, ymax, zmax}, {xmin, ymax, zmax}}},
        {{{xmax, ymin, zmin}, {xmin, ymin, zmin}, {xmin, ymax, zmin}, {xmax, ymax, zmin}}},
        {{{xmin, ymin, zmin}, {xmin, ymin, zmax}, {xmin, ymax, zmax}, {xmin, ymax, zmin}}},
        {{{xmax, ymin, zmax}, {xmax, ymin, zmin}, {xmax, ymax, zmin}, {xmax, ymax, zmax}}}
    };

    for (int face = 0; face < 6; ++face) {
        const glm::vec2 faceUvs[4] = {
            {uv[face].u0, uv[face].v0},
            {uv[face].u1, uv[face].v0},
            {uv[face].u1, uv[face].v1},
            {uv[face].u0, uv[face].v1}
        };

        for (const int idx : kQuadIndices) {
            const glm::vec3& p = faces[face].pos[idx];
            const glm::vec2& t = faceUvs[idx];
            const glm::vec3& n = kFaceNormals[static_cast<std::size_t>(face)];
            vertices.push_back({p.x, p.y, p.z, t.x, t.y, n.x, n.y, n.z});
        }
    }
}

float chestYawRadians(const BlockStateId stateId) {
    if (PropIndices::FACING == PropIndices::INVALID) {
        failBlockEntityRenderer("Chest block entity rendering requires the facing property index");
    }

    const uint16_t facing = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::FACING);
    if (facing == PropIndices::FACING_SOUTH) {
        return 0.0f;
    }
    if (facing == PropIndices::FACING_NORTH) {
        return glm::radians(180.0f);
    }
    if (facing == PropIndices::FACING_EAST) {
        return glm::radians(90.0f);
    }
    if (facing == PropIndices::FACING_WEST) {
        return glm::radians(-90.0f);
    }

    failBlockEntityRenderer("Chest block entity state has an unsupported facing value");
}

} // namespace

void BlockEntityRenderer::init(ResourceMgr& resourceMgr) {
    m_resourceMgr = &resourceMgr;
    m_rhiDevice = &resourceMgr.rhiDevice();
    m_gbufferShader = resourceMgr.getShader("block_entity_gbuffer");
    m_shadowShader = resourceMgr.getShader("block_entity_shadow");
    m_forwardShader = resourceMgr.getShader("steve_forward");
    glGenBuffers(1, &m_instanceVbo);
    if (!m_rhiInstanceBuffer.init(*m_rhiDevice,
                                  256u * sizeof(InstancedDrawData),
                                  rhiFlag(RhiBufferUsage::Vertex),
                                  "BlockEntity.InstanceBuffer")) {
        failBlockEntityRenderer("Failed to create block entity RHI instance buffer");
    }
    createGBufferRhiResources();

    const BlockID chestBlock = BlockRegistry::requireIdByName("minecraft:chest");

    ModelDefinition chest = makeChestDefinition();
    const RhiTextureHandle chestTexture = resourceMgr.getGuiTextureHandle(chest.textureKey);
    if (renderer::rhi::gl::textureId(chestTexture) == 0) {
        failBlockEntityRenderer("Chest block entity texture is not loaded");
    }

    ModelEntry entry;
    entry.mesh = buildMesh(chest);
    entry.texture = chestTexture;
    RhiTextureViewDesc textureViewDesc;
    textureViewDesc.texture = chestTexture;
    textureViewDesc.viewType = RhiTextureViewType::Texture2D;
    entry.textureView = m_rhiDevice->createTextureView(textureViewDesc);
    if (!entry.textureView.isValid()) {
        failBlockEntityRenderer("Failed to create block entity texture view");
    }
    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_gbufferBindGroupLayout;
    RhiBindGroupEntry textureEntry;
    textureEntry.binding = 0u;
    textureEntry.resource.combinedTextureSampler = {entry.textureView, m_rhiSampler};
    bindGroupDesc.entries.push_back(textureEntry);
    entry.gbufferBindGroup = m_rhiDevice->createBindGroup(bindGroupDesc);
    if (!entry.gbufferBindGroup.isValid()) {
        failBlockEntityRenderer("Failed to create block entity GBuffer bind group");
    }
    entry.usesHorizontalFacing = chest.usesHorizontalFacing;
    auto [it, inserted] = m_models.emplace(chestBlock, entry);
    static_cast<void>(inserted);
    configureInstanceAttributes(it->second.mesh);
}

void BlockEntityRenderer::shutdown() {
    for (auto& pair : m_models) {
        if (pair.second.gbufferBindGroup.isValid()) {
            m_rhiDevice->destroyBindGroup(pair.second.gbufferBindGroup);
        }
        if (pair.second.textureView.isValid()) {
            m_rhiDevice->destroyTextureView(pair.second.textureView);
        }
        destroyMesh(pair.second.mesh);
    }
    m_models.clear();
    destroyGBufferRhiResources();
    m_rhiInstanceBuffer.shutdown();
    m_sectionCaches.clear();
    m_flatInstances.clear();
    m_cacheSyncSerial = 0;
    m_syncedActiveChunkRevision = 0;
    m_syncedBlockContentRevision = 0;
    if (m_instanceVbo != 0) {
        glDeleteBuffers(1, &m_instanceVbo);
        m_instanceVbo = 0;
    }
    m_instanceCapacity = 0;
    m_instanceData.clear();
    m_gbufferBatches.clear();
    m_hasSyncedRevisions = false;
    m_instanceCacheSyncedThisFrame = false;
    m_instanceLightsSyncedThisFrame = false;
    m_resourceMgr = nullptr;
    m_rhiDevice = nullptr;
    m_gbufferShader = nullptr;
    m_shadowShader = nullptr;
    m_forwardShader = nullptr;
}

void BlockEntityRenderer::beginFrame() {
    m_instanceCacheSyncedThisFrame = false;
    m_instanceLightsSyncedThisFrame = false;
}

void BlockEntityRenderer::prepareFrame(const IWorldView& worldView) {
    synchronizeInstanceCache(worldView);
    updateInstanceLightsForFrame();
}

bool BlockEntityRenderer::prepareGBuffer(RhiCommandList& commandList) {
    m_instanceData.clear();
    m_gbufferBatches.clear();
    for (const auto& modelPair : m_models) {
        const ModelEntry& model = modelPair.second;
        const uint64_t instanceOffset = m_instanceData.size() * sizeof(InstancedDrawData);
        for (const BlockEntityInstance* instance : m_flatInstances) {
            if (instance->model == &model) {
                m_instanceData.push_back({instance->modelMatrix, instance->light});
            }
        }
        const uint64_t instanceCount =
            (m_instanceData.size() * sizeof(InstancedDrawData) - instanceOffset) /
            sizeof(InstancedDrawData);
        if (instanceCount != 0u) {
            m_gbufferBatches.push_back({&model, instanceOffset, static_cast<uint32_t>(instanceCount)});
        }
    }
    if (m_instanceData.empty()) {
        return true;
    }
    return m_rhiInstanceBuffer.write(commandList, 0u, m_instanceData.data(),
                                     m_instanceData.size() * sizeof(InstancedDrawData));
}

void BlockEntityRenderer::createGBufferRhiResources() {
    const auto vertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/block_entity_gbuffer_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/block_entity_gbuffer_rhi.frag");
    if (!vertexSource || !fragmentSource) {
        failBlockEntityRenderer("Failed to load block entity RHI shaders");
    }
    auto createShader = [this](const char* name, const RhiShaderStage stage,
                               const std::string& source) {
        RhiShaderDesc desc;
        desc.debugName = name;
        desc.stage = stage;
        desc.source = source.c_str();
        desc.sourceSize = source.size();
        return m_rhiDevice->createShader(desc);
    };
    m_gbufferVertexShader = createShader("BlockEntity.GBuffer.Vertex", RhiShaderStage::Vertex,
                                         *vertexSource);
    m_gbufferFragmentShader = createShader("BlockEntity.GBuffer.Fragment", RhiShaderStage::Fragment,
                                           *fragmentSource);
    RhiSamplerDesc samplerDesc;
    samplerDesc.addressU = RhiAddressMode::ClampToEdge;
    samplerDesc.addressV = RhiAddressMode::ClampToEdge;
    m_rhiSampler = m_rhiDevice->createSampler(samplerDesc);

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "BlockEntity.GBuffer.BindGroupLayout";
    bindGroupLayoutDesc.entries.push_back({0u, RhiBindingType::CombinedTextureSampler,
                                           rhiFlag(RhiShaderStage::Fragment), 1u});
    m_gbufferBindGroupLayout = m_rhiDevice->createBindGroupLayout(bindGroupLayoutDesc);
    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "BlockEntity.GBuffer.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_gbufferBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(glm::mat4);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex);
    m_gbufferPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "BlockEntity.GBuffer.Pipeline";
    pipelineDesc.vertexShader = m_gbufferVertexShader;
    pipelineDesc.fragmentShader = m_gbufferFragmentShader;
    pipelineDesc.layout = m_gbufferPipelineLayout;
    pipelineDesc.vertexInput.bindings = {
        {0u, sizeof(BlockEntityVertex), RhiVertexInputRate::Vertex},
        {1u, sizeof(InstancedDrawData), RhiVertexInputRate::Instance}
    };
    pipelineDesc.vertexInput.attributes = {
        {0u, 0u, RhiVertexFormat::Float3, offsetof(BlockEntityVertex, x)},
        {1u, 0u, RhiVertexFormat::Float2, offsetof(BlockEntityVertex, u)},
        {2u, 0u, RhiVertexFormat::Float3, offsetof(BlockEntityVertex, nx)},
        {3u, 1u, RhiVertexFormat::Float4, offsetof(InstancedDrawData, modelMatrix)},
        {4u, 1u, RhiVertexFormat::Float4, offsetof(InstancedDrawData, modelMatrix) + sizeof(glm::vec4)},
        {5u, 1u, RhiVertexFormat::Float4, offsetof(InstancedDrawData, modelMatrix) + sizeof(glm::vec4) * 2u},
        {6u, 1u, RhiVertexFormat::Float4, offsetof(InstancedDrawData, modelMatrix) + sizeof(glm::vec4) * 3u},
        {7u, 1u, RhiVertexFormat::Float2, offsetof(InstancedDrawData, light)}
    };
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.colorFormats = {RhiTextureFormat::Rgba8Unorm, RhiTextureFormat::Rgba16Float,
        RhiTextureFormat::Rg8Unorm, RhiTextureFormat::Rgba8Unorm, RhiTextureFormat::Rgba8Unorm,
        RhiTextureFormat::Rg16Float};
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    pipelineDesc.blend.attachments.resize(6u);
    m_gbufferPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    if (!m_gbufferVertexShader.isValid() || !m_gbufferFragmentShader.isValid() ||
        !m_rhiSampler.isValid() || !m_gbufferBindGroupLayout.isValid() ||
        !m_gbufferPipelineLayout.isValid() || !m_gbufferPipeline.isValid()) {
        failBlockEntityRenderer("Failed to create block entity GBuffer RHI resources");
    }
}

void BlockEntityRenderer::destroyGBufferRhiResources() {
    if (m_gbufferPipeline.isValid()) m_rhiDevice->destroyPipeline(m_gbufferPipeline);
    if (m_gbufferPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_gbufferPipelineLayout);
    if (m_gbufferBindGroupLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_gbufferBindGroupLayout);
    if (m_rhiSampler.isValid()) m_rhiDevice->destroySampler(m_rhiSampler);
    if (m_gbufferFragmentShader.isValid()) m_rhiDevice->destroyShader(m_gbufferFragmentShader);
    if (m_gbufferVertexShader.isValid()) m_rhiDevice->destroyShader(m_gbufferVertexShader);
    m_gbufferPipeline = {};
    m_gbufferPipelineLayout = {};
    m_gbufferBindGroupLayout = {};
    m_rhiSampler = {};
    m_gbufferFragmentShader = {};
    m_gbufferVertexShader = {};
}

BlockEntityRenderer::ModelDefinition BlockEntityRenderer::makeChestDefinition() {
    ModelDefinition definition;
    definition.textureKey = "chest";
    definition.textureWidth = 64.0f;
    definition.textureHeight = 64.0f;
    definition.usesHorizontalFacing = true;
    definition.cuboids = {
        {{1.0f, 0.0f, 1.0f}, {15.0f, 10.0f, 15.0f}, 0.0f, 19.0f},
        {{1.0f, 9.0f, 1.0f}, {15.0f, 14.0f, 15.0f}, 0.0f, 0.0f},
        {{7.0f, 7.0f, 15.0f}, {9.0f, 11.0f, 16.0f}, 0.0f, 0.0f}
    };
    return definition;
}

BlockEntityRenderer::Mesh BlockEntityRenderer::buildMesh(const ModelDefinition& definition) {
    std::vector<BlockEntityVertex> vertices;
    vertices.reserve(definition.cuboids.size() * 36);
    for (const CuboidDefinition& cuboid : definition.cuboids) {
        appendCuboidVertices(vertices, cuboid, definition.textureWidth, definition.textureHeight);
    }

    Mesh mesh;
    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    RhiBufferDesc rhiBufferDesc;
    rhiBufferDesc.debugName = "BlockEntity.MeshVertexBuffer";
    rhiBufferDesc.size = vertices.size() * sizeof(BlockEntityVertex);
    rhiBufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex);
    rhiBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    mesh.rhiVertexBuffer = m_rhiDevice->createBuffer(
        rhiBufferDesc, vertices.data(), vertices.size() * sizeof(BlockEntityVertex));
    if (!mesh.rhiVertexBuffer.isValid()) {
        failBlockEntityRenderer("Failed to create block entity RHI mesh buffer");
    }

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);

    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(BlockEntityVertex)),
                 vertices.data(),
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BlockEntityVertex),
                          reinterpret_cast<void*>(offsetof(BlockEntityVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BlockEntityVertex),
                          reinterpret_cast<void*>(offsetof(BlockEntityVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(BlockEntityVertex),
                          reinterpret_cast<void*>(offsetof(BlockEntityVertex, nx)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    return mesh;
}

void BlockEntityRenderer::destroyMesh(Mesh& mesh) {
    if (m_rhiDevice != nullptr && mesh.rhiVertexBuffer.isValid()) {
        m_rhiDevice->destroyBuffer(mesh.rhiVertexBuffer);
        mesh.rhiVertexBuffer = {};
    }
    if (mesh.vbo != 0) {
        glDeleteBuffers(1, &mesh.vbo);
        mesh.vbo = 0;
    }
    if (mesh.vao != 0) {
        glDeleteVertexArrays(1, &mesh.vao);
        mesh.vao = 0;
    }
    mesh.vertexCount = 0;
}

void BlockEntityRenderer::configureInstanceAttributes(const Mesh& mesh) const {
    if (mesh.vao == 0 || m_instanceVbo == 0) {
        return;
    }

    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);

    constexpr GLuint kModelLocation = 3;
    for (GLuint column = 0; column < 4; ++column) {
        const GLuint attrib = kModelLocation + column;
        glEnableVertexAttribArray(attrib);
        glVertexAttribPointer(attrib,
                              4,
                              GL_FLOAT,
                              GL_FALSE,
                              sizeof(InstancedDrawData),
                              reinterpret_cast<void*>(offsetof(InstancedDrawData, modelMatrix) +
                                                       sizeof(glm::vec4) * column));
        glVertexAttribDivisor(attrib, 1);
    }

    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7,
                          2,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(InstancedDrawData),
                          reinterpret_cast<void*>(offsetof(InstancedDrawData, light)));
    glVertexAttribDivisor(7, 1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void BlockEntityRenderer::ensureInstanceCapacity(const std::size_t instanceCount) {
    if (instanceCount <= m_instanceCapacity) {
        return;
    }

    std::size_t newCapacity = m_instanceCapacity == 0 ? static_cast<std::size_t>(256) : m_instanceCapacity * 2;
    while (newCapacity < instanceCount) {
        newCapacity *= 2;
    }

    glNamedBufferData(m_instanceVbo,
                      static_cast<GLsizeiptr>(newCapacity * sizeof(InstancedDrawData)),
                      nullptr,
                      GL_DYNAMIC_DRAW);
    m_instanceCapacity = newCapacity;
}

glm::mat4 BlockEntityRenderer::buildModelMatrix(const ModelEntry& entry,
                                                const BlockStateId stateId,
                                                const glm::vec3& blockPosition) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), blockPosition);
    if (entry.usesHorizontalFacing) {
        model = model
            * glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.0f, 0.5f))
            * glm::rotate(glm::mat4(1.0f), chestYawRadians(stateId), glm::vec3(0.0f, 1.0f, 0.0f))
            * glm::translate(glm::mat4(1.0f), glm::vec3(-0.5f, 0.0f, -0.5f));
    }
    return model;
}

void BlockEntityRenderer::rebuildSectionCache(const Chunk& chunk,
                                              const SubChunk& subChunk,
                                              const int scy,
                                              SectionCache& cache) const {
    cache.instances.clear();

    const glm::ivec3 chunkOffset = chunk.getWorldOffset();
    const int yBase = scy * SubChunk::SIZE;
    for (int ly = 0; ly < SubChunk::SIZE; ++ly) {
        const int columnY = yBase + ly;
        for (int lz = 0; lz < SubChunk::SIZE; ++lz) {
            for (int lx = 0; lx < SubChunk::SIZE; ++lx) {
                const BlockStateId stateId = subChunk.getBlockUnchecked(lx, ly, lz);
                if (stateId == NULL_BLOCK_STATE) {
                    continue;
                }

                const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
                const auto modelIt = m_models.find(blockId);
                if (modelIt == m_models.end()) {
                    continue;
                }

                const glm::vec3 blockPosition(static_cast<float>(chunkOffset.x + lx),
                                              static_cast<float>(columnY),
                                              static_cast<float>(chunkOffset.z + lz));
                BlockEntityInstance instance;
                instance.chunk = &chunk;
                instance.model = &modelIt->second;
                instance.blockId = blockId;
                instance.stateId = stateId;
                instance.localX = lx;
                instance.columnY = columnY;
                instance.localZ = lz;
                instance.blockPosition = blockPosition;
                instance.center = blockPosition + glm::vec3(0.5f, 0.5f, 0.5f);
                instance.modelMatrix = buildModelMatrix(*instance.model, instance.stateId, instance.blockPosition);
                cache.instances.push_back(instance);
            }
        }
    }
}

void BlockEntityRenderer::rebuildFlatInstanceList() {
    m_flatInstances.clear();
    for (auto& cachePair : m_sectionCaches) {
        SectionCache& cache = cachePair.second;
        for (BlockEntityInstance& instance : cache.instances) {
            m_flatInstances.push_back(&instance);
        }
    }
}

void BlockEntityRenderer::updateInstanceLightsForFrame() {
    if (m_instanceLightsSyncedThisFrame) {
        return;
    }
    m_instanceLightsSyncedThisFrame = true;

    for (BlockEntityInstance* instance : m_flatInstances) {
        const uint8_t packedLight = instance->chunk->getPackedLight(instance->localX,
                                                                    instance->columnY,
                                                                    instance->localZ);
        instance->light = glm::vec2(
            static_cast<float>((packedLight >> 4) & 0x0F) / 15.0f,
            static_cast<float>(packedLight & 0x0F) / 15.0f);
    }
}

void BlockEntityRenderer::synchronizeInstanceCache(const IWorldView& worldView) {
    if (m_instanceCacheSyncedThisFrame) {
        return;
    }
    m_instanceCacheSyncedThisFrame = true;

    const uint64_t activeChunkRevision = worldView.getActiveChunkRevision();
    const uint64_t blockContentRevision = worldView.getBlockContentRevision();
    if (m_hasSyncedRevisions &&
        m_syncedActiveChunkRevision == activeChunkRevision &&
        m_syncedBlockContentRevision == blockContentRevision) {
        return;
    }

    ++m_cacheSyncSerial;
    const uint64_t syncSerial = m_cacheSyncSerial;

    for (const auto& chunkPair : worldView.getActiveChunks()) {
        const Chunk* chunk = chunkPair.second.get();
        if (chunk == nullptr) {
            continue;
        }

        for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
            const SectionKey key{chunkPair.first, scy};
            const SubChunk* subChunk = chunk->getSubChunk(scy);
            if (subChunk == nullptr || subChunk->getType() == SubChunkType::Air) {
                m_sectionCaches.erase(key);
                continue;
            }

            const uint64_t revision = subChunk->getMeshRevision();
            auto [it, inserted] = m_sectionCaches.try_emplace(key);
            SectionCache& cache = it->second;
            if (inserted || cache.chunk != chunk || cache.meshRevision != revision) {
                cache.chunk = chunk;
                cache.meshRevision = revision;
                rebuildSectionCache(*chunk, *subChunk, scy, cache);
            }
            cache.syncSerial = syncSerial;
        }
    }

    for (auto it = m_sectionCaches.begin(); it != m_sectionCaches.end();) {
        if (it->second.syncSerial != syncSerial) {
            it = m_sectionCaches.erase(it);
        } else {
            ++it;
        }
    }

    m_syncedActiveChunkRevision = activeChunkRevision;
    m_syncedBlockContentRevision = blockContentRevision;
    rebuildFlatInstanceList();
    m_hasSyncedRevisions = true;
}

void BlockEntityRenderer::drawBlockEntitiesInstanced(const IWorldView& worldView,
                                                     const bool useSplitCulling,
                                                     const glm::vec3& cameraPos,
                                                     const float splitNear,
                                                     const float splitFar) {
    static_cast<void>(worldView);

    GLuint boundTexture = 0;
    glActiveTexture(GL_TEXTURE0);

    const float minSplitDistance = splitNear - 4.0f;
    const float minSplitDistanceSq = minSplitDistance * minSplitDistance;
    const float maxSplitDistance = splitFar + 4.0f;
    const float maxSplitDistanceSq = maxSplitDistance * maxSplitDistance;

    for (const auto& modelPair : m_models) {
        const ModelEntry& entry = modelPair.second;
        const Mesh& mesh = entry.mesh;
        if (mesh.vao == 0 || mesh.vertexCount == 0) {
            continue;
        }

        m_instanceData.clear();
        for (const BlockEntityInstance* instance : m_flatInstances) {
            if (instance->model != &entry) {
                continue;
            }

            if (useSplitCulling) {
                const glm::vec3 delta = instance->center - cameraPos;
                const float distanceSq = glm::dot(delta, delta);
                if ((minSplitDistance > 0.0f && distanceSq < minSplitDistanceSq) ||
                    distanceSq > maxSplitDistanceSq) {
                    continue;
                }
            }

            InstancedDrawData drawData;
            drawData.modelMatrix = instance->modelMatrix;
            drawData.light = instance->light;
            m_instanceData.push_back(drawData);
        }

        if (m_instanceData.empty()) {
            continue;
        }

        ensureInstanceCapacity(m_instanceData.size());
        glNamedBufferSubData(m_instanceVbo,
                             0,
                             static_cast<GLsizeiptr>(m_instanceData.size() * sizeof(InstancedDrawData)),
                             m_instanceData.data());

        const GLuint texture = static_cast<GLuint>(renderer::rhi::gl::textureId(entry.texture));
        if (boundTexture != texture) {
            glBindTexture(GL_TEXTURE_2D, texture);
            boundTexture = texture;
        }

        glBindVertexArray(mesh.vao);
        glDrawArraysInstanced(GL_TRIANGLES,
                              0,
                              static_cast<GLsizei>(mesh.vertexCount),
                              static_cast<GLsizei>(m_instanceData.size()));
    }
}

void BlockEntityRenderer::drawBlockEntities(const IWorldView& worldView,
                                            Shader& shader,
                                            const int modelLoc,
                                            const int prevModelLoc,
                                            const int sunlightLoc,
                                            const int blockLightLoc,
                                            const bool useSplitCulling,
                                            const glm::vec3& cameraPos,
                                            const float splitNear,
                                            const float splitFar) {
    static_cast<void>(worldView);

    GLuint boundTexture = 0;
    glActiveTexture(GL_TEXTURE0);

    const float minSplitDistance = splitNear - 4.0f;
    const float minSplitDistanceSq = minSplitDistance * minSplitDistance;
    const float maxSplitDistance = splitFar + 4.0f;
    const float maxSplitDistanceSq = maxSplitDistance * maxSplitDistance;

    for (const BlockEntityInstance* instance : m_flatInstances) {
        const ModelEntry* entry = instance->model;
        if (entry == nullptr) {
            continue;
        }

        const Mesh& mesh = entry->mesh;
        if (mesh.vao == 0 || mesh.vertexCount == 0) {
            continue;
        }

        if (useSplitCulling) {
            const glm::vec3 delta = instance->center - cameraPos;
            const float distanceSq = glm::dot(delta, delta);
            if ((minSplitDistance > 0.0f && distanceSq < minSplitDistanceSq) ||
                distanceSq > maxSplitDistanceSq) {
                continue;
            }
        }

        glUniform1f(sunlightLoc, instance->light.x);
        if (blockLightLoc >= 0) {
            glUniform1f(blockLightLoc, instance->light.y);
        }

        const GLuint texture = static_cast<GLuint>(renderer::rhi::gl::textureId(entry->texture));
        if (boundTexture != texture) {
            glBindTexture(GL_TEXTURE_2D, texture);
            boundTexture = texture;
        }

        shader.setMat4(modelLoc, instance->modelMatrix);
        if (prevModelLoc >= 0) {
            shader.setMat4(prevModelLoc, instance->modelMatrix);
        }

        glBindVertexArray(mesh.vao);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh.vertexCount));
    }
}

void BlockEntityRenderer::renderToGBuffer(RhiCommandList& commandList,
                                          const glm::mat4& viewProj) {
    commandList.setGraphicsPipeline(m_gbufferPipeline);
    commandList.pushConstants(&viewProj, sizeof(viewProj), rhiFlag(RhiShaderStage::Vertex));
    commandList.setVertexBuffer(1u, m_rhiInstanceBuffer.buffer(), 0u);
    for (const PreparedModelBatch& batch : m_gbufferBatches) {
        commandList.setBindGroup(0u, batch.model->gbufferBindGroup);
        commandList.setVertexBuffer(0u, batch.model->mesh.rhiVertexBuffer, 0u);
        commandList.setVertexBuffer(1u, m_rhiInstanceBuffer.buffer(), batch.instanceOffset);
        commandList.draw(batch.model->mesh.vertexCount, batch.instanceCount, 0u, 0u);
    }
}

void BlockEntityRenderer::renderToShadowMap(const IWorldView& worldView,
                                            const glm::mat4& shadowViewProj,
                                            const glm::vec3& cameraPos,
                                            const float splitNear,
                                            const float splitFar) {
    if (m_shadowShader == nullptr) {
        return;
    }

    m_shadowShader->use();
    m_shadowShader->setMat4("viewProj", shadowViewProj);
    m_shadowShader->setInt("uTexture", 0);
    drawBlockEntitiesInstanced(worldView,
                               true,
                               cameraPos,
                               splitNear,
                               splitFar);

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void BlockEntityRenderer::renderForward(const IWorldView& worldView,
                                        const glm::mat4& viewProj,
                                        const float skyIntensity) {
    if (m_forwardShader == nullptr) {
        return;
    }

    m_forwardShader->use();
    m_forwardShader->setMat4("viewProj", viewProj);
    m_forwardShader->setInt("uTexture", 0);
    m_forwardShader->setFloat("uSkyIntensity", skyIntensity);
    m_forwardShader->setFloat("uHurtFlash", 0.0f);

    const int modelLoc = m_forwardShader->getUniformLocation("model");
    const int sunlightLoc = m_forwardShader->getUniformLocation("uHeldSunlight");
    const int blockLightLoc = m_forwardShader->getUniformLocation("uHeldBlockLight");
    drawBlockEntities(worldView,
                      *m_forwardShader,
                      modelLoc,
                      -1,
                      sunlightLoc,
                      blockLightLoc,
                      false,
                      glm::vec3(0.0f),
                      0.0f,
                      0.0f);

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}
