#pragma once
#include "hybridengine/editor/utf8_text.hpp"   // t-text：UTF-8 码点工具（编辑器公共层）
#include <cstdint>
#include <string>
#include <vector>

namespace HybridEngine::Editor {

// 脚本文本缓冲：**按行存储** + 光标（行/码点列）+ 编辑操作。
//
// 设计取舍：
//  · 行存储（而非单一字符串）——绘制/词法/滚动都按行走，行内查找是 O(行内容)；
//    代码文件行数有限，够用且实现简单。
//  · 列一律**码点下标**（与 UTF-8 字节解耦）；Tab 一律展开为 kTabWidth 个空格，
//    于是「码点列 == 显示列」这个不变量成立（否则绘制时还要算 Tab 停靠位）。
//  · 换行统一 LF：载入时把 CRLF/CR 归一，去掉 BOM。
class ScriptBuffer {
public:
    static constexpr int kTabWidth = 4;

    void SetText(const std::string& utf8);      // 归一换行 + 去 BOM + 光标归零 + dirty=false
    std::string Text() const;                   // 各行 join("\n")（保留末尾空行的语义）
    void Clear();

    bool Dirty() const { return dirty_; }
    void MarkSaved() { dirty_ = false; }
    void MarkDirty() { dirty_ = true; }
    // t-perf-lex：内容修订号——高亮面板用它判断「需要重新词法」；光标/滚动/选区变化不递增。
    // ChangedFromLine=自上次消费以来影响的最早行（保守下界，用于增量重词法），消费后 Clear。
    uint64_t Revision() const { return revision_; }
    int ChangedFromLine() const { return changedFromLine_; }
    void ClearChangedFromLine() { changedFromLine_ = kNoLineChange; }

    int LineCount() const { return (int)lines_.size(); }
    const std::string& Line(int i) const;       // 越界=静态空串（调用方无需判空）
    int LineCpCount(int i) const;               // 该行码点数（列上界）

    int CaretLine() const { return caretLine_; }
    int CaretCol() const { return caretCol_; }
    int PreferredCol() const { return preferredCol_; }
    void SetCaret(int line, int col);           // 钳制到合法范围；清 preferredCol

    // t-vs（2026-09-18）：直接移动光标相对位置（不清选区、不碰记忆列以外的状态）。
    // 用途=**智能插入**：自动补全 `()`/`""` 后要把光标挪到中间，而 SetCaret 会清 preferredCol、
    //   也不表达"相对"语义。返回是否真的动了（越界不动的判据）。
    bool MoveCaret(int dCol);
    // t-vs：窥视光标**相对位置的码点**（0=该处没有字符）。
    // 用途=括号/引号智能语义：① 右侧已是同一个闭合符 → 跳过而不是再补一个；
    //   ② 回车时判断"是否正处在 `{|}` 之间" → 展开成三行。只读、不改状态。
    uint32_t PeekCaret(int dCol) const;

    // 光标移动（dCol 可跨行折返——左右键语义）；上下键保持「记忆列」
    void MoveLeft(bool ctrl = false);
    void MoveRight(bool ctrl = false);
    void MoveUp(int n = 1);
    void MoveDown(int n = 1);
    void MoveHome(bool ctrl = false);
    void MoveEnd(bool ctrl = false);
    void MovePage(int pages, int pageLines);
    void MoveDocStart();
    void MoveDocEnd();

    // —— 编辑（返回 true=内容有变化；均置 dirty）——
    bool InsertText(const std::string& utf8);   // 含 \n 可多行（粘贴路径）
    bool InsertCodepoint(uint32_t cp);          // 单字符（WM_CHAR 路径）；\r 忽略、\n 拆行、\t 展开
    bool Backspace();                           // 行首=与上一行合并
    bool Delete();                              // 行尾=与下一行合并
    // 回车。t-vs：**自动缩进**（新行继承本行前导空白）+ 在 `{|}` 之间回车自动展开为三行。
    bool SplitLine();
    bool IndentLine(bool outdent);              // Tab / Shift+Tab：行首增删一级缩进
    bool InsertLineAt(int index, const std::string& text);   // 测试/工具直插
    // t-vs：本行前导空白（空格与 Tab 视作同级——Tab 已在 InsertCodepoint 展开为空格，
    //   但历史文件里可能真有 '\t'，故两者都算）。返回**空格数**（Tab 计 kTabWidth）。
    int LineIndent(int line) const;
    // t-vs：本行前导空白段的**字节长度**（用于"整体反缩进"时精确删除）
    int LineIndentBytes(int line) const;

