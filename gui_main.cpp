// ========== 必须先定义 nuklear 实现宏 ==========
#define NK_IMPLEMENTATION
#define NK_GLFW_GL3_IMPLEMENTATION

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <GL/gl.h>  

#include "GUI/nuklear.h"
#include "GUI/nuklear_glfw_gl3.h"

// 项目头文件
#include "./include/DexArmController.h"
#include "./include/ConfigReader.h"

// 标准库
#include <iostream>
#include <string>
#include <atomic>
#include <fstream>
#include <cstring>
#include "include/json.hpp"   // 根据实际路径调整

// ========== 全局状态（使用 float 适配 nuklear API）==========
struct AppState {
    // 汉字文本
    std::string chineseText = "你好世界";

    // 书写参数（全部用 float，便于 nuklear 控件）
    float rate = 2000.0f;
    float startX = 0.0f;
    float startY = 0.0f;
    float zUp = 30.0f;
    float zDown = 0.0f;
    int rows = 1;
    int cols = 1;
    float charSize = 100.0f;
    nk_bool rowMajor = 1;      // nk_bool 即 int
    std::string gcodePath = "./G.gcode";

    // 串口参数
    std::string port = "COM3";
    int baudrate = 115200;
    nk_bool isConnected = 0;

    // 控制标志
    std::atomic<bool> running{false};
    std::string statusMessage = "就绪";

    // 机械臂控制器
    DexArmController arm;
};

static AppState g_state;

// 辅助函数：更新状态消息（线程安全用互斥锁，这里简单直接赋值）
static void setStatus(const std::string& msg) {
    g_state.statusMessage = msg;
    std::cout << msg << std::endl;
}

// ========== 机械臂操作函数（与 GUI 交互）==========
static void toggleConnection() {
    if (g_state.isConnected) {
        g_state.arm.disconnect();
        g_state.isConnected = 0;
        setStatus("已断开连接");
    } else {
        if (g_state.arm.connect(g_state.port, g_state.baudrate)) {
            g_state.isConnected = 1;
            setStatus("连接成功");
        } else {
            setStatus("连接失败，请检查串口和参数");
        }
    }
}

static void homeArm() {
    if (!g_state.isConnected) {
        setStatus("未连接机械臂");
        return;
    }
    setStatus("正在归位...");
    if (g_state.arm.home()) {
        setStatus("归位完成");
    } else {
        setStatus("归位失败");
    }
}

static void generateGCode() {
    setStatus("正在生成 G-code...");
    // 调用原 generateAndExecute 生成文件（但不执行）
    // 注意：原方法会同时生成并执行，这里需要单独生成。
    // 为了清晰，我们直接调用 m_hanzi 生成，不发送命令。
    // 简单方式：让 DexArmController 提供一个仅生成的方法，或直接调用 hanzi::printGCode。
    // 由于 DexArmController 已有 generateAndExecute 且会执行，我们暂时调用它并注释掉执行部分？
    // 更好的做法：在 DexArmController 中添加 generateOnly 方法。
    // 这里为简化，先使用 generateAndExecute，但只生成不执行需要修改原代码。
    // 我们假设 DexArmController 已有生成方法，但实际没有，下面临时调用 printGCode。
    // 直接使用 hanzi 实例生成，但 hanzi 对象在 DexArmController 内部，我们需要暴露。
    // 为避免修改原有代码，我们临时在 AppState 中添加一个 hanzi 实例，或者通过 arm 的公共接口。
    // 原项目中 DexArmController 有一个 m_hanzi 成员，但没有公开。为方便，我们新增一个方法或简单拷贝。
    // 这里采用直接使用 hanzi 类实例，并调用其 printGCode。
    static hanzi hz;  // 临时使用一个独立的 hanzi 实例（与原 arm 中的相同）
    hz.printGCode(g_state.chineseText,
                  g_state.rate,
                  g_state.startX, g_state.startY,
                  g_state.zUp, g_state.zDown,
                  g_state.rows, g_state.cols,
                  g_state.charSize,
                  g_state.rowMajor,
                  g_state.gcodePath);
    setStatus("G-code 已生成：" + g_state.gcodePath);
}

static void executeGCode() {
    if (!g_state.isConnected) {
        setStatus("未连接机械臂");
        return;
    }
    setStatus("正在执行 G-code...");
    if (g_state.arm.executeGCodeFile(g_state.gcodePath, true)) {
        setStatus("执行完成");
    } else {
        setStatus("执行失败");
    }
}

