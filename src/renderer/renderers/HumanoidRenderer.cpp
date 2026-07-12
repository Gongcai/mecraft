#include "HumanoidRenderer.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/camera/Camera.h"
#include "engine/platform/Window.h"
#include "../../resource/ResourceMgr.h"
#include "../../ecs/GameplayRegistry.h"
#include "../../ecs/entity/EntitySkinLayout.h"
#include "../../world/IWorldView.h"
#include "../../ecs/components/Components.h"
#include "../../ecs/components/NetworkComponents.h"
#include "../../world/World.h"
#include "../../world/chunk/Chunk.h"

namespace {
constexpr unsigned int kQuadIndices[] = {0, 1, 2, 0, 2, 3};

constexpr glm::vec3 kFaceNormals[] = {
    {0, 1, 0},   // top
    {0, -1, 0},  // bottom
    {0, 0, 1},   // front
    {0, 0, -1},  // back
    {-1, 0, 0},  // left
    {1, 0, 0}    // right
};

bool shouldRenderSteveRoot(const entt::registry& reg,
                           entt::entity steveRoot,
                           HumanoidRenderer::RenderMode mode) {
    return mode == HumanoidRenderer::kRenderAll
        || reg.all_of<ecs::EntityNetIdComponent>(steveRoot);
}

float hurtFlashForRoot(const entt::registry& reg, const entt::entity root) {
    const auto* hurt = reg.try_get<ecs::HurtEffectComponent>(root);
    if (hurt == nullptr || hurt->flashDurationSeconds <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(hurt->flashSecondsRemaining / hurt->flashDurationSeconds, 0.0f, 1.0f);
}

glm::mat4 applyMobVisualScale(const glm::mat4& model,
                              const glm::vec3& pivot,
                              const float scale) {
    assert(scale > 0.0f);
    if (std::abs(scale - 1.0f) <= 0.0001f) {
        return model;
    }

    return glm::translate(glm::mat4(1.0f), pivot) *
           glm::scale(glm::mat4(1.0f), glm::vec3(scale)) *
           glm::translate(glm::mat4(1.0f), -pivot) *
           model;
}

} // anonymous namespace

HumanoidRenderer::FaceUvRect HumanoidRenderer::pixelRectToUv(float x0, float y0, float x1, float y1,
                                                             float textureWidth, float textureHeight) {
    return {
        x0 / textureWidth,
        1.0f - y1 / textureHeight,
        x1 / textureWidth,
        1.0f - y0 / textureHeight
    };
}

HumanoidRenderer::PartMesh HumanoidRenderer::buildPartMesh(const renderer::HumanoidPartMeshDefinition& definition,
                                                           const float textureWidth,
                                                           const float textureHeight) const {
    PartMesh mesh;

    std::array<FaceUvRect, 6> uv{};
    for (std::size_t i = 0; i < uv.size(); ++i) {
        const renderer::HumanoidSkinPixelRect& rect = definition.faceUvs[i];
        uv[i] = pixelRectToUv(rect.x0, rect.y0, rect.x1, rect.y1, textureWidth, textureHeight);
    }

    const float ymin = -definition.halfHeight + definition.offsetY;
    const float ymax =  definition.halfHeight + definition.offsetY;
    const float xmin = -definition.halfWidth;
    const float xmax =  definition.halfWidth;
    const float zmin = -definition.halfDepth;
    const float zmax =  definition.halfDepth;

    struct FaceCorners {
        glm::vec3 pos[4];
    };

    const FaceCorners faces[6] = {
        // Top (+Y)
        {{{xmin, ymax, zmax}, {xmax, ymax, zmax}, {xmax, ymax, zmin}, {xmin, ymax, zmin}}},
        // Bottom (-Y)
        {{{xmin, ymin, zmin}, {xmax, ymin, zmin}, {xmax, ymin, zmax}, {xmin, ymin, zmax}}},
        // Front (+Z)
        {{{xmin, ymin, zmax}, {xmax, ymin, zmax}, {xmax, ymax, zmax}, {xmin, ymax, zmax}}},
        // Back (-Z)
        {{{xmax, ymin, zmin}, {xmin, ymin, zmin}, {xmin, ymax, zmin}, {xmax, ymax, zmin}}},
        // Left (-X)
        {{{xmin, ymin, zmin}, {xmin, ymin, zmax}, {xmin, ymax, zmax}, {xmin, ymax, zmin}}},
        // Right (+X)
        {{{xmax, ymin, zmax}, {xmax, ymin, zmin}, {xmax, ymax, zmin}, {xmax, ymax, zmax}}}
    };

    std::vector<SteveVertex> vertices;
    vertices.reserve(36);

    for (int f = 0; f < 6; ++f) {
        const glm::vec2 faceUvs[4] = {
            {uv[f].u0, uv[f].v0},
            {uv[f].u1, uv[f].v0},
            {uv[f].u1, uv[f].v1},
            {uv[f].u0, uv[f].v1}
        };

        for (int idx : kQuadIndices) {
            const auto& p = faces[f].pos[idx];
            const auto& t = faceUvs[idx];
            const auto& n = kFaceNormals[f];
            vertices.push_back({p.x, p.y, p.z, t.x, t.y, n.x, n.y, n.z});
        }
    }

    mesh.vertexCount = static_cast<uint32_t>(vertices.size());


    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "Humanoid.PartMesh.VertexBuffer";
    bufferDesc.size = vertices.size() * sizeof(SteveVertex);
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    bufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    bufferDesc.initialState = RhiResourceState::VertexBuffer;
    mesh.rhiVertexBuffer = m_rhiDevice->createBuffer(
        bufferDesc, vertices.data(), vertices.size() * sizeof(SteveVertex));
    if (!mesh.rhiVertexBuffer.isValid()) {
        destroyMesh(mesh);
    }

    return mesh;
}

HumanoidRenderer::PartMesh HumanoidRenderer::buildEntityModelPartMesh(
    const ecs::EntityModelPartDefinition& definition,
    const float textureWidth,
    const float textureHeight) const {
    PartMesh mesh;

    std::vector<SteveVertex> vertices;
    for (const ecs::EntityModelBoxDefinition& box : definition.boxes) {
        std::array<FaceUvRect, 6> uv{};
        for (std::size_t i = 0; i < uv.size(); ++i) {
            const ecs::EntityModelPixelRect& rect = box.faceUvs[i];
            uv[i] = pixelRectToUv(rect.x0, rect.y0, rect.x1, rect.y1, textureWidth, textureHeight);
        }

        const float xmin = (box.origin.x - box.inflate) / 16.0f;
        const float ymin = (box.origin.y - box.inflate) / 16.0f;
        const float zmin = (box.origin.z - box.inflate) / 16.0f;
        const float xmax = (box.origin.x + box.size.x + box.inflate) / 16.0f;
        const float ymax = (box.origin.y + box.size.y + box.inflate) / 16.0f;
        const float zmax = (box.origin.z + box.size.z + box.inflate) / 16.0f;

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

        for (int f = 0; f < 6; ++f) {
            const glm::vec2 faceUvs[4] = {
                {uv[f].u0, uv[f].v0},
                {uv[f].u1, uv[f].v0},
                {uv[f].u1, uv[f].v1},
                {uv[f].u0, uv[f].v1}
            };

            for (int idx : kQuadIndices) {
                const auto& p = faces[f].pos[idx];
                const auto& t = faceUvs[idx];
                const auto& n = kFaceNormals[f];
                vertices.push_back({p.x, p.y, p.z, t.x, t.y, n.x, n.y, n.z});
            }
        }
    }

    if (vertices.empty()) {
        return mesh;
    }

    mesh.vertexCount = static_cast<uint32_t>(vertices.size());


    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "Humanoid.EntityModelPart.VertexBuffer";
    bufferDesc.size = vertices.size() * sizeof(SteveVertex);
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    bufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    bufferDesc.initialState = RhiResourceState::VertexBuffer;
    mesh.rhiVertexBuffer = m_rhiDevice->createBuffer(
        bufferDesc, vertices.data(), vertices.size() * sizeof(SteveVertex));
    if (!mesh.rhiVertexBuffer.isValid()) {
        destroyMesh(mesh);
    }

    return mesh;
}

void HumanoidRenderer::destroyMesh(PartMesh& mesh) const {
    if (m_rhiDevice != nullptr && mesh.rhiVertexBuffer.isValid()) {
        m_rhiDevice->destroyBuffer(mesh.rhiVertexBuffer);
        mesh.rhiVertexBuffer = {};
    }
    mesh.vertexCount = 0;
}

HumanoidRenderer::PartMesh* HumanoidRenderer::getMeshForPart(ecs::StevePartType partType,
                                                              ecs::EntitySkinLayoutKind skinLayout) {
    return &m_skinLayoutMeshes[renderer::humanoidSkinLayoutIndex(skinLayout)]
                              [renderer::humanoidPartTypeIndex(partType)];
}

HumanoidRenderer::PartMesh* HumanoidRenderer::getMeshForEntityModelPart(const std::string& modelId,
                                                                         const std::string& partName) {
    const std::string key = modelId + "#" + partName;
    const auto existing = m_entityModelPartMeshes.find(key);
    if (existing != m_entityModelPartMeshes.end()) {
        return &existing->second;
    }

    const ecs::EntityModelDefinition* model = ecs::EntityModelRegistry::instance().findModel(modelId);
    if (model == nullptr) {
        return nullptr;
    }

    const ecs::EntityModelPartDefinition* part = model->findPart(partName);
    if (part == nullptr) {
        return nullptr;
    }

    PartMesh mesh = buildEntityModelPartMesh(*part, model->textureWidth, model->textureHeight);
    auto inserted = m_entityModelPartMeshes.emplace(key, std::move(mesh));
    return &inserted.first->second;
}

const HumanoidRenderer::TextureResource& HumanoidRenderer::requireTextureResource(
    const std::string& textureKey) {
    const auto existing = m_textureResources.find(textureKey);
    if (existing != m_textureResources.end()) {
        return existing->second;
    }

    TextureResource resource;
    resource.texture = m_resourceMgr->getGuiTextureHandle(textureKey);
    if (!resource.texture.isValid()) {
        std::abort();
    }
    RhiTextureViewDesc viewDesc;
    viewDesc.texture = resource.texture;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    resource.view = m_rhiDevice->createTextureView(viewDesc);
    if (!resource.view.isValid()) {
        std::abort();
    }
    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_gbufferRhiBindGroupLayout;
    RhiBindGroupEntry textureEntry;
    textureEntry.binding = 0u;
    textureEntry.resource.combinedTextureSampler = {resource.view, m_gbufferSampler};
    bindGroupDesc.entries.push_back(textureEntry);
    resource.gbufferBindGroup = m_rhiDevice->createBindGroup(bindGroupDesc);
    if (!resource.gbufferBindGroup.isValid()) {
        std::abort();
    }
    bindGroupDesc.layout = m_shadowRhiBindGroupLayout;
    resource.shadowBindGroup = m_rhiDevice->createBindGroup(bindGroupDesc);
    if (!resource.shadowBindGroup.isValid()) {
        std::abort();
    }
    return m_textureResources.emplace(textureKey, resource).first->second;
}

void HumanoidRenderer::init(ResourceMgr& resourceMgr, RhiDevice& rhiDevice) {
    m_resourceMgr = &resourceMgr;
    m_rhiDevice = &rhiDevice;
    createGBufferRhiResources();
    requireTextureResource("steve");

    const renderer::HumanoidSkinLayoutDefinitions& skinLayouts = renderer::humanoidSkinLayoutDefinitions();
    for (std::size_t layout = 0; layout < skinLayouts.size(); ++layout) {
        const renderer::HumanoidSkinLayoutDefinition& skinLayout = skinLayouts[layout];
        for (std::size_t part = 0; part < skinLayout.parts.size(); ++part) {
            m_skinLayoutMeshes[layout][part] = buildPartMesh(skinLayout.parts[part],
                                                             skinLayout.textureWidth,
                                                             skinLayout.textureHeight);
        }
    }
}

void HumanoidRenderer::shutdown() {
    for (auto& texturePair : m_textureResources) {
        if (texturePair.second.shadowBindGroup.isValid()) {
            m_rhiDevice->destroyBindGroup(texturePair.second.shadowBindGroup);
        }
        if (texturePair.second.gbufferBindGroup.isValid()) {
            m_rhiDevice->destroyBindGroup(texturePair.second.gbufferBindGroup);
        }
        if (texturePair.second.view.isValid()) {
            m_rhiDevice->destroyTextureView(texturePair.second.view);
        }
    }
    m_textureResources.clear();
    destroyGBufferRhiResources();
    m_preparedPartDraws.clear();
    m_currentModelMatrices.clear();
    for (auto& layoutMeshes : m_skinLayoutMeshes) {
        for (PartMesh& mesh : layoutMeshes) {
            destroyMesh(mesh);
        }
    }
    for (auto& pair : m_entityModelPartMeshes) {
        destroyMesh(pair.second);
    }
    m_entityModelPartMeshes.clear();
    m_rhiDevice = nullptr;
    m_resourceMgr = nullptr;
}

void HumanoidRenderer::prepareFrame(const IWorldView& worldView,
                                    ecs::GameplayRegistry& gameplayRegistry,
                                    const RenderMode mode) {
    auto& registry = gameplayRegistry.registry();
    m_preparedPartDraws.clear();
    m_currentModelMatrices.clear();

    const auto appendPart = [this](const entt::entity partEntity,
                                   const PartMesh* mesh,
                                   const TextureResource& texture,
                                   const glm::mat4& model,
                                   const glm::vec3& entityCenter,
                                   const glm::vec2& light,
                                   const float hurtFlash) {
        if (mesh == nullptr || !mesh->rhiVertexBuffer.isValid() || mesh->vertexCount == 0u) {
            return;
        }
        const auto previous = m_previousModelMatrices.find(partEntity);
        m_preparedPartDraws.push_back({
            mesh,
            &texture,
            model,
            previous != m_previousModelMatrices.end() ? previous->second : model,
            entityCenter,
            light,
            hurtFlash
        });
        m_currentModelMatrices[partEntity] = model;
    };

    const TextureResource& steveTexture = requireTextureResource("steve");
    auto steveView = registry.view<ecs::SteveTag, ecs::ChildrenComponent>();
    for (const entt::entity root : steveView) {
        if (!shouldRenderSteveRoot(registry, root, mode)) {
            continue;
        }
        const auto& rootChildren = steveView.get<ecs::ChildrenComponent>(root);
        glm::vec3 entityCenter(0.0f);
        bool hasCenter = false;
        for (const entt::entity child : rootChildren.children) {
            if (!registry.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(child)) {
                continue;
            }
            const auto& part = registry.get<ecs::StevePartComponent>(child);
            if (part.partType == ecs::StevePartType::Torso) {
                entityCenter = glm::vec3(registry.get<ecs::WorldTransformComponent>(child).worldMatrix[3]);
                hasCenter = true;
                break;
            }
        }
        if (!hasCenter) {
            continue;
        }
        const glm::vec2 light = queryWorldLight(worldView, entityCenter);
        const float hurtFlash = hurtFlashForRoot(registry, root);
        for (const entt::entity child : rootChildren.children) {
            if (registry.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(child)) {
                const auto& part = registry.get<ecs::StevePartComponent>(child);
                const auto& transform = registry.get<ecs::WorldTransformComponent>(child);
                appendPart(child,
                           getMeshForPart(part.partType, ecs::EntitySkinLayoutKind::Steve64x64),
                           steveTexture, transform.worldMatrix, entityCenter, light, hurtFlash);
            }
            const auto* children = registry.try_get<ecs::ChildrenComponent>(child);
            if (children == nullptr) {
                continue;
            }
            for (const entt::entity partEntity : children->children) {
                if (!registry.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(partEntity)) {
                    continue;
                }
                const auto& part = registry.get<ecs::StevePartComponent>(partEntity);
                const auto& transform = registry.get<ecs::WorldTransformComponent>(partEntity);
                appendPart(partEntity,
                           getMeshForPart(part.partType, ecs::EntitySkinLayoutKind::Steve64x64),
                           steveTexture, transform.worldMatrix, entityCenter, light, hurtFlash);
            }
        }
    }

    auto mobView = registry.view<ecs::MobTag, ecs::ChildrenComponent,
                                 ecs::MobVisualComponent, ecs::TransformComponent>();
    for (const entt::entity root : mobView) {
        const auto& visual = mobView.get<ecs::MobVisualComponent>(root);
        const auto& rootTransform = mobView.get<ecs::TransformComponent>(root);
        const auto& rootChildren = mobView.get<ecs::ChildrenComponent>(root);
        const TextureResource& texture = requireTextureResource(visual.textureKey);
        const glm::vec3 entityCenter = rootTransform.position +
            glm::vec3(0.0f, rootTransform.eyeHeight * 0.5f, 0.0f);
        const glm::vec2 light = queryWorldLight(worldView, entityCenter);
        const float hurtFlash = hurtFlashForRoot(registry, root);
        const auto* modelComponent = registry.try_get<ecs::EntityModelComponent>(root);
        std::vector<entt::entity> queue(rootChildren.children.begin(), rootChildren.children.end());
        for (std::size_t index = 0u; index < queue.size(); ++index) {
            const entt::entity partEntity = queue[index];
            if (const auto* children = registry.try_get<ecs::ChildrenComponent>(partEntity)) {
                queue.insert(queue.end(), children->children.begin(), children->children.end());
            }
            const auto* transform = registry.try_get<ecs::WorldTransformComponent>(partEntity);
            if (transform == nullptr) {
                continue;
            }
            PartMesh* mesh = nullptr;
            if (modelComponent != nullptr) {
                const auto* part = registry.try_get<ecs::EntityModelPartComponent>(partEntity);
                if (part != nullptr) {
                    mesh = getMeshForEntityModelPart(modelComponent->modelId, part->partName);
                }
            } else {
                const auto* part = registry.try_get<ecs::StevePartComponent>(partEntity);
                if (part != nullptr) {
                    mesh = getMeshForPart(part->partType, visual.skinLayout);
                }
            }
            const glm::mat4 model = applyMobVisualScale(
                transform->worldMatrix, rootTransform.position, visual.scale);
            appendPart(partEntity, mesh, texture, model, entityCenter, light, hurtFlash);
        }
    }
}

void HumanoidRenderer::finishFrame() {
    m_previousModelMatrices = m_currentModelMatrices;
}

void HumanoidRenderer::createGBufferRhiResources() {
    const auto vertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/entity_gbuffer_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/entity_gbuffer_rhi.frag");
    const auto shadowVertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/entity_shadow_rhi.vert");
    const auto shadowFragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/entity_shadow_rhi.frag");
    const auto forwardVertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/entity_forward_rhi.vert");
    const auto forwardFragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/entity_forward_rhi.frag");
    if (!vertexSource || !fragmentSource || !shadowVertexSource || !shadowFragmentSource ||
        !forwardVertexSource || !forwardFragmentSource) {
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
    m_gbufferRhiVertexShader = createShader("Humanoid.GBuffer.Vertex", RhiShaderStage::Vertex,
                                             *vertexSource);
    m_gbufferRhiFragmentShader = createShader("Humanoid.GBuffer.Fragment", RhiShaderStage::Fragment,
                                               *fragmentSource);
    m_shadowRhiVertexShader = createShader("Humanoid.Shadow.Vertex", RhiShaderStage::Vertex,
                                           *shadowVertexSource);
    m_shadowRhiFragmentShader = createShader("Humanoid.Shadow.Fragment", RhiShaderStage::Fragment,
                                             *shadowFragmentSource);
    m_forwardRhiVertexShader = createShader("Humanoid.Forward.Vertex", RhiShaderStage::Vertex,
                                            *forwardVertexSource);
    m_forwardRhiFragmentShader = createShader("Humanoid.Forward.Fragment", RhiShaderStage::Fragment,
                                              *forwardFragmentSource);
    RhiSamplerDesc samplerDesc;
    samplerDesc.addressU = RhiAddressMode::ClampToEdge;
    samplerDesc.addressV = RhiAddressMode::ClampToEdge;
    m_gbufferSampler = m_rhiDevice->createSampler(samplerDesc);
    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "Humanoid.GBuffer.BindGroupLayout";
    bindGroupLayoutDesc.entries.push_back({0u, RhiBindingType::CombinedTextureSampler,
                                           rhiFlag(RhiShaderStage::Fragment), 1u});
    m_gbufferRhiBindGroupLayout = m_rhiDevice->createBindGroupLayout(bindGroupLayoutDesc);
    bindGroupLayoutDesc.debugName = "Humanoid.Shadow.BindGroupLayout";
    m_shadowRhiBindGroupLayout = m_rhiDevice->createBindGroupLayout(bindGroupLayoutDesc);
    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "Humanoid.GBuffer.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_gbufferRhiBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(glm::mat4) * 4u + sizeof(glm::vec4);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) |
                                            rhiFlag(RhiShaderStage::Fragment);
    m_gbufferRhiPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    pipelineLayoutDesc.debugName = "Humanoid.Shadow.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts[0] = m_shadowRhiBindGroupLayout;
    pipelineLayoutDesc.pushConstantBytes = sizeof(glm::mat4) * 2u;
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex);
    m_shadowRhiPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    pipelineLayoutDesc.debugName = "Humanoid.Forward.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts[0] = m_gbufferRhiBindGroupLayout;
    pipelineLayoutDesc.pushConstantBytes = sizeof(glm::mat4) * 2u + sizeof(glm::vec4);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) |
                                            rhiFlag(RhiShaderStage::Fragment);
    m_forwardRhiPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "Humanoid.GBuffer.Pipeline";
    pipelineDesc.vertexShader = m_gbufferRhiVertexShader;
    pipelineDesc.fragmentShader = m_gbufferRhiFragmentShader;
    pipelineDesc.layout = m_gbufferRhiPipelineLayout;
    pipelineDesc.vertexInput.bindings = {{0u, sizeof(SteveVertex), RhiVertexInputRate::Vertex}};
    pipelineDesc.vertexInput.attributes = {
        {0u, 0u, RhiVertexFormat::Float3, offsetof(SteveVertex, x)},
        {1u, 0u, RhiVertexFormat::Float2, offsetof(SteveVertex, u)},
        {2u, 0u, RhiVertexFormat::Float3, offsetof(SteveVertex, nx)}
    };
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.colorFormats = {RhiTextureFormat::Rgba8Unorm, RhiTextureFormat::Rgba16Float,
        RhiTextureFormat::Rg8Unorm, RhiTextureFormat::Rgba8Unorm, RhiTextureFormat::Rgba8Unorm,
        RhiTextureFormat::Rg16Float};
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    pipelineDesc.blend.attachments.resize(6u);
    m_gbufferRhiPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    pipelineDesc.debugName = "Humanoid.Shadow.Pipeline";
    pipelineDesc.vertexShader = m_shadowRhiVertexShader;
    pipelineDesc.fragmentShader = m_shadowRhiFragmentShader;
    pipelineDesc.layout = m_shadowRhiPipelineLayout;
    pipelineDesc.colorFormats.clear();
    pipelineDesc.blend.attachments.clear();
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    m_shadowRhiPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    pipelineDesc.debugName = "Humanoid.Forward.Pipeline";
    pipelineDesc.vertexShader = m_forwardRhiVertexShader;
    pipelineDesc.fragmentShader = m_forwardRhiFragmentShader;
    pipelineDesc.layout = m_forwardRhiPipelineLayout;
    pipelineDesc.colorFormats = {m_rhiDevice->swapchainColorFormat()};
    pipelineDesc.depthFormat = m_rhiDevice->swapchainDepthStencilFormat();
    pipelineDesc.blend.attachments.resize(1u);
    m_forwardRhiPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    pipelineDesc.debugName = "Humanoid.InventoryPreview.Pipeline";
    pipelineDesc.raster.scissorEnabled = true;
    pipelineDesc.depthFormat = RhiTextureFormat::Undefined;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::LessOrEqual;
    m_inventoryPreviewPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    if (!m_gbufferRhiVertexShader.isValid() || !m_gbufferRhiFragmentShader.isValid() ||
        !m_gbufferSampler.isValid() || !m_gbufferRhiBindGroupLayout.isValid() ||
        !m_gbufferRhiPipelineLayout.isValid() || !m_gbufferRhiPipeline.isValid() ||
        !m_shadowRhiVertexShader.isValid() || !m_shadowRhiFragmentShader.isValid() ||
        !m_shadowRhiBindGroupLayout.isValid() || !m_shadowRhiPipelineLayout.isValid() ||
        !m_shadowRhiPipeline.isValid() || !m_forwardRhiVertexShader.isValid() ||
        !m_forwardRhiFragmentShader.isValid() || !m_forwardRhiPipelineLayout.isValid() ||
        !m_forwardRhiPipeline.isValid() || !m_inventoryPreviewPipeline.isValid()) {
        std::abort();
    }
}

