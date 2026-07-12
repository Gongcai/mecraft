#include "RainRenderer.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <random>
#include <glm/gtc/random.hpp>

#include "../resource/ResourceMgr.h"
#include "../renderer/rhi/RhiCommandList.h"
#include "../renderer/rhi/RhiDevice.h"
#include "../renderer/rhi/RhiShaderSourceLoader.h"

static std::mt19937 s_rng{42};
static constexpr int PRECIP_ATLAS_COLUMNS = 64;

namespace {
struct RainPushConstants {
    glm::mat4 viewProj;
    glm::vec4 colorStrength;
    glm::vec4 alphaScreenDepth;
    glm::ivec4 controls;
};

[[nodiscard]] bool sameTexture(const RhiTextureHandle lhs, const RhiTextureHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}
} // namespace

bool RainRenderer::init(ResourceMgr& resourceMgr) {
    m_rhiDevice = &resourceMgr.rhiDevice();
    m_rainTex = resourceMgr.getTexture2DHandle("rain");
    m_snowTex = resourceMgr.getTexture2DHandle("snow");
    if (!m_rainTex.isValid() || !m_snowTex.isValid()) {
        shutdown();
        return false;
    }
    return createRhiResources();
}

void RainRenderer::shutdown() {
    destroyRhiResources();
    m_rainTex = {};
    m_snowTex = {};
}

void RainRenderer::prepareFrame(const glm::vec3& cameraPos,
                                const glm::mat4& view,
                                const float rainStrength,
                                const float snowStrength,
                                float dt) {
    dt = std::clamp(dt, 0.001f, 0.1f);
    if (rainStrength > 0.01f) {
        ensureDrops(m_rainDrops, MAX_RAIN_DROPS, cameraPos);
        updateDrops(m_rainDrops, dt, RAIN_FALL_SPEED, cameraPos);
        buildVertices(m_rainDrops, rainStrength, RAIN_DROP_LENGTH, 0.006f,
                      true, view, m_rainVertices);
    } else {
        m_rainVertices.clear();
    }
    if (snowStrength > 0.01f) {
        ensureDrops(m_snowDrops, MAX_SNOW_DROPS, cameraPos);
        updateDrops(m_snowDrops, dt, SNOW_FALL_SPEED, cameraPos);
        buildVertices(m_snowDrops, snowStrength, SNOW_DROP_LENGTH, 0.025f,
                      false, view, m_snowVertices);
    } else {
        m_snowVertices.clear();
    }
}

void RainRenderer::uploadFrame(RhiCommandList& commandList) {
    if (!m_vertexBuffer.isValid()) {
        return;
    }
    if (m_rainVertices.empty() && m_snowVertices.empty()) {
        return;
    }
    commandList.bufferBarrier({m_vertexBuffer, RhiResourceState::VertexBuffer,
                               RhiResourceState::TransferDst});
    if (!m_rainVertices.empty()) {
        commandList.updateBuffer(m_vertexBuffer, RAIN_VERTEX_OFFSET,
                                 m_rainVertices.data(), m_rainVertices.size() * sizeof(float));
    }
    if (!m_snowVertices.empty()) {
        commandList.updateBuffer(m_vertexBuffer, SNOW_VERTEX_OFFSET,
                                 m_snowVertices.data(), m_snowVertices.size() * sizeof(float));
    }
    commandList.bufferBarrier({m_vertexBuffer, RhiResourceState::TransferDst,
                               RhiResourceState::VertexBuffer});
}

