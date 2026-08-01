#include "renderer/rhi/RhiShaderCompiler.h"

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <spirv_cross/spirv_cross_c.h>

#include <limits>
#include <mutex>

namespace renderer::rhi {
namespace {
[[nodiscard]] const char* backendPreamble(const RhiShaderBackend backend, const RhiShaderStage stage) {
    switch (backend) {
    case RhiShaderBackend::Vulkan: return "#define RHI_VULKAN 1\n";
    case RhiShaderBackend::OpenGl:
        if (stage == RhiShaderStage::Fragment) {
            // SPIR-V 1.6 maps GLSL discard to demote, which OpenGL GLSL cannot represent. The
            // terminate opcode cross-compiles to the native OpenGL discard statement.
            return "#extension GL_EXT_terminate_invocation : require\n"
                   "#define discard terminateInvocation\n"
                   "#define RHI_OPENGL 1\n";
        }
        return "#define RHI_OPENGL 1\n";
    }
    return nullptr;
}

// Injects the selected RHI backend preamble immediately after the GLSL version directive.
// The source parameter must contain one canonical GLSL stage, backend identifies the device that
// will execute the compiled shader, and stage selects stage-specific language mappings. The returned
// source preserves every original line while exposing the backend contract to shared shader helpers.
[[nodiscard]] std::optional<std::string> injectBackendPreamble(const std::string& source,
                                                               const RhiShaderBackend backend,
                                                               const RhiShaderStage stage, std::string& errorMessage) {
    const char* preamble = backendPreamble(backend, stage);
    if (preamble == nullptr) {
        errorMessage = "shader compilation received an invalid RHI backend";
        return std::nullopt;
    }

    size_t lineStart = 0u;
    while (lineStart < source.size()) {
        const size_t lineEnd = source.find('\n', lineStart);
        const size_t lineLength = lineEnd == std::string::npos ? source.size() - lineStart : lineEnd - lineStart;
        const size_t directiveStart = source.find_first_not_of(" \t", lineStart);
        if (directiveStart != std::string::npos && directiveStart < lineStart + lineLength &&
            source.compare(directiveStart, 8u, "#version") == 0) {
            const size_t insertionPoint = lineEnd == std::string::npos ? source.size() : lineEnd + 1u;
            std::string result;
            result.reserve(source.size() + std::char_traits<char>::length(preamble));
            result.append(source, 0u, insertionPoint);
            if (lineEnd == std::string::npos) {
                result.push_back('\n');
            }
            result += preamble;
            result.append(source, insertionPoint, std::string::npos);
            return result;
        }
        if (lineEnd == std::string::npos) {
            break;
        }
        lineStart = lineEnd + 1u;
    }

    errorMessage = "shader source does not contain a GLSL version directive";
    return std::nullopt;
}

[[nodiscard]] EShLanguage toGlslangStage(const RhiShaderStage stage) {
    switch (stage) {
    case RhiShaderStage::Vertex: return EShLangVertex;
    case RhiShaderStage::Fragment: return EShLangFragment;
    case RhiShaderStage::Compute: return EShLangCompute;
    }
    return EShLangCount;
}

class SpvcContextOwner {
public:
    SpvcContextOwner() = default;
    ~SpvcContextOwner() {
        if (m_context != nullptr) {
            spvc_context_destroy(m_context);
        }
    }

    SpvcContextOwner(const SpvcContextOwner&) = delete;
    SpvcContextOwner& operator=(const SpvcContextOwner&) = delete;

    [[nodiscard]] bool init(std::string& errorMessage) {
        m_errorMessage = &errorMessage;
        if (spvc_context_create(&m_context) != SPVC_SUCCESS) {
            errorMessage = "SPIRV-Cross context creation failed";
            return false;
        }
        spvc_context_set_error_callback(m_context, &SpvcContextOwner::onError, this);
        return true;
    }

    [[nodiscard]] spvc_context context() const { return m_context; }

    void captureLastError() const {
        if (m_context == nullptr || m_errorMessage == nullptr || !m_errorMessage->empty()) {
            return;
        }
        const char* error = spvc_context_get_last_error_string(m_context);
        *m_errorMessage = error != nullptr ? error : "SPIRV-Cross operation failed";
    }

private:
    static void onError(void* userdata, const char* error) {
        auto* owner = static_cast<SpvcContextOwner*>(userdata);
        if (owner != nullptr && owner->m_errorMessage != nullptr) {
            *owner->m_errorMessage = error != nullptr ? error : "SPIRV-Cross operation failed";
        }
    }

