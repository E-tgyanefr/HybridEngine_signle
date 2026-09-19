#pragma once
#include <memory>

namespace HybridEngine::Platform {

// ============ t8：手柄（Gamepad）输入 ============
// XInput 后端（xinput1_4.dll——Windows 系统组件；零第三方）。4 槽位（XInput 标准）。
// 边沿语义与 Keyboard 同式：ButtonDown/Up=本帧边沿（EndFrame 清）；Button=当前按住；
// Axis=模拟量（LX/LY/RX/RY=-1..1 含 deadzone 归零；LT/RT=0..1）。Poll=帧首采样
// （XInputGetState）——与 Input（窗口回调桥入）不同源：无窗口/游戏手柄均 headless 可查。
// 无 xinput1_4.dll=X 输入不可用（CreateWin32Gamepad 返回非空但 Connected 恒假——防御降级）。

// 按钮索引（XINPUT_GAMEPAD_* 位序——ABI/三语言同常量）
enum class GamepadButton : int {
    A = 0, B, X, Y,          // 0..3
    LB, RB,                  // 4..5
    Back, Start,             // 6..7
    LeftStick, RightStick,   // 8..9
    Count_                   // 10
};

// 轴索引（0..5）
enum class GamepadAxis : int {
    LX = 0, LY, RX, RY,      // -1..1（deadzone 归零）
    LT, RT,                  // 0..1
    Count_
};

class Gamepad {
public:
    virtual ~Gamepad() = default;
    virtual int Count() const = 0;                              // 槽位数（XInput=4）
    virtual void Poll() = 0;                                    // 帧首：XInputGetState 刷新（边沿累计）
    virtual bool Connected(int pad) const = 0;                  // pad 0..Count-1（越界=假）
    virtual bool Button(int pad, int button) const = 0;         // 当前按住
    virtual bool ButtonDown(int pad, int button) const = 0;     // 本帧按下沿（EndFrame 清）
    virtual bool ButtonUp(int pad, int button) const = 0;       // 本帧抬起沿（EndFrame 清）
    virtual double Axis(int pad, int axis) const = 0;           // LX/LY/RX/RY -1..1；LT/RT 0..1；越界=0
    virtual void EndFrame() = 0;                                // 帧尾清边沿（与 Input 同式）
};

// 默认 XInput 实现（nullptr=无 XInput——安全降级）
std::unique_ptr<Gamepad> CreateWin32Gamepad();

} // namespace HybridEngine::Platform