static void loadConfig() {
    const std::string configPath = "./config/writing_config.json";
    if (g_state.arm.loadConfig(configPath)) {
        // 将配置同步到 GUI 显示
        const auto& cfg = g_state.arm.getWritingConfig(); // 需要添加此方法
        g_state.rate = static_cast<float>(cfg.rate);
        g_state.startX = static_cast<float>(cfg.startX);
        g_state.startY = static_cast<float>(cfg.startY);
        g_state.zUp = static_cast<float>(cfg.zUp);
        g_state.zDown = static_cast<float>(cfg.zDown);
        g_state.rows = cfg.rows;
        g_state.cols = cfg.cols;
        g_state.charSize = static_cast<float>(cfg.charSize);
        g_state.rowMajor = cfg.rowMajor ? 1 : 0;
        g_state.gcodePath = cfg.gcodePath;
        setStatus("已加载配置：" + configPath);
    } else {
        setStatus("加载配置失败");
    }
}

static void saveConfig() {
    WritingConfig cfg;
    cfg.rate = g_state.rate;
    cfg.startX = g_state.startX;
    cfg.startY = g_state.startY;
    cfg.zUp = g_state.zUp;
    cfg.zDown = g_state.zDown;
    cfg.rows = g_state.rows;
    cfg.cols = g_state.cols;
    cfg.charSize = g_state.charSize;
    cfg.rowMajor = (g_state.rowMajor != 0);
    cfg.gcodePath = g_state.gcodePath;

    nlohmann::json j;
    j["rate"] = cfg.rate;
    j["startX"] = cfg.startX;
    j["startY"] = cfg.startY;
    j["zUp"] = cfg.zUp;
    j["zDown"] = cfg.zDown;
    j["rows"] = cfg.rows;
    j["cols"] = cfg.cols;
    j["charSize"] = cfg.charSize;
    j["rowMajor"] = cfg.rowMajor;
    j["gcodePath"] = cfg.gcodePath;

    std::ofstream file("./config/writing_config.json");
    if (file.is_open()) {
        file << j.dump(4);
        setStatus("配置已保存");
    } else {
        setStatus("保存配置失败");
    }
}