    spvc_context m_context = nullptr;
    std::string* m_errorMessage = nullptr;
};

[[nodiscard]] bool createSpvcCompiler(SpvcContextOwner& owner, const std::vector<uint32_t>& spirv,
                                      const spvc_backend backend, spvc_compiler& compiler, std::string& errorMessage) {
    if (!owner.init(errorMessage)) {
        return false;
    }
    spvc_parsed_ir parsedIr = nullptr;
    if (spvc_context_parse_spirv(owner.context(), spirv.data(), spirv.size(), &parsedIr) != SPVC_SUCCESS ||
        spvc_context_create_compiler(owner.context(), backend, parsedIr, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &compiler) !=
            SPVC_SUCCESS) {
        owner.captureLastError();
        return false;
    }
    return true;
}

[[nodiscard]] uint32_t reflectedArrayCount(const spvc_compiler compiler, const spvc_reflected_resource& resource,
                                           std::string& errorMessage) {
    const spvc_type type = spvc_compiler_get_type_handle(compiler, resource.type_id);
    const unsigned dimensions = spvc_type_get_num_array_dimensions(type);
    uint32_t count = 1u;
    for (unsigned i = 0u; i < dimensions; ++i) {
        if (spvc_type_array_dimension_is_literal(type, i) == SPVC_FALSE) {
            errorMessage = "runtime-sized descriptor arrays are not supported";
            return 0u;
        }
        const uint32_t dimension = spvc_type_get_array_dimension(type, i);
        if (dimension == 0u) {
            errorMessage = "descriptor array dimensions must be non-zero";
            return 0u;
        }
        if (count > std::numeric_limits<uint32_t>::max() / dimension) {
            errorMessage = "descriptor array element count exceeds the RHI limit";
            return 0u;
        }
        count *= dimension;
    }
    return count;
}

[[nodiscard]] bool appendReflectedBindings(const spvc_compiler compiler, const spvc_resources resources,
                                           const spvc_resource_type resourceType, const RhiBindingType type,
                                           const RhiShaderStage stage, RhiShaderReflection& reflection,
                                           std::string& errorMessage) {
    const spvc_reflected_resource* resourceList = nullptr;
    size_t resourceCount = 0u;
    if (spvc_resources_get_resource_list_for_type(resources, resourceType, &resourceList, &resourceCount) !=
        SPVC_SUCCESS) {
        errorMessage = "SPIRV-Cross resource reflection failed";
        return false;
    }
    for (size_t i = 0u; i < resourceCount; ++i) {
        const spvc_reflected_resource& resource = resourceList[i];
        const uint32_t arrayCount = reflectedArrayCount(compiler, resource, errorMessage);
        if (arrayCount == 0u) {
            return false;
        }
        RhiShaderBindingInfo binding;
        binding.set = spvc_compiler_get_decoration(compiler, resource.id, SpvDecorationDescriptorSet);
        binding.binding = spvc_compiler_get_decoration(compiler, resource.id, SpvDecorationBinding);
        binding.type = type;
        binding.arrayCount = arrayCount;
        binding.stages = rhiFlag(stage);
        binding.name = resource.name != nullptr ? resource.name : "";
        reflection.bindings.push_back(std::move(binding));
    }
    return true;
}

[[nodiscard]] bool reflectShader(RhiCompiledShader& shader, std::string& errorMessage) {
    SpvcContextOwner owner;
    spvc_compiler compiler = nullptr;
    if (!createSpvcCompiler(owner, shader.spirv, SPVC_BACKEND_NONE, compiler, errorMessage)) {
        return false;
    }
    spvc_resources resources = nullptr;
    if (spvc_compiler_create_shader_resources(compiler, &resources) != SPVC_SUCCESS) {
        owner.captureLastError();
        return false;
    }

    if (!appendReflectedBindings(compiler, resources, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, RhiBindingType::UniformBuffer,
                                 shader.stage, shader.reflection, errorMessage) ||
        !appendReflectedBindings(compiler, resources, SPVC_RESOURCE_TYPE_STORAGE_BUFFER, RhiBindingType::StorageBuffer,
                                 shader.stage, shader.reflection, errorMessage) ||
        !appendReflectedBindings(compiler, resources, SPVC_RESOURCE_TYPE_SAMPLED_IMAGE,
                                 RhiBindingType::CombinedTextureSampler, shader.stage, shader.reflection,
                                 errorMessage) ||
        !appendReflectedBindings(compiler, resources, SPVC_RESOURCE_TYPE_SEPARATE_IMAGE, RhiBindingType::SampledTexture,
                                 shader.stage, shader.reflection, errorMessage) ||
        !appendReflectedBindings(compiler, resources, SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS, RhiBindingType::Sampler,
                                 shader.stage, shader.reflection, errorMessage) ||
        !appendReflectedBindings(compiler, resources, SPVC_RESOURCE_TYPE_STORAGE_IMAGE, RhiBindingType::StorageTexture,
                                 shader.stage, shader.reflection, errorMessage)) {
        return false;
    }

    const spvc_reflected_resource* pushConstants = nullptr;
    size_t pushConstantCount = 0u;
    if (spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_PUSH_CONSTANT, &pushConstants,
                                                  &pushConstantCount) != SPVC_SUCCESS) {
        owner.captureLastError();
        return false;
    }
    if (pushConstantCount > 1u) {
        errorMessage = "a shader stage may declare at most one push-constant block";
        return false;
    }
    if (pushConstantCount == 1u) {
        const spvc_reflected_resource& resource = pushConstants[0];
        const spvc_type type = spvc_compiler_get_type_handle(compiler, resource.base_type_id);
        size_t byteSize = 0u;
        if (spvc_compiler_get_declared_struct_size(compiler, type, &byteSize) != SPVC_SUCCESS) {
            owner.captureLastError();
            return false;
        }
        RhiPushConstantInfo pushConstant;
        pushConstant.size = static_cast<uint32_t>(byteSize);
        pushConstant.stages = rhiFlag(shader.stage);
        pushConstant.name = resource.name != nullptr ? resource.name : "";
        shader.reflection.pushConstant = std::move(pushConstant);
    }
    return true;
}

} // namespace

