//
// Created by seawon on 2026/3/18.
//

#include "Shader.h"

#include <filesystem>

using namespace std;

namespace {
string trimShaderIncludeToken(string token) {
	const size_t first = token.find_first_not_of(" \t");
	if (first == string::npos) {
		return {};
	}
	const size_t last = token.find_last_not_of(" \t\r\n");
	token = token.substr(first, last - first + 1);
	if (token.size() >= 2 &&
		((token.front() == '"' && token.back() == '"') ||
		 (token.front() == '<' && token.back() == '>'))) {
		return token.substr(1, token.size() - 2);
	}
	return {};
}
} // namespace

string Shader::loadShaderSource(const string& path) {
	ifstream shaderFile;
	shaderFile.exceptions(ifstream::failbit | ifstream::badbit);
	shaderFile.open(path);
	stringstream shaderStream;
	shaderStream << shaderFile.rdbuf();
	return shaderStream.str();
}

string Shader::resolveIncludes(const string& source,
                               const string& sourcePath,
                               unordered_set<string>& includeStack) {
	const filesystem::path currentPath = filesystem::absolute(sourcePath).lexically_normal();
	const filesystem::path currentDir = currentPath.parent_path();
	stringstream input(source);
	string output;
	string line;
	while (getline(input, line)) {
		const size_t directiveStart = line.find_first_not_of(" \t");
		if (directiveStart != string::npos && line.compare(directiveStart, 8, "#include") == 0) {
			const string includeName = trimShaderIncludeToken(line.substr(directiveStart + 8));
			if (!includeName.empty()) {
				const filesystem::path includePath = filesystem::absolute(currentDir / includeName).lexically_normal();
				const string includeKey = includePath.string();
				output += "#line 1 0\n";
				if (includeStack.insert(includeKey).second) {
					try {
						output += resolveIncludes(loadShaderSource(includeKey), includeKey, includeStack);
					} catch (ifstream::failure&) {
						cout << "ERROR::SHADER::INCLUDE_NOT_SUCCESSFULLY_READ" << endl;
						cout << "ERROR::SHADER::FILENAME:" << includeKey << endl;
					}
					includeStack.erase(includeKey);
				} else {
					cout << "ERROR::SHADER::CYCLIC_INCLUDE:" << includeKey << endl;
				}
				output += "\n#line 1 0\n";
				continue;
			}
		}
		output += line;
		output += '\n';
	}
	return output;
}

