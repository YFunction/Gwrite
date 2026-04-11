# 提示词：为机械臂书写汉字项目添加串口控制功能

## 项目背景

你有一个 C++ 项目，主要功能是将汉字字形（骨架点数据）转换为带笔压信息的 G-code 路径，并输出为 `.gcode` 文件。现有代码位于 `hanzi.cpp` 和 `hanzi.h` 中，已经实现了：

- 加载汉字骨架点（JSON 格式）
- 笔画平滑、插值、子笔画类型识别（横、竖、撇、捺、钩、提、点等）
- 动态 Z 轴笔压生成（基于子笔画类型和贝塞尔/正弦曲线）
- 回笔、顿挫、笔锋等个性化处理
- 将单个或多个汉字排版并输出 G-code 字符串到文件

现在你需要**增加一个实时控制机械臂的功能**：通过串口直接发送 G-code 指令控制机械臂运动，而不是仅输出文件。机械臂使用基于 G-code 的串口协议，协议细节见 `rules.md`。

## 你的任务

为项目添加一个串口通信模块，并集成到现有 `hanzi` 类中，使得可以：

1. **建立/关闭串口连接**：支持选择 COM 端口、波特率（默认 115200）、数据位/停止位/校验位（8N1）。
2. **发送 G-code 指令并接收响应**：每条指令以 `\r\n` 结尾，机械臂返回 `ok\r\n` 或 `unknow command`。需要处理超时和重试。
3. **解析机械臂状态**：能够查询当前位置（M114）、编码器值（M890）等，并更新内部坐标系。
4. **将生成的笔画路径实时发送**：替代原有的文件输出模式，直接逐条发送 G0/G1 指令控制机械臂书写。
5. **支持机械臂特定指令**：如复位（M1111）、运动模式切换（M2000/M2001）、速度设置（G0 Fxxx）、延时（G4）、工作坐标系设定（G92）、前端模块切换（M888）等。
6. **错误处理与安全**：遇到错误指令或机械臂无响应时能停止运动、断开连接并报错。支持紧急停止（M81）。

## 具体实现要求

### 1. 新增类：`SerialController`

在 `hanzi.h` 中声明，在 `hanzi.cpp` 中实现（或单独文件，但需保持集成方便）。至少提供以下公共方法：

```cpp
class SerialController {
public:
    bool open(const std::string& port, int baudrate = 115200);
    void close();
    bool isOpen() const;
    
    // 发送一条 G-code 指令，等待 "ok" 响应。返回是否成功。
    bool sendCommand(const std::string& cmd, int timeoutMs = 2000);
    
    // 发送指令并获取多行响应（例如 M114 返回坐标）
    std::string queryResponse(const std::string& cmd, int timeoutMs = 2000);
    
    // 获取当前位置 (X, Y, Z, E)
    std::tuple<double, double, double, double> getPosition();  // 通过 M114
    
    // 紧急停止
    void emergencyStop();
    
    // 复位
    bool home();
};
```

**注意**：在 Windows 下可使用 `CreateFile` 或 `std::ifstream` 操作 COM 口，推荐使用跨平台库如 `boost::asio` 或手动封装 Windows API。为简化，可以只实现 Windows 版本，但代码中应预留跨平台结构。

### 2. 扩展 `hanzi` 类

在 `hanzi` 中添加新的公共方法，用于串口控制书写：

```cpp
class hanzi {
public:
    // ... 现有方法 ...
    
    // 通过串口实时书写汉字（不生成文件）
    void writeWithSerial(const std::string& name,
                         const std::string& port,
                         double Rate,
                         double x0, double y0,
                         double z_up, double z_down,
                         int n = 1, int m = 1,
                         double Size = 100.0,
                         bool rowMajor = true);
    
    // 设置串口控制器（外部传入，便于复用连接）
    void setSerialController(SerialController* controller);
    
private:
    SerialController* serialCtrl_ = nullptr;  // 非拥有指针
    bool sendStroke(const Bi_Hua& stroke,
                    double xOrigin, double yOrigin,
                    double scaleForChar, double xOffset, double yOffset,
                    double z_up, double z_down, double feedrate);
};
```