void RainRenderer::ensureDrops(std::vector<PrecipDrop>& drops, int maxDrops, const glm::vec3& cameraPos) {
    if (drops.empty()) {
        drops.reserve(maxDrops);
        std::uniform_real_distribution<float> distRadius(0.0f, SPAWN_RADIUS);
        std::uniform_real_distribution<float> distAngle(0.0f, 6.2831853f);
        std::uniform_real_distribution<float> distHeight(0.0f, SPAWN_HEIGHT);
        std::uniform_real_distribution<float> distSpeed(0.8f, 1.2f);
        std::uniform_int_distribution<int> distCol(0, PRECIP_ATLAS_COLUMNS - 1);

        for (int i = 0; i < maxDrops; ++i) {
            PrecipDrop d;
            float r = distRadius(s_rng);
            float a = distAngle(s_rng);
            d.position = cameraPos + glm::vec3(r * cosf(a), distHeight(s_rng), r * sinf(a));
            d.speed = distSpeed(s_rng); // multiplier, base speed applied at render
            d.length = distSpeed(s_rng); // multiplier, base length applied at render
            d.texU = (distCol(s_rng) + 0.5f) / static_cast<float>(PRECIP_ATLAS_COLUMNS);
            drops.push_back(d);
        }
    }
}

void RainRenderer::updateDrops(std::vector<PrecipDrop>& drops, float dt, float baseSpeed, const glm::vec3& cameraPos) {
    m_time += dt;
    if (m_time > 628.0f) m_time -= 628.0f;  // wrap at ~100*PI to prevent float precision loss
    for (auto& d : drops) {
        d.position.y -= d.speed * baseSpeed * dt;
    }
    wrapDrops(drops, cameraPos);
}

