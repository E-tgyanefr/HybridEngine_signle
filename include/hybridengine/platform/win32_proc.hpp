#pragma once
// Windows 动态符号解析助手（bind / platform / plugin 共用）。
//
// 为什么需要它：
//   `GetProcAddress` 的原型是 `FARPROC (*)()`（不接受参数的函数指针）。把它转成真实签名时
//   必然要做"函数指针类型不兼容"的转换——GCC 的 `-Wcast-function-type` 会对每一处调用点告警
//   （本项目 dev/perf/release 三链共 5 处）。这是 Win32 API 的固有形态，不是项目缺陷，
//   但 0 警告门禁不允许散落的告警。
//
// 做法：把转换收敛在**这一处**并附上契约说明，调用点统一走 `GetProcAddr<T>(module, name)`。
//   转换的正当性来自 Win32 契约：`GetProcAddress` 返回的地址就是 `name` 对应函数的真实入口，
//   签名由调用方按同名 API 文档保证（本项目各处均按官方签名声明 typedef）。
//
// 实现用 `reinterpret_cast`（C++ 标准不允许 void* ⇄ 函数指针的隐式/静态转换，只有
//   reinterpret_cast 被允许），并用 GCC diagnostic 局部抑制——**抑制范围仅限本头文件**，
//   绝不扩散到调用点。
//
// 非 Windows 平台：本头文件整体为空（调用点本身都在 `#if defined(_WIN32)` 内）。
#if defined(_WIN32)
#include <windows.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif

namespace HybridEngine::Platform {

// 按真实签名取符号：T = 目标函数指针类型（如 `int (*)(void*, int, void**)`）
template <typename T>
inline T GetProcAddr(HMODULE module, const char* name) {
    return reinterpret_cast<T>(reinterpret_cast<void*>(::GetProcAddress(module, name)));
}

} // namespace HybridEngine::Platform

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#endif // _WIN32
