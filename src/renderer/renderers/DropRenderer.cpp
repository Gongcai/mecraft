#include "DropRenderer.h"
#include "../mesh/BlockMeshBuilder.h"
#include "../mesh/ItemModelMesh.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiShaderSourceLoader.h"

#include <cstddef>
#include <utility>
#include <vector>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/camera/Camera.h"
#include "engine/platform/Window.h"
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
    m_deferredShader = resourceMgr.getShader("drop_block");
    m_deferredItemShader = resourceMgr.getShader("item_model");
    m_shader = m_deferredShader;
    m_itemShader = m_deferredItemShader;
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
    m_shader = nullptr;
    m_itemShader = nullptr;
    m_deferredShader = nullptr;
    m_deferredItemShader = nullptr;
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

void DropRenderer::setForwardMode(bool forward) {
    if (m_resourceMgr == nullptr) return;
    if (forward) {
        Shader* fwdBlock = m_resourceMgr->getShader("drop_block_forward");
        Shader* fwdItem = m_resourceMgr->getShader("item_model_forward");
        m_shader = fwdBlock ? fwdBlock : m_deferredShader;
        m_itemShader = fwdItem ? fwdItem : m_deferredItemShader;
    } else {
        m_shader = m_deferredShader;
        m_itemShader = m_deferredItemShader;
    }
}

