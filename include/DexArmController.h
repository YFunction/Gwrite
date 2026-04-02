#ifndef DEXARMCONTROLLER_H
#define DEXARMCONTROLLER_H

#include "SerialPort.h"
#include <string>
#include "hanzi.h"

struct WritingConfig {
    double rate = 2000.0;          // 速度 (mm/min)
    double startX = 0.0;           // 起始 X 坐标
    double startY = 0.0;           // 起始 Y 坐标
    double zUp = 30.0;             // 抬笔高度 (mm)
    double zDown = 0.0;            // 落笔高度 (mm)
    int rows = 1;                  // 行数
    int cols = 1;                  // 列数
    double charSize = 100.0;       // 字框边长 (mm)
    bool rowMajor = true;          // 行优先（true）或列优先（false）
    std::string gcodePath = "./G.gcode"; // 输出 G-code 文件路径
};

class DexArmController {
public:
    const WritingConfig& getWritingConfig() const { return m_writingConfig; }
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

    bool generateAndExecute(const std::string& chineseText,
                            double rate,
                            double startX, double startY,
                            double zUp, double zDown,
                            int rows = 1, int cols = 1,
                            double charSize = 100.0,
                            bool rowMajor = true,
                            const std::string& gcodePath = "./G.gcode");

    bool loadConfig(const std::string& configPath);

    // 自动生成并执行汉字 G-code（使用当前配置）
    bool generateAndExecute(const std::string& chineseText);
    
private:
    SerialPort m_serial;
    WritingConfig m_writingConfig;   // 当前配置
    hanzi m_hanzi;          // 汉字生成器实例
    // 发送单条命令并等待 "ok"
    
    bool sendCommand(const std::string& cmd, int timeoutMs = 2000);
    bool isCommentLine(const std::string& line);
};

#endif // DEXARMCONTROLLER_H