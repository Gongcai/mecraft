//
// Created by Caiwe on 2026/3/29.
//

#include "AudioClip.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <vector>
// WAV 文件头结构
#pragma pack(push, 1)
struct WAVHeader {
    char riff[4];           // "RIFF"
    uint32_t fileSize;      // 文件大小 - 8
    char wave[4];           // "WAVE"
    char fmt[4];            // "fmt "
    uint32_t fmtSize;       // fmt 块大小
    uint16_t audioFormat;   // 音频格式 (1 = PCM)
    uint16_t numChannels;   // 声道数
    uint32_t sampleRate;    // 采样率
    uint32_t byteRate;      // 字节率
    uint16_t blockAlign;    // 块对齐
    uint16_t bitsPerSample; // 位深度
};
#pragma pack(pop)

AudioClip::AudioClip(const std::string& filepath) 
    : m_filepath(filepath) {
    // 从路径提取文件名（不含扩展名）作为名称
    size_t lastSep = filepath.find_last_of("/\\");
    size_t lastDot = filepath.find_last_of('.');
    if (lastSep != std::string::npos && lastDot != std::string::npos && lastDot > lastSep) {
        m_name = filepath.substr(lastSep + 1, lastDot - lastSep - 1);
    } else {
        m_name = filepath;
    }

    m_valid = loadWAV(filepath);
    if (m_valid) {
        std::cout << "[Audio] Loaded: " << m_name << " (" << m_duration << "s)" << std::endl;
    }
}

AudioClip::~AudioClip() {
    if (m_buffer != 0) {
        alDeleteBuffers(1, &m_buffer);
        m_buffer = 0;
    }
}

bool AudioClip::loadWAV(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[Audio] Failed to open: " << filepath << std::endl;
        return false;
    }

    WAVHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(WAVHeader));

    // 验证 WAV 文件
    if (std::strncmp(header.riff, "RIFF", 4) != 0 ||
        std::strncmp(header.wave, "WAVE", 4) != 0 ||
        std::strncmp(header.fmt, "fmt ", 4) != 0) {
        std::cerr << "[Audio] Invalid WAV format: " << filepath << std::endl;
        return false;
    }

    // 只支持 PCM 格式
    if (header.audioFormat != 1) {
        std::cerr << "[Audio] Only PCM format supported: " << filepath << std::endl;
        return false;
    }

    // 跳过额外的 fmt 数据
    if (header.fmtSize > 16) {
        file.seekg(header.fmtSize - 16, std::ios::cur);
    }

    // 查找 data 块
    char chunkId[4];
    uint32_t chunkSize;
    while (file.read(chunkId, 4)) {
        file.read(reinterpret_cast<char*>(&chunkSize), 4);
        if (std::strncmp(chunkId, "data", 4) == 0) {
            break;
        }
        // 跳过其他块
        file.seekg(chunkSize, std::ios::cur);
    }

    if (!file) {
        std::cerr << "[Audio] No data chunk found: " << filepath << std::endl;
        return false;
    }

    // 读取音频数据
    std::vector<char> audioData(chunkSize);
    file.read(audioData.data(), chunkSize);
    file.close();

    // 确定 OpenAL 格式
    ALenum format;
    if (header.numChannels == 1) {
        format = (header.bitsPerSample == 8) ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;
    } else {
        format = (header.bitsPerSample == 8) ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
    }

    // 创建 OpenAL buffer
    alGenBuffers(1, &m_buffer);
    alBufferData(m_buffer, format, audioData.data(), chunkSize, header.sampleRate);

    // 计算时长
    m_duration = static_cast<float>(chunkSize) / header.byteRate;

    // 检查错误
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        std::cerr << "[Audio] OpenAL error: " << error << std::endl;
        if (m_buffer != 0) {
            alDeleteBuffers(1, &m_buffer);
            m_buffer = 0;
        }
        return false;
    }

    return true;
}