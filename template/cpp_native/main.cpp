// ============================================================================
// HybridEngine —— 原生 C++ 消费者模板（Native C++ consumer template）
//
// 与 `template/cpp`（走稳定 C ABI，`ms_bind.h`）的区别：
//   本模板走**完整 C++ 公共 API**（App::Engine / Core::Component / Platform::IRenderer /
//   Editor 字体 / engine_demo 黄金帧），并以 SDK 的 **静态库** 链接。
//   适用于"把引擎当库来写原生游戏"的形态（例如同仓库的 HybridProjects/STG弹幕射击Native）。
//
// 为什么要有这个模板：SDK 原先只展示 C ABI 用法，**从未验证过"原生 C++ 消费者能否只拿 SDK
//   构建"** —— 实测那条路会缺 editor 静态库、缺公共头、并在扁平 lib/ 上遇到单遍扫描链接失败。
//   本模板把那条路径钉死：能编、能跑、且自证与引擎黄金帧逐位一致。
//
// 自证（关键）：本模板直接调用引擎**公共头**里的 `App::RenderDemoScene` + `App::DemoFrameHash`，
//   所以它算出的哈希必须等于引擎三链冻结的黄金帧 `4634E387E024BE90`。
//   → 一次运行同时证明：① SDK 头/库自足 ② 链接正确 ③ 引擎的确定性契约在 SDK 交付形态下成立。
//
// 构建（在 EngineSDK 根目录旁新建工程）：
//   cmake -S . -B build -G Ninja -DENGINE_SDK=<EngineSDK 路径>
//   cmake --build build
//   build\app\template_cpp_native.exe         # 期望 exit 0 且输出 GOLDEN OK
//
// ⚠ 产物在 `build\app\` 下，**不是** `build\`：本模板的 CMakeLists 设了
//   `RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/app"`（与仓库其它工具的产物摆放一致）。
//   此处注释原先写成 `build\template_cpp_native.exe`，照抄会"找不到 exe"
//   ——2026-09-18 核对 release.ps1 模板自检的实际调用路径后更正。
// ============================================================================
#include "hybridengine/app/engine.hpp"      // App::Engine（顶层门面：含 Core/Platform）
#include "hybridengine/app/engine_demo.hpp" // RenderDemoScene / DemoFrameHash（公共头，非内部）
#include "hybridengine/core/core.hpp"       // Core：Scene/SceneObject/Component/Transform
#include "hybridengine/platform/renderer.hpp"
#include "hybridengine/platform/window.hpp" // 仅取常量/接口类型（本模板不开窗口）

#include <cstdint>
#include <cstdio>

namespace {

// 组件八回调（Awake/OnEnable/Start/FixedUpdate/Update/LateUpdate/OnDisable/OnDestroy）
// —— 这是"用引擎写游戏"的最小可运行单元。
class Spinner : public HybridEngine::Core::Component {
public:
    double angle = 0.0;
    int updates = 0;
    void Start() override { std::printf("[native] Spinner::Start()\n"); }
    void Update(double dt) override {
        angle += dt * 90.0;   // 每秒 90°
        ++updates;
    }
};

} // namespace

int main() {
    using namespace HybridEngine;

    std::printf("HybridEngine native C++ template (full public API + SDK static libs)\n");

    // ① 引擎（离屏：Tick/Render 照常，Pump 恒 0 —— 与黄金帧/CI 同形态，无需窗口/显卡）
    App::Engine engine(App::EngineDesc{.title = "template_cpp_native", .windowed = false});
    std::printf("[native] Engine ctor OK (headless)\n");

    // ② 场景 + 组件（生命周期由引擎驱动）
    auto* go = engine.Scene().AddRoot("Player");
    auto* spinner = go->AddComponent<Spinner>();
    go->GetTransform()->SetPosition({1.0, 2.0, 3.0});

    // ③ 跑 120 帧固定步长（1/60s）
    for (int i = 0; i < 120; ++i) engine.Tick(1.0 / 60.0);
    std::printf("[native] 120 ticks OK: updates=%d angle=%.2f\n", spinner->updates, spinner->angle);

    // ④ 确定性自证：用引擎公共头的黄金帧场景 + 哈希，比对冻结值
    Platform::IRenderer* face = engine.Renderer();   // parity 面（恒 1280x800）
    if (!face) { std::fprintf(stderr, "[native] FAIL: Renderer() == null\n"); return 2; }
    App::RenderDemoScene(face);
    const uint64_t h = App::DemoFrameHash(face->Frame(),
                                         (size_t)App::kDemoWidth * (size_t)App::kDemoHeight);
    constexpr uint64_t kGolden = 0x4634E387E024BE90ULL;
    std::printf("[native] frame-hash = %016llX (golden=%016llX)\n",
                (unsigned long long)h, (unsigned long long)kGolden);

    if (h != kGolden) {
        std::fprintf(stderr, "[native] FAIL: 与黄金帧不一致 —— SDK 交付形态下确定性契约被破坏\n");
        return 1;
    }
    std::printf("[native] GOLDEN OK —— SDK 头/库自足、链接正确、确定性契约成立\n");
    return 0;
}
