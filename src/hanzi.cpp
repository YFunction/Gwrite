#include"../include/preDEBUG.h"

#include<iostream>
#include<fstream>
#include<map>
#include<iomanip>
#include<cmath>
#include<filesystem>
#include<tuple>

#ifdef _WIN32
    //#include<windows.h>
#endif

#include"../include/json.hpp"
#include"../include/hanzi.h"

#ifndef M_PI
#define M_PI acos(-1.0)
#endif

using namespace std;
using json=nlohmann::json;

// 特征结构体
struct SubStrokeFeatures {
    double totalLen;
    double chordLen;
    double bend;
    double dirAngle;        // 首尾方向角（弧度）
    double maxCurvature;    // 最大曲率
    double avgCurvature;
    double width;           // 在方向上的投影宽度
    double height;          // 垂直方向投影高度
};

// 计算子笔画特征
SubStrokeFeatures computeFeatures(const vector<hanzi::Point>& sub) {
    SubStrokeFeatures f;
    size_t n = sub.size();
    if (n < 2) return f;

    // totalLen
    f.totalLen = 0.0;
    for (size_t i = 1; i < n; ++i) {
        double dx = sub[i].x - sub[i-1].x;
        double dy = sub[i].y - sub[i-1].y;
        f.totalLen += sqrt(dx*dx + dy*dy);
    }

    // chordLen
    double dx = sub.back().x - sub.front().x;
    double dy = sub.back().y - sub.front().y;
    f.chordLen = sqrt(dx*dx + dy*dy);

    // bend
    f.bend = (f.chordLen > 1e-9) ? f.totalLen / f.chordLen : 1.0;

    // dirAngle
    f.dirAngle = atan2(dy, dx);

    // curvature
    f.maxCurvature = 0.0;
    f.avgCurvature = 0.0;
    double sumCurv = 0.0;
    int count = 0;
    for (size_t i = 1; i + 1 < n; ++i) {
        // simple curvature approximation
        double dx1 = sub[i].x - sub[i-1].x;
        double dy1 = sub[i].y - sub[i-1].y;
        double dx2 = sub[i+1].x - sub[i].x;
        double dy2 = sub[i+1].y - sub[i].y;
        double len1 = sqrt(dx1*dx1 + dy1*dy1);
        double len2 = sqrt(dx2*dx2 + dy2*dy2);
        if (len1 > 1e-9 && len2 > 1e-9) {
            double cross = dx1*dy2 - dy1*dx2;
            double curv = fabs(cross) / (len1 * len2);
            f.maxCurvature = max(f.maxCurvature, curv);
            sumCurv += curv;
            count++;
        }
    }
    if (count > 0) f.avgCurvature = sumCurv / count;

    // width and height
    double minProj = 1e18, maxProj = -1e18;
    double minVert = 1e18, maxVert = -1e18;
    double cosA = cos(f.dirAngle);
    double sinA = sin(f.dirAngle);
    for (const auto& p : sub) {
        double proj = (p.x - sub.front().x) * cosA + (p.y - sub.front().y) * sinA;
        double vert = -(p.x - sub.front().x) * sinA + (p.y - sub.front().y) * cosA;
        minProj = min(minProj, proj);
        maxProj = max(maxProj, proj);
        minVert = min(minVert, vert);
        maxVert = max(maxVert, vert);
    }
    f.width = maxProj - minProj;
    f.height = maxVert - minVert;

    return f;
}

// 在笔画末端延长若干点
void hanzi::extendStrokeEnd(Bi_Hua& stroke, double length, int numPoints) {
    if (stroke.p.size() < 2 || length <= 0 || numPoints < 1) return;
    //if (stroke.types.size() < 2) return;
    // 用值拷贝，避免push_back后引用失效
    auto last = stroke.p.back();
    const auto& prev = stroke.p[stroke.p.size() - 2];
    double dx = last.x - prev.x;
    double dy = last.y - prev.y;
    double dz = last.z - prev.z;
    double norm = sqrt(dx*dx + dy*dy);
    if (norm < 1e-8) return;
    double ux = dx / norm;
    double uy = dy / norm;
    double step = length / numPoints;
    for (int i = 1; i <= numPoints; ++i) {
        last.x += ux * step;
        last.y += uy * step;
        stroke.p.push_back({last.x, last.y, last.z});
        //COUT<<"added"<< "("<<last.x<<","<<last.y<<","<<last.z<<")\n";
    }
}

