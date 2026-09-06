#include "renderer/mesh/WorldRenderBuffer.h"
#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiDeviceFactory.h"
#include "renderer/rhi/RhiShaderSourceLoader.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
using Metadata = WorldRenderBuffer::SubChunkDrawMetadata;
using Command = DrawArraysIndirectCommand;

struct CullConstants {
    glm::mat4 viewProjection{1.0f};
    glm::vec4 dimensions{};
    glm::vec4 options{};
};

// Runs the production build/cull shaders and reads back indirect commands.
// Every fixture owns its GPU resources until the submission has completed.
class Fixture {
public:
    explicit Fixture(RhiDevice& device) : m_device(device) {}
    ~Fixture() {
        m_device.waitIdle();
        for (auto group : m_groups)
            m_device.destroyBindGroup(group);
        for (auto pipeline : m_pipelines)
            m_device.destroyPipeline(pipeline);
        for (auto layout : m_pipelineLayouts)
            m_device.destroyPipelineLayout(layout);
        for (auto layout : m_layouts)
            m_device.destroyBindGroupLayout(layout);
        for (auto shader : m_shaders)
            m_device.destroyShader(shader);
        for (auto view : m_views)
            m_device.destroyTextureView(view);
        for (auto texture : m_textures)
            m_device.destroyTexture(texture);
        for (auto buffer : m_buffers)
            m_device.destroyBuffer(buffer);
        if (m_sampler.isValid())
            m_device.destroySampler(m_sampler);
    }

    bool run(uint32_t width, uint32_t height, const std::vector<float>& depth, const std::vector<Metadata>& metadata,
             std::vector<Command>& draws, const glm::mat4& projection, bool retest, uint32_t expectedCulled) {
        const auto stages = rhiFlag(RhiShaderStage::Compute);
        RhiSamplerDesc samplerDesc;
        samplerDesc.minFilter = samplerDesc.magFilter = RhiFilter::Nearest;
        samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
        m_sampler = m_device.createSampler(samplerDesc);
        auto pool = m_device.createCommandListPool({"HiZ.Test", 1u, 64u * 1024u});
        if (!m_sampler.isValid() || !pool)
            return false;
        auto* cmd = pool->acquire(RhiCommandListType::Graphics);
        if (!cmd || !cmd->begin({"HiZ.Test", RhiCommandListType::Graphics}))
            return false;

        RhiTextureDesc textureDesc;
        textureDesc.format = RhiTextureFormat::R32Float;
        textureDesc.width = width;
        textureDesc.height = height;
        textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::TransferDst);
        RhiTextureInitialData initial;
        initial.pixels = depth.data();
        initial.sizeBytes = depth.size() * sizeof(float);
        initial.finalState = RhiResourceState::ShaderRead;
        const auto source = m_device.createTexture(textureDesc, &initial);
        m_textures.push_back(source);
        const auto sourceView = view(source, 0u, 1u);

