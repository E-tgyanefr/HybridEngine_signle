#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace HybridEngine::Platform {

// M1：窗口接口（GLFW 式回调——Win32 默认后端；纹理/帧呈现归窗口）
struct WindowDesc {
    std::string title = "HybridEngine v3";
    int width = 1280, height = 720;
};

class IWindow {
public:
    virtual ~IWindow() = default;
    virtual bool Create(const WindowDesc&) = 0;
    virtual bool PumpOne() = 0;                 // 泵一条消息（键盘/鼠标/尺寸→回调）；WM_QUIT 返回 false
    virtual void Show() = 0;
    virtual void Close() = 0;
    virtual void Present(const uint32_t* frame) = 0;  // Win32 帧呈现（StretchDIBits——v1 语义；frame=客户区宽×高 0xFFRRGGBB 像素——32bpp BI_RGB 写 BGRA 字节）
    // **契约（务必遵守）**：`frame` 必须指向 ClientWidth()*ClientHeight() 个 uint32 像素。
    //   传更小的缓冲 = 越界读。历史事故：某测试用例用 320x200 的帧配 500x320 的客户区——
    //   旧实现只把指针交给 StretchDIBits 所以没炸；加了双重缓冲（需先拷贝进后备位图）后
    //   立刻 access violation。隐式契约必须写进接口，否则下一个实现者还会踩。
    virtual int ClientWidth() const = 0;
    virtual int ClientHeight() const = 0;
    virtual void OnClientSize(int w, int h) = 0;  // t128：内部客户区尺寸更新（Present 源/目标——WM_SIZE 驱动）
    virtual uint32_t Dpi() const = 0;           // 每窗口 DPI（96=100%；PerMonitorV2 语义）
    virtual void* Handle() const = 0;           // 原生句柄（Win32 HWND；后端无关者=nullptr）

    // —— 输入/尺寸回调（Host 绑 Input/渲染 Resize）——
    std::function<void(int key, bool down)> OnKey = nullptr;       // WM_KEYDOWN/UP（key=VK 码）
    std::function<void(double x, double y, bool down)> OnMouse = nullptr;  // 客户区坐标
    std::function<void(double x, double y)> OnMouseRight = nullptr;         // 客户区坐标（右键——编辑器上下文菜单）
    // 左键双击（WM_LBUTTONDBLCLK；窗口类需 CS_DBLCLKS——已在 Win32 后端注册）。
    // 为什么单列一条：单击与双击语义不同（选中 vs 打开），而 OnMouse 只有 down 位、区分不出来。
    std::function<void(double x, double y)> OnMouseDoubleClick = nullptr;
    // t136：3 键按钮+滚轮（M3.5 ScenePanel 3D orbit/pan/dolly）
    std::function<void(double x, double y, int button, bool down)> OnMouseButton = nullptr;  // 0=L 1=M 2=R
    std::function<void(double x, double y, int delta)> OnMouseWheel = nullptr;              // delta=±1 每格
    std::function<void(int w, int h)> OnResize = nullptr;          // 客户区像素
    std::function<void()> OnClose = nullptr;                        // WM_CLOSE（宿主可拦截）
    // P3：**已翻译的字符**输入（WM_CHAR 路径）——cp = Unicode 码点（UTF-32）。
    // 为什么必须单独有一条：OnKey 只给 VK 码（没有大小写、没有标点、没有 IME 结果），
    // 文本编辑离不开「用户实际想输入哪个字符」。中文/日文等要走 IME，
    // 而 IME 提交的结果只从 WM_CHAR 出来（VK 码拿不到）。UTF-16 代理对已在本层合成单码点。
    std::function<void(uint32_t cp)> OnChar = nullptr;
};

// 默认 Win32 实现（GLFW/其他后端=未来接口）
IWindow* CreateWindow(const WindowDesc&);

// t-ui-dpi：进程级 DPI 感知（Per-Monitor V2——失败=SetProcessDPIAware 降级；返回是否已生效）
// 必须在任何窗口创建前调用；无效（离屏/测试）也可调用=无窗口无副作用。
bool EnableDpiAwareness();

} // namespace HybridEngine::Platform