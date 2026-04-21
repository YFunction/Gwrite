#ifndef DEXARM_CONTROLLER_H
#define DEXARM_CONTROLLER_H

#include <string>
#include <vector>
#include <memory>

class SerialPortImpl;

/**
 * @brief DexArm 机械臂控制器
 */
class DexArmController {
public:
    explicit DexArmController(const std::string& portName);
    ~DexArmController();

    DexArmController(const DexArmController&) = delete;
    DexArmController& operator=(const DexArmController&) = delete;

    // 发送原始指令（自动追加 \r\n）
    int sendCommand(const std::string& cmd);

    // 读取一行（阻塞，超时返回空字符串）
    std::string readLine(int timeoutMs = 1000);

    // 发送并等待 "ok" 响应，自动忽略 "wait"
    bool sendAndWaitOk(const std::string& cmd, int timeoutMs = 5000);

    // ---------- 常用命令封装 ----------
    bool home();                    // M1112
    bool setWorkOrigin();           // G92 X0 Y0 Z0
    bool resetWorkOrigin();         // G92.1
    bool setAbsolutePositioning();  // G90
    bool setRelativePositioning();  // G91
    bool rapidMove(double x, double y, double z, double f = -1);
    bool linearMove(double x, double y, double z, double f);
    bool dwell(int milliseconds);   // G4 Pxxx
    bool setPenModule();            // M888 P0
    bool setLaserModule();          // M888 P1
    std::string getCurrentPosition(); // M114
    std::string getDeviceStatus();    // M503
    std::string getFirmwareVersion(); // M2010
    void emergencyStop();             // M112

private:
    std::unique_ptr<SerialPortImpl> serial_;
};

#endif