hanzi::hanzi(int option) : option_(option) {
    // 读取平滑滤波配置
    try {
        ifstream cfgF("./config/smooth_config.json");
        if(cfgF.is_open()){
            //COUT<<"OK"<<endl;
            json cfg;
            cfgF >> cfg;

        // minDistSq: 点间最小距离的平方，用于去除重复点（距离小于此值则合并），默认0.25，减小可保留更多细节但增加点数
        if(cfg.contains("minDistSq")) smoothCfg.minDistSq = cfg["minDistSq"].get<double>();

        // passes: 平滑滤波的迭代次数，默认2，增加可更平滑但可能丢失细节
        if(cfg.contains("passes")) smoothCfg.passes = cfg["passes"].get<int>();

        // colinearThreshold: 共线阈值，用于去除近似直线上的点，默认0.007，减小可保留更多弯曲点
        if(cfg.contains("colinearThreshold")) smoothCfg.colinearThreshold = cfg["colinearThreshold"].get<double>();

        // maxSegmentLenSq: 最大段长度的平方，用于控制共线检测的段长，默认9.0，增加可减少去除的点
        if(cfg.contains("maxSegmentLenSq")) smoothCfg.maxSegmentLenSq = cfg["maxSegmentLenSq"].get<double>();

        // zProfileType: Z轴压力曲线类型，"sin"或"bezier"，默认"bezier"，影响笔压变化的形状
        if(cfg.contains("zProfileType")) smoothCfg.zProfileType = cfg["zProfileType"].get<string>();

        // zMin: Z轴最小值（笔压最小，0表示抬笔），默认0.0
        if(cfg.contains("zMin")) smoothCfg.zMin = cfg["zMin"].get<double>();

        // zMax: Z轴最大值（笔压最大，1表示落笔），默认1.0
        if(cfg.contains("zMax")) smoothCfg.zMax = cfg["zMax"].get<double>();

        // zCtrl1: Bezier曲线控制点1（仅用于bezier类型），默认1.0，调整曲线形状
        if(cfg.contains("zCtrl1")) smoothCfg.zCtrl1 = cfg["zCtrl1"].get<double>();

        // zCtrl2: Bezier曲线控制点2（仅用于bezier类型），默认1.0，调整曲线形状
        if(cfg.contains("zCtrl2")) smoothCfg.zCtrl2 = cfg["zCtrl2"].get<double>();

        // backRatio: 回笔长度比例（相对于笔画总长），默认0.12，增加可延长回笔距离
        if(cfg.contains("backRatio")) smoothCfg.backRatio = cfg["backRatio"].get<double>();

        // backMinLen: 最小回笔长度（mm），默认1.0，确保回笔不小于此值
        if(cfg.contains("backMinLen")) smoothCfg.backMinLen = cfg["backMinLen"].get<double>();

        // backMaxLen: 最大回笔长度（mm），默认5.0，限制回笔不超过此值
        if(cfg.contains("backMaxLen")) smoothCfg.backMaxLen = cfg["backMaxLen"].get<double>();

        // fadeInPointCount: 渐入点数（0表示禁用），默认0，从笔画起点开始渐入笔压
        if(cfg.contains("fadeInPointCount")) smoothCfg.fadeInPointCount = cfg["fadeInPointCount"].get<int>();

        // fadeInLength: 渐入长度（mm，优先于点数），默认0.0，按距离渐入笔压

        }
    } catch(const std::exception& e) {
        cerr << "Warning: failed to load smooth_config.json (using defaults): " << e.what() << '\n';
    }

    // 如果option==2，预加载graphics.txt中的所有字符
    if (option_ == 2) {
        try {
            ifstream f("./hanzi_data/graphics.txt");
            if (f.is_open()) {
                string line;
                while (getline(f, line)) {
                    if (line.empty()) continue;
                    json j = json::parse(line);
                    if (j.contains("character") && j.contains("medians")) {
                        string ch = j["character"].get<string>();
                        cout<<ch<<" ";
                        HanZi h;
                        for (const auto& stroke : j["medians"]) {
                            Bi_Hua s;
                            for (const auto& point : stroke) {
                                Point pt{point[0], point[1], 0.0};
                                s.p.push_back(pt);
                            }
                            // 处理笔画，与loadCharData一致
                            inferStrokeType(s);
                            addPoint(s);
                            updateSubStrokeIndices(s);
                            //smooth(s);
                            extendStrokeEnd(s, 100.0, s.p.size()/8);
                            applyZProfile(s);
                            addBackstroke(s);
                            personalizeStroke(s);
                            h.bi_hua_.push_back(s);
                        }
                        mp[ch] = std::move(h);
                    }
                }
            }
        } catch (const std::exception& e) {
            cerr << "Warning: failed to load graphics.txt: " << e.what() << endl;
        }
    }

    return;
};

bool hanzi::loadCharData(const string& ch){
    if(mp.find(ch) != mp.end()) return true;

    std::filesystem::path p = std::filesystem::u8path("./hanzi_data");
    p /= std::filesystem::u8path(ch);
    p += u8".json";

    if(!std::filesystem::exists(p)) return false;

    std::ifstream f(p);
    if(!f.is_open()) return false;

    json j;
    try{
        f >> j;
    } catch(const std::exception&) {
        return false;
    }

    if(!j.contains("medians")) return false;

    HanZi h;
    COUT << ch << ":\n";

    ofstream out_;
    out_.open("point.csv",ios::app|ios::out);
    
    for(const auto& stroke : j["medians"]){
        Bi_Hua s;
        for(const auto& point : stroke){
            Point pt{point[0], point[1], 0.0};
            s.p.push_back(pt);
            
            // COUT<<pt.x<<","<<pt.y<<","<<pt.z<<endl;
            // out_<<pt.x<<","<<pt.y<<","<<pt.z<<endl;
        }
        inferStrokeType(s);
        addPoint(s);
        updateSubStrokeIndices(s);
        //COUT<<s.type<<endl;
        extendStrokeEnd(s, 100.0, s.p.size()/8);
        applyZProfile(s);
        addBackstroke(s);
        // for(auto& pt:s.p){
        //     //COUT<<pt.x<<","<<pt.y<<","<<pt.z<<endl;
        //     out_<<pt.x<<","<<pt.y<<","<<pt.z<<endl;
        // }
        personalizeStroke(s);
        //smooth(s);
        h.bi_hua_.push_back(s);
    }
    out_.close();
    mp[ch] = std::move(h);
    return true;
}

const hanzi::HanZi* hanzi::getHanZi(const string& ch){
    auto it = mp.find(ch);
    if(it != mp.end()) return &it->second;
    if(option_ == 1 && loadCharData(ch)){
        return &mp[ch];
    }
    return nullptr;
}

// 解析 UTF-8 字符串为单个 Unicode 字符（每个汉字或表情等），用于根据 name 自动判断要写多少个字。
static vector<string> splitUtf8Chars(const string& s) {
    vector<string> out;
    size_t i = 0;
    while(i < s.size()){
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = 1;
        if((c & 0x80) == 0) {
            len = 1;
        } else if((c & 0xE0) == 0xC0) {
            len = 2;
        } else if((c & 0xF0) == 0xE0) {
            len = 3;
        } else if((c & 0xF8) == 0xF0) {
            len = 4;
        }
        if(i + len > s.size()) len = s.size() - i;
        out.emplace_back(s.substr(i, len));
        i += len;
    }
    return out;
}