void RainRenderer::wrapDrops(std::vector<PrecipDrop>& drops, const glm::vec3& cameraPos) {
    std::uniform_real_distribution<float> distRadius(0.0f, SPAWN_RADIUS);
    std::uniform_real_distribution<float> distAngle(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> distHeight(0.0f, SPAWN_HEIGHT);
    std::uniform_real_distribution<float> distSpeed(0.8f, 1.2f);
    std::uniform_int_distribution<int> distCol(0, PRECIP_ATLAS_COLUMNS - 1);

    for (auto& d : drops) {
        const glm::vec2 toCamera = glm::vec2(d.position.x - cameraPos.x, d.position.z - cameraPos.z);
        const bool outsideCylinder = glm::dot(toCamera, toCamera) > SPAWN_RADIUS * SPAWN_RADIUS;
        const bool belowCamera = d.position.y < cameraPos.y + DESPAWN_BELOW;
        const bool farAboveCamera = d.position.y > cameraPos.y + SPAWN_HEIGHT + 4.0f;
        if (outsideCylinder || belowCamera || farAboveCamera) {
            float r = distRadius(s_rng);
            float a = distAngle(s_rng);
            d.position = cameraPos + glm::vec3(r * cosf(a), distHeight(s_rng), r * sinf(a));
            d.speed = distSpeed(s_rng);
            d.texU = (distCol(s_rng) + 0.5f) / static_cast<float>(PRECIP_ATLAS_COLUMNS);
        }
    }
}

void RainRenderer::buildVertices(const std::vector<PrecipDrop>& drops,
                                 const float strength,
                                 const float dropLength,
                                 const float streakWidth,
                                 const bool proceduralLines,
                                 const glm::mat4& view,
                                 std::vector<float>& vertices) const {
    const glm::vec3 right(view[0][0], view[1][0], view[2][0]);
    const float wind = std::sin(m_time * 0.1f) * 0.25f + 0.25f;
    constexpr float kWindAngle = 3.14159f / 60.0f;
    const glm::vec3 fallDir = glm::normalize(glm::vec3(
        -wind * std::cos(kWindAngle),
        -1.0f,
        -wind * std::sin(kWindAngle) * 0.3f));
    const int visibleCount = std::min(static_cast<int>(drops.size()),
                                      static_cast<int>(drops.size() * strength));

    vertices.clear();
    vertices.reserve(static_cast<size_t>(visibleCount) * 6u * 5u);
    for (int i = 0; i < visibleCount; ++i) {
        const PrecipDrop& drop = drops[static_cast<size_t>(i)];
        const glm::vec3 top = drop.position;
        const glm::vec3 bottom = top + fallDir * (dropLength * drop.length);
        const glm::vec3 rightOffset = right * streakWidth;
        const glm::vec3 corners[4] = {
            top - rightOffset,
            top + rightOffset,
            bottom + rightOffset,
            bottom - rightOffset
        };
        const float u0 = proceduralLines ? 0.0f : drop.texU;
        const float u1 = proceduralLines ? 1.0f : drop.texU;
        const int cornerIndices[6] = {0, 1, 2, 0, 2, 3};
        const float u[4] = {u0, u1, u1, u0};
        const float v[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        for (const int cornerIndex : cornerIndices) {
            const glm::vec3& position = corners[cornerIndex];
            vertices.insert(vertices.end(), {position.x, position.y, position.z,
                                             u[cornerIndex], v[cornerIndex]});
        }
    }
}

void RainRenderer::renderPrecipitation(RhiCommandList& commandList,
                                        const glm::mat4& projection,
                                        const glm::mat4& view,
                                        RhiTextureHandle texture,
                                        const std::vector<float>& vertices,
                                        float strength,
                                        float skyLightAtCamera,
                                        float alphaScale,
                                        const glm::vec3& color,
                                        bool proceduralLines,
                                        const RhiTextureHandle sceneDepthTexture,
                                        const glm::vec2& screenSize,
                                        bool hardwareDepthTest) {
    if (vertices.empty() || strength < 0.01f || skyLightAtCamera < 0.05f) {
        return;
    }
    const bool depthFade = sceneDepthTexture.isValid();
    if (depthFade == hardwareDepthTest || !createBindGroups(sceneDepthTexture)) {
        return;
    }

    const bool rain = sameTexture(texture, m_rainTex);
    const RhiPipelineHandle pipeline = depthFade ? m_depthSamplePipeline : m_depthTestPipeline;
    const RhiBindGroupHandle bindGroup = depthFade
        ? (rain ? m_rainDepthBindGroup : m_snowDepthBindGroup)
        : (rain ? m_rainBindGroup : m_snowBindGroup);
    const uint64_t vertexOffset = rain ? RAIN_VERTEX_OFFSET : SNOW_VERTEX_OFFSET;
    if (!pipeline.isValid() || !bindGroup.isValid()) {
        return;
    }

    const RainPushConstants pushConstants{
        projection * view,
        glm::vec4(color, strength * skyLightAtCamera),
        glm::vec4(alphaScale, screenSize.x, screenSize.y, depthFade ? 1.0f : 0.0f),
        glm::ivec4(proceduralLines ? 1 : 0, depthFade ? 1 : 0, 0, 0)
    };
    commandList.setGraphicsPipeline(pipeline);
    commandList.setBindGroup(0u, bindGroup);
    commandList.setVertexBuffer(0u, m_vertexBuffer, vertexOffset);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                              rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(static_cast<uint32_t>(vertices.size() / 5u), 1u, 0u, 0u);
}

void RainRenderer::render(RhiCommandList& commandList,
                           const glm::mat4& projection,
                           const glm::mat4& view,
                           float rainStrength,
                           float skyLightAtCamera,
                           float alphaScale,
                           const RhiTextureHandle sceneDepthTexture,
                           const glm::vec2& screenSize,
                           bool hardwareDepthTest) {
    renderPrecipitation(commandList, projection, view,
                        m_rainTex, m_rainVertices,
                        rainStrength, skyLightAtCamera,
                        alphaScale,
                        glm::vec3(0.72f, 0.78f, 0.85f), // rain blue-gray
                        true,
                        sceneDepthTexture,
                        screenSize,
                        hardwareDepthTest);
}

void RainRenderer::renderSnow(RhiCommandList& commandList,
                               const glm::mat4& projection,
                               const glm::mat4& view,
                               float snowStrength,
                               float skyLightAtCamera,
                               float alphaScale,
                               const RhiTextureHandle sceneDepthTexture,
                               const glm::vec2& screenSize,
                               bool hardwareDepthTest) {
    renderPrecipitation(commandList, projection, view,
                        m_snowTex, m_snowVertices,
                        snowStrength, skyLightAtCamera,
                        alphaScale,
                        glm::vec3(0.92f, 0.95f, 1.0f), // snow white
                        false,
                        sceneDepthTexture,
                        screenSize,
                        hardwareDepthTest);
}

bool RainRenderer::createRhiResources() {
    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/rain_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/rain_rhi.frag");
    renderer::rhi::RhiShaderSourceOptions depthOptions;
    depthOptions.preprocessorDefinitions.push_back("RAIN_SCENE_DEPTH");
    const std::optional<std::string> depthFragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/rain_rhi.frag", depthOptions);
    if (!vertexSource.has_value() || !fragmentSource.has_value() ||
        !depthFragmentSource.has_value()) {
        destroyRhiResources();
        return false;
    }

    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "Weather.VertexBuffer";
    bufferDesc.size = RAIN_VERTEX_BYTES + SNOW_VERTEX_BYTES;
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex) | rhiFlag(RhiBufferUsage::TransferDst);
    bufferDesc.memoryUsage = RhiMemoryUsage::CpuToGpu;
    bufferDesc.initialState = RhiResourceState::VertexBuffer;
    m_vertexBuffer = m_rhiDevice->createBuffer(bufferDesc, nullptr, 0u);

    RhiTextureViewDesc viewDesc;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.texture = m_rainTex;
    m_rainTextureView = m_rhiDevice->createTextureView(viewDesc);
    viewDesc.texture = m_snowTex;
    m_snowTextureView = m_rhiDevice->createTextureView(viewDesc);

    RhiSamplerDesc precipitationSamplerDesc;
    precipitationSamplerDesc.minFilter = RhiFilter::Linear;
    precipitationSamplerDesc.magFilter = RhiFilter::Linear;
    precipitationSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    m_precipitationSampler = m_rhiDevice->createSampler(precipitationSamplerDesc);
    RhiSamplerDesc depthSamplerDesc = precipitationSamplerDesc;
    depthSamplerDesc.minFilter = RhiFilter::Nearest;
    depthSamplerDesc.magFilter = RhiFilter::Nearest;
    m_depthSampler = m_rhiDevice->createSampler(depthSamplerDesc);

    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "Weather.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str();
    shaderDesc.sourceSize = vertexSource->size();
    m_vertexShader = m_rhiDevice->createShader(shaderDesc);
    shaderDesc.debugName = "Weather.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = fragmentSource->c_str();
    shaderDesc.sourceSize = fragmentSource->size();
    m_fragmentShader = m_rhiDevice->createShader(shaderDesc);
    shaderDesc.debugName = "Weather.DepthFragment";
    shaderDesc.source = depthFragmentSource->c_str();
    shaderDesc.sourceSize = depthFragmentSource->size();
    m_depthFragmentShader = m_rhiDevice->createShader(shaderDesc);

    RhiBindGroupLayoutDesc layoutDesc;
    layoutDesc.debugName = "Weather.BindGroupLayout";
    layoutDesc.entries.push_back({0u, RhiBindingType::CombinedTextureSampler,
                                  rhiFlag(RhiShaderStage::Fragment), 1u});
    m_bindGroupLayout = m_rhiDevice->createBindGroupLayout(layoutDesc);
    layoutDesc.debugName = "Weather.DepthBindGroupLayout";
    layoutDesc.entries.push_back({1u, RhiBindingType::CombinedTextureSampler,
                                  rhiFlag(RhiShaderStage::Fragment), 1u});
    m_depthBindGroupLayout = m_rhiDevice->createBindGroupLayout(layoutDesc);

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "Weather.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(RainPushConstants);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) |
                                             rhiFlag(RhiShaderStage::Fragment);
    m_pipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    pipelineLayoutDesc.debugName = "Weather.DepthPipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts[0] = m_depthBindGroupLayout;
    m_depthPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = m_vertexShader;
    pipelineDesc.fragmentShader = m_fragmentShader;
    pipelineDesc.layout = m_pipelineLayout;
    pipelineDesc.vertexInput.bindings.push_back({0u, 5u * sizeof(float), RhiVertexInputRate::Vertex});
    pipelineDesc.vertexInput.attributes.push_back({0u, 0u, RhiVertexFormat::Float3, 0u});
    pipelineDesc.vertexInput.attributes.push_back({1u, 0u, RhiVertexFormat::Float2, 3u * sizeof(float)});
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::LessOrEqual;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    RhiBlendAttachmentState blend;
    blend.blendEnabled = true;
    blend.srcColor = RhiBlendFactor::SrcAlpha;
    blend.dstColor = RhiBlendFactor::OneMinusSrcAlpha;
    blend.srcAlpha = RhiBlendFactor::One;
    blend.dstAlpha = RhiBlendFactor::OneMinusSrcAlpha;
    pipelineDesc.blend.attachments.push_back(blend);
    pipelineDesc.debugName = "Weather.DepthTestPipeline";
    m_depthTestPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    pipelineDesc.debugName = "Weather.DepthSamplePipeline";
    pipelineDesc.fragmentShader = m_depthFragmentShader;
    pipelineDesc.layout = m_depthPipelineLayout;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    m_depthSamplePipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);

    if (!m_vertexBuffer.isValid() || !m_rainTextureView.isValid() ||
        !m_snowTextureView.isValid() || !m_precipitationSampler.isValid() ||
        !m_depthSampler.isValid() || !m_vertexShader.isValid() ||
        !m_fragmentShader.isValid() || !m_depthFragmentShader.isValid() ||
        !m_bindGroupLayout.isValid() || !m_depthBindGroupLayout.isValid() ||
        !m_pipelineLayout.isValid() || !m_depthPipelineLayout.isValid() ||
        !m_depthTestPipeline.isValid() || !m_depthSamplePipeline.isValid() ||
        !createBindGroups({})) {
        destroyRhiResources();
        return false;
    }
    return true;
}

