#pragma once
#include <string>
#include <vector>

namespace HybridEngine::Editor::Project {

// ===== 项目生命周期（.hproj——主菜单/编辑器共用：B 章） =====
// .hproj = JSON：{ name, guid, root, createdAt, lastScene, lastOpenedAt }
//   - root：项目目录绝对路径（内含 Assets/——AssetLibrary::SetProjectRoot 目标）
//   - lastScene：当前场景名（""=从未保存场景；"未命名"=未命名标记——重开时按文件名解析
//     "Assets/<lastScene>.mscene"，不存在=回退默认 3D 场景）
//   - lastOpenedAt：最近打开时间戳（"YYYY-MM-DD HH:MM:SS"——启动器列表排序/显示）

struct ProjectInfo {
    std::string name;
    std::string guid;
    std::string root;          // 项目目录绝对路径
    std::string createdAt;
    std::string lastScene;
    std::string lastOpenedAt;
    std::string path;          // .hproj 文件绝对路径（非 JSON 字段——运行时信息）
    bool loaded = false;       // ReadProjectFile 成功=基础字段有效
};

// 默认项目扫描目录（主菜单默认 D:\HybridProjects；--projects <dir> 可覆盖）
std::string DefaultProjectsDir();

// 时间戳（本地 "YYYY-MM-DD HH:MM:SS"——显示/排序用）
std::string NowStamp();

// GUID（FNV-1a 64 hex——内容无关；guid 唯一性=name+时间戳+随机种子）
std::string MakeGuid(const std::string& seed);

// 写 .hproj（JSON：name/guid/root/createdAt/lastScene/lastOpenedAt——msjson 保序；目录自动创建）
bool WriteProjectFile(const std::string& hprojPath, const ProjectInfo& p);

// 读 .hproj（容错：缺字段=空串；非 JSON=loaded=false）
bool ReadProjectFile(const std::string& hprojPath, ProjectInfo& out);

// 扫描目录下全部 .hproj（按 lastOpenedAt 降序——无时间=排后；再按 name 稳定）
std::vector<ProjectInfo> ScanProjects(const std::string& projectsDir);

// 项目名合法性（用于新建：非空/不含非法字符/不以点开头）
bool ValidProjectName(const std::string& name, std::string& why);

} // namespace HybridEngine::Editor::Project
