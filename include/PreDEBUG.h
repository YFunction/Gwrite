#ifndef preDebugMode
#define preDebugMode

#include<iostream>

using namespace std;

class NullBuffer : public std::streambuf {
public:
    int overflow(int c) override { return c; }
};

class NullStream : public std::ostream {
public:
    NullStream() : std::ostream(&nullBuffer) {}
private:
    NullBuffer nullBuffer;
};

// 根据 DEBUG 宏定义不同的输出流
#ifdef DEBUG
    #define COUT std::cout
#else
    #define COUT NullStream()
#endif
#endif