#include "DropRenderer.h"
#include "../mesh/BlockMeshBuilder.h"
#include "../mesh/ItemModelMesh.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiShaderSourceLoader.h"

#include <cstddef>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "../../resource/ResourceMgr.h"
#include "../../world/DropSystem.h"
#include "../../world/IWorldView.h"
#include "../../world/chunk/Chunk.h"
#include "../../world/chunk/SubChunk.h"
#include "../../item/Item.h"

namespace {
bool isTorchShape(const BlockDef& def) {
    return def.renderShapeName == "torch";
}

bool prefersBlockMeshForItem(const BlockID renderBlock) {
    if (renderBlock == 0) {
        return false;
    }
    const BlockDef& def = BlockRegistry::get(renderBlock);
    return isTorchShape(def) || def.renderShapeName == "model";
}
}

void DropRenderer::init(ResourceMgr& resourceMgr) {
    m_resourceMgr = &resourceMgr;
    m_rhiDevice = &resourceMgr.rhiDevice();
    createItemGBufferRhiResources();
}

void DropRenderer::shutdown() {
    destroyItemGBufferRhiResources();
    for (auto& pair : m_blockMeshes) {
        destroyMesh(pair.second);
    }
    m_blockMeshes.clear();
    for (auto& pair : m_itemMeshes) {
        destroyMesh(pair.second);
    }
    m_itemMeshes.clear();
    m_preparedDrops.clear();
    m_previousModelMatrices.clear();
    m_currentModelMatrices.clear();
    m_resourceMgr = nullptr;
    m_rhiDevice = nullptr;
}

