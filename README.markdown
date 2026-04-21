
# Gwrite

基于 DexArm 的汉字“毛笔”书写与 G-code 生成工具。该项目将汉字骨架数据（位于 `hanzi_data/`）处理为平滑笔迹、加入笔压/Z 轴曲线与回笔，再输出为标准 G-code（`.gcode`），并可通过串口实时下发给 DexArm 机械臂执行。

## 项目简介

- **功能**：把汉字骨架数据转换为毛笔风格的 G-code，并支持通过串口发送给机械臂执行写字。
- **输出**：G-code 文件（默认 `output.gcode`，可自定义路径）。
- **交互**：命令行交互程序 `main.exe`（或编译所得可执行文件），支持生成 G-code、设置串口、回零、手动调整工作 Z 零点等。

## 快速开始

  （已编译，可直接运行main.exe）

- 使用 VS Code 内置的编译任务（已包含任务 `C/C++: g++.exe 生成活动文件`）。或者在安装有 g++ 的环境下执行：

```bash
g++ -std=c++17 -g main.cpp -o main.exe
```

- 可选（用于 DEBUG 三维可视化）：链接并安装 OpenCV，编译时加上 OpenCV 的 include 与 lib 路径并链接对应库。

- 运行程序：

```bash
./main.exe
```

在程序菜单中：
- 选择 `2` 设置串口（默认 `COM3`）。
- 选择 `5` 设置工作高度。
- 选择 `1` 输入要写的文本，程序会在生成 G-code 并保存（默认 `output.gcode`）后询问是否实时下发到机械臂执行。

## 项目架构

- `main.cpp`：程序入口，提供交互式菜单、G-code 文件发送逻辑（`sendGcodeFile`）与串口连接管理（通过 `DexArmController`）。
- `include/`：头文件集合。
	- `hanzi.h`：汉字处理类 `hanzi` 的声明。
	- `DexArmController.h`：机械臂串口通信封装。
	- `json.hpp`：nlohmann::json 单文件库（项目内置）。
- `src/`：实现文件。
	- `hanzi.cpp`：汉字笔画处理、平滑、Z 曲线、G-code 生成核心实现。
	- `DexArmController.cpp`：串口底层实现（Windows/ POSIX 两套实现）。
- `hanzi_data/`：汉字骨架数据（每个汉字一个 `.json` 文件，包含 `medians` 字段）。
- `config/`：运行与笔画平滑的配置文件。
	- `smooth_config.json`：平滑/笔压/回笔等参数。
	- `writing_config.json`：写字时的默认参数（速度、起点、尺寸等）。
- `G.gcode`, `output.gcode`：示例/输出 G-code。
- `Gview/`：用于书写并生成骨架点的页面资源（`generate.html`）。
- `APL/`、`include/`、`Introduction/`：编码/字体/说明等辅助资源。

## 使用示例

- 生成 G-code（交互）：启动 `main.exe` → 选择 `1` → 输入文本 → 程序会生成 `output.gcode`（默认）并询问是否下发给机械臂。
- 在代码里直接调用（示例）：

```cpp
hanzi hz(1);
hz.printGCode("要写的文字", 1000.0, -200.0, 200.0, 0.0, -2.0, 18, 1, 30.0, false, "./output.gcode");
```

推荐参数（示例）：

- `Rate`：1000.0（移动速度）
- `x,y`：起点坐标，例如 `-200, 200`
- `z_up,z_down`：抬笔/落笔高度，例如 `0, -2`（需配合机械臂工作高度校准）
- `n,m`：每行/列字数
- `Size`：每字占用方形边长（mm）

## 实现原理

主要流程：