void hanzi::printGCode(string name,
                double Rate,
                double x0,double y0,
                double z_up,double z_down,
                int n, int m,
                double Size,
                bool rowMajor,
                string path)
{
    ofstream gout;
    gout.open(path);
    if(!gout.is_open()){
        //COUT<<"Couldn't open the file "<<path<<endl;
        return;
    }
    gout<<fixed<<setprecision(2);
    gout<<"G21\n";
    gout<<"G90\n";
    gout<<"G17\n";
    gout<<"G0 Z"<<z_up+10<<"\n\n";

    auto chars = splitUtf8Chars(name);
    int cols = max(1, n);
    int rows = max(1, m);
    int maxCells = cols * rows;
    int count = min<int>(chars.size(), maxCells);

    for(int idx = 0; idx < count; ++idx){
        const string &ch = chars[idx];
        const auto* hz = getHanZi(ch);
        if(!hz){
            cerr<<"Warning: character '"<<ch<<"' not found in data, skipping\n";
            continue;
        }

        int row, col;
        if(rowMajor){
            row = idx / cols;
            col = idx % cols;
        } else {
            col = idx / rows;
            row = idx % rows;
        }
        row=m-row;//y坐标转化，上下字对换
        double xOrigin = x0 + col * Size;
        double yOrigin = y0 + row * Size;

        // 计算当前字符的边界，用于根据 Size 自动缩放到合适大小
        double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
        for(const auto& bh: hz->bi_hua_){
            for(const auto& p : bh.p){
                minX = min(minX, p.x);
                maxX = max(maxX, p.x);
                minY = min(minY, p.y);
                maxY = max(maxY, p.y);
            }
        }
        double glyphW = maxX - minX;
        double glyphH = maxY - minY;//默认就是1000
        double glyphSize = max(glyphW, glyphH);
        double scaleForChar = 1.0;
        if(glyphSize > 1e-6){
            scaleForChar = Size / glyphSize;
        }

        double xOffset = -minX * scaleForChar;
        double yOffset = -minY * scaleForChar;

        // for(const auto& bh: hz->bi_hua_){
        //     const auto& firstPoint = bh.p.front();
        //     double x = xOrigin + (firstPoint.x * scaleForChar + xOffset);
        //     double y = yOrigin + (firstPoint.y * scaleForChar + yOffset);
        //     double z = z_up + (z_down - z_up) * firstPoint.z;
        //     gout<<"G0 X"<<x<<" Y"<<y<<"\n";
        //     gout<<"G1 X"<<x<<" Y"<<y<<" Z"<<z<<" F"<<Rate<<"\n";
        //     for(int i=1;i<bh.p.size();i++){
        //         x = xOrigin + (bh.p[i].x * scaleForChar + xOffset);
        //         y = yOrigin + (bh.p[i].y * scaleForChar + yOffset);
        //         z = z_up + (z_down - z_up) * bh.p[i].z;
        //         gout<<"G1 X"<<x<<" Y"<<y<<" Z"<<z<<"\n";
        //     }
        //     gout<<"G0 Z"<<z_up<<"\n";
        //     gout<<"G0 Z"<<z_up+30<<"\n\n";
        // }

        for (auto bh : hz->bi_hua_) { // 注意这里要用auto bh副本，避免影响原始数据
            const auto& pts = bh.p;
            if (pts.empty()) continue;

            vector<Point> absPoints;
            for (size_t i = 0; i < pts.size(); ++i) {
                double x = xOrigin + (pts[i].x * scaleForChar + xOffset);
                double y = yOrigin + (pts[i].y * scaleForChar + yOffset);
                double z = z_up + (z_down - z_up) * pts[i].z;
                absPoints.push_back({x, y, z});
            }

            // ---- 步骤2：渐入处理（覆盖前几个点的 Z） ----
            int fadeCount = smoothCfg.fadeInPointCount;
            //COUT<<fadeCount<<endl;
            if (smoothCfg.fadeInLength > 0.0) {
                // 按长度计算渐入点数
                double cumLen = 0.0;
                fadeCount = 0;
                for (size_t i = 1; i < absPoints.size() && cumLen < smoothCfg.fadeInLength; ++i) {
                    double dx = absPoints[i].x - absPoints[i-1].x;
                    double dy = absPoints[i].y - absPoints[i-1].y;
                    cumLen += sqrt(dx*dx + dy*dy);
                    fadeCount = i;
                }
                if (cumLen < smoothCfg.fadeInLength && absPoints.size() > 0) fadeCount = absPoints.size() - 1;
            }

            else if (fadeCount > 0) {
                        //COUT<<"changed"<<endl;
                int endIdx = min(fadeCount, (int)absPoints.size() - 1);
                // 计算前 endIdx 个点的累积距离（物理距离）
                vector<double> dist(endIdx + 1, 0.0);
                for (int i = 1; i <= endIdx; ++i) {
                    double dx = absPoints[i].x - absPoints[i-1].x;
                    double dy = absPoints[i].y - absPoints[i-1].y;
                    dist[i] = dist[i-1] + sqrt(dx*dx + dy*dy);
                }
                double totalDist = dist[endIdx];
                if (totalDist > 1e-6) {
                    for (int i = 0; i <= endIdx; ++i) {
                        double t = dist[i] / totalDist;
                        double newZ = z_up + (absPoints[i].z - z_up) * t;
                        absPoints[i].z = newZ;
                    }
                }
            }

            // ---- 步骤3：将修改后的 absPoints 放入 drawPoints ----
            vector<tuple<double, double, double>> drawPoints;
            for (const auto& p : absPoints) {
                drawPoints.emplace_back(p.x, p.y, p.z);
            }

            // ---- 输出当前笔画（含回笔）的 G 代码 ----
            if (drawPoints.empty()) continue;

            auto [x0, y0, z0] = drawPoints[0];
            gout << "G0 X" << x0 << " Y" << y0 << "\n";
            gout << "G1 X" << x0 << " Y" << y0 << " Z" << z0 << " F" << Rate << "\n";

            for (size_t i = 1; i < drawPoints.size(); ++i) {
                auto [x, y, z] = drawPoints[i];
                gout << "G1 X" << x << " Y" << y << " Z" << z << "\n";
            }

            gout << "G0 Z" << z_up << "\n";
            gout << "G0 Z" << z_up + 30 << "\n\n";
        }
    }
    gout<<"G0 Z"<<z_up<<"\n";
    gout<<"M30\n";
    gout.close();
    //COUT<<"Generated "<<name<<" ("<<count<<" chars) at "<<path<<" which begin at "<<"("<<x0<<","<<y0<<")"<<endl;
    return;
}

