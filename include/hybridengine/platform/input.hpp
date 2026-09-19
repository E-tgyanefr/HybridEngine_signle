#pragma once
#include <memory>
#include <string>

namespace HybridEngine::Platform {

// M1：输入键码（值=Win32 VK——输入后端 VK 直通；平台无关语义）
enum class KeyCode {
    Space = 0x20, Enter = 0x0D, Escape = 0x1B,
    A = 0x41, D = 0x44, F = 0x46, J = 0x4A, K = 0x4B, P = 0x50, R = 0x52, S = 0x53, W = 0x57,
    Left = 0x25, Up = 0x26, Right = 0x27, Down = 0x28
};

// M1：输入实例接口（Win32Input 实现；宿主经 IWindow 回调桥——边沿语义=EndFrame 清）
class Input {
public:
    virtual ~Input() = default;
    virtual void OnKey(int vk, bool down) = 0;                       // 窗口回调桥入（vk=KeyCode 值）
    virtual void OnMouse(double x, double y, bool down) = 0;         // 客户区坐标（M1 仅记录状态）
    virtual bool GetKeyDown(KeyCode) const = 0;                      // 本帧按下沿
    virtual bool GetKeyUp(KeyCode) const = 0;                        // 本帧抬起沿
    virtual bool GetKey(KeyCode) const = 0;                          // 当前按住
    virtual double GetAxis(const std::string& name) const = 0;       // Horizontal/Vertical（WASD+方向键）
    virtual bool GetButton(const std::string& action) const = 0;     // Fire1=Space/Jump=Space/Submit=Enter/Cancel=Escape
    // t2（API 直觉化）：鼠标便利（窗口事件累计——按住状态跨帧保持；EndFrame 不清）
    virtual void OnMouseButton(double x, double y, int button, bool down) = 0;   // 3 键回调桥入（0=左 1=中 2=右——t136 窗口事件）
    virtual bool GetMouseButton(int button) const = 0;               // 当前按住（0=左 1=中 2=右；越界=假）
    virtual double GetMouseX() const = 0;                            // 当前客户区 x
    virtual double GetMouseY() const = 0;                            // 当前客户区 y
    // t1：鼠标边沿/滚轮（窗口事件桥入——本帧沿/累计语义；EndFrame 清——与 key_down 同式）
    virtual void OnMouseWheel(double x, double y, int delta) = 0;    // 滚轮回调桥入（delta=±1 每格；本帧累计）
    virtual bool GetMouseButtonDown(int button) const = 0;           // 本帧按下沿（EndFrame 清；越界=假）
    virtual bool GetMouseButtonUp(int button) const = 0;             // 本帧抬起沿（EndFrame 清；越界=假）
    virtual double GetMouseWheelDelta() const = 0;                   // 本帧累计滚轮格数（EndFrame 清）
    virtual void EndFrame() = 0;                                     // 帧尾清边沿
};

std::unique_ptr<Input> CreateWin32Input();   // 默认 Win32 实现（键码=VK 直通）

} // namespace HybridEngine::Platform
