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

using namespace std;
using json=nlohmann::json;

// 在笔画末端延长若干点
void hanzi::extendStrokeEnd(Bi_Hua& stroke, double length, int numPoints) {
    if (stroke.p.size() < 2 || length <= 0 || numPoints < 1) return;
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
    // double uz = dz / norm; // 通常z方向不延长
    double step = length / numPoints;
    for (int i = 1; i <= numPoints; ++i) {
        last.x += ux * step;
        last.y += uy * step;
        // last.z += uz * step; // 通常z不延长
        stroke.p.push_back({last.x, last.y, last.z});
        //cout<<"added"<< "("<<last.x<<","<<last.y<<","<<last.z<<")\n";
    }
}

hanzi::hanzi(){
    // 读取平滑滤波配置
    try {
        ifstream cfgF("./config/smooth_config.json");
        if(cfgF.is_open()){
            //cout<<"OK"<<endl;
            json cfg;
            cfgF >> cfg;
            if(cfg.contains("minDistSq")) smoothCfg.minDistSq = cfg["minDistSq"].get<double>();
            if(cfg.contains("passes")) smoothCfg.passes = cfg["passes"].get<int>();
            if(cfg.contains("colinearThreshold")) smoothCfg.colinearThreshold = cfg["colinearThreshold"].get<double>();
            if(cfg.contains("maxSegmentLenSq")) smoothCfg.maxSegmentLenSq = cfg["maxSegmentLenSq"].get<double>();

            if(cfg.contains("zProfileType")) smoothCfg.zProfileType = cfg["zProfileType"].get<string>();
            if(cfg.contains("zMin")) smoothCfg.zMin = cfg["zMin"].get<double>();
            if(cfg.contains("zMax")) smoothCfg.zMax = cfg["zMax"].get<double>();
            if(cfg.contains("zCtrl1")) smoothCfg.zCtrl1 = cfg["zCtrl1"].get<double>();
            if(cfg.contains("zCtrl2")) smoothCfg.zCtrl2 = cfg["zCtrl2"].get<double>();

            if(cfg.contains("backRatio")) smoothCfg.backRatio = cfg["backRatio"].get<double>();
            if(cfg.contains("backMinLen")) smoothCfg.backMinLen = cfg["backMinLen"].get<double>();
            if(cfg.contains("backMaxLen")) smoothCfg.backMaxLen = cfg["backMaxLen"].get<double>();
        
            if(cfg.contains("fadeInPointCount")) smoothCfg.fadeInPointCount = cfg["fadeInPointCount"].get<int>();
            if(cfg.contains("fadeInLength")) smoothCfg.fadeInLength = cfg["fadeInLength"].get<double>();
            
            //cout<<smoothCfg.backRatio<<endl<<smoothCfg.backMinLen<<endl<<smoothCfg.backMaxLen<<endl<<smoothCfg.fadeInPointCount<<endl;

        }
    } catch(const std::exception& e) {
        cerr << "Warning: failed to load smooth_config.json (using defaults): " << e.what() << '\n';
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
    for(const auto& stroke : j["medians"]){
        Bi_Hua s;
        for(const auto& point : stroke){
            Point pt{point[0], point[1], 0.0};
            s.p.push_back(pt);
        }
        addPoint(s);
        applyZProfile(s);
        smooth(s);
        s.type = inferStrokeType(s);
        h.bi_hua_.push_back(s);
    }
    mp[ch] = std::move(h);
    return true;
}

const hanzi::HanZi* hanzi::getHanZi(const string& ch){
    auto it = mp.find(ch);
    if(it != mp.end()) return &it->second;
    if(loadCharData(ch)){
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
        cout<<"Couldn't open the file "<<path<<endl;
        return;
    }
    gout<<fixed<<setprecision(2);
    gout<<"G21\n";
    gout<<"G90\n";
    gout<<"G17\n";
    gout<<"G0 Z"<<z_up<<"\n\n";

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
            // 1. 延长笔画末端
            extendStrokeEnd(bh, 100.0, bh.p.size()/10); // 可调整参数：延长20单位，3个点
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
            //cout<<fadeCount<<endl;
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

            if (fadeCount > 0) {
                        //cout<<"changed"<<endl;
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
            //std::cout<<bh.type<<std::endl;
            // ---- 回笔处理（仅对横 "h" 和竖 "sh"） ----
            if (bh.type == "h" || bh.type == "sh") {
                size_t n = pts.size();
                if (n >= 3) {
                    // 计算原始坐标中的累积距离
                    vector<double> origDist(n, 0.0);
                    for (size_t i = 1; i < n; ++i) {
                        double dx = pts[i].x - pts[i-1].x;
                        double dy = pts[i].y - pts[i-1].y;
                        origDist[i] = origDist[i-1] + sqrt(dx*dx + dy*dy);
                    }
                    double origTotalLen = origDist.back();

                    double physTotalLen = origTotalLen * scaleForChar;
                    const double backRatio = smoothCfg.backRatio;
                    const double minBackLen = smoothCfg.backMinLen;
                    const double maxBackLen = smoothCfg.backMaxLen;
                    double backPhysLen = physTotalLen * backRatio;
                    backPhysLen = min(max(backPhysLen, minBackLen), maxBackLen);
                    double backOrigLen = backPhysLen / scaleForChar;

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
                        // 回笔轨迹的累积距离
                        vector<double> backDist(backPts.size(), 0.0);
                        double dx0 = backPts[0].x - pts.back().x;
                        double dy0 = backPts[0].y - pts.back().y;
                        backDist[0] = sqrt(dx0*dx0 + dy0*dy0);
                        for (size_t i = 1; i < backPts.size(); ++i) {
                            double dx = backPts[i].x - backPts[i-1].x;
                            double dy = backPts[i].y - backPts[i-1].y;
                            backDist[i] = backDist[i-1] + sqrt(dx*dx + dy*dy);
                        }
                        double backTotalLen = backDist.back();

                        double startZNorm = pts.back().z;
                        double endZNorm = 0.0;

                        double startZ = z_down;               // 回笔起点高度
                        double endZ = z_up * 0.3;       // 回笔终点高度

                        for (size_t i = 0; i < backPts.size(); ++i) {
                            double t = (backTotalLen > 1e-6) ? (backDist[i] / backTotalLen) : 0.0;
                            double zNorm = startZNorm * (1.0 - t) + endZNorm * t;
                            double x = xOrigin + (backPts[i].x * scaleForChar + xOffset);
                            double y = yOrigin + (backPts[i].y * scaleForChar + yOffset);
                            double z = startZ * (1.0 -t) + endZ * t;   // 线性插值
                            //cout<<"added"<<x<<","<<y<<","<<z<<endl;
                            drawPoints.emplace_back(x, y, z);
                        }
                    }
                }
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
    //cout<<"Generated "<<name<<" ("<<count<<" chars) at "<<path<<" which begin at "<<"("<<x0<<","<<y0<<")"<<endl;
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
            //cout<<filename<<":\n";
            try{
                json j;
                std::ifstream f(entry.path());
                f>>j;
                const auto& medians = j["medians"];
                for(const auto& stroke : medians){
                    for(const auto& point : stroke){
                        int x = point[0];
                        int y = point[1];
                        //cout<<"("<<x<<","<<y<<") ";
                    }
                    //cout<<endl;
                }
            } catch(const std::exception&){
                // ignore malformed files
            }
            //cout<<endl;
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

string hanzi::inferStrokeType(const Bi_Hua& stroke){
    if(stroke.p.size() < 2) return "o";
    const auto& a = stroke.p.front();
    const auto& b = stroke.p.back();
    double dx = b.x - a.x;
    double dy = b.y - a.y;
    double adx = fabs(dx);
    double ady = fabs(dy);

    if(adx < 1e-6 && ady < 1e-6) return "o";

    // 1) 横
    if(ady < adx * 0.5) return "h";
    // 2) 竖
    if(adx < ady * 0.5) return "sh";
    // 3) 撇/捺
    if(dx < 0 && dy > 0) return "p"; // 撇
    if(dx > 0 && dy > 0) return "n"; // 捺

    return "o";
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