void DropRenderer::prepareFrame(const IWorldView& worldView, const DropSystem& dropSystem) {
    m_preparedDrops.clear();
    const auto& drops = dropSystem.getDrops();
    m_preparedDrops.reserve(drops.size());
    m_currentModelMatrices.clear();
    m_currentModelMatrices.reserve(drops.size());

    for (const DropEntity& drop : drops) {
        const ItemDef& itemDef = ItemRegistry::get(drop.itemId);
        const int itemTileIndex = m_resourceMgr->getItemTextureIndex(itemDef.iconTextureName);
        const BlockID renderBlock = ItemRegistry::toRenderBlock(drop.itemId);
        const bool useItemMesh = !prefersBlockMeshForItem(renderBlock) && itemTileIndex >= 0;

        Mesh* mesh = useItemMesh ? getOrCreateItemMesh(drop.itemId)
                                 : getOrCreateBlockMesh(renderBlock);
        if (mesh == nullptr || !mesh->rhiVertexBuffer.isValid() || mesh->vertexCount == 0u) {
            continue;
        }

        glm::mat4 model(1.0f);
        model = glm::translate(model, drop.position);
        model = glm::rotate(model, drop.yawRadians, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::scale(model, glm::vec3(drop.halfExtents * 2.0f));
        model = glm::translate(model, glm::vec3(-0.5f));
        const auto previous = m_previousModelMatrices.find(drop.id);
        m_preparedDrops.push_back({
            mesh,
            model,
            previous != m_previousModelMatrices.end() ? previous->second : model,
            queryWorldLight(worldView, drop.position),
            useItemMesh
        });
        m_currentModelMatrices.emplace(drop.id, model);
    }
}

void DropRenderer::finishGBufferFrame() {
    m_previousModelMatrices = m_currentModelMatrices;
}

void DropRenderer::renderItemsToGBuffer(RhiCommandList& commandList,
                                        const glm::mat4& viewProj,
                                        const glm::mat4& previousViewProj) {
    struct PushConstants {
        glm::mat4 viewProj;
        glm::mat4 previousViewProj;
        glm::mat4 model;
        glm::mat4 previousModel;
        glm::vec4 light;
    };
    commandList.setGraphicsPipeline(m_itemGBufferPipeline);
    commandList.setBindGroup(0u, m_itemGBufferBindGroup);
    for (const PreparedDrop& drop : m_preparedDrops) {
        if (!drop.itemMesh) {
            continue;
        }
        const PushConstants pushConstants{
            viewProj, previousViewProj, drop.model, drop.previousModel,
            glm::vec4(drop.light, 0.0f, 0.0f)
        };
        commandList.setVertexBuffer(0u, drop.mesh->rhiVertexBuffer, 0u);
        commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                  rhiFlag(RhiShaderStage::Vertex) |
                                  rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(drop.mesh->vertexCount, 1u, 0u, 0u);
    }
}

void DropRenderer::renderBlocksToGBuffer(RhiCommandList& commandList,
                                         const glm::mat4& viewProj,
                                         const glm::mat4& previousViewProj,
                                         const float animationTime) {
    struct PushConstants {
        glm::mat4 viewProj;
        glm::mat4 previousViewProj;
        glm::mat4 model;
        glm::mat4 previousModel;
        glm::vec4 lightAnimation;
    };
    commandList.setGraphicsPipeline(m_blockGBufferPipeline);
    commandList.setBindGroup(0u, m_blockGBufferBindGroup);
    for (const PreparedDrop& drop : m_preparedDrops) {
        if (drop.itemMesh) {
            continue;
        }
        const PushConstants pushConstants{
            viewProj, previousViewProj, drop.model, drop.previousModel,
            glm::vec4(drop.light, animationTime, 0.0f)
        };
        commandList.setVertexBuffer(0u, drop.mesh->rhiVertexBuffer, 0u);
        commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                  rhiFlag(RhiShaderStage::Vertex) |
                                  rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(drop.mesh->vertexCount, 1u, 0u, 0u);
    }
}

void DropRenderer::renderForward(RhiCommandList& commandList,
                                 const glm::mat4& viewProj,
                                 const float skyIntensity,
                                 const float animationTime) {
    struct PushConstants {
        glm::mat4 viewProj;
        glm::mat4 model;
        glm::vec4 lightingAnimation;
    };
    for (const PreparedDrop& drop : m_preparedDrops) {
        const PushConstants pushConstants{
            viewProj, drop.model,
            glm::vec4(drop.light, skyIntensity, animationTime)
        };
        commandList.setGraphicsPipeline(drop.itemMesh ? m_itemForwardPipeline
                                                      : m_blockForwardPipeline);
        commandList.setBindGroup(0u, drop.itemMesh ? m_itemGBufferBindGroup
                                                   : m_blockGBufferBindGroup);
        commandList.setVertexBuffer(0u, drop.mesh->rhiVertexBuffer, 0u);
        commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                  rhiFlag(RhiShaderStage::Vertex) |
                                  rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(drop.mesh->vertexCount, 1u, 0u, 0u);
    }
}

 DropRenderer::Mesh* DropRenderer::getOrCreateItemMesh(const ItemID itemId) {
    const auto it = m_itemMeshes.find(itemId);
    if (it != m_itemMeshes.end()) {
        return &it->second;
    }

    Mesh mesh = buildItemMesh(itemId);
    auto inserted = m_itemMeshes.emplace(itemId, std::move(mesh));
    return &inserted.first->second;
}

DropRenderer::Mesh DropRenderer::buildItemMesh(const ItemID itemId) const {
    Mesh mesh;
    if (m_resourceMgr == nullptr || itemId == 0) {
        return mesh;
    }

    const ItemDef& itemDef = ItemRegistry::get(itemId);
    const int tileIndex = m_resourceMgr->getItemTextureIndex(itemDef.iconTextureName);
    if (tileIndex < 0) {
        return mesh;
    }

    std::vector<ItemModelVertex> vertices;
    if (!buildExtrudedItemMesh(m_resourceMgr->getItemTextureAtlas(),
                               m_resourceMgr->getItemTexturePixels(),
                               tileIndex,
                               vertices)) {
        return mesh;
    }

    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "Drop.ItemMesh.VertexBuffer";
    bufferDesc.size = vertices.size() * sizeof(ItemModelVertex);
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    bufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    bufferDesc.initialState = RhiResourceState::VertexBuffer;
    RhiDevice& rhiDevice = m_resourceMgr->rhiDevice();
    mesh.rhiVertexBuffer = rhiDevice.createBuffer(
        bufferDesc, vertices.data(), vertices.size() * sizeof(ItemModelVertex));
    mesh.rhiDevice = &rhiDevice;
    if (!mesh.rhiVertexBuffer.isValid()) {
        destroyMesh(mesh);
    }
    return mesh;
}

DropRenderer::Mesh* DropRenderer::getOrCreateBlockMesh(const BlockID blockId) {
    const auto it = m_blockMeshes.find(blockId);
    if (it != m_blockMeshes.end()) {
        return &it->second;
    }

    Mesh mesh = buildBlockMesh(blockId);
    auto inserted = m_blockMeshes.emplace(blockId, std::move(mesh));
    return &inserted.first->second;
}

DropRenderer::Mesh DropRenderer::buildBlockMesh(const BlockID blockId) const {
    Mesh mesh;
    if (m_resourceMgr == nullptr || blockId == 0) {
        return mesh;
    }

    renderer::BlockCubeMesh shared = renderer::buildBlockCubeMesh(blockId, *m_resourceMgr);
    mesh.rhiVertexBuffer = shared.rhiVertexBuffer;
    mesh.rhiDevice = shared.rhiDevice;
    mesh.vertexCount = shared.vertexCount;
    shared.rhiVertexBuffer = {};
    shared.rhiDevice = nullptr;
    shared.vertexCount = 0;
    return mesh;
}

void DropRenderer::renderToShadowMap(RhiCommandList& commandList,
                                     const glm::mat4& shadowViewProj,
                                     const float animationTime) {
    struct ItemPushConstants { glm::mat4 viewProj; glm::mat4 model; };
    struct BlockPushConstants { glm::mat4 viewProj; glm::mat4 model; glm::vec4 animationTime; };
    for (const PreparedDrop& drop : m_preparedDrops) {
        if (drop.itemMesh) {
            const ItemPushConstants pushConstants{shadowViewProj, drop.model};
            commandList.setGraphicsPipeline(m_itemShadowPipeline);
            commandList.setBindGroup(0u, m_itemShadowBindGroup);
            commandList.setVertexBuffer(0u, drop.mesh->rhiVertexBuffer, 0u);
            commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                      rhiFlag(RhiShaderStage::Vertex));
        } else {
            const BlockPushConstants pushConstants{
                shadowViewProj, drop.model, glm::vec4(animationTime, 0.0f, 0.0f, 0.0f)
            };
            commandList.setGraphicsPipeline(m_blockShadowPipeline);
            commandList.setBindGroup(0u, m_blockShadowBindGroup);
            commandList.setVertexBuffer(0u, drop.mesh->rhiVertexBuffer, 0u);
            commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                      rhiFlag(RhiShaderStage::Vertex) |
                                      rhiFlag(RhiShaderStage::Fragment));
        }
        commandList.draw(drop.mesh->vertexCount, 1u, 0u, 0u);
    }
}