// ========== GUI 绘制 ==========
static void drawGUI(struct nk_context* ctx) {
    // 为文本编辑准备临时缓冲区
    static char textBuf[256];
    static int textLen = 0;
    static char portBuf[64];
    static int portLen = 0;
    static char gcodePathBuf[256];
    static int gcodePathLen = 0;

    // 每次绘制前从 std::string 同步到缓冲区
    strncpy(textBuf, g_state.chineseText.c_str(), sizeof(textBuf)-1);
    textBuf[sizeof(textBuf)-1] = '\0';
    textLen = static_cast<int>(g_state.chineseText.size());

    strncpy(portBuf, g_state.port.c_str(), sizeof(portBuf)-1);
    portBuf[sizeof(portBuf)-1] = '\0';
    portLen = static_cast<int>(g_state.port.size());

    strncpy(gcodePathBuf, g_state.gcodePath.c_str(), sizeof(gcodePathBuf)-1);
    gcodePathBuf[sizeof(gcodePathBuf)-1] = '\0';
    gcodePathLen = static_cast<int>(g_state.gcodePath.size());

    // 开始绘制窗口
    if (nk_begin(ctx, "DexArm 汉字书写控制台",
                 nk_rect(50, 50, 600, 700),
                 NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_TITLE))
    {
        // ========== 汉字输入 ==========
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_label(ctx, "汉字文本:", NK_TEXT_LEFT);
        nk_edit_string(ctx, NK_EDIT_FIELD, textBuf, &textLen, sizeof(textBuf)-1, nk_filter_default);
        g_state.chineseText = textBuf;

        // ========== 书写参数 ==========
        nk_layout_row_dynamic(ctx, 25, 1);
        nk_label(ctx, "书写参数", NK_TEXT_CENTERED);
        nk_layout_row_dynamic(ctx, 25, 2);
        nk_label(ctx, "速度 (mm/min):", NK_TEXT_LEFT);
        nk_property_float(ctx, "#速度", 500.0f, &g_state.rate, 5000.0f, 50.0f, 1.0f);
        nk_label(ctx, "起始 X:", NK_TEXT_LEFT);
        nk_property_float(ctx, "#X", -500.0f, &g_state.startX, 500.0f, 10.0f, 1.0f);
        nk_label(ctx, "起始 Y:", NK_TEXT_LEFT);
        nk_property_float(ctx, "#Y", -500.0f, &g_state.startY, 500.0f, 10.0f, 1.0f);
        nk_label(ctx, "抬笔高度 (mm):", NK_TEXT_LEFT);
        nk_property_float(ctx, "#Z_up", 10.0f, &g_state.zUp, 100.0f, 5.0f, 1.0f);
        nk_label(ctx, "落笔高度 (mm):", NK_TEXT_LEFT);
        nk_property_float(ctx, "#Z_down", -20.0f, &g_state.zDown, 20.0f, 1.0f, 1.0f);
        nk_label(ctx, "行数:", NK_TEXT_LEFT);
        nk_property_int(ctx, "#行", 1, &g_state.rows, 10, 1, 1.0f);
        nk_label(ctx, "列数:", NK_TEXT_LEFT);
        nk_property_int(ctx, "#列", 1, &g_state.cols, 10, 1, 1.0f);
        nk_label(ctx, "字框边长 (mm):", NK_TEXT_LEFT);
        nk_property_float(ctx, "#尺寸", 20.0f, &g_state.charSize, 200.0f, 5.0f, 1.0f);
        nk_label(ctx, "行优先:", NK_TEXT_LEFT);
        nk_checkbox_label(ctx, "行优先 (勾选)/列优先", &g_state.rowMajor);
        nk_label(ctx, "G-code 输出路径:", NK_TEXT_LEFT);
        nk_edit_string(ctx, NK_EDIT_FIELD, gcodePathBuf, &gcodePathLen, sizeof(gcodePathBuf)-1, nk_filter_default);
        g_state.gcodePath = gcodePathBuf;

        // ========== 串口连接 ==========
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_label(ctx, "串口设置", NK_TEXT_CENTERED);
        nk_layout_row_dynamic(ctx, 25, 2);
        nk_label(ctx, "端口:", NK_TEXT_LEFT);
        nk_edit_string(ctx, NK_EDIT_FIELD, portBuf, &portLen, sizeof(portBuf)-1, nk_filter_default);
        g_state.port = portBuf;
        nk_label(ctx, "波特率:", NK_TEXT_LEFT);
        char baudBuf[16];
        snprintf(baudBuf, sizeof(baudBuf), "%d", g_state.baudrate);
        int baudLen = static_cast<int>(strlen(baudBuf));
        if (nk_edit_string(ctx, NK_EDIT_FIELD, baudBuf, &baudLen, sizeof(baudBuf)-1, nk_filter_decimal)) {
            g_state.baudrate = atoi(baudBuf);
        }

        nk_layout_row_dynamic(ctx, 30, 2);
        if (nk_button_label(ctx, g_state.isConnected ? "断开连接" : "连接")) {
            toggleConnection();
        }
        if (nk_button_label(ctx, "归位")) {
            homeArm();
        }

        // ========== 操作按钮 ==========
        nk_layout_row_dynamic(ctx, 30, 3);
        if (nk_button_label(ctx, "加载配置")) loadConfig();
        if (nk_button_label(ctx, "保存配置")) saveConfig();
        if (nk_button_label(ctx, "生成 G-code")) generateGCode();
        if (nk_button_label(ctx, "执行 G-code")) executeGCode();

        // ========== 状态显示 ==========
        nk_layout_row_dynamic(ctx, 50, 1);
        nk_label_colored(ctx, ("状态: " + g_state.statusMessage).c_str(),
                         NK_TEXT_LEFT, nk_rgb(0, 200, 0));
    }
    nk_end(ctx);
}

// ========== 主函数 ==========
int main() {
    // 1. 初始化 GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }
    GLFWwindow* win = glfwCreateWindow(800, 800, "DexArm 汉字书写控制台", nullptr, nullptr);
    if (!win) {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(win);

    // 2. 加载 OpenGL 函数指针（使用 glad）
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        glfwTerminate();
        return -1;
    }

    // 3. 初始化 nuklear 后端
    struct nk_glfw glfw_backend = {0};
    struct nk_context* ctx = nk_glfw3_init(&glfw_backend, win, NK_GLFW3_INSTALL_CALLBACKS);
    if (!ctx) {
        std::cerr << "Failed to init nuklear" << std::endl;
        glfwTerminate();
        return -1;
    }

    // 4. （可选）加载中文字体
    // struct nk_font_atlas *atlas;
    // nk_glfw3_font_stash_begin(&glfw_backend, &atlas);
    // struct nk_font *font = nk_font_atlas_add_from_file(atlas, "C:/Windows/Fonts/simhei.ttf", 18, 0);
    // nk_glfw3_font_stash_end(&glfw_backend);
    // if (font) nk_style_set_font(ctx, &font->handle);

    // 5. 主循环
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        nk_glfw3_new_frame(&glfw_backend);
        drawGUI(ctx);
        glfwSwapBuffers(win);
    }

    // 6. 清理
    nk_glfw3_shutdown(&glfw_backend);
    glfwTerminate();
    return 0;
}