bool RainRenderer::createBindGroups(const RhiTextureHandle sceneDepthTexture) {
    if (m_rainBindGroup.isValid() && m_snowBindGroup.isValid() &&
        sameTexture(m_boundSceneDepthTexture, sceneDepthTexture)) {
        return !sceneDepthTexture.isValid() ||
               (m_rainDepthBindGroup.isValid() && m_snowDepthBindGroup.isValid());
    }
    destroyBindGroups();

    auto createPrecipitationBindGroup = [&](const RhiTextureViewHandle precipitationView) {
        RhiBindGroupDesc desc;
        desc.layout = m_bindGroupLayout;
        RhiBindGroupEntry entry;
        entry.binding = 0u;
        entry.resource.combinedTextureSampler = {precipitationView, m_precipitationSampler};
        desc.entries.push_back(entry);
        return m_rhiDevice->createBindGroup(desc);
    };
    m_rainBindGroup = createPrecipitationBindGroup(m_rainTextureView);
    m_snowBindGroup = createPrecipitationBindGroup(m_snowTextureView);
    if (!m_rainBindGroup.isValid() || !m_snowBindGroup.isValid()) {
        destroyBindGroups();
        return false;
    }
    if (!sceneDepthTexture.isValid()) {
        m_boundSceneDepthTexture = {};
        return true;
    }

    RhiTextureViewDesc depthViewDesc;
    depthViewDesc.texture = sceneDepthTexture;
    depthViewDesc.viewType = RhiTextureViewType::Texture2D;
    m_sceneDepthTextureView = m_rhiDevice->createTextureView(depthViewDesc);
    if (!m_sceneDepthTextureView.isValid()) {
        destroyBindGroups();
        return false;
    }
    auto createDepthBindGroup = [&](const RhiTextureViewHandle precipitationView) {
        RhiBindGroupDesc desc;
        desc.layout = m_depthBindGroupLayout;
        RhiBindGroupEntry precipitationEntry;
        precipitationEntry.binding = 0u;
        precipitationEntry.resource.combinedTextureSampler = {precipitationView, m_precipitationSampler};
        desc.entries.push_back(precipitationEntry);
        RhiBindGroupEntry depthEntry;
        depthEntry.binding = 1u;
        depthEntry.resource.combinedTextureSampler = {m_sceneDepthTextureView, m_depthSampler};
        desc.entries.push_back(depthEntry);
        return m_rhiDevice->createBindGroup(desc);
    };
    m_rainDepthBindGroup = createDepthBindGroup(m_rainTextureView);
    m_snowDepthBindGroup = createDepthBindGroup(m_snowTextureView);
    if (!m_rainDepthBindGroup.isValid() || !m_snowDepthBindGroup.isValid()) {
        destroyBindGroups();
        return false;
    }
    m_boundSceneDepthTexture = sceneDepthTexture;
    return true;
}

