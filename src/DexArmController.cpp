#include "../include/DexArmController.h"
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

        if (!sendCommand(line)) {
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