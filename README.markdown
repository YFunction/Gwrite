# Gwrite - 机械臂毛笔字书写路径算法

## 📋 项目简介

Gwrite 是一个基于机械臂的汉字毛笔字书写路径算法项目。该项目通过 C++ 实现，能够将汉字的笔画信息转换为机械臂可执行的 G-code 控制指令，实现自动化毛笔字书写功能。项目包含完整的汉字数据库、路径平滑算法和可视化工具。

## ✨ 核心特性

| 功能模块 | 说明 |
|---------|------|
| **汉字数据库** | 完整的汉字笔画数据集（JSON 格式），支持多种字符编码（GB、Big5、UTF-8等） |
| **G-code 生成** | 将汉字笔画转换为 G-code 指令，驱动机械臂执行书写动作 |
| **路径平滑算法** | 对笔画进行平滑处理，优化机械臂运动轨迹，降低震动 |
| **Z轴控制** | 支持 Bezier 曲线和正弦波等多种笔毛压力控制策略 |
| **可视化预览** | 使用 HTML5 Canvas 和 Python (OpenCV) 可视化生成的 G-code |
| **模块化设计** | 清晰的代码结构，便于扩展和维护 |

## 📁 项目结构

```
Gwrite/
├── test.cpp                   # 测试入口程序
├── src/
│   ├── hanzi.cpp             # 汉字处理核心实现
│   └── ConfigReader.cpp       # 配置文件读取模块
├── include/
│   ├── hanzi.h               # 汉字类定义
│   ├── ConfigReader.h        # 配置读取接口
│   ├── json.hpp              # JSON 解析库 (nlohmann/json)
│   └── PreDEBUG.h            # 调试预定义头文件
├── config/
│   ├── smooth_config.json    # 路径平滑配置（最小距离、通道数、Z轴参数等）
│   └── writing_config.json   # 书写配置参数
├── Gview/
│   └── generate.html         # G-code 可视化工具（HTML5 Canvas）
├── hanzi_data/
│   └── [*.json]              # 汉字笔画数据（共5000+汉字）
│       ├── 0-9.json          # 数字字符
│       ├── 标点符号.json       # 中文标点
│       └── 汉字.json          # 汉字笔画信息
├── APL/                      # 字体和字符集支持
│   ├── gb/                   # 简体中文字符集
│   ├── big5/                 # 繁体中文字符集
│   ├── english/              # 英文字符集
│   ├── zh_CN.UTF-8/          # 简体UTF-8
│   └── zh_TW.UTF-8/          # 繁体UTF-8
├── G.gcode                   # 示例 G-code 文件
├── license.md                # 字体使用许可证
└── README.markdown           # 本文件
```

## 🔧 编译与使用

### 系统要求

- **编译器**：GCC 7.0+ 或 Clang（支持 C++17）
- **操作系统**：Windows / Linux / macOS
- **依赖库**：OpenCV（图像处理）、nlohmann/json（JSON 解析）
- **可视化**：Python 3.6+、OpenCV Python、NumPy、Matplotlib（可选）

### 编译步骤

#### 方式一：使用 VS Code 任务（推荐）
```bash
# 使用内置 g++.exe 编译任务
# 按 Ctrl+Shift+B 或通过命令面板运行构建任务
```

#### 方式二：手动编译
```bash
g++ -std=c++17 -g \
  -I"C:\msys64\mingw64\include\opencv4" \
  -L"C:\msys64\mingw64\lib" \
  test.cpp -o test.exe \
  -lopencv_highgui -lopencv_imgcodecs -lopencv_imgproc -lopencv_core \
  -lopencv_viz -lgdi32 -lcomctl32
```

### 使用方法

```bash
# 1. 编译程序
g++ -std=c++17 test.cpp -o test.exe

# 2. 运行程序，输入要书写的汉字
./test.exe
# 输入: 你好
# 输出: G-code 控制指令和路径数据

# 3. 使用 G-code 可视化工具查看书写路径
# 用浏览器打开 Gview/generate.html，加载生成的 G-code 文件
```

## 📊 配置说明

### `smooth_config.json` - 路径平滑参数

```json
{
  "minDistSq": 0.005,              // 最小距离平方（点过滤）
  "passes": 0.25,                  // 平滑通道数
  "colinearThreshold": 0.0002,     // 共线点检测阈值
  "maxSegmentLenSq": 49.0,         // 最大线段长度平方
  "zProfileType": "bezier",        // Z轴压力曲线（"bezier" 或 "sin"）
  "zMin": 0.0,                     // 笔毛最小压力
  "zMax": 1.0,                     // 笔毛最大压力
  "backRatio": 0.2,                // 回笔比例
  "backMinLen": 5.0,               // 回笔最小长度
  "backMaxLen": 100.0,             // 回笔最大长度
  "strokeLeadInMinLen": 450.0,     // 笔画引入最小长度
  "fadeInPointCount": 5,           // 淡入点数
  "fadeInLength": 0.0              // 淡入长度
}
```

## 🎨 汉字数据格式

每个汉字的 JSON 文件包含笔画信息：

```json
{
  "character": "好",
  "strokes": [
    {
      "type": "横",
      "points": [[x1, y1], [x2, y2], ...],
      "startIdx": 0,
      "endIdx": 10
    },
    ...
  ]
}
```

## 🔍 核心类说明

### `hanzi` 类
- **功能**：汉字处理的核心类
- **主要方法**：
  - `printGCode()`：生成 G-code 指令
  - `printSingleWord()`：处理单个汉字
  - 路径平滑、Z轴控制等内部处理

### `ConfigReader` 类
- **功能**：读取 JSON 配置文件
- **用途**：加载平滑参数和书写配置

## 📝 示例

```cpp
#include "include/hanzi.h"

int main() {
    hanzi robot(1);  // 创建汉字处理对象
    
    // 生成 G-code：输入"你好"，基础位置1000.0, 笔毛深度-190
    robot.printGCode("你好", 1000.0, -190, 200, 0, -2, 18, 1, 30.0, false);
    
    return 0;
}
```

## 📚 多语言支持

项目支持多种字符编码和字符集：
- **简体中文**：GB (GBK/GB2312)、UTF-8
- **繁体中文**：Big5、UTF-8
- **英文**：ASCII 及扩展字符集
- **标点符号**：中文标点、数学符号

通过 `APL/` 目录下的字符集文件实现字符映射和查询。

## 🖥️ 可视化工具

### Gview HTML5 可视化
打开 `Gview/generate.html`，可以：
- 加载并显示 G-code 路径
- 实时预览笔毛轨迹和压力变化
- 调试路径平滑算法效果

## 📄 许可证

字体使用遵循 `license.md` 中的许可证条款。本项目代码可根据具体许可证规定使用。

## 🤝 贡献指南

欢迎提交 Issue 和 Pull Request 来改进项目！

## 📞 联系方式

如有问题或建议，请通过 GitHub Issue 或邮件联系项目维护者。

---

**最后更新**：2026年4月

2. 预览 G-code 路径：
   ```bash
   python Gview/main.py
   ```

## 项目状态

⚠️ **项目尚处于初期开发阶段，尚未经过充分验证**。目前功能包括：
- 基础汉字路径生成
- G-code 输出
- 简单的可视化预览
- 路径平滑处理

## 未来计划

- 完善机械臂控制接口
- 优化路径算法，提高书写质量
- 扩充汉字数据库
- 增加更多书法风格支持
- 实际机械臂硬件集成测试
