//
// Created by Caiwe on 2026/3/21.
//
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "TestCube.h"
#include <cmath>


TestCube::TestCube() {
	texture = loadTexture("../assets/textures/blocks/test.png");
    float vertices[] = {
			// back face		  //norm			  // tex
			-1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 0.0f, 0.0f, // bottom-left
			 1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 1.0f, 1.0f, // top-right
			 1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 1.0f, 0.0f, // bottom-right
			 1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 1.0f, 1.0f, // top-right
			-1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 0.0f, 0.0f, // bottom-left
			-1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 0.0f, 1.0f, // top-left
			// front face
			-1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 0.0f, // bottom-left
			 1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f, 0.0f, // bottom-right
			 1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f, 1.0f, // top-right
			 1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f, 1.0f, // top-right
			-1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 1.0f, // top-left
			-1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 0.0f, // bottom-left
			// left face
			-1.0f,  1.0f,  1.0f, -1.0f,  0.0f,  0.0f, 1.0f, 0.0f, // top-right
			-1.0f,  1.0f, -1.0f, -1.0f,  0.0f,  0.0f, 1.0f, 1.0f, // top-left
			-1.0f, -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, 0.0f, 1.0f, // bottom-left
			-1.0f, -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, 0.0f, 1.0f, // bottom-left
			-1.0f, -1.0f,  1.0f, -1.0f,  0.0f,  0.0f, 0.0f, 0.0f, // bottom-right
			-1.0f,  1.0f,  1.0f, -1.0f,  0.0f,  0.0f, 1.0f, 0.0f, // top-right
			// right face
			 1.0f,  1.0f,  1.0f,  1.0f,  0.0f,  0.0f, 1.0f, 0.0f, // top-left
			 1.0f, -1.0f, -1.0f,  1.0f,  0.0f,  0.0f, 0.0f, 1.0f, // bottom-right
			 1.0f,  1.0f, -1.0f,  1.0f,  0.0f,  0.0f, 1.0f, 1.0f, // top-right
			 1.0f, -1.0f, -1.0f,  1.0f,  0.0f,  0.0f, 0.0f, 1.0f, // bottom-right
			 1.0f,  1.0f,  1.0f,  1.0f,  0.0f,  0.0f, 1.0f, 0.0f, // top-left
			 1.0f, -1.0f,  1.0f,  1.0f,  0.0f,  0.0f, 0.0f, 0.0f, // bottom-left
			 // bottom face
			 -1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f, 0.0f, 1.0f, // top-right
			  1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f, 1.0f, 1.0f, // top-left
			  1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f, 1.0f, 0.0f, // bottom-left
			  1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f, 1.0f, 0.0f, // bottom-left
			 -1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f, 0.0f, 0.0f, // bottom-right
			 -1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f, 0.0f, 1.0f, // top-right
			 // top face
			 -1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f, 0.0f, 1.0f, // top-left
			  1.0f,  1.0f , 1.0f,  0.0f,  1.0f,  0.0f, 1.0f, 0.0f, // bottom-right
			  1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f, 1.0f, 1.0f, // top-right
			  1.0f,  1.0f,  1.0f,  0.0f,  1.0f,  0.0f, 1.0f, 0.0f, // bottom-right
			 -1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f, 0.0f, 1.0f, // top-left
			 -1.0f,  1.0f,  1.0f,  0.0f,  1.0f,  0.0f, 0.0f, 0.0f  // bottom-left
		};
		glGenVertexArrays(1, &VAO);
		glGenBuffers(1, &VBO);
		// fill buffer
		glBindBuffer(GL_ARRAY_BUFFER, VBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
		// link vertex attributes
		glBindVertexArray(VAO);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
}

TestCube::TestCube(glm::vec3 pos) {
	this->pos = pos;
	texture = loadTexture("../assets/textures/blocks/test.png");
    float vertices[] = {
        // Position           // UV      // Norm // Sun  // Block // AO
        -0.5f, -0.5f, -0.5f,  0.0f, 0.0f, 0.0f,  15.0f,  10.0f,    3.0f,
         0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,  15.0f,  10.0f,    3.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 0.0f,  15.0f,  10.0f,    3.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 0.0f,  15.0f,  10.0f,    3.0f,
        -0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 0.0f,  15.0f,  10.0f,    3.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, 0.0f, 0.0f,  15.0f,  10.0f,    3.0f,

        -0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 1.0f,  15.0f,  10.0f,    3.0f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 1.0f,  15.0f,  10.0f,    3.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 1.0f,  15.0f,  10.0f,    3.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 1.0f,  15.0f,  10.0f,    3.0f,
        -0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 1.0f,  15.0f,  10.0f,    3.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 1.0f,  15.0f,  10.0f,    3.0f,

        -0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 2.0f,  15.0f,  10.0f,    3.0f,
        -0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 2.0f,  15.0f,  10.0f,    3.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 2.0f,  15.0f,  10.0f,    3.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 2.0f,  15.0f,  10.0f,    3.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 2.0f,  15.0f,  10.0f,    3.0f,
        -0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 2.0f,  15.0f,  10.0f,    3.0f,

         0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 3.0f,  15.0f,  10.0f,    3.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 3.0f,  15.0f,  10.0f,    3.0f,
         0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 3.0f,  15.0f,  10.0f,    3.0f,
         0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 3.0f,  15.0f,  10.0f,    3.0f,
         0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 3.0f,  15.0f,  10.0f,    3.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 3.0f,  15.0f,  10.0f,    3.0f,

        -0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 4.0f,  15.0f,  10.0f,    3.0f,
         0.5f, -0.5f, -0.5f,  1.0f, 1.0f, 4.0f,  15.0f,  10.0f,    3.0f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 4.0f,  15.0f,  10.0f,    3.0f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 4.0f,  15.0f,  10.0f,    3.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 4.0f,  15.0f,  10.0f,    3.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 4.0f,  15.0f,  10.0f,    3.0f,

        -0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 5.0f,  15.0f,  10.0f,    3.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 5.0f,  15.0f,  10.0f,    3.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 5.0f,  15.0f,  10.0f,    3.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 5.0f,  15.0f,  10.0f,    3.0f,
        -0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 5.0f,  15.0f,  10.0f,    3.0f,
        -0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 5.0f,  15.0f,  10.0f,    3.0f
    };

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // aPos (layout 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // aUV (layout 1)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // aNormalIndex (layout 2)
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    // aSunlight (layout 3)
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    // aBlockLight (layout 4)
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(7 * sizeof(float)));
    glEnableVertexAttribArray(4);
    // aAO (layout 5)
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(5);
}

