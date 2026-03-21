#include<iostream>
#include<fstream>
#include<map>
#include<iomanip>
#include<cmath>
#include<filesystem>

#ifdef _WIN32
    //#include<windows.h>
#endif

#include"../include/json.hpp"
#include"../include/hanzi.h"

using namespace std;
using json=nlohmann::json;

hanzi::hanzi(){
    // 读取平滑滤波配置
    // 1) minDistSq：去除近似重复点的最小距离的平方
    //    值越大，去掉的点越多（更平滑，但细节减少）
    // 2) passes：移动平均滤波的迭代次数
    //    次数越多，曲线越圆滑（更明显的低通滤波效果）
    // 3) colinearThreshold：判定共线的阈值（sin(theta)）
    //    值越大，越多点会被认为共线而删除
    // 4) maxSegmentLenSq：在去除共线点时，仅处理更短的线段
    //    避免将长直线段上的点误删。
    
    try {
        ifstream cfgF("./smooth_config.json");
        if(cfgF.is_open()){
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
        }
    } catch(const std::exception& e) {
        cerr << "Warning: failed to load smooth_config.json (using defaults): " << e.what() << '\n';
    }

    // Data is loaded lazily per character to avoid parsing a large all.json at startup.
    // Individual character strokes are stored as ./hanzi_data/<char>.json.
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

        for(const auto& bh: hz->bi_hua_){
            const auto& firstPoint = bh.p.front();
            double x = xOrigin + (firstPoint.x * scaleForChar + xOffset);
            double y = yOrigin + (firstPoint.y * scaleForChar + yOffset);
            double z = z_up + (z_down - z_up) * firstPoint.z;
            gout<<"G0 X"<<x<<" Y"<<y<<"\n";
            gout<<"G1 X"<<x<<" Y"<<y<<" Z"<<z<<" F"<<Rate<<"\n";
            for(int i=1;i<bh.p.size();i++){
                x = xOrigin + (bh.p[i].x * scaleForChar + xOffset);
                y = yOrigin + (bh.p[i].y * scaleForChar + yOffset);
                z = z_up + (z_down - z_up) * bh.p[i].z;
                gout<<"G1 X"<<x<<" Y"<<y<<" Z"<<z<<"\n";
            }
            gout<<"G0 Z"<<z_up<<"\n";
            gout<<"G0 Z"<<z_up+30<<"\n\n";
        }
    }
    gout<<"G0 Z"<<z_up<<"\n";
    gout<<"M30\n";
    gout.close();
    cout<<"Generated "<<name<<" ("<<count<<" chars) at "<<path<<" which begin at "<<"("<<x0<<","<<y0<<")"<<endl;
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
            cout<<filename<<":\n";
            try{
                json j;
                std::ifstream f(entry.path());
                f>>j;
                const auto& medians = j["medians"];
                for(const auto& stroke : medians){
                    for(const auto& point : stroke){
                        int x = point[0];
                        int y = point[1];
                        cout<<"("<<x<<","<<y<<") ";
                    }
                    cout<<endl;
                }
            } catch(const std::exception&){
                // ignore malformed files
            }
            cout<<endl;
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
            Point p3=(i==n-1)?points[i+1]:points[i+2];
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

void hanzi::applyZProfile(Bi_Hua& stroke){
    auto& pts = stroke.p;
    if(pts.size() < 2) return;

    // 计算路径长度和每点归一化 t
    vector<double> dist(pts.size(), 0.0);
    for(size_t i = 1; i < pts.size(); ++i){
        double dx = pts[i].x - pts[i-1].x;
        double dy = pts[i].y - pts[i-1].y;
        dist[i] = dist[i-1] + sqrt(dx*dx + dy*dy);
    }
    double total = dist.back();
    if(total <= 1e-9){
        for(auto& p : pts) p.z = smoothCfg.zMin;
        return;
    }

    auto evalZ = [&](double t){
        // 先计算归一化的曲线值 base（0..1），再映射到 [zMin,zMax]
        double base = 0.0;
        if(smoothCfg.zProfileType == "bezier"){
            // 从 0 到 0 的三次贝塞尔曲线，控制点决定中间峰值：
            //   B(t) = (1-t)^3*0 + 3(1-t)^2 t*c1 + 3(1-t) t^2*c2 + t^3*0
            double u = 1.0 - t;
            base = 3*u*u*t*smoothCfg.zCtrl1 + 3*u*t*t*smoothCfg.zCtrl2;
        } else {
            // 默认 sin 曲线（start/end 0，中间 1）
            base = sin(M_PI * t);
        }

        return smoothCfg.zMin + (smoothCfg.zMax - smoothCfg.zMin) * base;
    };

    for(size_t i = 0; i < pts.size(); ++i){
        double t = dist[i] / total;
        pts[i].z = evalZ(t);
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