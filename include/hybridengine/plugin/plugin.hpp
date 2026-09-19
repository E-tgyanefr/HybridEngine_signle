#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace HybridEngine::App { class Engine; }

// ============ plug：引擎插件机制（主流引擎惯例——功能模块可插拔） ============
// 插件=命名模块（静态注册/动态 Dll 均可）；核心经 IPlugin 接口调用其生命周期与能力。
// 用法：
//   - 插件实现：PLUGIN_EXPORT extern "C" 构造描述（名称/版本/init/shutdown/create）
//   - 插件注册：RegisterPlugin()（静态链接）或 Dll 加载（PLUGIN_ENTRY）
//   - 引擎启动：Plugin::LoadAll()（init 序）→ 使用 QueryPlugins() 检索能力
// 红线不变：零第三方（win32 动态库/静态链接均系统能力）。

namespace HybridEngine::Plugin {

// 插件能力标记（位——插件可声明自身提供什么）
enum class Capability : uint32_t {
    AssetTypes = 1u << 0,     // 注册外部资产类型（模型/网格/材质…）
    SceneBuilder = 1u << 1,   // 场景构建（预设场景/演示装配）
    RenderExt = 1u << 2,      // 渲染扩展（后端/光栅器）
    Tools = 1u << 3,          // 工具/命令（编辑器集成）
};

// 插件描述（构造时填写——名称/版本/能力/生命周期）
struct PluginDesc {
    const char* name = nullptr;          // 插件名（唯一标识）
    const char* version = "1.0";         // 语义版本
    uint32_t capabilities = 0;           // Capability 位组合
    std::string(*getDescription)() = nullptr;   // 描述文案（UI/日志——可空）

    // —— 生命周期（可空=无钩子）——
    bool (*init)(HybridEngine::App::Engine&) = nullptr;       // 引擎创建后调用（注册资产类型/初始化）
    void (*shutdown)() = nullptr;                             // 引擎释放前（清理/注销）

    // —— 能力入口（按 capability 声明检索）——
    bool (*buildScene)(HybridEngine::App::Engine&, const char* preset) = nullptr;   // SceneBuilder：构建预设场景
    // t4：SceneBuilder 预设名数组（nullptr 结尾；可空=无预设——编辑器插件弹层「一键装配」按钮源）
    const char* const* presets = nullptr;
};

// —— 注册表（进程内；静态+Dll 共用）——
// 静态链接用法：插件内 `static bool s_r = RegisterPlugin(desc);`
bool RegisterPlugin(const PluginDesc& desc);
const PluginDesc* FindPlugin(const char* name);
std::vector<const PluginDesc*> QueryPlugins(uint32_t capMask);   // 按能力位检索
int PluginCount();

// 引擎启动统一加载（init 序=注册序）；返回成功数
int LoadAll(HybridEngine::App::Engine& engine);

// —— 动态插件加载（Win32 Dll——独立分发；零第三方：LoadLibrary/GetProcAddress 系统 API）——
// Dll 约定：导出 extern "C" const PluginDesc* <name>_plugin_desc()（插件自带入口）
// 加载流程：LoadLibrary → GetProcAddress "<basename>_plugin_desc" → RegisterPlugin（插件自身 init 钩子）
// 返回：0=成功；<0=错误（-1=文件加载失败 -2=无入口 -3=注册失败）；重复加载=0（幂等）
int LoadDll(const char* path);

} // namespace HybridEngine::Plugin