void hanzi::printAllWord(){
    freopen("out.txt","w",stdout);
    namespace fs = std::filesystem;

    const fs::path dataDir = fs::u8path("./hanzi_data");
    if(!fs::exists(dataDir) || !fs::is_directory(dataDir)){
        cerr<<"hanzi_data directory not found."<<endl;
        fclose(stdout);
        return;
    }

    for(const auto& entry : fs::directory_iterator(dataDir)){
        if(entry.is_regular_file() && entry.path().extension() == ".json"){
            const auto filename = entry.path().stem().u8string();
            //COUT<<filename<<":\n";
            try{
                json j;
                std::ifstream f(entry.path());
                f>>j;
                const auto& medians = j["medians"];
                for(const auto& stroke : medians){
                    for(const auto& point : stroke){
                        int x = point[0];
                        int y = point[1];
                        //COUT<<"("<<x<<","<<y<<") ";
                    }
                    //COUT<<endl;
                }
            } catch(const std::exception&){
                // ignore malformed files
            }
            //COUT<<endl;
        }
    }
    fclose(stdout);
}

void hanzi::printSingleWord(string name){
    const auto* hz = getHanZi(name);
    if(!hz){
        cerr<<"Character '"<<name<<"' not found in data."<<endl;
        return;
    }

    ofstream out_;
    out_.open("point.csv");
    out_<<"x,y\n";
    for(const auto& bh : hz->bi_hua_){
        for(const auto& p : bh.p){
            out_<<p.x<<","<<p.y<<","<<p.z<<endl;
        }
    }
    out_.close();
}

void hanzi::addPoint(Bi_Hua& stroke,double role){
    const auto& points=stroke.p;
    vector<Point>newP;
    if(points.size()<2)return;
    int n=points.size();
    auto F=[](const Point& p0,const Point& p1,const Point& p2,const Point& p3,double t){
        double t2=t*t;
        double t3=t2*t;
        double x = 0.5 * ((2 * p1.x) +
                          (-p0.x + p2.x) * t +
                          (2*p0.x - 5*p1.x + 4*p2.x - p3.x) * t2 +
                          (-p0.x + 3*p1.x - 3*p2.x + p3.x) * t3);
        double y = 0.5 * ((2 * p1.y) +
                          (-p0.y + p2.y) * t +
                          (2*p0.y - 5*p1.y + 4*p2.y - p3.y) * t2 +
                          (-p0.y + 3*p1.y - 3*p2.y + p3.y) * t3);
        double z = 0.5 * ((2 * p1.z) +
                          (-p0.z + p2.z) * t +
                          (2*p0.z - 5*p1.z + 4*p2.z - p3.z) * t2 +
                          (-p0.z + 3*p1.z - 3*p2.z + p3.z) * t3);
        return Point{x,y,z};
    };
    for(int i=0;i<n-1;i++){
        double dx=points[i+1].x-points[i].x;
        double dy=points[i+1].y-points[i].y;
        double len=sqrt(dx*dx+dy*dy);
        int nums=int(len/role)-1;
        if(nums>0){
            Point p0=(i==0)?points[i]:points[i-1];
            Point p1=points[i];
            Point p2=points[i+1];
            Point p3=(i+2<n)?points[i+2]:points[i+1];
            double step=1.0/(nums+1);
            for(double t=0.0;t<1.0;t+=step){
                newP.push_back(F(p0,p1,p2,p3,t));
            }
        }
    }
    newP.push_back(points.back());
    stroke.p=move(newP);
    return;
}

