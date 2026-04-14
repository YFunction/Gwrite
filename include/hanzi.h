#ifndef HANZI_H
#define HANZI_H

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <map>
#include <iomanip>
#include <cmath>
#include <chrono>
#include <thread>
#include <mutex>
#include <tuple>
#include <functional>

#include "../include/json.hpp"

using namespace std;
using json=nlohmann::json;

// ============ hanzi 类声明 ============
class hanzi {
public:
    struct Point{
        double x,y,z;
    };

private:
    struct SubStroke {
        string type;
        size_t startIndex;  // 在笔画点集中的起始点索引
        size_t endIndex;    // 结束点索引
        double startRatio;  // 在笔画总长度中的起始位置比例
        double endRatio;    // 在笔画总长度中的结束位置比例
        Point dir;          // 子笔画的方向向量
        Point originStart;  // 原始骨架点的起点坐标
        Point originEnd;    // 原始骨架点的终点坐标
        size_t originStartIdx = 0; // 原始骨架点的起点下标
        size_t originEndIdx = 0;   // 原始骨架点的终点下标
    };
    struct Bi_Hua{
        std::vector<Point> p;
        vector<SubStroke> subStrokes;  // 子笔画列表
        std::string type;
        std::vector<Point> originPoints; // 原始骨架点集
    };
    struct HanZi{
        vector<Bi_Hua> bi_hua_;
    };

    struct SmoothConfig {
        double minDistSq = 0.25;
        int passes = 2;
        double colinearThreshold = 0.007;
        double maxSegmentLenSq = 9.0;

        // Z 轴控制参数（0..1）
        string zProfileType = "bezier"; // "sin" 或 "bezier"
        double zMin = 0.0;
        double zMax = 1.0;
        double zCtrl1 = 1.0; // only used for bezier
        double zCtrl2 = 1.0; // only used for bezier

        double backRatio = 0.12;      // 回笔长度比例
        double backMinLen = 1.0;      // 最小回笔长度 (mm)
        double backMaxLen = 5.0;      // 最大回笔长度 (mm)

        int fadeInPointCount = 0;       // 渐入点数（0 表示禁用）
        double fadeInLength = 0.0;      // 渐入长度（mm，优先于点数，0 表示使用点数）
        double strokeLeadInMinLen = 20.0;
    } smoothCfg;

    double scale=1.0;
    map<string,HanZi>mp;
    int option_; // 骨架点文件选项：1=原来路径，2=graphics.txt

    // Lazy-load character data from ./hanzi_data/<char>.json on demand.
    bool loadCharData(const string& ch);
    const HanZi* getHanZi(const string& ch);

    void addPoint(Bi_Hua& stroke,double rol=10.0);//插值系数，越大插值越多
    void applyZProfile(Bi_Hua& stroke); // 生成 Z 轴轻重起伏（0~1）
    static void inferStrokeType(Bi_Hua& stroke); // 识别笔画类型（如:h/sh/p/n），同时填充子笔画信息
    void smooth(Bi_Hua& stroke);//滤波平滑
    void addBackstroke(Bi_Hua& stroke); // 添加回笔    void personalizeStroke(Bi_Hua& stroke); // 个性化业画（添加提笔和z章调整）    void addLiftForTipOrHook(Bi_Hua& stroke); // 为提或钩添加提笔
    void addStrokeLeadIn(Bi_Hua& stroke, size_t pointCount = 3);
    void extendStrokeEnd(Bi_Hua& stroke, double length, int numPoints = 3);
    void personalizeStroke(Bi_Hua& stroke);
    void addLiftForTipOrHook(Bi_Hua& stroke);
    void updateSubStrokeIndices(Bi_Hua& stroke);
    void outPoint(const Bi_Hua& stroke, const std::string& filename);

    #ifdef DEBUG
    void print3D(const hanzi::Bi_Hua& stroke, const std::string& windowName = "Stroke 3D");
    #endif

public:
    //初始构造函数
    explicit hanzi(int option = 1);

    void printGCode(string name,            // 要写的汉字（可包含多个字符）
                double Rate,               // 速度
                double x,double y,        // 相对零点绝对坐标
                double z_up,double z_down,// 起笔落笔深度
                int n=1, int m=1,         // 行/列，最多写 n*m 个字
                double Size=100.0,        // 每个字占用的正方形边长（mm）
                bool rowMajor=true,       // true: 以行优先写，false: 以列优先写
                string path="./G.gcode"); // 输出文件
    
    // -------- 新增：串口实时控制方法 --------
    void writeWithSerial(const std::string& name,
                         const std::string& port,
                         double Rate,
                         double x0, double y0,
                         double z_up, double z_down,
                         int n = 1, int m = 1,
                         double Size = 100.0,
                         bool rowMajor = true);
    
    void printAllWord();
    void printSingleWord(string name);
};


#endif // HANZI_H