void DropRenderer::render(const DropSystem& dropSystem, const Camera& camera, const Window& window) {
    if (m_resourceMgr == nullptr) {
        return;
    }

    const auto& drops = dropSystem.getDrops();
    if (drops.empty()) {
        return;
    }

    const TextureArray& texArray = m_resourceMgr->getTextureArray();
    const TextureAtlas& itemAtlas = m_resourceMgr->getItemTextureAtlas();
    const GLuint texArrayId = static_cast<GLuint>(renderer::rhi::gl::textureId(texArray.texture));
    const GLuint itemAtlasId = static_cast<GLuint>(renderer::rhi::gl::textureId(itemAtlas.texture));
    const bool canRenderBlocks = (m_shader != nullptr && texArrayId != 0);
    const bool canRenderItems = (m_itemShader != nullptr && itemAtlasId != 0);
    if (!canRenderBlocks && !canRenderItems) {
        return;
    }

    const glm::mat4 viewProj = camera.getProjectionMatrix(window.getAspectRatio()) * camera.getViewMatrix();
    int blockModelLoc = -1;
    if (canRenderBlocks) {
        const int blockViewProjLoc = m_shader->getUniformLocation("viewProj");
        blockModelLoc = m_shader->getUniformLocation("model");
        m_shader->use();
        m_shader->setMat4("view", camera.getViewMatrix());
        m_shader->setMat4(blockViewProjLoc, viewProj);
        m_shader->setInt("texArray", 0);
        m_shader->setInt("uForceBaseLod", 0);
        m_shader->setInt("uGrassColormap", 3);
        m_shader->setInt("uFoliageColormap", 4);
        m_shader->setFloat("uWindStrength", 0.0f);
        m_shader->setFloat("uWindSpeed", 0.0f);
        m_shader->setFloat("uWindSpatialFreq", 1.0f);
        m_shader->setFloat("uWindTime", 0.0f);
        m_shader->setInt("uFogEnabled", 0);
        m_shader->setFloat("uSkyIntensity", 1.0f);
        m_shader->setInt("uLightmapDay", 1);
        m_shader->setInt("uLightmapNight", 2);
    }

    int itemModelLoc = -1;
    if (canRenderItems) {
        const int itemViewProjLoc = m_itemShader->getUniformLocation("viewProj");
        itemModelLoc = m_itemShader->getUniformLocation("model");
        m_itemShader->use();
        m_itemShader->setMat4(itemViewProjLoc, viewProj);
        m_itemShader->setInt("uAtlas", 0);
    }

    const GLuint lightmapDayId = static_cast<GLuint>(renderer::rhi::gl::textureId(m_resourceMgr->getLightmapDay()));
    const GLuint lightmapNightId = static_cast<GLuint>(renderer::rhi::gl::textureId(m_resourceMgr->getLightmapNight()));
    const GLuint grassColormapId = static_cast<GLuint>(renderer::rhi::gl::textureId(m_resourceMgr->getGrassColormap()));
    const GLuint foliageColormapId = static_cast<GLuint>(renderer::rhi::gl::textureId(m_resourceMgr->getFoliageColormap()));

    for (const DropEntity& drop : drops) {
        const ItemDef& itemDef = ItemRegistry::get(drop.itemId);
        const int itemTileIndex = m_resourceMgr->getItemTextureIndex(itemDef.iconTextureName);
        const BlockID renderBlock = ItemRegistry::toRenderBlock(drop.itemId);
        const bool preferBlockMesh = prefersBlockMeshForItem(renderBlock);

        glm::mat4 model(1.0f);
        model = glm::translate(model, drop.position);
        model = glm::rotate(model, drop.yawRadians, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::scale(model, glm::vec3(drop.halfExtents * 2.0f));
        model = glm::translate(model, glm::vec3(-0.5f, -0.5f, -0.5f));

        if (!preferBlockMesh && itemTileIndex >= 0 && canRenderItems) {
            Mesh* mesh = getOrCreateItemMesh(drop.itemId);
            if (mesh != nullptr && mesh->vao != 0 && mesh->vertexCount > 0) {
                m_itemShader->use();
                m_itemShader->setMat4(itemModelLoc, model);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, itemAtlasId);
                glBindVertexArray(mesh->vao);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
            }
            continue;
        }

        if (renderBlock == 0 || !canRenderBlocks) {
            continue;
        }

        Mesh* mesh = getOrCreateBlockMesh(renderBlock);
        if (mesh == nullptr || mesh->vao == 0 || mesh->vertexCount == 0) {
            continue;
        }

        m_shader->use();
        m_shader->setMat4(blockModelLoc, model);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, texArrayId);
        // Bind lightmap textures for drop block rendering
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, lightmapDayId);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, lightmapNightId);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, grassColormapId);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, foliageColormapId);
        glBindVertexArray(mesh->vao);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->vertexCount));
    }

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
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

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    mesh.vertexCount = static_cast<uint32_t>(vertices.size());

    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(ItemModelVertex)),
                 vertices.data(),
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ItemModelVertex), reinterpret_cast<void*>(offsetof(ItemModelVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ItemModelVertex), reinterpret_cast<void*>(offsetof(ItemModelVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(ItemModelVertex), reinterpret_cast<void*>(offsetof(ItemModelVertex, shade)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(ItemModelVertex), reinterpret_cast<void*>(offsetof(ItemModelVertex, nx)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "Drop.ItemMesh.VertexBuffer";
    bufferDesc.size = vertices.size() * sizeof(ItemModelVertex);
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex);
    bufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
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
    mesh.vao = shared.vao;
    mesh.vbo = shared.vbo;
    mesh.rhiVertexBuffer = shared.rhiVertexBuffer;
    mesh.rhiDevice = shared.rhiDevice;
    mesh.vertexCount = shared.vertexCount;
    shared.vao = 0;
    shared.vbo = 0;
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
    if (!blockVertexSource || !blockFragmentSource ||
        !itemShadowVertexSource || !itemShadowFragmentSource ||
        !blockShadowVertexSource || !blockShadowFragmentSource) {
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
        !m_blockShadowPipeline.isValid() || !m_blockShadowBindGroup.isValid()) {
        std::abort();
    }
}

void DropRenderer::destroyItemGBufferRhiResources() {
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
    if (mesh.vbo != 0) {
        glDeleteBuffers(1, &mesh.vbo);
        mesh.vbo = 0;
    }
    if (mesh.vao != 0) {
        glDeleteVertexArrays(1, &mesh.vao);
        mesh.vao = 0;
    }
    mesh.vertexCount = 0;
    mesh.rhiDevice = nullptr;
}