void RainRenderer::destroyBindGroups() {
    if (m_rhiDevice != nullptr) {
        if (m_rainBindGroup.isValid()) m_rhiDevice->destroyBindGroup(m_rainBindGroup);
        if (m_snowBindGroup.isValid()) m_rhiDevice->destroyBindGroup(m_snowBindGroup);
        if (m_rainDepthBindGroup.isValid()) m_rhiDevice->destroyBindGroup(m_rainDepthBindGroup);
        if (m_snowDepthBindGroup.isValid()) m_rhiDevice->destroyBindGroup(m_snowDepthBindGroup);
        if (m_sceneDepthTextureView.isValid()) m_rhiDevice->destroyTextureView(m_sceneDepthTextureView);
    }
    m_rainBindGroup = {};
    m_snowBindGroup = {};
    m_rainDepthBindGroup = {};
    m_snowDepthBindGroup = {};
    m_sceneDepthTextureView = {};
    m_boundSceneDepthTexture = {};
}

void RainRenderer::destroyRhiResources() {
    destroyBindGroups();
    if (m_rhiDevice != nullptr) {
        if (m_depthSamplePipeline.isValid()) m_rhiDevice->destroyPipeline(m_depthSamplePipeline);
        if (m_depthTestPipeline.isValid()) m_rhiDevice->destroyPipeline(m_depthTestPipeline);
        if (m_depthPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_depthPipelineLayout);
        if (m_pipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_pipelineLayout);
        if (m_depthBindGroupLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_depthBindGroupLayout);
        if (m_bindGroupLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_bindGroupLayout);
        if (m_depthFragmentShader.isValid()) m_rhiDevice->destroyShader(m_depthFragmentShader);
        if (m_fragmentShader.isValid()) m_rhiDevice->destroyShader(m_fragmentShader);
        if (m_vertexShader.isValid()) m_rhiDevice->destroyShader(m_vertexShader);
        if (m_depthSampler.isValid()) m_rhiDevice->destroySampler(m_depthSampler);
        if (m_precipitationSampler.isValid()) m_rhiDevice->destroySampler(m_precipitationSampler);
        if (m_snowTextureView.isValid()) m_rhiDevice->destroyTextureView(m_snowTextureView);
        if (m_rainTextureView.isValid()) m_rhiDevice->destroyTextureView(m_rainTextureView);
        if (m_vertexBuffer.isValid()) m_rhiDevice->destroyBuffer(m_vertexBuffer);
    }
    m_depthSamplePipeline = {};
    m_depthTestPipeline = {};
    m_depthPipelineLayout = {};
    m_pipelineLayout = {};
    m_depthBindGroupLayout = {};
    m_bindGroupLayout = {};
    m_depthFragmentShader = {};
    m_fragmentShader = {};
    m_vertexShader = {};
    m_depthSampler = {};
    m_precipitationSampler = {};
    m_snowTextureView = {};
    m_rainTextureView = {};
    m_vertexBuffer = {};
    m_rhiDevice = nullptr;
}
