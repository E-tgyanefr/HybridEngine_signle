// ============================================================================
// HybridEngine EngineSDK — 最小消费模板（C++ / C ABI 直接接线）
//
// 【怎么接一个项目】三步：
//   1. 复制本目录到你的工程（或直接原地开发）。
//   2. CMake 配置时传 ENGINE_SDK 指向 EngineSDK 根（默认 D:/EngineSDK）：
//        cmake -S . -B build -DENGINE_SDK=D:/EngineSDK
//        cmake --build build
//   3. 运行：把 EngineSDK\bin 加入 PATH（hybridengine.dll + mingw 运行时），然后
//        build\app\template_cpp.exe --frames 120
//      期望输出 drawHash=... stable=ok nonempty=ok，退出码 0。
//
// 【模板内容】空壳=空白窗口 + 一帧矩形 + 一行文本。不含任何游戏内容。
//   每帧：tick → ms_rnd_clear_list（帧首清列表）→ 录制命令 → ms_engine_render（回放+哈希）。
//   自检：静态内容 → frame60 与 frame120 哈希一致（确定性）+ 非空（≠无绘制黄金帧 4634E387E024BE90）。
//
// 【C ABI 说明】仅依赖 ms_bind.h（稳定 ABI——函数名/签名/错误码为契约）：
//   - 句柄 = C++ 对象：ms_engine_create/destroy；跨线程调用=MS_ERR_THREAD；
//   - 颜色 = uint32 0xAARRGGBB（A=alpha；A=FF 不透明=既有行为）；
//   - 文本 = UTF-8（ms_text_draw/ms_text_measure——GDI 白掩码光栅，CJK 直支持）；
//   - 单线程：创建引擎的线程 = 主线程（Windows 消息泵必须同线程）。
// ============================================================================
#include "hybridengine/bind/ms_bind.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// 从 argv 取 --frames N（默认 120）
int ParseFrames(int argc, char** argv, int def) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--frames") == 0) return std::atoi(argv[i + 1]);
    return def;
}

} // namespace

int main(int argc, char** argv) {
    const int frames = ParseFrames(argc, argv, 120);

    int err = 0;
    ms_engine* e = ms_engine_create("EngineSDK template cpp", 1280, 800, &err);
    if (!e) {
        std::printf("engine create failed (err=%d)\n", err);
        return 2;
    }

    const uint32_t BG = 0xFF12161E;        // 0xAARRGGBB（A=FF 不透明）
    const uint32_t BLUE = 0xFF3478DC;
    const uint32_t WHITE = 0xFFFFFFFF;

    uint64_t hMid = 0, hLast = 0;
    for (int i = 0; i < frames; ++i) {
        ms_engine_tick(e, 1.0 / 60.0);
        // 帧序（与 C#/Python 包装层一致）：帧首清录制列表 → 录制本帧命令 → render（回放+哈希）
        ms_rnd_clear_list(e);
        ms_rnd_clear(e, BG);                                   // 整帧清色（空白窗口底）
        ms_rnd_fill_rect(e, 80, 60, 320, 180, BLUE);           // 一帧矩形
        ms_text_draw(e, "HybridEngine SDK Template", 80, 300, 40, WHITE);   // 一行文本
        ms_engine_render(e, nullptr);
        const uint64_t h = ms_engine_render_hash(e);
        if (i == frames / 2) hMid = h;
        if (i == frames - 1) hLast = h;
    }
    ms_engine_destroy(e);

    const bool stable = (hMid == hLast);                       // 静态内容 → 帧间确定性
    const bool nonempty = (hLast != 0x4634E387E024BE90ULL);    // ≠ 无绘制基线（黄金帧）
    std::printf("drawHash=%016llX stable=%s nonempty=%s\n",
                (unsigned long long)hLast, stable ? "ok" : "FAIL", nonempty ? "ok" : "FAIL");
    return (stable && nonempty) ? 0 : 1;
}