void hanzi::applyZProfile(Bi_Hua& stroke) {
    auto& pts = stroke.p;
    if (pts.size() < 2) return;

    // ---------- 1. 计算弧长和归一化参数 t ----------
    vector<double> dist(pts.size(), 0.0);
    for (size_t i = 1; i < pts.size(); ++i) {
        double dx = pts[i].x - pts[i - 1].x;
        double dy = pts[i].y - pts[i - 1].y;
        dist[i] = dist[i - 1] + sqrt(dx * dx + dy * dy);
    }
    double totalLen = dist.back();
    if (totalLen < 1e-9) {
        for (auto& p : pts) p.z = smoothCfg.zMin;
        return;
    }

    // ---------- 2. 计算每个点的方向角和角度变化 ----------
    vector<double> theta(pts.size(), 0.0);
    for (size_t i = 1; i < pts.size(); ++i) {
        double dx = pts[i].x - pts[i - 1].x;
        double dy = pts[i].y - pts[i - 1].y;
        theta[i] = atan2(dy, dx);
    }
    // 首点方向用第一段方向代替
    theta[0] = theta[1];

    vector<double> angleChange(pts.size(), 0.0);
    for (size_t i = 1; i < pts.size() - 1; ++i) {
        double delta = theta[i + 1] - theta[i];
        // 归一化到 [-π, π]
        delta = fmod(delta + M_PI, 2 * M_PI) - M_PI;
        angleChange[i] = fabs(delta);
    }

    // ---------- 3. 识别整体方向（首尾点方向） ----------
    double dxTotal = pts.back().x - pts.front().x;
    double dyTotal = pts.back().y - pts.front().y;
    double thetaTotal = atan2(dyTotal, dxTotal);

    // ---------- 4. 定义基础曲线形状（基于整体方向） ----------
    auto baseCurve = [&](double t) -> double {
        // t 从 0 到 1
        const double eps = 1e-6;
        if (totalLen < 20.0) {   // 短笔画（如点）
            // 中间重，两端轻：使用 sin(π * t) 曲线
            return sin(M_PI * t);
        }

        // 根据整体方向选择曲线形态
        double absTheta = fabs(thetaTotal);
        // 横 / 竖（两端重，中间轻）：1 - sin(π * t) 再调整范围
        if (absTheta < M_PI / 6 || fabs(M_PI - absTheta) < M_PI / 6) {
            // 横或提
            double val = 1.0 - sin(M_PI * t);
            // 映射到 [0.2, 0.9] 范围，避免笔尖完全离纸
            return 0.2 + 0.7 * val;
        }
        if (fabs(absTheta - M_PI / 2) < M_PI / 6) {
            // 竖
            double val = 1.0 - sin(M_PI * t);
            return 0.2 + 0.7 * val;
        }
        if (thetaTotal > M_PI / 2 && thetaTotal < M_PI) {
            // 撇（中间重）
            double val = sin(M_PI * t);
            return 0.2 + 0.8 * val;
        }
        if (thetaTotal > -M_PI / 2 && thetaTotal < 0) {
            // 捺（中间重）
            double val = sin(M_PI * t);
            return 0.2 + 0.8 * val;
        }
        // 默认：中间重
        double val = sin(M_PI * t);
        return 0.2 + 0.8 * val;
    };

    // ---------- 5. 生成基础 Z 值 ----------
    vector<double> zVals(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) {
        double t = dist[i] / totalLen;
        zVals[i] = baseCurve(t);
    }

    // ---------- 6. 转折点增强 ----------
    const double ANGLE_THRESH = 0.8;      // 弧度，约 45°
    const int ENHANCE_RADIUS = 2;         // 影响半径（点数）
    const double ENHANCE_AMOUNT = 0.3;    // 增强量（归一化值，0~1）
    for (size_t i = 1; i + 1 < pts.size(); ++i) {
        if (angleChange[i] > ANGLE_THRESH) {
            int start = max(0, (int)i - ENHANCE_RADIUS);
            int end = min((int)pts.size() - 1, (int)i + ENHANCE_RADIUS);
            for (int j = start; j <= end; ++j) {
                double factor = 1.0 - fabs(j - (int)i) / ENHANCE_RADIUS;
                zVals[j] += ENHANCE_AMOUNT * factor;
                zVals[j] = max(0.0, min(1.0, zVals[j]));
            }
        }
    }

    // ---------- 7. 末端钩增强 ----------
    if (pts.size() >= 3) {
        int last = pts.size() - 1;
        double dxLast = pts[last].x - pts[last - 1].x;
        double dyLast = pts[last].y - pts[last - 1].y;
        double lenLast = sqrt(dxLast * dxLast + dyLast * dyLast);
        if (lenLast < totalLen / 5.0) {
            // 最后一段较短，检查方向变化
            double dxPrev = pts[last - 1].x - pts[last - 2].x;
            double dyPrev = pts[last - 1].y - pts[last - 2].y;
            double thetaPrev = atan2(dyPrev, dxPrev);
            double thetaLast = atan2(dyLast, dxLast);
            double delta = fmod(thetaLast - thetaPrev + M_PI, 2 * M_PI) - M_PI;
            if (fabs(delta) > 60.0 * M_PI / 180.0) {
                // 钩：末端增加下压
                int start = max(0, last - 3);
                for (int j = start; j <= last; ++j) {
                    double factor = 1.0 - (last - j) / 3.0;
                    zVals[j] += 0.3 * factor;
                    zVals[j] = min(1.0, zVals[j]);
                }
            }
        }
    }

    // ---------- 8. 写回点集，并映射到 [zMin, zMax] ----------
    for (size_t i = 0; i < pts.size(); ++i) {
        double zNorm = max(0.0, min(1.0, zVals[i]));
        pts[i].z = smoothCfg.zMin + (smoothCfg.zMax - smoothCfg.zMin) * zNorm;
    }
}

