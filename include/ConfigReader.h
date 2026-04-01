#ifndef CONFIGREADER_H
#define CONFIGREADER_H

#include <string>
#include "DexArmController.h" // 需要 WritingConfig 定义
#include "../include/json.hpp"

class ConfigReader {
public:
    // 从 JSON 文件加载配置，成功返回 true
    static bool loadWritingConfig(const std::string& configPath, WritingConfig& config);
};

#endif // CONFIGREADER_H