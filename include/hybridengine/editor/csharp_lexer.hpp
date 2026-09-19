#pragma once
#include <string>
#include <vector>

namespace HybridEngine::Editor {

// C# 轻量词法器（为**语法高亮**服务，不是编译器前端）。
//
// 定位与取舍：
//  · 目标是「一眼能读」的着色，不做语义分析：不解析泛型/表达式/类型推断。
//  · 但**跨行状态必须正确**——块注释 `/* */`、逐字字符串 `@"…"`、原始字符串 `"""…"""`
//    会跨行，所以逐行词法必须携带并更新状态；否则一旦进入块注释，后面整个文件都会花掉。
//  · Token 的 start/length 是**字节**偏移，且边界一定落在码点首字节上
//    （扫描时非 ASCII 字节一律并入标识符/字符串/注释），因此绘制端按字节切片是安全的。
//  · 覆盖整行不重叠：所有 token 之和 = 行字节长度（有测试守这条不变量）。
enum class TokenKind {
    Plain = 0,     // 普通标识符/空白/运算符
    Keyword,       // 语言关键字 + 内建类型名（int/string/void/bool…）
    Type,          // class/struct/interface/enum/new/typeof 之后的类型名（语法感知，非首字母猜大小写）
    String,        // "…" / @"…" / $"" / '…' / """…"""
    Comment,       // // … 与 /* … */（含 /// 文档注释）
    Number,        // 123 / 0x1F / 0b1010 / 1.5f / 1_000
    Preprocessor   // 行首 # 指令
};

struct Token {
    int start = 0;        // 字节偏移
    int length = 0;
    TokenKind kind = TokenKind::Plain;
};

class CSharpLexer {
public:
    // 跨行状态（调用方按行顺序传入同一个 State）
    struct State {
        bool inBlockComment = false;
        bool inVerbatimString = false;   // @"…" 或 $@"…"（含原始字符串 """…"""）
        int  rawQuoteRun = 0;            // 原始字符串的引号个数（≥3）；0=非原始字符串
    };

    /// 对一行做词法；state 进出一致更新。返回的 token 覆盖整行、按 start 升序、互不重叠。
    static std::vector<Token> LexLine(const std::string& line, State& state);

    /// 便捷：整段文本逐行词法（测试/整文件染色用）
    static std::vector<std::vector<Token>> LexAll(const std::string& text);

    /// 关键字判定（含内建类型名与上下文关键字）
    static bool IsKeyword(const std::string& word);
};

} // namespace HybridEngine::Editor
