//
// Created by seawon on 2026/3/18.
//

#include "Shader.h"

using namespace std;

Shader::Shader(const char* vertexPath, const char* fragmentPath, const char* geometryPath )
{

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
		vertexCode = vShaderStream.str();
		framentCode = fShaderStream.str();
		geometryCode = gShaderStream.str();
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
		cout << "ERROR:SHADER::VERTEX::COMPILATION_FAILED\n" << infolog << endl;

	}
	//片段着色器
	fragment = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragment, 1, &fShaderCode, NULL);
	glCompileShader(fragment);
	glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(fragment, 512, NULL, infolog);
		cout << "ERROR:SHADER::FRAGMENT::COMPILATION_FAILED\n" << infolog << endl;

	}
	//几何着色器
	geometry = glCreateShader(GL_GEOMETRY_SHADER);
	glShaderSource(geometry,1,&gShaderCode,NULL);
	glCompileShader(geometry);
	glGetShaderiv(geometry,GL_COMPILE_STATUS,&success);
	if (!success)
	{
		glGetShaderInfoLog(geometry,512,NULL,infolog);
		cout << "ERROR::SHADER::GEOMETRY::COMPILATION_FAILED\n" << infolog << endl;
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
		cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infolog << endl;
	}

	glDeleteShader(vertex);
	glDeleteShader(fragment);
	glDeleteShader(geometry);
}
Shader::Shader(const char* vertexPath,const char* fragmentPath){
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

		vertexCode = vShaderStream.str();
		framentCode = fShaderStream.str();

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
		cout << "ERROR:SHADER::VERTEX::COMPILATION_FAILED\n" << infolog << endl;

	}
	//片段着色器
	fragment = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragment, 1, &fShaderCode, NULL);
	glCompileShader(fragment);
	glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(fragment, 512, NULL, infolog);
		cout << "ERROR:SHADER::FRAGMENT::COMPILATION_FAILED\n" << infolog << endl;

	}



	ID = glCreateProgram();
	glAttachShader(ID,vertex);
	glAttachShader(ID,fragment);

	glLinkProgram(ID);
	glGetProgramiv(ID, GL_LINK_STATUS, &success);
	if (!success)
	{
		glGetProgramInfoLog(ID, 512, NULL, infolog);
		cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infolog << endl;
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
	glUniform1i(glGetUniformLocation(ID, name.c_str()), (int)value);
}
void Shader::setFloat (const string& name, float value) const
{
	glUniform1f(glGetUniformLocation(ID,name.c_str()),value);
}
void Shader::setInt(const string& name, int value) const
{
	glUniform1i(glGetUniformLocation(ID, name.c_str()), value);
}
void Shader::setMat4(const string& name,const glm::mat4& value) const
{
	glUniformMatrix4fv(glGetUniformLocation(ID, name.c_str()),1,GL_FALSE, glm::value_ptr(value));
}
void Shader::setVec4(const std::string &name, const glm::vec4 &value) const
{
    glUniform4fv(glGetUniformLocation(ID, name.c_str()), 1, &value[0]);
}
void Shader::setVec3(const string &name, const glm::vec3 &value) const
{
	glUniform3fv(glGetUniformLocation(ID,name.c_str()),1,&value[0]);
}
void Shader::setVec3(const std::string& name, float x, float y, float z) const
{
	glUniform3f(glGetUniformLocation(ID, name.c_str()), x, y, z);
}
 void Shader::setVec2(const std::string &name, const glm::vec2 &value) const
{
    glUniform2fv(glGetUniformLocation(ID, name.c_str()), 1, &value[0]);
}
 void Shader::setMat2(const std::string &name, const glm::mat2 &value) const
{
    glUniformMatrix2fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, &value[0][0]);
}

void Shader::setMat3(const string& name, const glm::mat3 &value) const
{
    glUniformMatrix3fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, &value[0][0]);
}