        uint32_t mipCount = 1u;
        for (uint32_t size = std::max(width, height); size > 1u; size >>= 1u)
            ++mipCount;
        textureDesc.mipLevels = mipCount;
        textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::Storage);
        const auto pyramid = m_device.createTexture(textureDesc, nullptr);
        m_textures.push_back(pyramid);
        const auto pyramidView = view(pyramid, 0u, mipCount);
        if (!source.isValid() || !sourceView.isValid() || !pyramid.isValid() || !pyramidView.isValid())
            return false;

        RhiBindGroupLayoutDesc buildLayout;
        buildLayout.entries = {{0u, RhiBindingType::CombinedTextureSampler, stages, 1u},
                               {1u, RhiBindingType::StorageTexture, stages, 1u}};
        const auto buildBindings = layout(buildLayout);
        const auto build = pipeline("assets/shaders/hiz_build.comp", buildBindings, sizeof(glm::ivec4));
        if (!build.isValid())
            return false;
        auto previousView = sourceView;
        for (uint32_t mip = 0u; mip < mipCount; ++mip) {
            const auto destinationView = view(pyramid, mip, 1u);
            RhiBindGroupDesc groupDesc;
            groupDesc.layout = buildBindings;
            RhiBindGroupEntry input;
            input.binding = 0u;
            input.resource.combinedTextureSampler = {previousView, m_sampler};
            RhiBindGroupEntry output;
            output.binding = 1u;
            output.resource.textureView = destinationView;
            groupDesc.entries = {input, output};
            const auto bindings = group(groupDesc);
            if (!bindings.isValid())
                return false;
            const uint32_t mipWidth = std::max(1u, width >> mip);
            const uint32_t mipHeight = std::max(1u, height >> mip);
            const glm::ivec4 constants(mipWidth, mipHeight, mip == 0u ? 1 : 0, 0);
            cmd->textureBarrier({pyramid, RhiResourceState::Undefined, RhiResourceState::ShaderWrite, mip, 1u});
            cmd->setComputePipeline(build);
            cmd->setBindGroup(0u, bindings);
            cmd->pushConstants(&constants, sizeof(constants), stages);
            cmd->dispatch((mipWidth + 7u) / 8u, (mipHeight + 7u) / 8u, 1u);
            cmd->textureBarrier({pyramid, RhiResourceState::ShaderWrite, RhiResourceState::ShaderRead, mip, 1u});
            previousView = destinationView;
        }

        const uint64_t commandBytes = draws.size() * sizeof(Command);
        const std::array<uint32_t, 2u> zero{};
        const auto commands = buffer(draws.data(), commandBytes, false);
        const auto origins = buffer(metadata.data(), metadata.size() * sizeof(Metadata), false);
        const auto counters = buffer(zero.data(), sizeof(zero), false);
        const auto readback = buffer(nullptr, commandBytes + sizeof(zero), true);
        if (!commands.isValid() || !origins.isValid() || !counters.isValid() || !readback.isValid())
            return false;

        RhiBindGroupLayoutDesc cullLayout;
        cullLayout.entries = {{0u, RhiBindingType::StorageBuffer, stages, 1u},
                              {1u, RhiBindingType::StorageBuffer, stages, 1u},
                              {2u, RhiBindingType::CombinedTextureSampler, stages, 1u},
                              {3u, RhiBindingType::StorageBuffer, stages, 1u}};
        const auto cullBindings = layout(cullLayout);
        const auto cull = pipeline("assets/shaders/hiz_cull.comp", cullBindings, sizeof(CullConstants));
        if (!cull.isValid())
            return false;
        RhiBindGroupDesc cullGroup;
        cullGroup.layout = cullBindings;
        RhiBindGroupEntry commandEntry;
        commandEntry.binding = 0u;
        commandEntry.resource.buffer = {commands, 0u, commandBytes};
        RhiBindGroupEntry metadataEntry;
        metadataEntry.binding = 1u;
        metadataEntry.resource.buffer = {origins, 0u, metadata.size() * sizeof(Metadata)};
        RhiBindGroupEntry pyramidEntry;
        pyramidEntry.binding = 2u;
        pyramidEntry.resource.combinedTextureSampler = {pyramidView, m_sampler};
        RhiBindGroupEntry counterEntry;
        counterEntry.binding = 3u;
        counterEntry.resource.buffer = {counters, 0u, sizeof(zero)};
        cullGroup.entries = {commandEntry, metadataEntry, pyramidEntry, counterEntry};
        const auto bindings = group(cullGroup);
        if (!bindings.isValid())
            return false;
        CullConstants constants;
        constants.viewProjection = projection;
        constants.dimensions = glm::vec4(width, height, mipCount - 1u, draws.size());
        constants.options = glm::vec4(5.0e-4f, 0.0f, retest ? 1.0f : 0.0f, 0.0f);
        cmd->setComputePipeline(cull);
        cmd->setBindGroup(0u, bindings);
        cmd->pushConstants(&constants, sizeof(constants), stages);
        cmd->dispatch((static_cast<uint32_t>(draws.size()) + 63u) / 64u, 1u, 1u);
        cmd->bufferBarrier({commands, RhiResourceState::StorageBuffer, RhiResourceState::TransferSrc});
        cmd->bufferBarrier({counters, RhiResourceState::StorageBuffer, RhiResourceState::TransferSrc});
        cmd->copyBuffer({commands, readback, 0u, 0u, commandBytes});
        cmd->copyBuffer({counters, readback, 0u, commandBytes, sizeof(zero)});
        cmd->bufferBarrier({readback, RhiResourceState::TransferDst, RhiResourceState::HostRead});
        RhiCommandList* lists[] = {cmd};
        if (!cmd->end() || !m_device.submit({"HiZ.Test", lists, 1u}))
            return false;
        m_device.waitIdle();
        const auto* mapped =
            static_cast<const unsigned char*>(m_device.mapBuffer(readback, 0u, commandBytes + sizeof(zero)));
        if (!mapped)
            return false;
        std::memcpy(draws.data(), mapped, static_cast<size_t>(commandBytes));
        std::array<uint32_t, 2u> counts{};
        std::memcpy(counts.data(), mapped + commandBytes, sizeof(counts));
        m_device.unmapBuffer(readback);
        return counts[0] == expectedCulled && counts[1] == 0u && pool->reset();
    }