`writeWithSerial` 内部逻辑应与 `printGCode` 类似，但不再输出文件，而是调用 `sendStroke` 将每个笔画的点转换为绝对坐标并逐条发送 `G0`（抬刀）和 `G1`（落刀运动）指令。注意在笔画开始前发送 `G0 Z{up}` 抬刀，运动到起点后下笔到第一个点的 Z 值，然后逐点 `G1`。

### 3. 协议处理细节

参考 `rules.md`，你需要实现以下辅助功能：

- **初始化连接**：打开串口后，等待接收 `start\r\n` 表示设备就绪（可能需要复位）。可以发送一个空指令或 `M114` 测试。
- **运动模式**：在开始书写前发送 `M2000`（直线模式）以确保路径精度。
- **速度设置**：使用 `G0 F{rate}` 设置进给速率（单位 mm/min）。注意 `Rate` 参数可能需要转换。
- **绝对/相对模式**：默认使用绝对定位 `G90`。为方便，可以在每个笔画前重新设置坐标系（`G92`）或将原点偏移量加到每个点坐标中。推荐使用绝对坐标，将每个点的世界坐标直接发送。
- **坐标系原点**：机械臂的 HOME 点通常是 (300,0,0)。你需要在代码中定义机械臂的工作空间，并将汉字排版原点 (x0, y0) 映射到机械臂坐标系。最简单的做法：使用 `G92 X0 Y0 Z0` 将当前点设为临时原点，然后发送所有相对偏移。更稳定：直接发送绝对坐标，但需确保机械臂坐标范围不冲突。
- **前端模块**：书写前应发送 `M888 P0` 选择夹笔模块。
- **笔压控制**：现有代码中每个点都有 Z 值（0~1 映射到 `z_up` 到 `z_down`）。在 G-code 中，Z 坐标就是笔的升降。注意机械臂的 Z 轴正方向可能是向上，而你的 `z_up` 和 `z_down` 需要与机械臂实际方向一致（通常 `z_down` < `z_up` 表示下笔更低）。
- **回笔和顿挫**：现有笔画已经包含了回笔点（Z=0）。发送时应按点顺序发送，无需额外处理。

### 4. 错误处理与日志

- 每条指令发送后应检查返回是否为 `ok`。若超时或返回错误，应停止当前笔画、断开连接并抛出异常或记录错误。
- 提供回调函数接口，用于实时输出发送的指令和机械臂响应（调试用）。
- 在关键步骤（如复位、切换模块、开始书写）前验证机械臂状态。

### 5. 线程安全与非阻塞

书写过程可能耗时较长，建议在单独的工作线程中执行发送循环，避免阻塞 GUI（如果有）。当前项目可能无 GUI，但应允许中断（如按 Ctrl+C 时发送紧急停止并关闭串口）。

### 6. 示例使用代码

在 `main` 函数中应能这样调用：

```cpp
hanzi hz(1);  // 加载单个汉字数据
hz.writeWithSerial("你好", "COM3", 3000, 100, 100, 20, 0, 1, 1, 50.0);
```

### 7. 依赖项

如果需要串口库，推荐使用简单易集成的 `CSerial` 类或 `boost::asio`。为了减少依赖，你可以提供一个基于 Windows API 的简易实现，并在注释中说明 Linux 下需替换。提示词中应允许 Claude 选择实现方式，但必须保证代码清晰可读。

## 输出要求

请 Claude 提供：

1. **修改后的 `hanzi.h`** 完整内容（包含新增类声明和类成员）。
2. **修改后的 `hanzi.cpp`** 完整内容（包含新增类的实现以及 `writeWithSerial` 等方法的实现）。
3. 如果新建了 `SerialController` 的实现文件（如 `serial_controller.cpp`），也一并提供。
4. 一个简单的 `main.cpp` 示例，演示如何使用新功能。
5. 必要的 CMakeLists.txt 或编译说明（如果需要链接额外的库）。

## 注意事项

- 保持现有功能（文件输出）不受影响，新增功能作为可选路径。
- 代码风格与现有代码一致（缩进、命名等）。
- 充分考虑机械臂的安全：发送运动指令前确保 Z 轴已抬升到安全高度；发生错误时立即发送 M81。
- 串口通信要处理粘包和响应不完整的情况，使用简单的行读取缓冲区。
- 对于不熟悉的 G-code 指令（如 M888、M1000 等），请参考 `rules.md` 实现对应的接口函数。

请开始你的实现。