Shader::Shader(const char* vertexPath, const char* fragmentPath, const char* geometryPath )
{
	uniformLocationCache.reserve(128);

	//从文件中读取着色器并写到字符串里转成const char* c
	string vertexCode;
	string framentCode;
	string geometryCode;
	ifstream vShaderFile;
	ifstream fShaderFile;
	ifstream gShaderFile;

	vShaderFile.exceptions(ifstream::failbit|ifstream::badbit);
	fShaderFile.exceptions(ifstream::failbit | ifstream::badbit);
	gShaderFile.exceptions(ifstream::failbit | ifstream::badbit);
	try
	{
		vShaderFile.open(vertexPath);
		fShaderFile.open(fragmentPath);
		gShaderFile.open(geometryPath);
		stringstream vShaderStream, fShaderStream,gShaderStream;
		vShaderStream << vShaderFile.rdbuf();
		fShaderStream << fShaderFile.rdbuf();
		gShaderStream << gShaderFile.rdbuf();
		vShaderFile.close();
		fShaderFile.close();
		gShaderFile.close();
		unordered_set<string> includeStack;
		vertexCode = resolveIncludes(vShaderStream.str(), vertexPath, includeStack);
		framentCode = resolveIncludes(fShaderStream.str(), fragmentPath, includeStack);
		geometryCode = resolveIncludes(gShaderStream.str(), geometryPath, includeStack);
	}
	catch (ifstream::failure e)
	{
		cout << "ERROR::SHADER::FILE_NOT_SUCCESFULLY_READ" << endl;
		cout << "ERROR::SHADER::FILENAME:" << vertexPath << endl;
		cout << "ERROR::SHADER::FILENAME:" << fragmentPath << endl;
		cout << "ERROR::SHADER::FILENAME:" << geometryPath << endl;


	}
	const char* vShaderCode = vertexCode.c_str();
	const char* fShaderCode = framentCode.c_str();
	const char* gShaderCode = geometryCode.c_str();
	//着色器的编译和链接
	unsigned int vertex, fragment, geometry;
	int success;
	char infolog[512];

	//顶点着色器
	vertex = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertex,1,&vShaderCode,NULL);
	glCompileShader(vertex);
	glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(vertex,512,NULL,infolog);
		cout << "ERROR:SHADER::VERTEX::COMPILATION_FAILED [" << vertexPath << "]\n" << infolog << endl;

	}
	//片段着色器
	fragment = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragment, 1, &fShaderCode, NULL);
	glCompileShader(fragment);
	glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(fragment, 512, NULL, infolog);
		cout << "ERROR:SHADER::FRAGMENT::COMPILATION_FAILED [" << fragmentPath << "]\n" << infolog << endl;

	}
	//几何着色器
	geometry = glCreateShader(GL_GEOMETRY_SHADER);
	glShaderSource(geometry,1,&gShaderCode,NULL);
	glCompileShader(geometry);
	glGetShaderiv(geometry,GL_COMPILE_STATUS,&success);
	if (!success)
	{
		glGetShaderInfoLog(geometry,512,NULL,infolog);
		cout << "ERROR::SHADER::GEOMETRY::COMPILATION_FAILED [" << geometryPath << "]\n" << infolog << endl;
	}



	ID = glCreateProgram();
	glAttachShader(ID,vertex);
	glAttachShader(ID,fragment);
	glAttachShader(ID,geometry);
	glLinkProgram(ID);
	glGetProgramiv(ID, GL_LINK_STATUS, &success);
	if (!success)
	{
		glGetProgramInfoLog(ID, 512, NULL, infolog);
		cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED [" << vertexPath << " + " << fragmentPath << " + " << geometryPath << "]\n" << infolog << endl;
	}

	glDeleteShader(vertex);
	glDeleteShader(fragment);
	glDeleteShader(geometry);
}
Shader::Shader(const char* vertexPath,const char* fragmentPath){
	uniformLocationCache.reserve(128);
	//从文件中读取着色器并写到字符串里转成const char* c
	string vertexCode;
	string framentCode;
	string geometryCode;
	ifstream vShaderFile;
	ifstream fShaderFile;
	ifstream gShaderFile;

	vShaderFile.exceptions(ifstream::failbit|ifstream::badbit);
	fShaderFile.exceptions(ifstream::failbit | ifstream::badbit);
	gShaderFile.exceptions(ifstream::failbit | ifstream::badbit);
	try
	{
		vShaderFile.open(vertexPath);
		fShaderFile.open(fragmentPath);

		stringstream vShaderStream, fShaderStream,gShaderStream;
		vShaderStream << vShaderFile.rdbuf();
		fShaderStream << fShaderFile.rdbuf();

		vShaderFile.close();
		fShaderFile.close();

		unordered_set<string> includeStack;
		vertexCode = resolveIncludes(vShaderStream.str(), vertexPath, includeStack);
		framentCode = resolveIncludes(fShaderStream.str(), fragmentPath, includeStack);

	}
	catch (ifstream::failure e)
	{
		cout << "ERROR::SHADER::FILE_NOT_SUCCESFULLY_READ" << endl;
		cout << "ERROR::SHADER::FILENAME:" << vertexPath << endl;
		cout << "ERROR::SHADER::FILENAME:" << fragmentPath << endl;


	}
	const char* vShaderCode = vertexCode.c_str();
	const char* fShaderCode = framentCode.c_str();

	//着色器的编译和链接
	unsigned int vertex, fragment, geometry;
	int success;
	char infolog[512];

	//顶点着色器
	vertex = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertex,1,&vShaderCode,NULL);
	glCompileShader(vertex);
	glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(vertex,512,NULL,infolog);
		cout << "ERROR:SHADER::VERTEX::COMPILATION_FAILED [" << vertexPath << "]\n" << infolog << endl;

	}
	//片段着色器
	fragment = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragment, 1, &fShaderCode, NULL);
	glCompileShader(fragment);
	glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(fragment, 512, NULL, infolog);
		cout << "ERROR:SHADER::FRAGMENT::COMPILATION_FAILED [" << fragmentPath << "]\n" << infolog << endl;

	}



	ID = glCreateProgram();
	glAttachShader(ID,vertex);
	glAttachShader(ID,fragment);

	glLinkProgram(ID);
	glGetProgramiv(ID, GL_LINK_STATUS, &success);
	if (!success)
	{
		glGetProgramInfoLog(ID, 512, NULL, infolog);
		cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED [" << vertexPath << " + " << fragmentPath << "]\n" << infolog << endl;
	}

	glDeleteShader(vertex);
	glDeleteShader(fragment);

}
void Shader::use()
{
	glUseProgram(ID);
}