void hanzi::inferStrokeType(hanzi::Bi_Hua& stroke){
    if(stroke.p.size() < 2) {
        SubStroke base;
        base.type = "o";
        base.startIndex = 0;
        base.endIndex = stroke.p.empty() ? 0 : stroke.p.size() - 1;
        base.startRatio = 0.0;
        base.endRatio = 1.0;
        base.dir = {0.0, 0.0, 0.0};
        stroke.subStrokes = {base};
        return;
    }
    stroke.subStrokes.clear();
    const auto& pts = stroke.p;
    size_t n = pts.size();

    // 计算总长度
    double totalLen = 0.0;
    for (size_t i = 1; i < n; ++i) {
        double dx = pts[i].x - pts[i - 1].x;
        double dy = pts[i].y - pts[i - 1].y;
        totalLen += sqrt(dx * dx + dy * dy);
    }

    vector<double> dist(n, 0.0);
    for (size_t i = 1; i < n; ++i) {
        double dx = pts[i].x - pts[i - 1].x;
        double dy = pts[i].y - pts[i - 1].y;
        dist[i] = dist[i - 1] + sqrt(dx * dx + dy * dy);
    }

    // 计算方向角
    vector<double> theta(n, 0.0);
    for (size_t i = 1; i < n; ++i) {
        double dx = pts[i].x - pts[i - 1].x;
        double dy = pts[i].y - pts[i - 1].y;
        theta[i] = atan2(dy, dx);
    }
    theta[0] = theta[1]; // 首点用第一段

    // 平滑角度序列 (简单移动平均)
    vector<double> smoothTheta = theta;
    for (size_t i = 1; i + 1 < n; ++i) {
        smoothTheta[i] = (theta[i-1] + theta[i] + theta[i+1]) / 3.0;
    }

    // 基于累积角度变化的分段
    vector<size_t> keyPoints = {0};
    double cumAngle = 0.0;
    const double ANGLE_THRESH = 0.8; // 累积阈值
    for (size_t i = 1; i < n; ++i) {
        double delta = fmod(smoothTheta[i] - smoothTheta[i-1] + M_PI, 2*M_PI) - M_PI;
        cumAngle += fabs(delta);
        if (cumAngle > ANGLE_THRESH) {
            keyPoints.push_back(i);
            cumAngle = 0.0;
        }
    }
    keyPoints.push_back(n - 1);

    // 输出关键点
    // COUT << "Key points: ";
    // for (auto kp : keyPoints) 
    //     COUT << "(" << stroke.p[kp].x << "," << stroke.p[kp].y << ")\n";
    // COUT << endl;

    // 分割成子笔画并判断类型
    vector<SubStroke> subStrokes;
    for (size_t k = 0; k + 1 < keyPoints.size(); ++k) {
        size_t start = keyPoints[k];
        size_t end = keyPoints[k + 1];

        vector<Point> subPts(pts.begin() + start, pts.begin() + end + 1);
        SubStrokeFeatures f = computeFeatures(subPts);

        if (end - start < 2 && f.totalLen < 50.0) continue;

        string subType = "o";
        if (f.totalLen < 10.0) {
            subType = "d"; // 点
        } else if (f.bend > 1.3 && f.maxCurvature > 0.05) {
            // 钩：高弯曲度和曲率
            subType = "g";
        } else {
            double absDir = fabs(f.dirAngle);
            if (absDir < M_PI / 6) {
                subType = "h"; // 横
            } else if (fabs(absDir - M_PI / 2) < M_PI / 6 && f.dirAngle < 0) {
                subType = "sh"; // 竖（向下写）
            } else if (f.dirAngle > -M_PI  && f.dirAngle < -M_PI / 2) {
                subType = "p"; // 撇
            } else if (f.dirAngle > -M_PI / 2 && f.dirAngle < 0) {
                subType = "n"; // 捺
            } else if (f.dirAngle > -M_PI / 2  && f.bend < 1.2) {
                subType = "t"; // 提
            } else if (fabs(absDir - M_PI / 2) < M_PI / 6 && f.dirAngle > 0) {
                subType = "g"; // 钩（向上写）
            }
        }
        // 如果子笔画长度小于总长度的5%，忽略，除非是末尾的钩
        bool isLastSubstroke = (k + 1 == keyPoints.size() - 1);
        if (f.totalLen < totalLen * 0.05 && !(isLastSubstroke && subType == "g")) continue;
        
        SubStroke sub;
        sub.type = subType;
        sub.startIndex = start;
        sub.endIndex = end;
        sub.startRatio = (totalLen > 1e-9) ? dist[start] / totalLen : 0.0;
        sub.endRatio = (totalLen > 1e-9) ? dist[end] / totalLen : 1.0;
        double dx = subPts.back().x - subPts.front().x;
        double dy = subPts.back().y - subPts.front().y;
        double mag = sqrt(dx*dx + dy*dy);
        if (mag < 1e-9) {
            sub.dir = {0.0, 0.0, 0.0};
        } else {
            sub.dir = {dx / mag, dy / mag, 0.0};
        }
        subStrokes.push_back(sub);
    }

    // 合并相邻相同元笔画
    vector<SubStroke> mergedSubStrokes;
    if (!subStrokes.empty()) {
        mergedSubStrokes.push_back(subStrokes[0]);
        for (size_t i = 1; i < subStrokes.size(); ++i) {
            if (subStrokes[i].type != mergedSubStrokes.back().type) {
                mergedSubStrokes.push_back(subStrokes[i]);
            } else {
                // 合并：更新 endIndex 和 dir（平均）
                mergedSubStrokes.back().endIndex = subStrokes[i].endIndex;
                double dx = (mergedSubStrokes.back().dir.x + subStrokes[i].dir.x) / 2;
                double dy = (mergedSubStrokes.back().dir.y + subStrokes[i].dir.y) / 2;
                double mag = sqrt(dx*dx + dy*dy);
                if (mag > 1e-9) {
                    mergedSubStrokes.back().dir = {dx/mag, dy/mag, 0.0};
                }
            }
        }
    }

    stroke.subStrokes = std::move(mergedSubStrokes);

    // 返回子笔画类型向量
    COUT << "Result: ";
    for (size_t i = 0; i < stroke.subStrokes.size(); ++i) {
        if (i > 0) COUT << "-";
        COUT << stroke.subStrokes[i].type;
    }
    COUT << endl;
}

void hanzi::smooth(Bi_Hua& stroke){
    // Smooth the stroke points to remove jitter/rough points at the stroke edges.
    // This does a light low-pass filter (moving average) and removes near-duplicate points.
    auto& pts = stroke.p;
    if(pts.size() < 3) return;

    // 1) Remove near-duplicate points to avoid excessive jitter.
    vector<Point> filtered;
    filtered.reserve(pts.size());
    filtered.push_back(pts.front());
    for(size_t i = 1; i < pts.size(); ++i){
        double dx = pts[i].x - filtered.back().x;
        double dy = pts[i].y - filtered.back().y;
        double dz = pts[i].z - filtered.back().z;
        if(dx*dx + dy*dy + dz*dz > smoothCfg.minDistSq){
            filtered.push_back(pts[i]);
        }
    }
    if(filtered.size() < 3){
        stroke.p = move(filtered);
        return;
    }

    // 2) Apply a simple weighted moving-average filter (passes times) to smooth jitter.
    for(int pass = 0; pass < smoothCfg.passes; ++pass){
        vector<Point> tmp(filtered.size());
        tmp.front() = filtered.front();
        tmp.back() = filtered.back();
        for(size_t i = 1; i + 1 < filtered.size(); ++i){
            double wsum = 0.0;
            double sx = 0.0;
            double sy = 0.0;
            double sz = 0.0;

            auto add = [&](size_t idx, double w){
                sx += filtered[idx].x * w;
                sy += filtered[idx].y * w;
                sz += filtered[idx].z * w;
                wsum += w;
            };

            // kernel weights: [1, 2, 4, 2, 1]
            if(i >= 2) add(i - 2, 1.0);
            add(i - 1, 2.0);
            add(i, 4.0);
            add(i + 1, 2.0);
            if(i + 2 < filtered.size()) add(i + 2, 1.0);

            tmp[i].x = sx / wsum;
            tmp[i].y = sy / wsum;
            tmp[i].z = sz / wsum;
        }
        filtered.swap(tmp);
    }

    // 3) Remove points that are nearly colinear to reduce tiny spikes.
    vector<Point> out;
    out.reserve(filtered.size());
    out.push_back(filtered.front());
    for(size_t i = 1; i + 1 < filtered.size(); ++i){
        const auto& A = out.back();
        const auto& B = filtered[i];
        const auto& C = filtered[i + 1];
        double ux = B.x - A.x;
        double uy = B.y - A.y;
        double vx = C.x - B.x;
        double vy = C.y - B.y;

        double cross = fabs(ux * vy - uy * vx);
        double len = sqrt((ux*ux + uy*uy) * (vx*vx + vy*vy));
        double sinTheta = (len > 1e-9) ? (cross / len) : 0.0;

        // If the point is nearly on a straight line (small angle) and spacing is small, drop it.
        if(sinTheta < smoothCfg.colinearThreshold && (ux*ux + uy*uy) < smoothCfg.maxSegmentLenSq){
            continue;
        }
        out.push_back(B);
    }
    out.push_back(filtered.back());

    stroke.p = move(out);
}