private:
    RhiTextureViewHandle view(RhiTextureHandle texture, uint32_t mip, uint32_t count) {
        RhiTextureViewDesc desc;
        desc.texture = texture;
        desc.baseMip = mip;
        desc.mipCount = count;
        const auto result = m_device.createTextureView(desc);
        m_views.push_back(result);
        return result;
    }
    RhiBindGroupLayoutHandle layout(const RhiBindGroupLayoutDesc& desc) {
        const auto result = m_device.createBindGroupLayout(desc);
        m_layouts.push_back(result);
        return result;
    }
    RhiBindGroupHandle group(const RhiBindGroupDesc& desc) {
        const auto result = m_device.createBindGroup(desc);
        m_groups.push_back(result);
        return result;
    }
    RhiBufferHandle buffer(const void* data, uint64_t bytes, bool readback) {
        RhiBufferDesc desc;
        desc.size = bytes;
        desc.usage = readback ? rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::MapRead)
                              : rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::TransferSrc) |
                                    rhiFlag(RhiBufferUsage::TransferDst);
        desc.initialState = readback ? RhiResourceState::TransferDst : RhiResourceState::StorageBuffer;
        desc.memoryUsage = readback ? RhiMemoryUsage::GpuToCpu : RhiMemoryUsage::GpuOnly;
        const auto result = m_device.createBuffer(desc, data, data ? bytes : 0u);
        m_buffers.push_back(result);
        return result;
    }
    RhiPipelineHandle pipeline(const char* path, RhiBindGroupLayoutHandle bindings, uint32_t bytes) {
        const auto source = renderer::rhi::loadShaderSource(path);
        if (!source)
            return {};
        RhiShaderDesc shaderDesc;
        shaderDesc.stage = RhiShaderStage::Compute;
        shaderDesc.source = source->c_str();
        shaderDesc.sourceSize = source->size();
        const auto shader = m_device.createShader(shaderDesc);
        m_shaders.push_back(shader);
        RhiPipelineLayoutDesc layoutDesc;
        layoutDesc.bindGroupLayouts = {bindings};
        layoutDesc.pushConstantBytes = bytes;
        layoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Compute);
        const auto pipelineLayout = m_device.createPipelineLayout(layoutDesc);
        m_pipelineLayouts.push_back(pipelineLayout);
        RhiComputePipelineDesc desc;
        desc.computeShader = shader;
        desc.layout = pipelineLayout;
        const auto result = m_device.createComputePipeline(desc);
        m_pipelines.push_back(result);
        return result;
    }
    RhiDevice& m_device;
    RhiSamplerHandle m_sampler;
    std::vector<RhiTextureHandle> m_textures;
    std::vector<RhiTextureViewHandle> m_views;
    std::vector<RhiBufferHandle> m_buffers;
    std::vector<RhiShaderHandle> m_shaders;
    std::vector<RhiBindGroupLayoutHandle> m_layouts;
    std::vector<RhiPipelineLayoutHandle> m_pipelineLayouts;
    std::vector<RhiPipelineHandle> m_pipelines;
    std::vector<RhiBindGroupHandle> m_groups;
};

