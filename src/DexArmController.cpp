#include "../include/DexArmController.h"
#include "../include/ConfigReader.h"
#include <iostream>
#include <fstream>
#include <algorithm>

bool DexArmController::connect(const std::string& port, int baudrate) {
    return m_serial.open(port, baudrate);
}

void DexArmController::disconnect() {
    m_serial.close();
}

bool DexArmController::sendCommand(const std::string& cmd, int timeoutMs) {
    if (!m_serial.write(cmd + "\r\n")) return false;
    return m_serial.waitForResponse("ok", timeoutMs);
}

bool DexArmController::home() {
    return sendCommand("M1112");
}

bool DexArmController::moveTo(float x, float y, float z, float speed) {
    char buf[128];
    snprintf(buf, sizeof(buf), "G90 G1 X%.1f Y%.1f Z%.1f F%.1f", x, y, z, speed);
    return sendCommand(buf);
}

bool DexArmController::moveRelative(float dx, float dy, float dz, float speed) {
    char buf[128];
    snprintf(buf, sizeof(buf), "G91 G1 X%.1f Y%.1f Z%.1f F%.1f", dx, dy, dz, speed);
    return sendCommand(buf);
}

bool DexArmController::railMove(float e, float speed) {
    char buf[128];
    snprintf(buf, sizeof(buf), "G1 E%.1f F%.1f", e, speed);
    return sendCommand(buf);
}

bool DexArmController::initRail() {
    if (!sendCommand("M888 P6")) return false;
    return sendCommand("M2005");
}

bool DexArmController::isCommentLine(const std::string& line) {
    if (line.empty()) return true;
    size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos) return true;
    return line[start] == ';';
}

bool DexArmController::executeGCodeFile(const std::string& filepath, bool verbose) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "无法打开文件: " << filepath << std::endl;
        return false;
    }

    std::string line;
    int lineNum = 0;
    int successCount = 0;

    while (std::getline(file, line)) {
        lineNum++;

        // 去除行首尾空白
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (isCommentLine(line)) {
            if (verbose) std::cout << "[" << lineNum << "] 跳过注释: " << line << std::endl;
            continue;
        }

        if (verbose) std::cout << "[" << lineNum << "] 发送: " << line << std::endl;

        if (!sendCommand(line, 10000)) {
            std::cerr << "错误: 第" << lineNum << "行发送失败或超时" << std::endl;
            file.close();
            return false;
        }

        successCount++;
        if (verbose) std::cout << "    -> 成功" << std::endl;
    }

    file.close();
    std::cout << "执行完成: 共发送 " << successCount << " 条命令" << std::endl;
    return true;
}

bool DexArmController::generateAndExecute(const std::string& chineseText,
                                          double rate,
                                          double startX, double startY,
                                          double zUp, double zDown,
                                          int rows, int cols,
                                          double charSize,
                                          bool rowMajor,
                                          const std::string& gcodePath)
{
    // 1. 调用 hanzi 类生成 G-code 文件
    // printGCode 参数顺序：汉字文本, 速度, 起始X, 起始Y, 抬笔高度, 落笔高度, 行数, 列数, 字框边长, 行优先标志, 输出路径
    m_hanzi.printGCode(chineseText, rate, startX, startY,
                       zUp, zDown, rows, cols, charSize, rowMajor, gcodePath);

    // 2. 检查文件是否生成成功
    std::ifstream testFile(gcodePath);
    if (!testFile.is_open()) {
        std::cerr << "错误：无法生成 G-code 文件 " << gcodePath << std::endl;
        return false;
    }
    testFile.close();

    // 3. 执行生成的 G-code 文件
    return executeGCodeFile(gcodePath, true);
}

bool DexArmController::loadConfig(const std::string& configPath) {
    return ConfigReader::loadWritingConfig(configPath, m_writingConfig);
}

bool DexArmController::generateAndExecute(const std::string& chineseText) {
    // 1. 调用 hanzi 类生成 G-code 文件
    // 参数顺序与 printGCode 一致
    m_hanzi.printGCode(chineseText,
                       m_writingConfig.rate,
                       m_writingConfig.startX, m_writingConfig.startY,
                       m_writingConfig.zUp, m_writingConfig.zDown,
                       m_writingConfig.rows, m_writingConfig.cols,
                       m_writingConfig.charSize,
                       m_writingConfig.rowMajor,
                       m_writingConfig.gcodePath);

    // 2. 检查文件是否生成成功
    std::ifstream testFile(m_writingConfig.gcodePath);
    if (!testFile.is_open()) {
        std::cerr << "错误：无法生成 G-code 文件 " << m_writingConfig.gcodePath << std::endl;
        return false;
    }
    testFile.close();

    // 3. 执行生成的 G-code 文件
    return executeGCodeFile(m_writingConfig.gcodePath, true);
}
