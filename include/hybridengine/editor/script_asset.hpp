#pragma once
#include "hybridengine/core/assets/asset_library.hpp"
#include <string>

// 编程文件资产（.py/.cs——文本脚本；C 章「编程文件」分类）
//   - 经 AssetLibrary 外部类型表注册：".py"→PythonScript、".cs"→CSharpScript
//   - 工厂=文本直读（bytes→text）——仅元数据视图（列表/图标/Inspector）；不接脚本执行层
namespace HybridEngine::Editor {

struct ScriptAsset : HybridEngine::Core::Assets::AssetBase {
    std::string text;   // 脚本源码（模板写入后=模板内容）
};

// 注册 .py/.cs 外部类型（幂等——EditorApp 构造/测试入口调用一次）
void RegisterScriptAssetTypes();

} // namespace HybridEngine::Editor
