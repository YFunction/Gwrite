#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstdlib>
#include <algorithm>

#include "include/hanzi.h"
#include "src/hanzi.cpp"
#include "include/DexArmController.h"
#include "src/DexArmController.cpp"

// 过滤不支持的指令
bool isCommandSupported(const std::string& line) {
    std::string cmd = line;
    cmd.erase(0, cmd.find_first_not_of(" \t"));
    cmd.erase(cmd.find_last_not_of(" \t\r\n") + 1);
    if (cmd.empty() || cmd[0] == ';') return false;

    static const std::vector<std::string> unsupported = {"G17", "M30"};
    for (const auto& bad : unsupported) {
        if (cmd.compare(0, bad.size(), bad) == 0) {
            if (cmd.size() == bad.size() || cmd[bad.size()] == ' ' || cmd[bad.size()] == '\0')
                return false;
        }
    }
    return true;
}

bool sendGcodeFile(DexArmController& arm, const std::string& filepath) {
    std::ifstream infile(filepath);
    if (!infile.is_open()) {
        std::cerr << "Failed to open G-code file: " << filepath << std::endl;
        return false;
    }

    std::string line;
    int lineNum = 0;
    while (std::getline(infile, line)) {
        lineNum++;
        if (!isCommandSupported(line)) continue;

        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);

        std::cout << "Sending [" << lineNum << "]: " << line << std::endl;
        if (!arm.sendAndWaitOk(line, 5000)) {
            std::cerr << "Aborting due to send failure." << std::endl;
            return false;
        }
    }
    std::cout << "G-code file sent successfully.\n";
    return true;
}

void showMenu() {
    std::cout << "\n=====================================\n";
    std::cout << "    DexArm Chinese Character Writer\n";
    std::cout << "=====================================\n";
    std::cout << "1. Write text\n";
    std::cout << "2. Set serial port\n";
    std::cout << "3. Home arm\n";
    std::cout << "4. Exit\n";
    std::cout << "5. Set work Z height (manual)\n";
    std::cout << "Choice: ";
}

int main() {

    hanzi hz(1);
    std::string portName = "COM3";
    std::unique_ptr<DexArmController> arm = nullptr;
    double workZ0 = 0.0; // 工作Z零点

    auto connectArm = [&]() -> bool {
        try {
            arm = std::make_unique<DexArmController>(portName);
            std::cout << "Connected to " << portName << std::endl;
            if (!arm->setPenModule()) {
                std::cerr << "Warning: Failed to set pen module.\n";
            }
            if (!arm->home()) {
                std::cerr << "Warning: Homing failed.\n";
            }
            arm->setAbsolutePositioning();
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Connection error: " << e.what() << std::endl;
            return false;
        }
    };

    bool running = true;
    while (running) {
        showMenu();
        int choice;
        std::cin >> choice;
        std::cin.ignore();

        switch (choice) {
            case 1: {
                if (!arm) {
                    std::cout << "Not connected. Set serial port first.\n";
                    break;
                }
                std::cout << "Enter text to write: ";
                std::string text;
                std::getline(std::cin, text);
                if (text.empty()) break;

                double rate = 1000.0;
                double x0 = -200.0, y0 = 200.0;
                double z_up = 0.0 + workZ0, z_down = -2.0 + workZ0;
                int cols = 10, rows = 1;
                double charSize = 30.0;
                std::string gcodeFile = "./output.gcode";

                std::cout << "Generating G-code...\n";
                hz.printGCode(text, rate, x0, y0, z_up, z_down, cols, rows, charSize, true, gcodeFile);
                std::cout << "Saved to " << gcodeFile << std::endl;

                std::cout << "Start writing? (y/n): ";
                char confirm;
                std::cin >> confirm;
                std::cin.ignore();
                if (confirm != 'y' && confirm != 'Y') break;

                if (!sendGcodeFile(*arm, gcodeFile)) {
                    std::cerr << "Writing interrupted.\n";
                } else {
                    std::cout << "Writing completed.\n";
                }
                arm->linearMove(x0, y0, z_up, 800);
                break;
            }
            case 2: {
                std::cout << "Enter serial port name: ";
                std::getline(std::cin, portName);
                if (connectArm())
                    std::cout << "Connected.\n";
                else
                    std::cout << "Connection failed.\n";
                break;
            }
            case 3: {
                if (arm) {
                    std::cout << "Homing...\n";
                    if (arm->home()) std::cout << "Homed.\n";
                    else std::cout << "Home failed.\n";
                } else {
                    std::cout << "Arm not connected.\n";
                }
                break;
            }
            case 4: {
                running = false;
                break;
            }
            case 5: {
                if (!arm) {
                    std::cout << "Not connected. Set serial port first.\n";
                    break;
                }
                std::cout << "\n进入Z轴手动控制模式。\n";
                std::cout << "当前工作Z零点: " << workZ0 << "\n";
                std::cout << "操作说明：\n";
                std::cout << "1. Z+20mm\n2. Z+5mm\n3. Z+1mm\n4. Z+0.1mm\n5. Z-0.1mm\n6. Z-1mm\n7. Z-5mm\n8. Z-20mm\n9. 设置当前位置为工作Z零点\n0. 退出\n";
                double curZ = workZ0;
                bool zmode = true;
                while (zmode) {
                    std::cout << "当前Z: " << curZ << "  (工作零点: " << workZ0 << ")\n";
                    std::cout << "选择操作: ";
                    int zopt;
                    std::cin >> zopt;
                    switch (zopt) {
                        case 1: curZ += 20; break;
                        case 2: curZ += 5; break;
                        case 3: curZ += 1; break;
                        case 4: curZ += 0.1; break;
                        case 5: curZ -= 0.1; break;
                        case 6: curZ -= 1; break;
                        case 7: curZ -= 5; break;
                        case 8: curZ -= 20; break;
                        case 9: workZ0 = curZ; std::cout << "已设置当前位置为工作Z零点: " << workZ0 << "\n"; break;
                        case 0: zmode = false; continue;
                        default: std::cout << "无效选项。\n"; continue;
                    }
                    arm->linearMove(0, 300, curZ, 1000);
                }
                break;
            }
            default:
                std::cout << "Invalid choice.\n";
        }
    }

    if (arm) {
        arm->linearMove(0, 300, 10 + workZ0, 1000);
    }

    std::cout << "Program terminated.\n";
    return 0;
}