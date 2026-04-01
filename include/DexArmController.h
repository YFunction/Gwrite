#ifndef DEXARMCONTROLLER_H
#define DEXARMCONTROLLER_H

#include "SerialPort.h"
#include <string>

class DexArmController {
public:
    // 连接串口
    bool connect(const std::string& port, int baudrate = 115200);
    void disconnect();

    // 归位命令
    bool home();

    // 绝对移动 (X, Y, Z, F速度)
    bool moveTo(float x, float y, float z, float speed = 40);

    // 相对移动
    bool moveRelative(float dx, float dy, float dz, float speed = 40);

    // 滑轨控制 (E轴)
    bool railMove(float e, float speed = 40);

    // 初始化滑轨模块
    bool initRail();

    // 执行G-code文件（指定路径）
    bool executeGCodeFile(const std::string& filepath, bool verbose = true);

private:
    SerialPort m_serial;

    // 发送单条命令并等待 "ok"
    bool sendCommand(const std::string& cmd, int timeoutMs = 2000);
    bool isCommentLine(const std::string& line);
};

#endif // DEXARMCONTROLLER_H