void hanzi::addBackstroke(Bi_Hua& stroke) {
    bool hasH = false, hasSH = false, lastIsHoriz = false;
    for (const auto& sub : stroke.subStrokes) {
        if (sub.type == "h") hasH = true;
        if (sub.type == "sh") hasSH = true;
    }
    if (!hasH && !hasSH) return;
    if (!stroke.subStrokes.empty() && (stroke.subStrokes.back().type == "h" || stroke.subStrokes.back().type == "sh")) {
        lastIsHoriz = true;
    }
    if (!lastIsHoriz) return;
    auto& pts = stroke.p;
    size_t n = pts.size();
    if (n < 3) return;

    // 计算原始坐标中的累积距离
    vector<double> origDist(n, 0.0);
    for (size_t i = 1; i < n; ++i) {
        double dx = pts[i].x - pts[i-1].x;
        double dy = pts[i].y - pts[i-1].y;
        origDist[i] = origDist[i-1] + sqrt(dx*dx + dy*dy);
    }
    double origTotalLen = origDist.back();

    double physTotalLen = origTotalLen; // 假设scale=1
    const double backRatio = smoothCfg.backRatio;
    const double minBackLen = smoothCfg.backMinLen;
    const double maxBackLen = smoothCfg.backMaxLen;
    double backPhysLen = physTotalLen * backRatio;
    backPhysLen = min(max(backPhysLen, minBackLen), maxBackLen);
    double backOrigLen = backPhysLen;

    int startIdx = -1;
    for (int i = n-1; i >= 0; --i) {
        if (origTotalLen - origDist[i] >= backOrigLen) {
            startIdx = i;
            break;
        }
    }
    if (startIdx < 0) startIdx = 0;
    if (startIdx >= n-1) startIdx = n-2;

    // 收集逆序回笔点（不含原末尾点）
    vector<Point> backPts;
    for (int i = n-2; i >= startIdx; --i) {
        backPts.push_back(pts[i]);
    }

    if (!backPts.empty()) {
        // 设置backPts的z为0.0
        for (auto& pt : backPts) {
            pt.z = 0.0;
        }
        // 添加到stroke.p
        pts.insert(pts.end(), backPts.begin(), backPts.end());
    }
}

void hanzi::updateSubStrokeIndices(Bi_Hua& stroke) {
    if (stroke.p.empty() || stroke.subStrokes.empty()) return;

    // 计算当前密集点集的累积距离
    vector<double> dist(stroke.p.size(), 0.0);
    for (size_t i = 1; i < stroke.p.size(); ++i) {
        double dx = stroke.p[i].x - stroke.p[i-1].x;
        double dy = stroke.p[i].y - stroke.p[i-1].y;
        dist[i] = dist[i-1] + sqrt(dx*dx + dy*dy);
    }
    double totalLen = dist.back();
    if (totalLen < 1e-9) return;

    // 用比例定位最近的索引
    for (auto& sub : stroke.subStrokes) {
        double targetStart = sub.startRatio * totalLen;
        double targetEnd   = sub.endRatio   * totalLen;

        // 二分或线性查找起始索引
        size_t startIdx = 0;
        while (startIdx < dist.size() && dist[startIdx] < targetStart) ++startIdx;
        if (startIdx > 0 && fabs(dist[startIdx] - targetStart) > fabs(dist[startIdx-1] - targetStart))
            startIdx = startIdx - 1;
        sub.startIndex = min(startIdx, stroke.p.size() - 1);

        // 结束索引
        size_t endIdx = 0;
        while (endIdx < dist.size() && dist[endIdx] < targetEnd) ++endIdx;
        if (endIdx > 0 && fabs(dist[endIdx] - targetEnd) > fabs(dist[endIdx-1] - targetEnd))
            endIdx = endIdx - 1;
        sub.endIndex = min(endIdx, stroke.p.size() - 1);
    }
}