void DropRenderer::createItemGBufferRhiResources() {
    const auto vertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/item_drop_gbuffer_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/item_drop_gbuffer_rhi.frag");
    if (!vertexSource || !fragmentSource) {
        std::abort();
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
    m_itemGBufferVertexShader = createShader("Drop.ItemGBuffer.Vertex", RhiShaderStage::Vertex,
                                              *vertexSource);
    m_itemGBufferFragmentShader = createShader("Drop.ItemGBuffer.Fragment", RhiShaderStage::Fragment,
                                                *fragmentSource);
    const auto blockVertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/falling_block_gbuffer_rhi.vert");
    const auto blockFragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/falling_block_gbuffer_rhi.frag");
    const auto itemShadowVertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/item_drop_shadow_rhi.vert");
    const auto itemShadowFragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/item_drop_shadow_rhi.frag");
    const auto blockShadowVertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/falling_block_shadow_rhi.vert");
    const auto blockShadowFragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/falling_block_shadow_rhi.frag");
    const auto itemForwardVertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/item_drop_forward_rhi.vert");
    const auto itemForwardFragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/item_drop_forward_rhi.frag");
    const auto blockForwardVertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/block_drop_forward_rhi.vert");
    const auto blockForwardFragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/block_drop_forward_rhi.frag");
    if (!blockVertexSource || !blockFragmentSource ||
        !itemShadowVertexSource || !itemShadowFragmentSource ||
        !blockShadowVertexSource || !blockShadowFragmentSource ||
        !itemForwardVertexSource || !itemForwardFragmentSource ||
        !blockForwardVertexSource || !blockForwardFragmentSource) {
        std::abort();
    }
    m_blockGBufferVertexShader = createShader("Drop.BlockGBuffer.Vertex", RhiShaderStage::Vertex,
                                               *blockVertexSource);
    m_blockGBufferFragmentShader = createShader("Drop.BlockGBuffer.Fragment", RhiShaderStage::Fragment,
                                                 *blockFragmentSource);
    m_itemShadowVertexShader = createShader("Drop.ItemShadow.Vertex", RhiShaderStage::Vertex,
                                            *itemShadowVertexSource);
    m_itemShadowFragmentShader = createShader("Drop.ItemShadow.Fragment", RhiShaderStage::Fragment,
                                              *itemShadowFragmentSource);
    m_blockShadowVertexShader = createShader("Drop.BlockShadow.Vertex", RhiShaderStage::Vertex,
                                             *blockShadowVertexSource);
    m_blockShadowFragmentShader = createShader("Drop.BlockShadow.Fragment", RhiShaderStage::Fragment,
                                               *blockShadowFragmentSource);
    m_itemForwardVertexShader = createShader("Drop.ItemForward.Vertex", RhiShaderStage::Vertex,
                                             *itemForwardVertexSource);
    m_itemForwardFragmentShader = createShader("Drop.ItemForward.Fragment", RhiShaderStage::Fragment,
                                               *itemForwardFragmentSource);
    m_blockForwardVertexShader = createShader("Drop.BlockForward.Vertex", RhiShaderStage::Vertex,
                                              *blockForwardVertexSource);
    m_blockForwardFragmentShader = createShader("Drop.BlockForward.Fragment", RhiShaderStage::Fragment,
                                                *blockForwardFragmentSource);
    RhiTextureViewDesc textureViewDesc;
    textureViewDesc.texture = m_resourceMgr->getItemTextureAtlas().texture;
    textureViewDesc.viewType = RhiTextureViewType::Texture2D;
    m_itemAtlasView = m_rhiDevice->createTextureView(textureViewDesc);
    RhiSamplerDesc samplerDesc;
    samplerDesc.addressU = RhiAddressMode::ClampToEdge;
    samplerDesc.addressV = RhiAddressMode::ClampToEdge;
    m_itemSampler = m_rhiDevice->createSampler(samplerDesc);
    samplerDesc.addressU = RhiAddressMode::Repeat;
    samplerDesc.addressV = RhiAddressMode::Repeat;
    m_blockSampler = m_rhiDevice->createSampler(samplerDesc);
    const RhiTextureHandle blockTextures[] = {
        m_resourceMgr->getTextureArray().texture,
        m_resourceMgr->getGrassColormap(),
        m_resourceMgr->getFoliageColormap()
    };
    RhiTextureViewHandle* blockViews[] = {
        &m_blockTextureArrayView, &m_grassColormapView, &m_foliageColormapView
    };
    for (uint32_t index = 0u; index < 3u; ++index) {
        RhiTextureViewDesc viewDesc;
        viewDesc.texture = blockTextures[index];
        viewDesc.viewType = index == 0u ? RhiTextureViewType::Texture2DArray
                                       : RhiTextureViewType::Texture2D;
        *blockViews[index] = m_rhiDevice->createTextureView(viewDesc);
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "Drop.ItemGBuffer.BindGroupLayout";
    bindGroupLayoutDesc.entries.push_back({0u, RhiBindingType::CombinedTextureSampler,
                                           rhiFlag(RhiShaderStage::Fragment), 1u});
    m_itemGBufferBindGroupLayout = m_rhiDevice->createBindGroupLayout(bindGroupLayoutDesc);
    bindGroupLayoutDesc.debugName = "Drop.BlockGBuffer.BindGroupLayout";
    bindGroupLayoutDesc.entries.clear();
    for (uint32_t binding = 0u; binding < 3u; ++binding) {
        bindGroupLayoutDesc.entries.push_back({binding, RhiBindingType::CombinedTextureSampler,
                                               rhiFlag(RhiShaderStage::Fragment), 1u});
    }
    m_blockGBufferBindGroupLayout = m_rhiDevice->createBindGroupLayout(bindGroupLayoutDesc);
    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "Drop.ItemGBuffer.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_itemGBufferBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(glm::mat4) * 4u + sizeof(glm::vec4);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) |
                                            rhiFlag(RhiShaderStage::Fragment);
    m_itemGBufferPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    pipelineLayoutDesc.debugName = "Drop.BlockGBuffer.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts[0] = m_blockGBufferBindGroupLayout;
    m_blockGBufferPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    pipelineLayoutDesc.debugName = "Drop.ItemShadow.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts[0] = m_itemGBufferBindGroupLayout;
    pipelineLayoutDesc.pushConstantBytes = sizeof(glm::mat4) * 2u;
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex);
    m_itemShadowPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    pipelineLayoutDesc.debugName = "Drop.BlockShadow.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts[0] = m_blockGBufferBindGroupLayout;
    pipelineLayoutDesc.pushConstantBytes = sizeof(glm::mat4) * 2u + sizeof(glm::vec4);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) |
                                            rhiFlag(RhiShaderStage::Fragment);
    m_blockShadowPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    pipelineLayoutDesc.debugName = "Drop.ItemForward.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts[0] = m_itemGBufferBindGroupLayout;
    pipelineLayoutDesc.pushConstantBytes = sizeof(glm::mat4) * 2u + sizeof(glm::vec4);
    m_itemForwardPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    pipelineLayoutDesc.debugName = "Drop.BlockForward.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts[0] = m_blockGBufferBindGroupLayout;
    m_blockForwardPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "Drop.ItemGBuffer.Pipeline";
    pipelineDesc.vertexShader = m_itemGBufferVertexShader;
    pipelineDesc.fragmentShader = m_itemGBufferFragmentShader;
    pipelineDesc.layout = m_itemGBufferPipelineLayout;
    pipelineDesc.vertexInput.bindings.push_back(
        {0u, sizeof(ItemModelVertex), RhiVertexInputRate::Vertex});
    pipelineDesc.vertexInput.attributes = {
        {0u, 0u, RhiVertexFormat::Float3, offsetof(ItemModelVertex, x)},
        {1u, 0u, RhiVertexFormat::Float2, offsetof(ItemModelVertex, u)},
        {2u, 0u, RhiVertexFormat::Float, offsetof(ItemModelVertex, shade)},
        {3u, 0u, RhiVertexFormat::Float3, offsetof(ItemModelVertex, nx)}
    };
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.colorFormats = {RhiTextureFormat::Rgba8Unorm, RhiTextureFormat::Rgba16Float,
        RhiTextureFormat::Rg8Unorm, RhiTextureFormat::Rgba8Unorm, RhiTextureFormat::Rgba8Unorm,
        RhiTextureFormat::Rg16Float};
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    pipelineDesc.blend.attachments.resize(6u);
    m_itemGBufferPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    pipelineDesc.debugName = "Drop.BlockGBuffer.Pipeline";
    pipelineDesc.vertexShader = m_blockGBufferVertexShader;
    pipelineDesc.fragmentShader = m_blockGBufferFragmentShader;
    pipelineDesc.layout = m_blockGBufferPipelineLayout;
    pipelineDesc.vertexInput = {};
    renderer::setBlockVertexInputLayout(pipelineDesc);
    m_blockGBufferPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    pipelineDesc.debugName = "Drop.ItemShadow.Pipeline";
    pipelineDesc.vertexShader = m_itemShadowVertexShader;
    pipelineDesc.fragmentShader = m_itemShadowFragmentShader;
    pipelineDesc.layout = m_itemShadowPipelineLayout;
    pipelineDesc.vertexInput.bindings = {{0u, sizeof(ItemModelVertex), RhiVertexInputRate::Vertex}};
    pipelineDesc.vertexInput.attributes = {
        {0u, 0u, RhiVertexFormat::Float3, offsetof(ItemModelVertex, x)},
        {1u, 0u, RhiVertexFormat::Float2, offsetof(ItemModelVertex, u)}
    };
    pipelineDesc.colorFormats.clear();
    pipelineDesc.blend.attachments.clear();
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    m_itemShadowPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    pipelineDesc.debugName = "Drop.BlockShadow.Pipeline";
    pipelineDesc.vertexShader = m_blockShadowVertexShader;
    pipelineDesc.fragmentShader = m_blockShadowFragmentShader;
    pipelineDesc.layout = m_blockShadowPipelineLayout;
    pipelineDesc.vertexInput = {};
    renderer::setBlockVertexInputLayout(pipelineDesc);
    m_blockShadowPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    pipelineDesc.debugName = "Drop.ItemForward.Pipeline";
    pipelineDesc.vertexShader = m_itemForwardVertexShader;
    pipelineDesc.fragmentShader = m_itemForwardFragmentShader;
    pipelineDesc.layout = m_itemForwardPipelineLayout;
    pipelineDesc.vertexInput.bindings = {{0u, sizeof(ItemModelVertex), RhiVertexInputRate::Vertex}};
    pipelineDesc.vertexInput.attributes = {
        {0u, 0u, RhiVertexFormat::Float3, offsetof(ItemModelVertex, x)},
        {1u, 0u, RhiVertexFormat::Float2, offsetof(ItemModelVertex, u)},
        {2u, 0u, RhiVertexFormat::Float, offsetof(ItemModelVertex, shade)}
    };
    pipelineDesc.colorFormats = {m_rhiDevice->swapchainColorFormat()};
    pipelineDesc.depthFormat = m_rhiDevice->swapchainDepthStencilFormat();
    pipelineDesc.blend.attachments.resize(1u);
    m_itemForwardPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    pipelineDesc.debugName = "Drop.BlockForward.Pipeline";
    pipelineDesc.vertexShader = m_blockForwardVertexShader;
    pipelineDesc.fragmentShader = m_blockForwardFragmentShader;
    pipelineDesc.layout = m_blockForwardPipelineLayout;
    pipelineDesc.vertexInput = {};
    renderer::setBlockVertexInputLayout(pipelineDesc);
    m_blockForwardPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_itemGBufferBindGroupLayout;
    RhiBindGroupEntry textureEntry;
    textureEntry.binding = 0u;
    textureEntry.resource.combinedTextureSampler = {m_itemAtlasView, m_itemSampler};
    bindGroupDesc.entries.push_back(textureEntry);
    m_itemGBufferBindGroup = m_rhiDevice->createBindGroup(bindGroupDesc);
    RhiBindGroupDesc blockBindGroupDesc;
    blockBindGroupDesc.layout = m_blockGBufferBindGroupLayout;
    const RhiTextureViewHandle blockTextureViews[] = {
        m_blockTextureArrayView, m_grassColormapView, m_foliageColormapView
    };
    for (uint32_t binding = 0u; binding < 3u; ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler = {blockTextureViews[binding], m_blockSampler};
        blockBindGroupDesc.entries.push_back(entry);
    }
    m_blockGBufferBindGroup = m_rhiDevice->createBindGroup(blockBindGroupDesc);
    m_itemShadowBindGroup = m_rhiDevice->createBindGroup(bindGroupDesc);
    m_blockShadowBindGroup = m_rhiDevice->createBindGroup(blockBindGroupDesc);
    if (!m_itemGBufferVertexShader.isValid() || !m_itemGBufferFragmentShader.isValid() ||
        !m_itemAtlasView.isValid() || !m_itemSampler.isValid() ||
        !m_itemGBufferBindGroupLayout.isValid() || !m_itemGBufferPipelineLayout.isValid() ||
        !m_itemGBufferPipeline.isValid() || !m_itemGBufferBindGroup.isValid() ||
        !m_blockTextureArrayView.isValid() || !m_grassColormapView.isValid() ||
        !m_foliageColormapView.isValid() || !m_blockSampler.isValid() ||
        !m_blockGBufferVertexShader.isValid() || !m_blockGBufferFragmentShader.isValid() ||
        !m_blockGBufferBindGroupLayout.isValid() || !m_blockGBufferPipelineLayout.isValid() ||
        !m_blockGBufferPipeline.isValid() || !m_blockGBufferBindGroup.isValid() ||
        !m_itemShadowVertexShader.isValid() || !m_itemShadowFragmentShader.isValid() ||
        !m_itemShadowPipelineLayout.isValid() || !m_itemShadowPipeline.isValid() ||
        !m_itemShadowBindGroup.isValid() || !m_blockShadowVertexShader.isValid() ||
        !m_blockShadowFragmentShader.isValid() || !m_blockShadowPipelineLayout.isValid() ||
        !m_blockShadowPipeline.isValid() || !m_blockShadowBindGroup.isValid() ||
        !m_itemForwardVertexShader.isValid() || !m_itemForwardFragmentShader.isValid() ||
        !m_itemForwardPipelineLayout.isValid() || !m_itemForwardPipeline.isValid() ||
        !m_blockForwardVertexShader.isValid() || !m_blockForwardFragmentShader.isValid() ||
        !m_blockForwardPipelineLayout.isValid() || !m_blockForwardPipeline.isValid()) {
        std::abort();
    }
}

