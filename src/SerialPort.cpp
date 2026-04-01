#include "../include/SerialPort.h"
#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>

SerialPort::SerialPort() : m_isOpen(false)
#ifdef _WIN32
    , m_hCom(INVALID_HANDLE_VALUE)
#else
    , m_fd(-1)
#endif
{
}

SerialPort::~SerialPort() {
    close();
}

bool SerialPort::open(const std::string& port, int baudrate) {
    if (m_isOpen) close();

#ifdef _WIN32
    // Windows 串口打开
    std::string portName = "\\\\.\\" + port; // 支持 COM10 以上
    m_hCom = CreateFileA(portName.c_str(),
                         GENERIC_READ | GENERIC_WRITE,
                         0,                     // 独占访问
                         NULL,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         NULL);
    if (m_hCom == INVALID_HANDLE_VALUE) {
        std::cerr << "无法打开串口 " << port << std::endl;
        return false;
    }

    // 配置串口
    if (!configurePort(baudrate)) {
        CloseHandle(m_hCom);
        m_hCom = INVALID_HANDLE_VALUE;
        return false;
    }

    m_isOpen = true;
    return true;

#else
    // Linux 串口打开
    m_fd = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (m_fd == -1) {
        std::cerr << "无法打开串口 " << port << std::endl;
        return false;
    }

    if (!configurePort(baudrate)) {
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    m_isOpen = true;
    return true;
#endif
}

void SerialPort::close() {
    if (!m_isOpen) return;
#ifdef _WIN32
    if (m_hCom != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hCom);
        m_hCom = INVALID_HANDLE_VALUE;
    }
#else
    if (m_fd != -1) {
        ::close(m_fd);
        m_fd = -1;
    }
#endif
    m_isOpen = false;
}

bool SerialPort::write(const std::string& data) {
    if (!m_isOpen) return false;
#ifdef _WIN32
    DWORD bytesWritten;
    BOOL success = WriteFile(m_hCom, data.c_str(), data.size(), &bytesWritten, NULL);
    return success && (bytesWritten == data.size());
#else
    ssize_t bytesWritten = ::write(m_fd, data.c_str(), data.size());
    return bytesWritten == (ssize_t)data.size();
#endif
}

bool SerialPort::readLine(std::string& line, int timeoutMs) {
    if (!m_isOpen) return false;

    line.clear();
    char ch;
    auto start = std::chrono::steady_clock::now();

    while (true) {
        // 超时检查
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() > timeoutMs) {
            return false; // 超时未收到完整行
        }

#ifdef _WIN32
        DWORD bytesRead;
        if (!ReadFile(m_hCom, &ch, 1, &bytesRead, NULL) || bytesRead != 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
#else
        ssize_t bytesRead = ::read(m_fd, &ch, 1);
        if (bytesRead != 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
#endif

        if (ch == '\n') {
            // 去掉末尾的 \r（如果有）
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        }
        line.push_back(ch);
    }
}

bool SerialPort::waitForResponse(const std::string& expected, int timeoutMs) {
    if (!m_isOpen) return false;
    auto start = std::chrono::steady_clock::now();

    while (true) {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() > timeoutMs) {
            return false;
        }

        std::string line;
        if (readLine(line, 100)) { // 每次最多等 100ms，以便检查总超时
            if (line.find(expected) != std::string::npos) {
                return true;
            }
        }
    }
}

// 平台相关的串口参数配置
bool SerialPort::configurePort(int baudrate) {
#ifdef _WIN32
    DCB dcb = { 0 };
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(m_hCom, &dcb)) return false;

    dcb.BaudRate = baudrate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState(m_hCom, &dcb)) return false;

    // 设置超时
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    if (!SetCommTimeouts(m_hCom, &timeouts)) return false;

    return true;

#else
    struct termios tty;
    if (tcgetattr(m_fd, &tty) != 0) return false;

    // 设置波特率
    speed_t speed;
    switch (baudrate) {
        case 9600:   speed = B9600; break;
        case 19200:  speed = B19200; break;
        case 38400:  speed = B38400; break;
        case 57600:  speed = B57600; break;
        case 115200: speed = B115200; break;
        default: return false;
    }
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    // 8N1
    tty.c_cflag &= ~PARENB;   // 无校验
    tty.c_cflag &= ~CSTOPB;   // 1停止位
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;       // 8数据位

    // 无流控
    tty.c_cflag &= ~CRTSCTS;

    // 本地连接，启用接收
    tty.c_cflag |= CREAD | CLOCAL;

    // 原始模式
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_oflag &= ~OPOST;

    // 设置读取超时（1/10秒）
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(m_fd, TCSANOW, &tty) != 0) return false;

    return true;
#endif
}