void hanzi::personalizeStroke(Bi_Hua& stroke) {
    if (stroke.p.size() < 2 || stroke.subStrokes.empty()) return;

    const SubStroke& lastSub = stroke.subStrokes.back();
    //钩提撇捺的笔锋
    if ((lastSub.type == "g" || lastSub.type == "t" || lastSub.type == "p" || lastSub.type == "n")) {
        vector<double> dist(stroke.p.size(), 0.0);
        for (size_t i = 1; i < stroke.p.size(); ++i) {
            double dx = stroke.p[i].x - stroke.p[i-1].x;
            double dy = stroke.p[i].y - stroke.p[i-1].y;
            dist[i] = dist[i-1] + sqrt(dx*dx + dy*dy);
        }
        double totalLen = dist.back();
        if (totalLen < 1e-9) return;

        auto indexByRatio = [&](double ratio) {
            if (ratio <= 0.0) return size_t(0);
            if (ratio >= 1.0) return stroke.p.size() - 1;
            double target = ratio * totalLen;
            size_t idx = 0;
            while (idx + 1 < dist.size() && dist[idx + 1] < target) {
                ++idx;
            }
            return idx;
        };

        size_t startIdx = indexByRatio(lastSub.startRatio);
        size_t endIdx = indexByRatio(lastSub.endRatio);
        if (endIdx >= stroke.p.size()) endIdx = stroke.p.size() - 1;
        if (endIdx <= startIdx) return;

        double segLen = dist[endIdx] - dist[startIdx];
        if (segLen < 1e-6) return;

        double startZ = stroke.p[startIdx].z;
        for (size_t i = startIdx; i <= endIdx; ++i) {
            double t = (dist[i] - dist[startIdx]) / segLen;
            if (t < 0.0) t = 0.0;
            if (t > 1.0) t = 1.0;
            // 使用 sin 函数从 startZ 覆盖到 zMin (Z_up)
            double sinVal = sin(M_PI * 0.5 * t);
            double zNorm = (sinVal + 1.0) / 2.0; // 从 0 到 1
            stroke.p[i].z = startZ + (smoothCfg.zMin - startZ) * zNorm;
        }
    }
    
    //h-sh结构的折
    {
        for (size_t i = 0; i + 1 < stroke.subStrokes.size(); ++i) {
            const auto& sub1 = stroke.subStrokes[i];
            const auto& sub2 = stroke.subStrokes[i + 1];
            if (sub1.type != "h" || sub2.type != "sh") continue;
            if (sub1.endIndex >= stroke.p.size() || sub2.startIndex >= stroke.p.size()) continue;

            size_t hEnd = sub1.endIndex;
            size_t shStart = sub2.startIndex;

            Point hDir = sub1.dir;
            if (fabs(hDir.x) < 1e-9 && fabs(hDir.y) < 1e-9) continue;

            // ---------- 可调参数 ----------
            size_t extendCount = 3;               // 延伸点数
            double stepScale = 1.05;               // 步长放大系数
            double pauseDepth = smoothCfg.zMax;   // 按笔深度
            size_t transCount = extendCount + 2;  // 过渡点数
            double stayRatio = 0.6;               // 保持重压比例
            // -----------------------------

            // 计算步长（参考横末端两点间距）
            double step = 0.0;
            if (hEnd > 0) {
                double dx = stroke.p[hEnd].x - stroke.p[hEnd - 1].x;
                double dy = stroke.p[hEnd].y - stroke.p[hEnd - 1].y;
                step = sqrt(dx*dx + dy*dy);
            }
            if (step < 0.5) step = 1.0;
            step *= stepScale;

            Point lastHPt = stroke.p[hEnd];

            // 1. 插入停顿点（同一位置加重）
            Point pausePt = lastHPt;
            pausePt.z = pauseDepth;
            stroke.p.insert(stroke.p.begin() + hEnd + 1, pausePt);
            size_t offset = 1;

            // 2. 插入延伸点
            vector<Point> extendedPts;
            for (size_t k = 1; k <= extendCount; ++k) {
                Point newPt = lastHPt;
                newPt.x += hDir.x * step * k;
                newPt.y += hDir.y * step * k;
                newPt.z = pauseDepth;
                extendedPts.push_back(newPt);
            }
            stroke.p.insert(stroke.p.begin() + hEnd + 1 + offset, extendedPts.begin(), extendedPts.end());
            offset += extendedPts.size();

            // 更新竖起点索引（因前面插入了点）
            shStart += offset;

            // 3. 在竖起点前插入过渡点
            if (shStart < stroke.p.size() && extendCount > 0) {
                Point shDir = sub2.dir;
                if (fabs(shDir.x) > 1e-9 || fabs(shDir.y) > 1e-9) {
                    Point transStart = extendedPts.empty() ? pausePt : extendedPts.back();
                    Point transEnd = stroke.p[shStart];

                    vector<Point> transPts;
                    for (size_t k = 1; k <= transCount; ++k) {
                        double t = k / double(transCount + 1);
                        Point interp;
                        interp.x = transStart.x + (transEnd.x - transStart.x) * t;
                        interp.y = transStart.y + (transEnd.y - transStart.y) * t;

                        double t2 = (t < stayRatio) ? 0.0 : (t - stayRatio) / (1.0 - stayRatio);
                        interp.z = transStart.z + (transEnd.z - transStart.z) * t2;

                        transPts.push_back(interp);
                    }
                    stroke.p.insert(stroke.p.begin() + shStart, transPts.begin(), transPts.end());
                }
            }
        }

    }
    return;
}

void hanzi::addLiftForTipOrHook(Bi_Hua& stroke) {
    auto& pts = stroke.p;
    size_t n = pts.size();
    if (n < 3) return;

    // 计算累积距离
    vector<double> dist(n, 0.0);
    for (size_t i = 1; i < n; ++i) {
        double dx = pts[i].x - pts[i-1].x;
        double dy = pts[i].y - pts[i-1].y;
        dist[i] = dist[i-1] + sqrt(dx*dx + dy*dy);
    }
    double totalLen = dist.back();

    // 后三分之一的长度
    double liftLen = totalLen / 3.0;

    // 找到起始点：距离末尾 >= liftLen 的点
    size_t startIdx = n - 1;
    for (size_t i = 0; i < n; ++i) {
        if (totalLen - dist[i] >= liftLen) {
            startIdx = i;
            break;
        }
    }

    // 对 startIdx 到 n-1 的点，设置 z 从 1 到 0，使用 sin 曲线
    for (size_t i = startIdx; i < n; ++i) {
        double localDist = totalLen - dist[i]; // 从末尾开始的距离
        double t = (liftLen - localDist) / liftLen; // 0 到 1
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        // sin 曲线：从 1 到 0
        double zNorm = 1.0 - sin(M_PI * t / 2.0) * 2.0 ;
        pts[i].z = zNorm;
    }
}