void HumanoidRenderer::destroyGBufferRhiResources() {
    if (m_inventoryPreviewPipeline.isValid()) m_rhiDevice->destroyPipeline(m_inventoryPreviewPipeline);
    if (m_forwardRhiPipeline.isValid()) m_rhiDevice->destroyPipeline(m_forwardRhiPipeline);
    if (m_forwardRhiPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_forwardRhiPipelineLayout);
    if (m_forwardRhiFragmentShader.isValid()) m_rhiDevice->destroyShader(m_forwardRhiFragmentShader);
    if (m_forwardRhiVertexShader.isValid()) m_rhiDevice->destroyShader(m_forwardRhiVertexShader);
    if (m_shadowRhiPipeline.isValid()) m_rhiDevice->destroyPipeline(m_shadowRhiPipeline);
    if (m_shadowRhiPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_shadowRhiPipelineLayout);
    if (m_shadowRhiBindGroupLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_shadowRhiBindGroupLayout);
    if (m_shadowRhiFragmentShader.isValid()) m_rhiDevice->destroyShader(m_shadowRhiFragmentShader);
    if (m_shadowRhiVertexShader.isValid()) m_rhiDevice->destroyShader(m_shadowRhiVertexShader);
    if (m_gbufferRhiPipeline.isValid()) m_rhiDevice->destroyPipeline(m_gbufferRhiPipeline);
    if (m_gbufferRhiPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_gbufferRhiPipelineLayout);
    if (m_gbufferRhiBindGroupLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_gbufferRhiBindGroupLayout);
    if (m_gbufferSampler.isValid()) m_rhiDevice->destroySampler(m_gbufferSampler);
    if (m_gbufferRhiFragmentShader.isValid()) m_rhiDevice->destroyShader(m_gbufferRhiFragmentShader);
    if (m_gbufferRhiVertexShader.isValid()) m_rhiDevice->destroyShader(m_gbufferRhiVertexShader);
    m_gbufferRhiPipeline = {};
    m_gbufferRhiPipelineLayout = {};
    m_gbufferRhiBindGroupLayout = {};
    m_gbufferSampler = {};
    m_gbufferRhiFragmentShader = {};
    m_gbufferRhiVertexShader = {};
    m_shadowRhiPipeline = {};
    m_shadowRhiPipelineLayout = {};
    m_shadowRhiBindGroupLayout = {};
    m_shadowRhiFragmentShader = {};
    m_shadowRhiVertexShader = {};
    m_forwardRhiPipeline = {};
    m_forwardRhiPipelineLayout = {};
    m_forwardRhiFragmentShader = {};
    m_forwardRhiVertexShader = {};
    m_inventoryPreviewPipeline = {};
}

