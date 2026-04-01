#include <iostream>
#include <string>

#include "include/DexArmController.h"
#include "include/hanzi.h"
#include "src/hanzi.cpp"
#include "include/ConfigReader.h"
#include "include/SerialPort.h"
#include "src/ConfigReader.cpp"
#include "src/DexArmController.cpp"
#include "src/SerialPort.cpp"

int main() {
    // 创建控制器实例
    DexArmController arm;

    // 1. 加载配置文件（假设文件名为 writing_config.json，与程序同目录）
    std::cout << "加载配置文件 writing_config.json ..." << std::endl;
    if (!arm.loadConfig("./config/writing_config.json")) {
        std::cerr << "配置文件加载失败，程序退出。" << std::endl;
        return 1;
    }

    // 2. 连接机械臂（端口可从配置文件读取，或在此指定）
    // 方案一：直接指定端口（推荐用于测试）
    std::string port = "COM3";  // Windows 示例，Linux 为 "/dev/ttyUSB0"
    // 方案二：从配置中读取（需要先扩展 WritingConfig 添加 port 字段，并修改 ConfigReader）
    // 这里为了演示，直接指定端口

    std::cout << "正在连接机械臂到 " << port << " ..." << std::endl;
    if (!arm.connect(port)) {
        std::cerr << "连接失败，请检查串口号和机械臂电源。" << std::endl;
        return 1;
    }
    std::cout << "连接成功！" << std::endl;

    // 3. 归位机械臂（确保安全位置）
    std::cout << "归位中..." << std::endl;
    if (!arm.home()) {
        std::cerr << "归位失败，请检查机械臂状态。" << std::endl;
        arm.disconnect();
        return 1;
    }
    std::cout << "归位完成。" << std::endl;

    // 4. 执行汉字书写（使用配置文件中的参数）
    std::string text = "你好";  // 要书写的汉字
    std::cout << "开始书写汉字: " << text << std::endl;

    if (!arm.generateAndExecute(text)) {
        std::cerr << "书写任务执行失败。" << std::endl;
        arm.disconnect();
        return 1;
    }

    std::cout << "书写任务完成！" << std::endl;

    // 5. 断开连接
    arm.disconnect();
    std::cout << "已断开连接。" << std::endl;

    return 0;
}