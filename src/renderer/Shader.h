//
// Created by seawon on 2026/3/18.
//

#ifndef MECRAFT_SHADER_H
#define MECRAFT_SHADER_H


#include<glad/glad.h>
#include<string>
#include<fstream>
#include<sstream>
#include<iostream>
#include<glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <unordered_map>
using namespace std;
class Shader
{
public:
    unsigned int ID;
    //读取代码并构建着色器
    Shader(const char* vertexPath,const char* fragmentPath, const char* geometryPath);
    Shader(const char* vertexPath,const char* fragmentPath);

    //使用着色器
    void use();
    //uniform值设置
    void setBool(const string& name, bool value) const;
    void setInt(const string& name,int value) const;
    void setFloat(const string& name,float value) const;
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
    mutable std::unordered_map<std::string,int> uniformLocationCache;
};





#endif //MECRAFT_SHADER_H