// Maps world x/y directly to pixels while keeping depth independent of pixel size.
glm::mat4 pixelProjection(uint32_t width, uint32_t height) {
    glm::mat4 result(1.0f);
    result[0][0] = 2.0f / static_cast<float>(width);
    result[1][1] = 2.0f / static_cast<float>(height);
    result[2][2] = 0.001f;
    result[3][0] = result[3][1] = -1.0f;
    return result;
}

bool testHistoryRecovery(RhiDevice& device) {
    constexpr uint32_t width = 135u, height = 75u;
    std::vector<Metadata> metadata(7u);
    metadata[1].originAndFlags = glm::vec4(20.0f, 10.0f, -900.0f, 4.0f);
    metadata[3].originAndFlags = glm::vec4(40.0f, 20.0f, 500.0f, 4.0f);
    metadata[6].originAndFlags = glm::vec4(70.0f, 30.0f, 500.0f, 4.0f);
    std::vector<Command> draws{{36u, 1u, 10u, 3u}, {72u, 1u, 20u, 1u}, {18u, 1u, 30u, 6u}};
    const auto projection = pixelProjection(width, height);
    std::vector<float> depth(width * height, 0.2f);
    if (!Fixture(device).run(width, height, depth, metadata, draws, projection, false, 0u) ||
        draws[0].instanceCount != 0u || draws[1].instanceCount != 1u || draws[2].instanceCount != 0u)
        return false;
    // Removing the historical occluder exposes draw 0. Draw 2 stays occluded.
    const bool flipY = device.backend() == RhiBackend::Vulkan;
    for (uint32_t y = 15u; y < 30u; ++y) {
        for (uint32_t x = 35u; x < 50u; ++x)
            depth[(flipY ? height - 1u - y : y) * width + x] = 1.0f;
    }
    if (!Fixture(device).run(width, height, depth, metadata, draws, projection, true, 1u))
        return false;
    return draws[0].instanceCount == 1u && draws[0].count == 36u && draws[0].first == 10u &&
           draws[0].baseInstance == 3u && draws[1].instanceCount == 0u && draws[2].instanceCount == 0u;
}

