#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace HybridEngine::Editor {

// M3.1：EditorLog（引擎+编辑器共用日志——环形 512 条；M3-f：不做独立日志系统）
class EditorLog {
public:
    enum class Level : int { Info = 0, Warn = 1, Error = 2 };
    struct Entry { Level level; std::string text; double timeMs; };

    static void Write(Level level, const std::string& text);
    static const std::vector<Entry>& Buffer();
    static void Clear();
    // 过滤视图（按 level≥minLevel）
    static size_t Count(Level minLevel);
    static Entry At(size_t i, Level minLevel);   // 过滤后索引
    static Level LevelOf(Entry e) { return e.level; }
private:
    static std::vector<Entry>& Backing();
};

} // namespace HybridEngine::Editor