void DropRenderer::destroyItemGBufferRhiResources() {
    if (m_blockForwardPipeline.isValid()) m_rhiDevice->destroyPipeline(m_blockForwardPipeline);
    if (m_blockForwardPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_blockForwardPipelineLayout);
    if (m_blockForwardFragmentShader.isValid()) m_rhiDevice->destroyShader(m_blockForwardFragmentShader);
    if (m_blockForwardVertexShader.isValid()) m_rhiDevice->destroyShader(m_blockForwardVertexShader);
    if (m_itemForwardPipeline.isValid()) m_rhiDevice->destroyPipeline(m_itemForwardPipeline);
    if (m_itemForwardPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_itemForwardPipelineLayout);
    if (m_itemForwardFragmentShader.isValid()) m_rhiDevice->destroyShader(m_itemForwardFragmentShader);
    if (m_itemForwardVertexShader.isValid()) m_rhiDevice->destroyShader(m_itemForwardVertexShader);
    if (m_blockShadowBindGroup.isValid()) m_rhiDevice->destroyBindGroup(m_blockShadowBindGroup);
    if (m_blockShadowPipeline.isValid()) m_rhiDevice->destroyPipeline(m_blockShadowPipeline);
    if (m_blockShadowPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_blockShadowPipelineLayout);
    if (m_blockShadowFragmentShader.isValid()) m_rhiDevice->destroyShader(m_blockShadowFragmentShader);
    if (m_blockShadowVertexShader.isValid()) m_rhiDevice->destroyShader(m_blockShadowVertexShader);
    if (m_itemShadowBindGroup.isValid()) m_rhiDevice->destroyBindGroup(m_itemShadowBindGroup);
    if (m_itemShadowPipeline.isValid()) m_rhiDevice->destroyPipeline(m_itemShadowPipeline);
    if (m_itemShadowPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_itemShadowPipelineLayout);
    if (m_itemShadowFragmentShader.isValid()) m_rhiDevice->destroyShader(m_itemShadowFragmentShader);
    if (m_itemShadowVertexShader.isValid()) m_rhiDevice->destroyShader(m_itemShadowVertexShader);
    if (m_blockGBufferBindGroup.isValid()) m_rhiDevice->destroyBindGroup(m_blockGBufferBindGroup);
    if (m_blockGBufferPipeline.isValid()) m_rhiDevice->destroyPipeline(m_blockGBufferPipeline);
    if (m_blockGBufferPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_blockGBufferPipelineLayout);
    if (m_blockGBufferBindGroupLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_blockGBufferBindGroupLayout);
    if (m_blockSampler.isValid()) m_rhiDevice->destroySampler(m_blockSampler);
    if (m_foliageColormapView.isValid()) m_rhiDevice->destroyTextureView(m_foliageColormapView);
    if (m_grassColormapView.isValid()) m_rhiDevice->destroyTextureView(m_grassColormapView);
    if (m_blockTextureArrayView.isValid()) m_rhiDevice->destroyTextureView(m_blockTextureArrayView);
    if (m_blockGBufferFragmentShader.isValid()) m_rhiDevice->destroyShader(m_blockGBufferFragmentShader);
    if (m_blockGBufferVertexShader.isValid()) m_rhiDevice->destroyShader(m_blockGBufferVertexShader);
    if (m_itemGBufferBindGroup.isValid()) m_rhiDevice->destroyBindGroup(m_itemGBufferBindGroup);
    if (m_itemGBufferPipeline.isValid()) m_rhiDevice->destroyPipeline(m_itemGBufferPipeline);
    if (m_itemGBufferPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_itemGBufferPipelineLayout);
    if (m_itemGBufferBindGroupLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_itemGBufferBindGroupLayout);
    if (m_itemSampler.isValid()) m_rhiDevice->destroySampler(m_itemSampler);
    if (m_itemAtlasView.isValid()) m_rhiDevice->destroyTextureView(m_itemAtlasView);
    if (m_itemGBufferFragmentShader.isValid()) m_rhiDevice->destroyShader(m_itemGBufferFragmentShader);
    if (m_itemGBufferVertexShader.isValid()) m_rhiDevice->destroyShader(m_itemGBufferVertexShader);
    m_itemGBufferBindGroup = {};
    m_itemGBufferPipeline = {};
    m_itemGBufferPipelineLayout = {};
    m_itemGBufferBindGroupLayout = {};
    m_itemSampler = {};
    m_itemAtlasView = {};
    m_itemGBufferFragmentShader = {};
    m_itemGBufferVertexShader = {};
    m_blockGBufferBindGroup = {};
    m_blockGBufferPipeline = {};
    m_blockGBufferPipelineLayout = {};
    m_blockGBufferBindGroupLayout = {};
    m_blockSampler = {};
    m_foliageColormapView = {};
    m_grassColormapView = {};
    m_blockTextureArrayView = {};
    m_blockGBufferFragmentShader = {};
    m_blockGBufferVertexShader = {};
    m_blockShadowBindGroup = {};
    m_blockShadowPipeline = {};
    m_blockShadowPipelineLayout = {};
    m_blockShadowFragmentShader = {};
    m_blockShadowVertexShader = {};
    m_itemShadowBindGroup = {};
    m_itemShadowPipeline = {};
    m_itemShadowPipelineLayout = {};
    m_itemShadowFragmentShader = {};
    m_itemShadowVertexShader = {};
    m_blockForwardPipeline = {};
    m_blockForwardPipelineLayout = {};
    m_blockForwardFragmentShader = {};
    m_blockForwardVertexShader = {};
    m_itemForwardPipeline = {};
    m_itemForwardPipelineLayout = {};
    m_itemForwardFragmentShader = {};
    m_itemForwardVertexShader = {};
}