void HumanoidRenderer::renderPreparedToGBuffer(RhiCommandList& commandList,
                                               const glm::mat4& viewProj,
                                               const glm::mat4& previousViewProj) {
    struct PushConstants {
        glm::mat4 viewProj;
        glm::mat4 previousViewProj;
        glm::mat4 model;
        glm::mat4 previousModel;
        glm::vec4 lightHurt;
    };
    commandList.setGraphicsPipeline(m_gbufferRhiPipeline);
    for (const PreparedPartDraw& draw : m_preparedPartDraws) {
        const PushConstants pushConstants{
            viewProj, previousViewProj, draw.model, draw.previousModel,
            glm::vec4(draw.light, draw.hurtFlash, 0.0f)
        };
        commandList.setBindGroup(0u, draw.texture->gbufferBindGroup);
        commandList.setVertexBuffer(0u, draw.mesh->rhiVertexBuffer, 0u);
        commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                  rhiFlag(RhiShaderStage::Vertex) |
                                  rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(draw.mesh->vertexCount, 1u, 0u, 0u);
    }
}

void HumanoidRenderer::renderPreparedToShadowMap(RhiCommandList& commandList,
                                                 const glm::mat4& shadowViewProj,
                                                 const glm::vec3& cameraPos,
                                                 const float splitNear,
                                                 const float splitFar) {
    struct PushConstants { glm::mat4 viewProj; glm::mat4 model; };
    const float minDistance = splitNear - 4.0f;
    const float maxDistance = splitFar + 4.0f;
    commandList.setGraphicsPipeline(m_shadowRhiPipeline);
    for (const PreparedPartDraw& draw : m_preparedPartDraws) {
        const float distance = glm::length(draw.entityCenter - cameraPos);
        if ((minDistance > 0.0f && distance < minDistance) || distance > maxDistance) {
            continue;
        }
        const PushConstants pushConstants{shadowViewProj, draw.model};
        commandList.setBindGroup(0u, draw.texture->shadowBindGroup);
        commandList.setVertexBuffer(0u, draw.mesh->rhiVertexBuffer, 0u);
        commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                  rhiFlag(RhiShaderStage::Vertex));
        commandList.draw(draw.mesh->vertexCount, 1u, 0u, 0u);
    }
}

