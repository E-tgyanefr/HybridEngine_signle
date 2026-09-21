# HybridEngine EngineSDK

引擎侧交付包（游戏编写/移植 = 另一团队的工程——本包只含引擎 ABI 与接线壳）。

> 作者/著作权人：**QDR-team**（GitHub: E-tgyanefr）。本包为**源码可见的专有软件**：
> 允许浏览/克隆/fork/评估/链接使用，并允许**自由发布你用本包做出的产品**；
> 但**不得再分发 SDK 本身**。完整条款见仓库 `LICENSE`。

## 目录
| 路径 | 说明 |
|---|---|
| `include/hybridengine/**` | 全部公共头（**54 个**）。`bind/ms_bind.h` = 稳定 C ABI；其余 = 原生 C++ API |
| `lib/libhybridengine_*.a` | 静态库全链（app/core/platform/render3d/**editor**/plugin/bind + DLL 导入库） |
| `bin/hybridengine.dll` | C ABI 共享库（C#/Python 运行时共用） |
| `bin/PackPlayer.exe` | 打包谱面的原生播放器模板（1.2MB；见下） |
| `dotnet/HybridEngine.Bind/` | C# 绑定层源码（net8.0 纯 BCL） |
| `python/hybridengine/` + `hybridengine.zip` | Python 绑定包（ctypes）+ zip 存档 |
| `template/cpp` | C++ 最小模板（**稳定 C ABI** 用法） |
| `template/cpp_native` | C++ 原生模板（**完整 C++ 公共 API** + 静态库；自证黄金帧） |
| `template/cs` `template/py` | C# / Python 最小模板 |
| `docs/绑定层.md` | 绑定层文档快照 |
| `abi.txt` | ABI 指纹（头 sha256 + PE 导出表） |
| `VERSION.txt` | 版本戳（**sdk 版本号**/commit/构建日期/矩阵） |

> **本包版本：`0.5.0`**（引擎项目版本，取自引擎 `CMakeLists.txt` 的 `project(... VERSION ...)`）。
> `abi.txt` 里的两个 sha256（公共头 + DLL 字节）是**兼容性判据**——版本号用于沟通，指纹用于校验。

## 一分钟接一个项目（四选一）
```powershell
# 先把 SDK 根告诉模板（四个模板都认这个环境变量；也可用各模板内的 CMake -DENGINE_SDK=... 覆盖）
$env:HYBRIDENGINE_SDK = '<EngineSDK 根目录>'      # 例如 D:\EngineSDK

# C++（稳定 C ABI）：template\cpp        —— 只 include ms_bind.h，链接 libhybridengine_bind.a
# C++（原生 API）：  template\cpp_native —— include 完整公共头，用 App::Engine/Component/IRenderer
#   两者都必须用 -Wl,--start-group ... -Wl,--end-group 包住引擎静态库（库间互相依赖，
#   ld 对归档单遍扫描，扁平 lib\ 下顺序不保证），系统库给 user32 gdi32 winmm d3d11 dxgi d3dcompiler
# C#：复制 template\cs —— ProjectReference → EngineSDK\dotnet\HybridEngine.Bind；原生 dll 自动复制
# Python：三行环境变量（见 docs\绑定层.md §4.1）：PYTHONPATH=EngineSDK\python  HYBRIDENGINE_DLL=EngineSDK\bin\hybridengine.dll  HYBRIDENGINE_MINGW=EngineSDK\bin
```

> ⚠ **`template\cs` 与 `template\py` 想拷到别处用，必须靠上面的 `HYBRIDENGINE_SDK`**
> （或在原地开发）：它们要回头找 `EngineSDK\dotnet\HybridEngine.Bind` 与 `EngineSDK\python`，
> 跨目录拷走后相对路径就断了。两个 C++ 模板只依赖 `-DENGINE_SDK`/环境变量，随便拷。

## 运行时的 DLL 需要
- **两个 C++ 模板静态链引擎**：产出的 exe **不导入** `hybridengine.dll`（`objdump -p` 可验）；
  真正需要的是 **MinGW 运行时**（`libgcc_s_seh-1.dll`、`libstdc++-6.dll`，native 模板另有 `libwinpthread-1.dll`），
  它们就在 `bin\` 里——把 `EngineSDK\bin` 加进 `PATH`，或把这几个 dll 复制到 exe 旁。
- **C# / Python 消费者才需要 `hybridengine.dll`**（P/Invoke 与 ctypes 直接加载它）：
  C# 模板的 MSBuild target 会自动把它复制到输出目录；Python 用 `HYBRIDENGINE_DLL` 指路。

## 打包谱面（单文件 exe）
`bin\PackPlayer.exe` = 谱面导出模板。打包器把它**原样复制**再尾部追加谱面数据：
`[payload][int64 len]["MILSTPK1"]` → `<曲名>.exe`（双击即播）。
它取代了旧的 .NET 自包含模板（64.5MB → 1.2MB，玩家机零 .NET 依赖）。

## 契约要点（红线）
- 单线程：创建引擎的线程 = 主线程；跨线程调用 = `MS_ERR_THREAD`；Windows 消息泵必须同线程。
- 句柄 = C++ 对象（`ms_engine_destroy` 释放；调用方不 free；跨语言 `Dispose`/`with` 对称）。
- 颜色 = `uint32_t 0xAARRGGBB`（A=alpha；A=FF=不透明=既有行为；帧输出恒 0xFFRRGGBB）。
- 文本 = UTF-8（`ms_text_draw`/`draw_text`/`DrawText`——GDI 白掩码光栅，CJK 直支持）。
- 确定性 = 同命令序列 → 同 render_hash（黄金帧 `4634E387E024BE90` = 无绘制基线；模板自检以此为基线）。