glm::vec2 DropRenderer::queryWorldLight(const IWorldView& worldView, const glm::vec3& position) {
    const int bx = static_cast<int>(std::floor(position.x));
    const int by = static_cast<int>(std::floor(position.y));
    const int bz = static_cast<int>(std::floor(position.z));

    if (!worldView.isChunkLoadedForBlock(bx, by, bz)) {
        return {1.0f, 0.0f};
    }

    const glm::ivec2 cc = worldView.getChunkCoords(bx, bz);
    const auto& chunks = worldView.getActiveChunks();
    const auto it = chunks.find(IWorldView::chunkKey(cc.x, cc.y));
    if (it == chunks.end()) {
        return {1.0f, 0.0f};
    }

    const glm::ivec3 local = Chunk::worldToLocal(bx, by, bz);
    const uint8_t sun = it->second->getSunlight(local.x, local.y, local.z);
    const uint8_t block = it->second->getBlockLight(local.x, local.y, local.z);
    return {sun / 15.0f, block / 15.0f};
}

void DropRenderer::destroyMesh(Mesh& mesh) {
    if (mesh.rhiDevice != nullptr && mesh.rhiVertexBuffer.isValid()) {
        mesh.rhiDevice->destroyBuffer(mesh.rhiVertexBuffer);
        mesh.rhiVertexBuffer = {};
    }
    mesh.vertexCount = 0;
    mesh.rhiDevice = nullptr;
}
