#pragma once
#include <string>
#include <vector>

// ============================================================================
// shell.hpp —— 「调外部程序 / 系统剪贴板」的可移植抽象层
//
// 为什么需要它（2026-09-17 实测的 Linux 构建阻塞点）：
//   编辑器有两个文件**无条件**包含 `<windows.h>`——
//     · `src/editor/editor_app.cpp`（`ShellExecuteW` / `HINSTANCE` / `SearchPathA` /
//        `GetModuleFileNameW` / `SetEnvironmentVariableW` / `CreateProcessW`）
//     · `src/editor/script_editor_panel.cpp`（`OpenClipboard` / `CF_UNICODETEXT` / `HWND`）
//   它们又在 CMake 里**无条件编译**，所以 `cmake --preset unix`（以及 CI 的 build-linux job）
//   必然失败——而 README/构建指南当时声称"已支持 Linux/macOS 全量编译"。
//   本层把那两处的平台调用收进一组窄接口，Windows 实现走 Win32，非 Windows 实现走
//   `posix_spawn`/`popen` + `xclip`/`xsel`/`wl-paste`（可选），缺失时安全降级（返回 false / 空串）。
//
// 设计约束：
//   · 归 `hybridengine_platform`（editor 依赖它）——不引入新依赖，core 不反向依赖。
//   · 接口只谈"要做什么"，不泄漏任何 Win32 概念（`HINSTANCE`/`HWND`/`HGLOBAL` 一律不出现）。
//   · 失败**不抛异常**：全部返回 bool / 空串，由调用方决定日志话术（与既有行为一致）。
// ============================================================================

namespace HybridEngine::Platform {

// 环境变量里"路径列表"的分隔符：Windows = ';'，POSIX = ':'。
// 用于拼 PYTHONPATH 之类（写死 ';' 会让 POSIX 下的 PYTHONPATH 变成一整条无效路径）。
#if defined(_WIN32)
inline constexpr char kEnvPathSep = ';';
#else
inline constexpr char kEnvPathSep = ':';
#endif

// ---- 外部程序启动 ----------------------------------------------------------

// 用一个**已存在的可执行文件**（绝对路径）启动脚本，并把 argv 传给它。
// 用于「用外部 Python 运行 .py」。成功 = 已成功创建进程。
//   · Windows：`CreateProcessW`（全宽字符，中文路径必须）+ 可选环境变量覆盖 + `CREATE_NEW_CONSOLE`
//   · 非 Windows：`posix_spawn`（失败回退 `fork`+`execv`）+ 环境变量覆盖
// 参数一律 UTF-8 入、内部转平台原生编码。
bool ShellRunProcess(const std::string& exeAbsPath,
                     const std::vector<std::string>& args,
                     const std::vector<std::pair<std::string, std::string>>& envAdd,
                     std::string* errOut = nullptr);

// 交给**系统 shell 解析**一个目标（用于 Store 应用别名 / .desktop / 打开 URL）。
// `target` 可以是可执行名（如 "explorer.exe"）或 URL；`args` 为附加参数（原始串）。
//   · Windows：`ShellExecuteW("open", target, args)`；返回码 `> 32` 视为成功
//   · 非 Windows：`xdg-open`，失败再试 `open`（macOS）
bool ShellOpen(const std::string& target, const std::string& args = std::string(),
               std::string* errOut = nullptr);

// 在文件管理器里定位一个文件（"Reveal in Explorer"）。
//   · Windows：`explorer.exe /select,"<abs>"`
//   · 非 Windows：打开**所在目录**（xdg-open 无统一的"选中"语义）
bool ShellReveal(const std::string& absPath, std::string* errOut = nullptr);

// ---- 解释器 / 可执行文件查找 ----------------------------------------------

// 在 PATH 里查找可执行文件，返回**绝对路径**；找不到返回空串。
// `requireRealExe`：跳过"应用执行别名"这类 0 字节重解析点（Windows 特有概念；
//   非 Windows 上该参数无效果）。用于 `python.exe`——Store 别名能搜到但起不动。
std::string ShellFindExecutable(const std::string& name, bool requireRealExe = false,
                                std::string* skippedAliasOut = nullptr);

// 历史留档（Windows 实测）：Microsoft Store 的 `python.exe` 是**应用执行别名**
//   （0 字节重解析点），`CreateProcessW` 起不动它（能搜到、一启动就失败）。
//   故查找顺序是：真 `python.exe`（非 WindowsApps）→ `py.exe`（真 exe 启动器，配 `-3`）
//   → Store 别名（只能走 `ShellOpen` 由 shell 解析）。`IsStoreAliasPath` 用于第 4 步判定。
bool IsStoreAliasPath(const std::string& exePath);

// 本进程自身可执行文件的**绝对目录**（用于向上找仓库 `python/` 绑定目录）。
// 失败返回空串。内部走平台原生宽路径 API —— 历史坑：`GetModuleFileNameA` 在中文路径下
//   返回 ANSI(GBK) 字节，直接构造 `std::filesystem::path` 会抛 "Illegal byte sequence"。
std::string ShellExecutableDir();

// ---- 系统剪贴板（文本） ----------------------------------------------------

// 读系统剪贴板的文本（UTF-8）。不可用/被占用/非文本 → 空串（**不抛**）。
std::string ClipboardGetText();

// 写系统剪贴板的文本（UTF-8）。成功 = true。
//   · Windows：`CF_UNICODETEXT` + `GlobalAlloc`（成功后所有权归系统，不再 free）
//   · 非 Windows：`xclip -selection clipboard` / `xsel -b` / `wl-copy`（依次尝试）
bool ClipboardSetText(const std::string& utf8);

} // namespace HybridEngine::Platform