void HumanoidRenderer::renderPreparedForward(RhiCommandList& commandList,
                                             const glm::mat4& viewProj,
                                             const float skyIntensity) {
    struct PushConstants { glm::mat4 viewProj; glm::mat4 model; glm::vec4 lighting; };
    commandList.setGraphicsPipeline(m_forwardRhiPipeline);
    for (const PreparedPartDraw& draw : m_preparedPartDraws) {
        const PushConstants pushConstants{
            viewProj, draw.model, glm::vec4(draw.light, skyIntensity, draw.hurtFlash)
        };
        commandList.setBindGroup(0u, draw.texture->gbufferBindGroup);
        commandList.setVertexBuffer(0u, draw.mesh->rhiVertexBuffer, 0u);
        commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                  rhiFlag(RhiShaderStage::Vertex) |
                                  rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(draw.mesh->vertexCount, 1u, 0u, 0u);
    }
}

 void HumanoidRenderer::renderInventoryPreview(RhiCommandList& commandList,
                                              const float x,
                                              const float y,
                                              const float width,
                                              const float height,
                                              const float uiScale,
                                              const float pointerX,
                                              const float pointerY,
                                              const float timeSeconds,
                                              const int screenWidth,
                                              const int screenHeight) {
    if (uiScale <= 0.0f || width <= 0.0f || height <= 0.0f) {
        return;
    }
    const int32_t viewportX = static_cast<int32_t>(std::lround(x * uiScale));
    const int32_t viewportY = static_cast<int32_t>(std::lround(y * uiScale));
    const uint32_t viewportW = static_cast<uint32_t>(std::max(1l, std::lround(width * uiScale)));
    const uint32_t viewportH = static_cast<uint32_t>(std::max(1l, std::lround(height * uiScale)));
    const RhiRect2D previewRect{viewportX, viewportY, viewportW, viewportH};
    commandList.clearDepthAttachment(1.0f, previewRect);
    commandList.setViewport({static_cast<float>(viewportX), static_cast<float>(viewportY),
                             static_cast<float>(viewportW), static_cast<float>(viewportH), 0.0f, 1.0f});
    commandList.setScissor(previewRect);

    const float aspect = static_cast<float>(viewportW) / static_cast<float>(viewportH);
    const glm::mat4 projection = glm::perspective(glm::radians(28.0f), aspect, 0.1f, 20.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, -0.02f, 4.9f),
                                       glm::vec3(0.0f, -0.02f, 0.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
    const float previewCenterX = x + width * 0.5f;
    const float previewCenterY = y + height * 0.58f;
    const float lookX = std::clamp((pointerX - previewCenterX) / std::max(1.0f, width * 1.2f), -1.0f, 1.0f);
    const float lookY = std::clamp((pointerY - previewCenterY) / std::max(1.0f, height * 1.2f), -1.0f, 1.0f);
    const float dt = m_inventoryPreviewLastTime >= 0.0f
        ? std::clamp(timeSeconds - m_inventoryPreviewLastTime, 0.0f, 0.1f)
        : 0.0f;
    m_inventoryPreviewLastTime = timeSeconds;

    const float headAlpha = (dt > 0.0f) ? (1.0f - std::exp(-dt * 18.0f)) : 1.0f;
    const float bodyAlpha = (dt > 0.0f) ? (1.0f - std::exp(-dt * 7.0f)) : 1.0f;
    m_inventoryPreviewHeadLookX += (lookX - m_inventoryPreviewHeadLookX) * headAlpha;
    m_inventoryPreviewHeadLookY += (lookY - m_inventoryPreviewHeadLookY) * headAlpha;
    m_inventoryPreviewBodyLookX += (lookX - m_inventoryPreviewBodyLookX) * bodyAlpha;
    m_inventoryPreviewBodyLookY += (lookY - m_inventoryPreviewBodyLookY) * bodyAlpha;

    const float bodyYaw = glm::radians(12.0f) * m_inventoryPreviewBodyLookX;
    const float bodyPitch = glm::radians(-5.0f) * m_inventoryPreviewBodyLookY;
    const float headYaw = glm::radians(28.0f) * m_inventoryPreviewHeadLookX;
    const float headPitch = glm::radians(-13.0f) * m_inventoryPreviewHeadLookY;

    const glm::mat4 root =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.96f, 0.0f))
        * glm::rotate(glm::mat4(1.0f), bodyYaw, glm::vec3(0.0f, 1.0f, 0.0f))
        * glm::rotate(glm::mat4(1.0f), bodyPitch, glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::mat4 torso = root * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.125f, 0.0f));

    struct PushConstants { glm::mat4 viewProj; glm::mat4 model; glm::vec4 lighting; };
    const TextureResource& steveTexture = requireTextureResource("steve");
    commandList.setGraphicsPipeline(m_inventoryPreviewPipeline);
    commandList.setBindGroup(0u, steveTexture.gbufferBindGroup);
    const glm::mat4 viewProj = projection * view;

    const auto drawPart = [&](ecs::StevePartType partType, const glm::mat4& model) {
        PartMesh* mesh = getMeshForPart(partType, ecs::EntitySkinLayoutKind::Steve64x64);
        if (mesh == nullptr || !mesh->rhiVertexBuffer.isValid() || mesh->vertexCount == 0u) {
            return;
        }
        const PushConstants pushConstants{viewProj, model, glm::vec4(1.0f, 0.0f, 1.0f, 0.0f)};
        commandList.setVertexBuffer(0u, mesh->rhiVertexBuffer, 0u);
        commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                  rhiFlag(RhiShaderStage::Vertex) |
                                  rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(mesh->vertexCount, 1u, 0u, 0u);
    };

    drawPart(ecs::StevePartType::Torso, torso);
    drawPart(ecs::StevePartType::Head,
             torso
             * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.375f, 0.0f))
             * glm::rotate(glm::mat4(1.0f), headYaw, glm::vec3(0.0f, 1.0f, 0.0f))
             * glm::rotate(glm::mat4(1.0f), headPitch, glm::vec3(1.0f, 0.0f, 0.0f)));
    drawPart(ecs::StevePartType::RightArm,
             torso * glm::translate(glm::mat4(1.0f), glm::vec3(-0.3125f, 0.375f, 0.0f)));
    drawPart(ecs::StevePartType::LeftArm,
             torso * glm::translate(glm::mat4(1.0f), glm::vec3(0.3125f, 0.375f, 0.0f)));
    drawPart(ecs::StevePartType::RightLeg,
             torso * glm::translate(glm::mat4(1.0f), glm::vec3(-0.125f, -0.375f, 0.0f)));
    drawPart(ecs::StevePartType::LeftLeg,
             torso * glm::translate(glm::mat4(1.0f), glm::vec3(0.125f, -0.375f, 0.0f)));

    const uint32_t fullWidth = static_cast<uint32_t>(std::max(1l, std::lround(screenWidth * uiScale)));
    const uint32_t fullHeight = static_cast<uint32_t>(std::max(1l, std::lround(screenHeight * uiScale)));
    commandList.setViewport({0.0f, 0.0f, static_cast<float>(fullWidth),
                             static_cast<float>(fullHeight), 0.0f, 1.0f});
    commandList.setScissor({0, 0, fullWidth, fullHeight});

}

 glm::vec2 HumanoidRenderer::queryWorldLight(const IWorldView& worldView, const glm::vec3& position) {
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