void TestCube::draw() {
	shader.use();
	shader.setInt("textAtlas", 0);
	auto model = glm::mat4(1.0f);
	model = glm::translate(model, pos);
	shader.setMat4("model", model);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glBindVertexArray(VAO);
	glDrawArrays(GL_TRIANGLES, 0, 36);
}

void TestCube::setViewProjection(glm::mat4 viewProj) {
	shader.use();
	shader.setMat4("viewProj", viewProj);
}

void TestCube::update() {
    float radius = 2.0f;
    pos.x = static_cast<float>(cos(Time::currentGameTime) * radius);
    pos.z = static_cast<float>(sin(Time::currentGameTime) * radius);
}

unsigned int TestCube::loadTexture(const char *path) {
	unsigned int textureID;
	glGenTextures(1, &textureID);

	int width, height, nrComponents;
	unsigned char *data = stbi_load(path, &width, &height, &nrComponents, 0);
	if (data) {
		GLenum format;
		if (nrComponents == 1) {
			format = GL_RED;
		}
		else if (nrComponents == 3) {
			format = GL_RGB;

		}
		else if (nrComponents == 4) {
			format = GL_RGBA;
		}
		glBindTexture(GL_TEXTURE_2D, textureID);
		glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
		glGenerateMipmap(GL_TEXTURE_2D);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		stbi_image_free(data);
	}
	else {
		std::cout << "Texture load failed" << std::endl;
		stbi_image_free(data);
	}
	return textureID;
}