1. 字符分割与懒加载：把输入的 UTF-8 字符串切分为单字符，调用 `getHanZi`/`loadCharData` 从 `./hanzi_data/<char>.json` 加载骨架点（包含 `medians`）。
2. 笔画识别（`inferStrokeType`）：对每个骨架笔画按方向与曲率分段，识别为若干子笔画（点、横、竖、撇、捺、提、钩等）。
3. 插值平滑（`addPoint`）：在骨架点间使用 Catmull–Rom 样条插值补点，使轨迹连贯平滑。
4. 子笔画索引更新（`updateSubStrokeIndices`）：把子笔画在原始骨架与插值后点集中对应的索引同步。
5. 末端延长（`extendStrokeEnd`）与回笔（`addBackstroke`）：确保写笔动作在起落和回收时自然过渡。
6. Z 轴压力曲线（`applyZProfile`）：根据子笔画类型与比例，生成局部 z 值（0..1），再映射到 `smooth_config.json` 的 `[zMin,zMax]` 范围以实现笔压变化效果。
7. 个性化修正（`personalizeStroke`）：对横起、顿挫、钩提等局部笔势做额外点/高度调整，增加毛笔特性（顿挫、提笔等）。
8. 平滑与降噪（`smooth`）：移除近似重复点、加权移动平均滤波、去除近似共线点以减少冗余采样。
9. 输出 G-code（`printGCode`）：按字符格网、缩放与坐标偏移计算最终绝对坐标，输出以 `G0/G1` 为主的 G-code，并在笔画间做抬笔（`G0 Z...`）与落笔（`G1 Z... F...`）。

## 关键模块说明

- `hanzi`（`include/hanzi.h` / `src/hanzi.cpp`）
	- 负责读取骨架数据、构建笔画、插值、平滑、Z 曲线与最终 G-code 的生成。
	- 提供 `printGCode`、`printSingleWord`、`printAllWord` 与 `writeWithSerial` 等接口。
- `DexArmController`（`include/DexArmController.h` / `src/DexArmController.cpp`）
	- 串口封装：在 Windows 上使用 WinAPI（CreateFile/ReadFile/WriteFile），在 POSIX 上使用 termios。
	- 提供 `sendAndWaitOk`，发送命令并等待机械臂返回 `ok`，自动忽略 `wait` 心跳。

## 配置说明

- `config/smooth_config.json`（笔画平滑与笔压参数，常见字段说明）：
	- **minDistSq**: 去重阈值（点之间最小距离的平方），用于删除过密点。
	- **passes**: 平滑滤波迭代次数（移动平均次数）。
	- **colinearThreshold**: 共线检测阈值，用于删去近似共线点。
	- **maxSegmentLenSq**: 共线检测时的最大段长平方。
	- **zProfileType**: Z 曲线类型（`sin` 或 `bezier`）。
	- **zMin / zMax**: Z 映射范围（笔压映射到真实 Z 值）。
	- **backRatio / backMinLen / backMaxLen**: 回笔长度计算参数（比例与最小/最大物理长度）。
	- **strokeLeadInMinLen**: 触发笔迹引入（lead-in）的最小笔画长度。

- `config/writing_config.json`（写字默认参数）：
	- **rate**: 速度（F 值）。
	- **startX / startY**: 写字网格第一个字的起点坐标。
	- **zUp / zDown**: 抬笔与落笔高度（注意需与机械臂工作坐标系校正）。
	- **rows / cols / charSize / rowMajor / gcodePath**: 布局与输出设置。

## 数据格式（`hanzi_data/`）

- 每个汉字对应一个 JSON 文件（`<char>.json`），结构中包含 `medians` 字段：一个数组，数组元素为单笔画的点列表（[[x,y], [x,y], ...]）。
- 项目也支持 `graphics.txt`（逐行 JSON）或 `pre.json` 用于收录特殊字符。构造 `hanzi` 时传入 `option==2` 会尝试预加载 `graphics.txt`。

## 调试、依赖与注意事项

- 依赖：仅需 `g++` 与项目内的 `json.hpp` 即可构建并运行。可选依赖：OpenCV（仅用于 `DEBUG` 下的 3D 可视化）。
- 串口：在 Windows 上请使用 `COM*` 形式端口名（默认 `COM3`，可在程序中修改）；在 Linux 上使用 `/dev/tty*` 路径。
- 单位：坐标与尺寸以 mm 为单位，`charSize` 表示字符的方形边长（mm）。请务必先在安全位置手动测试 G-code，再在机械臂上运行完整写字任务以避免碰撞。

## 常见问题

- 如果机械臂没有响应，请检查串口波特率与端口是否正确，确认设备处于就绪状态并且程序中调用了 `setPenModule()`。
- 若输出 G-code 出现断裂或抖动，可调小 `smooth_config.json` 中的 `minDistSq` 与 `passes`，或者检查源骨架数据。

## 许可证

- 请参见仓库根目录的 [license.md](license.md) 文件。

---

若需要，我可以：
- 将 README 进一步本地化（增加更多使用截图或 GIF）；
- 帮你执行一次编译并生成示例 `output.gcode`；
- 或者把改动提交为 git commit（需要你授权或提供提交说明）。

