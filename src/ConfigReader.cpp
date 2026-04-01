#include "../include/ConfigReader.h"
#include <fstream>
#include <iostream>

using json = nlohmann::json;

bool ConfigReader::loadWritingConfig(const std::string& configPath, WritingConfig& config) {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        std::cerr << "无法打开配置文件: " << configPath << std::endl;
        return false;
    }

    try {
        json j;
        file >> j;

        // 读取各个参数，若不存在则使用默认值
        if (j.contains("rate")) config.rate = j["rate"].get<double>();
        if (j.contains("startX")) config.startX = j["startX"].get<double>();
        if (j.contains("startY")) config.startY = j["startY"].get<double>();
        if (j.contains("zUp")) config.zUp = j["zUp"].get<double>();
        if (j.contains("zDown")) config.zDown = j["zDown"].get<double>();
        if (j.contains("rows")) config.rows = j["rows"].get<int>();
        if (j.contains("cols")) config.cols = j["cols"].get<int>();
        if (j.contains("charSize")) config.charSize = j["charSize"].get<double>();
        if (j.contains("rowMajor")) config.rowMajor = j["rowMajor"].get<bool>();
        if (j.contains("gcodePath")) config.gcodePath = j["gcodePath"].get<std::string>();

        return true;
    } catch (const json::exception& e) {
        std::cerr << "JSON 解析错误: " << e.what() << std::endl;
        return false;
    }
}