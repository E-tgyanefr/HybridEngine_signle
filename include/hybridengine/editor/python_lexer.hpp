#pragma once
#include "hybridengine/editor/csharp_lexer.hpp"   // 复用 Token/TokenKind（着色种类是同一套）
#include <string>
#include <vector>

namespace HybridEngine::Editor {

// Python 轻量词法器（为**语法高亮**服务，不是解释器/编译器前端）。
//
// 为什么不能直接复用 CSharpLexer：两种语言的「字符串与注释」规则根本不同——
//   · 注释符：Python 是 `#` 到行尾（C# 是 `//`、`/* */`）
//   · 字符串：Python 有 `'…'`/`"…"`/`'''…'''`/`"""…"""` + 前缀（r/b/f/u/rb/fr…）；
//     三引号**跨行**（C# 的跨行是 `@"…"` 与 `"""…"""`）——状态机不同，混用必然整片染错。
//   · 装饰器 `@name`（C# 是特性 `[Name]`）、`def`/`class` 后的名字、`self`、内建名。
//
// 覆盖与取舍：
//  · 三引号字符串跨行状态**必须**正确（否则一个未闭合的 `"""` 会把后面整个文件染成字符串色）。
//  · 单引号/双引号字符串**不允许**跨行（Python 语法如此）：行尾未闭合就按行结束复位，
//    这样一处笔误只影响一行，不会污染全文件。
//  · token 覆盖整行、不重叠、边界落在码点首字节（与 C# 词法同一不变量）。
class PythonLexer {
public:
    // 跨行状态：只有三引号字符串会跨行
    struct State {
        int tripleQuote = 0;        // 0=无；1=''' ；2="""（进入后直到再次遇到同种三引号才结束）
        bool raw = false;           // 当前三引号是否 raw（r"""…"""——反斜杠不转义）
    };

    /// 对一行做词法；state 进出一致更新。返回 token 覆盖整行、按 start 升序、互不重叠。
    static std::vector<Token> LexLine(const std::string& line, State& state);

    /// 便捷：整段文本逐行词法（测试用）
    static std::vector<std::vector<Token>> LexAll(const std::string& text);

    /// 关键字判定（Python 3 关键字 + True/False/None + 软关键字）
    static bool IsKeyword(const std::string& word);

    /// 内建名/常用类型（着色为 Type：int/str/print/len/self/…）
    static bool IsBuiltin(const std::string& word);
};

} // namespace HybridEngine::Editor