void Shader::setBool(const string& name, bool value) const
{
	glUniform1i(getUniformLocation(name), (int)value);
}
void Shader::setFloat (const string& name, float value) const
{
	glUniform1f(getUniformLocation(name),value);
}
void Shader::setInt(const string& name, int value) const
{
	glUniform1i(getUniformLocation(name), value);
}
void Shader::setMat4(const string& name,const glm::mat4& value) const
{
	glUniformMatrix4fv(getUniformLocation(name),1,GL_FALSE, glm::value_ptr(value));
}

void Shader::setMat4(const int location, const glm::mat4& value) const
{
	if (location < 0) {
		return;
	}
	glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
}
void Shader::setVec4(const std::string &name, const glm::vec4 &value) const
{
    glUniform4fv(getUniformLocation(name), 1, &value[0]);
}
void Shader::setVec3(const string &name, const glm::vec3 &value) const
{
	glUniform3fv(getUniformLocation(name),1,&value[0]);
}
void Shader::setVec3(const std::string& name, float x, float y, float z) const
{
	glUniform3f(getUniformLocation(name), x, y, z);
}

int Shader::getUniformLocation(const string &name) const {
	const auto cached = uniformLocationCache.find(name);
	if (cached != uniformLocationCache.end()) {
		return cached->second;
	}

	int location = glGetUniformLocation(ID, name.c_str());
	uniformLocationCache.emplace(name, location);
	return location;
}

void Shader::setVec2(const std::string &name, const glm::vec2 &value) const
{
    glUniform2fv(getUniformLocation(name), 1, &value[0]);
}
 void Shader::setMat2(const std::string &name, const glm::mat2 &value) const
{
    glUniformMatrix2fv(getUniformLocation(name), 1, GL_FALSE, &value[0][0]);
}

void Shader::setMat3(const string& name, const glm::mat3 &value) const
{
    glUniformMatrix3fv(getUniformLocation(name), 1, GL_FALSE, &value[0][0]);
}

void Shader::setUint(const string& name, unsigned int value) const
{
    glUniform1ui(getUniformLocation(name), value);
}

Shader* Shader::createCompute(const char* computePath) {
    string computeCode;
    try {
        ifstream file;
        file.exceptions(ifstream::failbit | ifstream::badbit);
        file.open(computePath);
        stringstream stream;
        stream << file.rdbuf();
        file.close();
        unordered_set<string> includeStack;
        computeCode = resolveIncludes(stream.str(), computePath, includeStack);
    } catch (ifstream::failure& e) {
        cout << "ERROR::SHADER::COMPUTE::FILE_NOT_SUCCESSFULLY_READ" << endl;
        cout << "ERROR::SHADER::FILENAME:" << computePath << endl;
        return nullptr;
    }

    const char* code = computeCode.c_str();
    GLuint compute = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(compute, 1, &code, nullptr);
    glCompileShader(compute);

    int success = 0;
    char infolog[1024];
    glGetShaderiv(compute, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(compute, sizeof(infolog), nullptr, infolog);
        cout << "ERROR::SHADER::COMPUTE::COMPILATION_FAILED [" << computePath << "]\n" << infolog << endl;
        glDeleteShader(compute);
        return nullptr;
    }

    auto* shader = new Shader();
    shader->ID = glCreateProgram();
    glAttachShader(shader->ID, compute);
    glLinkProgram(shader->ID);

    glGetProgramiv(shader->ID, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shader->ID, sizeof(infolog), nullptr, infolog);
        cout << "ERROR::SHADER::COMPUTE::LINKING_FAILED [" << computePath << "]\n" << infolog << endl;
        glDeleteProgram(shader->ID);
        delete shader;
        shader = nullptr;
    }

    glDeleteShader(compute);
    return shader;
}

void Shader::dispatch(const GLuint numGroupsX, const GLuint numGroupsY, const GLuint numGroupsZ) const {
    glDispatchCompute(numGroupsX, numGroupsY, numGroupsZ);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
}

void Shader::bindImage(const GLuint unit, const GLuint texture, const GLint level,
                       const GLboolean layered, const GLint layer,
                       const GLenum access, const GLenum internalFormat) {
    glBindImageTexture(unit, texture, level, layered, layer, access, internalFormat);
}