bool testPyramidCoverage(RhiDevice& device) {
    // A single sky texel inside each box forbids rejection, including odd tails
    // and non-power-of-two mip boundaries. The expected result uses mip-0 data.
    for (const auto extent : {glm::uvec2(135u, 75u), glm::uvec2(257u, 129u), glm::uvec2(1u, 65u)}) {
        const uint32_t width = extent.x, height = extent.y;
        std::vector<float> depth(width * height, 0.2f);
        std::vector<Metadata> metadata;
        std::vector<Command> draws;
        for (const uint32_t x : {0u, width / 2u, width - 1u}) {
            for (const uint32_t y : {0u, height / 2u, height - 1u}) {
                Metadata entry;
                entry.originAndFlags =
                    glm::vec4(static_cast<float>(x) + 0.1f, static_cast<float>(y) + 0.1f, 500.0f, 0.5f);
                draws.push_back({36u, 0u, 0u, static_cast<uint32_t>(metadata.size())});
                metadata.push_back(entry);
                const uint32_t row = device.backend() == RhiBackend::Vulkan ? height - 1u - y : y;
                depth[row * width + x] = 1.0f;
            }
        }
        if (!Fixture(device).run(width, height, depth, metadata, draws, pixelProjection(width, height), true, 0u))
            return false;
        for (const auto& draw : draws)
            if (draw.instanceCount != 1u)
                return false;
    }
    // At mip 4 of a 135-wide target, UV sampling maps both rectangle ends
    // to texel 1, missing the sky at base x=32 stored in mip texel 2.
    constexpr uint32_t width = 135u, height = 75u;
    std::vector<float> depth(width * height, 0.2f);
    const uint32_t row = device.backend() == RhiBackend::Vulkan ? height - 1u - 32u : 32u;
    depth[row * width + 32u] = 1.0f;
    std::vector<Metadata> metadata(1u);
    metadata[0].originAndFlags = glm::vec4(17.1f, 17.1f, 500.0f, 15.5f);
    std::vector<Command> draws{{36u, 0u, 0u, 0u}};
    if (!Fixture(device).run(width, height, depth, metadata, draws, pixelProjection(width, height), true, 0u) ||
        draws[0].instanceCount != 1u)
        return false;
    return true;
}

bool testLowAngleProjection(RhiDevice& device) {
    constexpr uint32_t width = 255u, height = 127u;
    for (float yaw : {0.0f, 1.5707963f, 3.1415927f, 4.7123890f}) {
        const glm::vec3 direction(std::cos(yaw), -0.03f, std::sin(yaw));
        const glm::vec3 eye(0.0f, 1.0f, 0.0f);
        const auto matrix = glm::perspective(glm::radians(70.0f), float(width) / float(height), 0.1f, 1000.0f) *
                            glm::lookAt(eye, eye + direction, glm::vec3(0.0f, 1.0f, 0.0f));
        std::vector<Metadata> metadata(3u);
        metadata[1].originAndFlags = glm::vec4(eye + direction * 50.0f - glm::vec3(2.0f), 4.0f);
        metadata[2].originAndFlags = glm::vec4(eye - glm::vec3(1.0f), 2.0f);
        std::vector<Command> draws{{36u, 0u, 0u, 1u}, {36u, 0u, 0u, 2u}};
        if (!Fixture(device).run(width, height, std::vector<float>(width * height, 0.2f), metadata, draws, matrix, true,
                                 1u) ||
            draws[0].instanceCount != 0u || draws[1].instanceCount != 1u)
            return false;
    }
    return true;
}
} // namespace

int main() {
    if (!glfwInit())
        return 1;
    bool passed = true;
    for (const auto backend : {RhiBackend::OpenGL, RhiBackend::Vulkan}) {
        if (!renderer::rhi::isRhiBackendAvailable(backend))
            continue;
        auto device = renderer::rhi::createRhiDevice(backend);
        if (!device || !device->prepareWindowCreation()) {
            passed = false;
            break;
        }
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        auto* window = glfwCreateWindow(64, 64, "HiZ regression", nullptr, nullptr);
        if (!window) {
            passed = false;
            break;
        }
        RhiDeviceDesc desc;
        desc.nativeWindow = window;
        desc.width = desc.height = 64;
        desc.enableDebugOutput = true;
        const bool initialized = device->init(desc);
        const bool history = initialized && testHistoryRecovery(*device);
        const bool coverage = initialized && testPyramidCoverage(*device);
        const bool projection = initialized && testLowAngleProjection(*device);
        std::cout << renderer::rhi::rhiBackendDisplayName(backend) << ": history=" << history
                  << " coverage=" << coverage << " low-angle=" << projection << '\n';
        passed = passed && history && coverage && projection;
        device->shutdown();
        device.reset();
        glfwDestroyWindow(window);
    }
    glfwTerminate();
    return passed ? 0 : 1;
}
