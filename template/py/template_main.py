# ============================================================================
# HybridEngine EngineSDK — 最小消费模板（Python / hybridengine 包接线）
#
# 【怎么接一个项目】三步（三行环境变量缺一不可——见 EngineSDK\docs\绑定层.md §4.1）：
#   1. 解压 EngineSDK\python\hybridengine.zip 到任意目录（本脚本自动用 SDK 内的 python 目录）；
#   2. 设置环境变量（或直接 python template_main.py --frames 120——脚本自带的默认即 SDK 默认路径）；
#   3. 期望输出 drawHash=... stable=ok nonempty=ok，退出码 0。
#
# 【模板内容】空壳=空白窗口 + 一帧矩形 + 一行文本。不含任何游戏内容。
#   自检：静态内容 → frame60 与 frame120 哈希一致（确定性）+ 非空（≠无绘制黄金帧 4634E387E024BE90）。
#
# 【绑定层说明】（hybridengine——ctypes 零第三方，同一 ms_bind.h C ABI；与 C# 对称）：
#   - engine.on_render(draw) = 客户端绘制钩子（DrawCtx）；run_frame(dt) 自动帧序；
#   - 颜色 = int 0xAARRGGBB（A=alpha；默认 FF=不透明）；文本 = draw_text UTF-8（CJK 直支持）；
#   - 单线程：创建引擎的线程=主线程；确定性 = 同命令序列 → 同 render_hash。
# ============================================================================
import os
import sys

SDK = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

# 接线①：包根（把 SDK python 目录插到 import 路径——hybridengine 包已由 release.ps1 解压于此）
sys.path.insert(0, os.path.join(SDK, "python"))
# 接线②③：原生 DLL + mingw 运行时（EngineSDK\bin 已含 hybridengine.dll + mingw 运行时三件套）
os.environ.setdefault("HYBRIDENGINE_DLL", os.path.join(SDK, "bin", "hybridengine.dll"))
os.environ.setdefault("HYBRIDENGINE_MINGW", os.path.join(SDK, "bin"))

from hybridengine import GameEngine  # noqa: E402

GOLDEN = 0x4634E387E024BE90   # 无绘制基线（黄金帧）


def parse_frames(argv):
    frames = 120
    for i, a in enumerate(argv):
        if a == "--frames" and i + 1 < len(argv):
            frames = int(argv[i + 1])
    return frames


def main() -> int:
    frames = parse_frames(sys.argv[1:])

    def draw_gui(draw):
        draw.clear(0xFF12161E)                                     # 整帧清色（空白窗口底）
        draw.fill_rect(80, 60, 320, 180, 0xFF3478DC)               # 一帧矩形
        draw.draw_text("HybridEngine SDK Template", 80, 300, 40, 0xFFFFFFFF)   # 一行文本

    h_mid = h_last = 0
    with GameEngine(title="EngineSDK template py", width=1280, height=800) as engine:
        engine.on_render = draw_gui
        for i in range(frames):
            engine.run_frame(1.0 / 60.0)
            if i == frames // 2:
                h_mid = engine.render_hash
            if i == frames - 1:
                h_last = engine.render_hash

    stable = h_mid == h_last        # 静态内容 → 帧间确定性
    nonempty = h_last != GOLDEN     # ≠ 无绘制基线
    print(f"drawHash={h_last:016X} stable={'ok' if stable else 'FAIL'} "
          f"nonempty={'ok' if nonempty else 'FAIL'}")
    return 0 if (stable and nonempty) else 1


if __name__ == "__main__":
    sys.exit(main())
