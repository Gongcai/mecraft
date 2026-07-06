//
// Created by seawon on 2026/3/18.
//

#ifndef MECRAFT_SHADER_H
#define MECRAFT_SHADER_H


#include <cstdint>
#include<string>
#include<fstream>
#include <optional>
#include<sstream>
#include<iostream>
#include <unordered_set>
#include<glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <unordered_map>
using namespace std;
class Shader
{
public:
    unsigned int ID = 0;
    // Read source files and build a shader program.
    Shader(const char* vertexPath,const char* fragmentPath, const char* geometryPath);
    Shader(const char* vertexPath,const char* fragmentPath);

    // Compute shader factory: compiles a single compute shader program.
    static Shader* createCompute(const char* computePath);

    // Bind the program for rendering.
    void use();

    // Dispatch a compute shader. numGroups can be 0 to skip that dimension.
    void dispatch(uint32_t numGroupsX, uint32_t numGroupsY = 1, uint32_t numGroupsZ = 1) const;

    // Bind a texture as an image for imageLoad/imageStore in compute shaders.
    static void bindImage(uint32_t unit, uint32_t texture, int32_t level, bool layered,
                          int32_t layer, uint32_t access, uint32_t internalFormat);

    // Uniform setters.
    void setBool(const string& name, bool value) const;
    void setInt(const string& name,int value) const;
    void setFloat(const string& name,float value) const;
    void setUint(const string& name, unsigned int value) const;
    void setMat4(const string& name,const glm::mat4 &value) const;
    void setMat4(int location, const glm::mat4 &value) const;
    void setMat3(const string& name, const glm::mat3 &value) const;
    void setMat2(const string& name, const glm::mat2 &value) const;
    void setVec4(const string& name,const glm::vec4 &value) const;
    void setVec3(const string& name,const glm::vec3 &value) const;
    void setVec2(const string& name,const glm::vec2 &value) const;
    void setVec3(const std::string& name, float x, float y, float z) const;

    [[nodiscard]] int getUniformLocation(const string& name) const;

private:
    Shader() : ID(0) { uniformLocationCache.reserve(128); } // private default for factory
    static std::optional<std::string> loadShaderSource(const std::string& path);
    static std::optional<std::string> resolveIncludes(const std::string& source,
                                                      const std::string& sourcePath,
                                                      std::unordered_set<std::string>& includeStack);
    mutable std::unordered_map<std::string,int> uniformLocationCache;
};





#endif //MECRAFT_SHADER_H
