#include "renderer/rhi/gl/GlRhiShaderCompiler.h"

#include <spirv_cross/spirv_cross_c.h>

#include <algorithm>
#include <sstream>

namespace renderer::rhi::gl {
namespace {

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
                                      spvc_compiler& compiler, std::string& errorMessage) {
    if (!owner.init(errorMessage)) {
        return false;
    }
    spvc_parsed_ir parsedIr = nullptr;
    if (spvc_context_parse_spirv(owner.context(), spirv.data(), spirv.size(), &parsedIr) != SPVC_SUCCESS ||
        spvc_context_create_compiler(owner.context(), SPVC_BACKEND_GLSL, parsedIr, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP,
                                     &compiler) != SPVC_SUCCESS) {
        owner.captureLastError();
        return false;
    }
    return true;
}

[[nodiscard]] const GlRhiShaderBindingRemap* findRemap(const std::vector<GlRhiShaderBindingRemap>& remaps,
                                                       const uint32_t set, const uint32_t binding,
                                                       const RhiBindingType type) {
    const auto it = std::find_if(remaps.begin(), remaps.end(), [&](const GlRhiShaderBindingRemap& remap) {
        return remap.set == set && remap.binding == binding && remap.type == type;
    });
    return it == remaps.end() ? nullptr : &*it;
}

[[nodiscard]] bool remapResources(const spvc_compiler compiler, const spvc_resources resources,
                                  const spvc_resource_type resourceType, const RhiBindingType type,
                                  const std::vector<GlRhiShaderBindingRemap>& remaps, std::string& errorMessage) {
    const spvc_reflected_resource* resourceList = nullptr;
    size_t resourceCount = 0u;
    if (spvc_resources_get_resource_list_for_type(resources, resourceType, &resourceList, &resourceCount) !=
        SPVC_SUCCESS) {
        errorMessage = "SPIRV-Cross resource reflection failed during OpenGL remapping";
        return false;
    }
    for (size_t i = 0u; i < resourceCount; ++i) {
        const spvc_reflected_resource& resource = resourceList[i];
        const uint32_t set = spvc_compiler_get_decoration(compiler, resource.id, SpvDecorationDescriptorSet);
        const uint32_t binding = spvc_compiler_get_decoration(compiler, resource.id, SpvDecorationBinding);
        const GlRhiShaderBindingRemap* remap = findRemap(remaps, set, binding, type);
        if (remap == nullptr) {
            std::ostringstream stream;
            stream << "missing OpenGL binding remap for set " << set << ", binding " << binding;
            errorMessage = stream.str();
            return false;
        }
        spvc_compiler_unset_decoration(compiler, resource.id, SpvDecorationDescriptorSet);
        spvc_compiler_set_decoration(compiler, resource.id, SpvDecorationBinding, remap->physicalBinding);
    }
    return true;
}

} // namespace

std::optional<std::string> crossCompileShaderToOpenGl(const RhiCompiledShader& shader,
                                                      const std::vector<GlRhiShaderBindingRemap>& remaps,
                                                      const std::optional<uint32_t>& pushConstantBinding,
                                                      std::string& errorMessage) {
    errorMessage.clear();
    if (shader.spirv.empty()) {
        errorMessage = "OpenGL cross-compilation requires SPIR-V bytecode";
        return std::nullopt;
    }

    SpvcContextOwner owner;
    spvc_compiler compiler = nullptr;
    if (!createSpvcCompiler(owner, shader.spirv, compiler, errorMessage)) {
        return std::nullopt;
    }
    spvc_resources resources = nullptr;
    if (spvc_compiler_create_shader_resources(compiler, &resources) != SPVC_SUCCESS) {
        owner.captureLastError();
        return std::nullopt;
    }
    if (!remapResources(compiler, resources, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, RhiBindingType::UniformBuffer, remaps,
                        errorMessage) ||
        !remapResources(compiler, resources, SPVC_RESOURCE_TYPE_STORAGE_BUFFER, RhiBindingType::StorageBuffer, remaps,
                        errorMessage) ||
        !remapResources(compiler, resources, SPVC_RESOURCE_TYPE_SAMPLED_IMAGE, RhiBindingType::CombinedTextureSampler,
                        remaps, errorMessage) ||
        !remapResources(compiler, resources, SPVC_RESOURCE_TYPE_SEPARATE_IMAGE, RhiBindingType::SampledTexture, remaps,
                        errorMessage) ||
        !remapResources(compiler, resources, SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS, RhiBindingType::Sampler, remaps,
                        errorMessage) ||
        !remapResources(compiler, resources, SPVC_RESOURCE_TYPE_STORAGE_IMAGE, RhiBindingType::StorageTexture, remaps,
                        errorMessage)) {
        return std::nullopt;
    }

    const spvc_reflected_resource* pushConstants = nullptr;
    size_t pushConstantCount = 0u;
    if (spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_PUSH_CONSTANT, &pushConstants,
                                                  &pushConstantCount) != SPVC_SUCCESS) {
        owner.captureLastError();
        return std::nullopt;
    }
    if (pushConstantCount != 0u) {
        if (!pushConstantBinding.has_value()) {
            errorMessage = "shader declares push constants but the pipeline layout does not";
            return std::nullopt;
        }
        spvc_compiler_set_decoration(compiler, pushConstants[0].id, SpvDecorationBinding, *pushConstantBinding);
    }

    spvc_compiler_options options = nullptr;
    if (spvc_compiler_create_compiler_options(compiler, &options) != SPVC_SUCCESS ||
        spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_GLSL_VERSION, 450u) != SPVC_SUCCESS ||
        spvc_compiler_options_set_bool(options, SPVC_COMPILER_OPTION_GLSL_ES, SPVC_FALSE) != SPVC_SUCCESS ||
        spvc_compiler_options_set_bool(options, SPVC_COMPILER_OPTION_GLSL_VULKAN_SEMANTICS, SPVC_FALSE) !=
            SPVC_SUCCESS ||
        spvc_compiler_options_set_bool(options, SPVC_COMPILER_OPTION_GLSL_EMIT_PUSH_CONSTANT_AS_UNIFORM_BUFFER,
                                       SPVC_TRUE) != SPVC_SUCCESS ||
        spvc_compiler_install_compiler_options(compiler, options) != SPVC_SUCCESS) {
        owner.captureLastError();
        return std::nullopt;
    }
    const char* source = nullptr;
    if (spvc_compiler_compile(compiler, &source) != SPVC_SUCCESS || source == nullptr) {
        owner.captureLastError();
        return std::nullopt;
    }
    return std::string(source);
}

} // namespace renderer::rhi::gl