std::optional<RhiCompiledShader> compileShaderToSpirv(const RhiShaderDesc& desc, const RhiShaderBackend backend,
                                                      std::string& errorMessage) {
    errorMessage.clear();
    if (desc.source == nullptr || desc.sourceSize == 0u || desc.bytecode != nullptr || desc.bytecodeSize != 0u ||
        desc.entryPoint == nullptr || desc.entryPoint[0] == '\0') {
        errorMessage = "shader compilation requires canonical GLSL source and an entry point";
        return std::nullopt;
    }
    const EShLanguage language = toGlslangStage(desc.stage);
    if (language == EShLangCount) {
        errorMessage = "shader compilation received an invalid stage";
        return std::nullopt;
    }

    static std::once_flag initializationFlag;
    static bool initialized = false;
    std::call_once(initializationFlag, []() { initialized = glslang::InitializeProcess(); });
    if (!initialized) {
        errorMessage = "glslang process initialization failed";
        return std::nullopt;
    }

    if (desc.sourceSize > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        errorMessage = "shader source exceeds the compiler length limit";
        return std::nullopt;
    }
    const std::string canonicalSource(desc.source, static_cast<size_t>(desc.sourceSize));
    const std::optional<std::string> source = injectBackendPreamble(canonicalSource, backend, desc.stage, errorMessage);
    if (!source.has_value()) {
        return std::nullopt;
    }
    if (source->size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        errorMessage = "shader source exceeds the compiler length limit after backend injection";
        return std::nullopt;
    }
    const char* sourcePointer = source->c_str();
    const int sourceLength = static_cast<int>(source->size());
    glslang::TShader shader(language);
    shader.setStringsWithLengths(&sourcePointer, &sourceLength, 1);
    shader.setEntryPoint(desc.entryPoint);
    shader.setSourceEntryPoint(desc.entryPoint);
    shader.setEnvInput(glslang::EShSourceGlsl, language, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);

    constexpr EShMessages kMessages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(), 450, false, kMessages)) {
        errorMessage = shader.getInfoLog();
        const char* debugLog = shader.getInfoDebugLog();
        if (debugLog != nullptr && debugLog[0] != '\0') {
            errorMessage += '\n';
            errorMessage += debugLog;
        }
        return std::nullopt;
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(kMessages)) {
        errorMessage = program.getInfoLog();
        return std::nullopt;
    }
    const glslang::TIntermediate* intermediate = program.getIntermediate(language);
    if (intermediate == nullptr) {
        errorMessage = "glslang did not produce an intermediate shader";
        return std::nullopt;
    }

    RhiCompiledShader compiled;
    compiled.stage = desc.stage;
    compiled.entryPoint = desc.entryPoint;
    glslang::SpvOptions options;
    options.generateDebugInfo = false;
    options.disableOptimizer = false;
    options.optimizeSize = true;
    glslang::GlslangToSpv(*intermediate, compiled.spirv, &options);
    if (compiled.spirv.empty() || !reflectShader(compiled, errorMessage)) {
        if (errorMessage.empty()) {
            errorMessage = "SPIR-V generation produced no bytecode";
        }
        return std::nullopt;
    }
    return compiled;
}

} // namespace renderer::rhi
