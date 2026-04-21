#include "../include/DexArmController.h"
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <cstring>
#include <iostream>

#ifdef _WIN32
    #define NOMINMAX
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #undef byte
#else
    #include <fcntl.h>
    #include <unistd.h>
    #include <termios.h>
    #include <errno.h>
#endif

class SerialPortImpl {
public:
    SerialPortImpl(const std::string& port, int baudrate) {
#ifdef _WIN32
        std::string fullPort = "\\\\.\\" + port;
        hCom_ = CreateFileA(fullPort.c_str(),
                            GENERIC_READ | GENERIC_WRITE,
                            0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hCom_ == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Failed to open serial port: " + port);
        DCB dcb = {0};
        dcb.DCBlength = sizeof(DCB);
        GetCommState(hCom_, &dcb);
        dcb.BaudRate = baudrate;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        SetCommState(hCom_, &dcb);
        COMMTIMEOUTS timeouts = {0};
        // 设置读取超时：立即返回现有数据，若无数据则等待最多 50ms
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.ReadTotalTimeoutConstant = 50;
        timeouts.WriteTotalTimeoutConstant = 100;
        SetCommTimeouts(hCom_, &timeouts);
#else
        fd_ = open(port.c_str(), O_RDWR | O_NOCTTY);
        if (fd_ == -1)
            throw std::runtime_error("Failed to open serial port: " + port);
        struct termios options;
        tcgetattr(fd_, &options);
        cfsetispeed(&options, baudrate);
        cfsetospeed(&options, baudrate);
        options.c_cflag |= (CLOCAL | CREAD);
        options.c_cflag &= ~PARENB;
        options.c_cflag &= ~CSTOPB;
        options.c_cflag &= ~CSIZE;
        options.c_cflag |= CS8;
        options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        options.c_iflag &= ~(IXON | IXOFF | IXANY);
        options.c_oflag &= ~OPOST;
        tcsetattr(fd_, TCSANOW, &options);
#endif
    }

    ~SerialPortImpl() {
#ifdef _WIN32
        if (hCom_ != INVALID_HANDLE_VALUE) CloseHandle(hCom_);
#else
        if (fd_ != -1) close(fd_);
#endif
    }

    int write(const std::string& data) {
#ifdef _WIN32
        DWORD written = 0;
        WriteFile(hCom_, data.c_str(), data.size(), &written, NULL);
        return static_cast<int>(written);
#else
        return ::write(fd_, data.c_str(), data.size());
#endif
    }

    // 阻塞读取一行，直到遇到 '\n' 或总超时到达
    std::string readLine(int timeoutMs) {
        std::string line;
        auto start = std::chrono::steady_clock::now();
        char ch;
        while (true) {
            int n = readChar(ch);
            if (n > 0) {
                if (ch == '\n') {
                    // 去除结尾的 '\r'
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    return line;
                }
                line += ch;
            } else {
                // 检查总超时
                if (timeoutMs > 0) {
                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() > timeoutMs) {
                        return ""; // 超时
                    }
                }
                // 短暂休眠，避免 CPU 空转
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

private:
#ifdef _WIN32
    HANDLE hCom_ = INVALID_HANDLE_VALUE;
    int readChar(char& ch) {
        DWORD read = 0;
        if (ReadFile(hCom_, &ch, 1, &read, NULL) && read == 1)
            return 1;
        return 0;
    }
#else
    int fd_ = -1;
    int readChar(char& ch) {
        return read(fd_, &ch, 1);
    }
#endif
};

// ---------- DexArmController 实现 ----------
DexArmController::DexArmController(const std::string& portName)
    : serial_(std::make_unique<SerialPortImpl>(portName, 115200)) {}

DexArmController::~DexArmController() = default;

int DexArmController::sendCommand(const std::string& cmd) {
    std::string fullCmd = cmd + "\r\n";
    return serial_->write(fullCmd);
}

std::string DexArmController::readLine(int timeoutMs) {
    return serial_->readLine(timeoutMs);
}

bool DexArmController::sendAndWaitOk(const std::string& cmd, int timeoutMs) {
    if (sendCommand(cmd) <= 0) {
        std::cerr << "Failed to send: " << cmd << std::endl;
        return false;
    }

    auto start = std::chrono::steady_clock::now();
    while (true) {
        // 剩余超时时间
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        int remain = timeoutMs - static_cast<int>(elapsed);
        if (remain <= 0) {
            std::cerr << "Timeout waiting for 'ok' after: " << cmd << std::endl;
            return false;
        }

        std::string resp = readLine(remain);
        if (resp.empty()) {
            // 超时
            std::cerr << "Timeout waiting for 'ok' after: " << cmd << std::endl;
            return false;
        }

        if (resp == "wait")
            continue;  // 忽略心跳
        if (resp == "ok")
            return true;

        // 其他响应：打印出来（如错误回显）
        std::cout << "[Arm] " << resp << std::endl;
        // 继续等待 ok
    }
}

// 常用命令封装（同前，略作保留）
bool DexArmController::home() { return sendAndWaitOk("M1112"); }
bool DexArmController::setWorkOrigin() { return sendAndWaitOk("G92 X0 Y0 Z0"); }
bool DexArmController::resetWorkOrigin() { return sendAndWaitOk("G92.1"); }
bool DexArmController::setAbsolutePositioning() { return sendAndWaitOk("G90"); }
bool DexArmController::setRelativePositioning() { return sendAndWaitOk("G91"); }

bool DexArmController::rapidMove(double x, double y, double z, double f) {
    std::ostringstream oss;
    oss << "G0 X" << std::fixed << std::setprecision(1) << x
        << " Y" << y << " Z" << z;
    if (f > 0) oss << " F" << f;
    return sendAndWaitOk(oss.str());
}

bool DexArmController::linearMove(double x, double y, double z, double f) {
    std::ostringstream oss;
    oss << "G1 X" << std::fixed << std::setprecision(1) << x
        << " Y" << y << " Z" << z << " F" << f;
    return sendAndWaitOk(oss.str());
}

bool DexArmController::dwell(int milliseconds) {
    return sendAndWaitOk("G4 P" + std::to_string(milliseconds));
}

bool DexArmController::setPenModule() {
    return sendAndWaitOk("M888 P0");
}

bool DexArmController::setLaserModule() {
    return sendAndWaitOk("M888 P1");
}

std::string DexArmController::getCurrentPosition() {
    sendCommand("M114");
    return readLine(1000);
}

std::string DexArmController::getDeviceStatus() {
    sendCommand("M503");
    return readLine(2000);
}

std::string DexArmController::getFirmwareVersion() {
    sendCommand("M2010");
    return readLine(1000);
}

void DexArmController::emergencyStop() {
    sendCommand("M112");
}