    void SelectAll() { selAnchorLine_ = 0; selAnchorCol_ = 0; caretLine_ = (int)lines_.size() - 1;
                       caretCol_ = LineCpCount(caretLine_); preferredCol_ = -1; }
    // t-ux：直接设置选区（双击选词/宿主驱动）——锚点=起点，光标=终点。
    void SetSelection(int anchorLine, int anchorCol, int caretLine, int caretCol) {
        selAnchorLine_ = anchorLine;
        selAnchorCol_ = anchorCol;
        caretLine_ = caretLine;
        caretCol_ = caretCol;
        preferredCol_ = -1;
        ClampCaret();
    }
    void ClearSelection() { selAnchorLine_ = -1; }
    bool HasSelection() const { return selAnchorLine_ >= 0; }
    // t-edit：Shift+移动 的选区扩展——首次扩展时把锚点钉在当前光标，之后光标移动即改选区。
    // 没有它就只能「先选中再打字」以外的编辑动作全做不了（Shift+方向/Home/End 是编辑器基本操作）。
    void BeginOrKeepSelectionAnchor() { if (selAnchorLine_ < 0) { selAnchorLine_ = caretLine_; selAnchorCol_ = caretCol_; } }
    // 选区的规范化区间（[start,end)，行列序）——无选区时 start==end==caret
    void SelectionRange(int& sl, int& sc, int& el, int& ec) const;
    // t-clip：选中文本（UTF-8，多行用 \\n 连接）。无选区返回空串。
    // 复制/剪切走这里；**码点列**→字节偏移用 utf8_text 的 Utf8ByteOffset（中文不会被切半个字）。
    std::string SelectedText() const;
    bool DeleteSelection();

    // ============ t-undo（2026-09-18）：撤销/重做 ============
    // 为什么必须补：脚本页此前**完全没有撤销**（编辑器级的命令栈只管场景操作，不管文本）。
    //   写代码不能撤销 = 不敢改，这是"能不能当编辑器用"的门槛，不是锦上添花。
    // 设计：**快照式**（整份文本 + 光标/选区），而不是按操作类型逐条实现逆操作。
    //   理由：文本编辑的操作种类多（插入/删除/换行/缩进/包裹/粘贴/大小写…），逐条写逆操作
    //   既易漏又易错；快照的正确性只依赖"存取一致"，且与所有现有编辑路径天然兼容
    //   （不需要给每个编辑函数都补一个逆函数）。
    // 代价与边界：每步存一份 `vector<string>`。**上限 kUndoDepth 步**（超出丢最旧），
    //   并在超长文档上退化（单行极长的文件会占内存）——这是刻意取舍：正确性优先，
    //   且编辑器的文档量级（脚本文件）远小于这个上限的痛感。
    // 合并（coalesce）：连续**输入单个字符且无可合并间隔**时不新开一步——
    //   否则打 20 个字要按 20 次 Ctrl+Z（与 VS/VS Code 的"输入合并"一致）。
    //   由调用方（面板）在编辑前置调用 `MaybeSnapshotTyping` 表达这个意图。
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    bool Undo();     // 恢复上一步（返回 false=没有可撤销的）
    bool Redo();     // 重做（返回 false=没有可重做的）
    void ClearHistory();   // SetText（新文件）时调用——跨文件不留历史
    size_t UndoDepth() const { return undo_.size(); }
    const std::string& UndoTopText() const;   // 诊断/测试：栈顶快照文本（空栈=静态空串）
    void PushHistory();                       // 显式存一步（面板在每次编辑前调用）
    void NotifyEdited();                      // 编辑后：新编辑作废 redo 栈

private:
    std::vector<std::string> lines_{std::string()};
    int caretLine_ = 0;
    int caretCol_ = 0;          // 码点下标
    int preferredCol_ = -1;     // 上下键记忆列（-1=无）
    int selAnchorLine_ = -1;    // 选区锚点（-1=无选区）
    int selAnchorCol_ = 0;
    bool dirty_ = false;
    uint64_t revision_ = 0;
    static constexpr int kNoLineChange = 1 << 30;
    int changedFromLine_ = 0;   // 0=最早（SetText）；具体行号=增量重词法起点

    void Touch(int line);       // ++revision_ + dirty + 记最早变更行
    void ClampCaret();
    int  ClampCol(int line, int col) const;

    // —— t-undo：历史（快照式；见上面 public 段的说明）——
    struct Snapshot {
        std::vector<std::string> lines;
        int caretLine = 0, caretCol = 0;
        int selAnchorLine = -1, selAnchorCol = 0;
    };
    static constexpr size_t kUndoDepth = 200;   // 步数上限（超出丢最旧）
    Snapshot TakeSnapshot() const;
    void RestoreSnapshot(const Snapshot& s);
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
};
} // namespace HybridEngine::Editor
