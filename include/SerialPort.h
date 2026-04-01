#ifndef SERIALPORT_H
#define SERIALPORT_H

#include <string>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <termios.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

class SerialPort {
public:
    SerialPort();
    ~SerialPort();

    // 打开串口，参数：端口名（Windows: "COM3", Linux: "/dev/ttyUSB0"），波特率
    bool open(const std::string& port, int baudrate = 115200);
    void close();

    // 发送数据（原始字节）
    bool write(const std::string& data);

    // 读取一行数据（直到 '\n'），返回是否成功
    bool readLine(std::string& line, int timeoutMs = 1000);

    // 等待特定响应（如 "ok"），返回是否在规定时间内收到
    bool waitForResponse(const std::string& expected, int timeoutMs = 2000);

    // 检查是否已打开
    bool isOpen() const { return m_isOpen; }

private:
    bool m_isOpen;

#ifdef _WIN32
    HANDLE m_hCom;
#else
    int m_fd;
#endif

    // 平台相关：设置串口参数
    bool configurePort(int baudrate);
};

#endif // SERIALPORT_H