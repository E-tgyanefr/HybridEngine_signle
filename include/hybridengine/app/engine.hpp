#pragma once
#include "hybridengine/core/scene.hpp"
#include "hybridengine/core/event_bus.hpp"
#include "hybridengine/core/time_singleton.hpp"
#include "hybridengine/core/assets/asset_library.hpp"
#include "hybridengine/platform/window.hpp"
#include "hybridengine/platform/renderer.hpp"
#include "hybridengine/render3d/render3d.hpp"   // M5：RenderStats（3D 统计——绑定层查询）
#include "hybridengine/platform/viewport.hpp"
#include "hybridengine/platform/input.hpp"
#include "hybridengine/platform/gamepad.hpp"   // t8：手柄（XInput——帧首 Poll/帧尾 EndFrame 同输入池）
#include "hybridengine/platform/audio_backend.hpp"
#include "hybridengine/platform/audio_clock.hpp"
#include "hybridengine/app/graph_asset.hpp"   // t-graph：节点图（连线式编程）运行时
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace HybridEngine::Render3D { class GpuRenderer; }   // m-gpu：前置（Renderer 实现隐藏）

namespace HybridEngine::App {

// t1：GUI 平面渲染命令（ms_rnd_* 录制——单帧容器；BlitRect/BlitAlpha 像素在录制时立即拷入=无悬垂；
// Text=掩码共享引用（t10 零拷贝——RuntimeFont 缓存引用计数 pin））
struct RndOp {
    enum class Kind : uint8_t {
        Clear = 0,        // 全屏清（IRenderer::Clear 语义）——color=清屏色
        ClearRect,        // v[0..3]=x,y,w,h（ClearRect=FillRect 同语义）
        FillRect,         // v[0..3]=x,y,w,h
        FillRoundedRect,  // v[0..4]=x,y,w,h,radius
        DrawLine,         // v[0..4]=x1,y1,x2,y2,thickness
        FillCircle,       // v[0..2]=cx,cy,r
        DrawCircle,       // v[0..3]=cx,cy,r,thickness
        FillTriangle,     // v[0..5]=x1,y1,x2,y2,x3,y3
        FillQuad,         // v[0..7]=x1,y1,x2,y2,x3,y3,x4,y4
        BlitRect,         // v[0..3]=x,y,w,h；src=录制时拷入像素（row-major，w×h）——不透明拷贝（背靠背）
        BlitAlpha,        // t1：v[0..3]=x,y,w,h；src=录制时拷入像素（row-major，w×h）——0xAARRGGBB 逐像素 straight-alpha 合成
        ClipPush,         // t6：v[0..3]=x,y,w,h（矩形裁剪入栈——回放映射 IRenderer::PushClip）
        ClipPushRotated,  // t-rot-clip：v[0..4]=cx,cy,w,h,angleRad（旋转矩形裁剪——斜劈/分离位移）
        ClipPop,          // t6：弹栈（回放映射 IRenderer::PopClip；空栈在录制层拒绝——不产生 op）
        Text,             // v[0..1]=x,y；src=录制时 GDI 光栅化白掩码像素（row-major，srcW×srcH——回放零系统调用=严格确定性）
        FillCircles,      // t-perf：**批量实心圆**（pts=每圆 3 个 double：x,y,r；color 共用）——弹幕/粒子负载用
        Bullets,          // t-sprite：**批量子弹**（pts=每颗 2 个 double：x,y；v[0]=r v[1]=线宽；color=核心 color2=外圈）
        DrawCircles       // t-perf：**批量圆环**（pts=每圆 3 个：x,y,r；v[0]=线宽；color 共用）
    };
    Kind kind = Kind::Clear;
    double v[8] = {};        // 紧凑参数（与 IRenderer 原语 1:1——坐标 double）
    uint32_t color = 0;      // 0xAARRGGBB（与 ms_bind ABI 同口径——A=alpha 直通；现调用方全 FF=行为不变）
    uint32_t color2 = 0;     // t-sprite：第二色（批量子弹的外圈色）
    std::vector<uint32_t> src;   // BlitRect/BlitAlpha 像素（立即拷入——调用方改 buffer 不影响回放）
    std::shared_ptr<std::vector<uint32_t>> srcShared;   // t10：Text 掩码共享引用（RuntimeFont 缓存——零拷贝；引用计数 pin——回放安全）
    // t-perf：批量圆参数（FillCircles/DrawCircles）——每圆 3 个 double：x,y,r。
    // 为什么用它：弹幕场景每帧几百颗子弹，逐颗一条 op 时"入队 + 析构 + 回放分发"的开销
    // 与真实像素工作量同量级（实测 897 条 op/帧 ≈ 1.3ms）；批量后同色子弹合成 1 条（800 条 → 8 条）。
    std::vector<double> pts;
    int srcW = 0, srcH = 0;      // BlitRect 源尺寸（= (int)v[2],(int)v[3]）
};

// —— 分辨率：**两个不同概念，不要混**（2026-09-18 收敛为单一真值）——
// ① 游戏面（窗口客户区）＝宿主看到的分辨率，**默认 1080P**，可由 EngineDesc/ABI 参数改。
// ② parity 面（哈希锚）＝固定 1280×800（`engine_demo.hpp` 的 kDemoWidth/kDemoHeight），
//    `RenderHash()` 与黄金帧 `4634E387E024BE90` 都锚在它上面，**与窗口尺寸无关、不得随游戏分辨率变化**。
//    为什么解耦：若 parity 面跟着窗口走，改个默认分辨率就会推翻跨链冻结的哈希锚
//    （ms_bench / test_parity / test_app / PackPlayer HUD 全依赖 1280×800）。
// 历史：这里原写 `1280x720`（与 parity 的 1280×800 只差高度，极易被误读成同一个数）。
inline constexpr int kGameWidth = 1920;    // 游戏/窗口默认宽（1080P）
inline constexpr int kGameHeight = 1080;   // 游戏/窗口默认高（1080P）

// M2.5：顶层宿主门面（M0-M2 无顶层——绑定层/M3 编辑器复用）
// 单场景单引擎模型；Platform 可选（windowed/audio）；Run 循环=host 驱动 Tick/Pump/Render
struct EngineDesc {
    std::string title = "HybridEngine v3";
    int width = kGameWidth, height = kGameHeight;   // 窗口/游戏面（**不**影响 parity 哈希面）
    bool windowed = false;   // true=创建 Win32 窗口（M1 Platform）
    bool audio = false;      // true=创建 WinMM 后端+AudioClock
    bool gpuRender = false;  // m-gpu：用户场景窗口面经由 D3D11 硬件光栅（无 GPU/失败=软渲降级）
};

class Engine {
public:
    explicit Engine(const EngineDesc& desc = {});
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // —— P2/多场景：引擎持有**已加载场景列表**（≥1）+ 活动场景索引 ——
    // Scene() 语义不变=「活动场景」，故既有单场景调用点逐行无需改动；
    // 场景对象按 unique_ptr 持有 → 引用稳定（追加/卸载不影响已取得的引用）。
    Core::Scene& Scene() { return *scenes_[(size_t)activeScene_]; }
    const Core::Scene& Scene() const { return *scenes_[(size_t)activeScene_]; }
    Core::Scene& SceneAt(int i) { return *scenes_[(size_t)i]; }
    const Core::Scene& SceneAt(int i) const { return *scenes_[(size_t)i]; }
    int SceneCount() const { return (int)scenes_.size(); }
    int ActiveSceneIndex() const { return activeScene_; }
    bool IsValidSceneIndex(int i) const { return i >= 0 && i < SceneCount(); }
    bool SetActiveScene(int i);                                  // 越界=拒绝（false）
    // 附加加载：追加一个新场景（活动场景不变）；返回其稳定指针（失败=nullptr）
    Core::Scene* AddScene(std::unique_ptr<Core::Scene> s);
    // 卸载：拆除该场景全部对象（OnDisable/OnDestroy → 托管登记清退）后移除槽位。
    // 活动场景被卸载 → 活动索引回落到 0；至少保留 1 个场景（最后一个不可卸载）。
    bool RemoveScene(int i);
    // 单场景载入 = 替换**活动场景**的内容（兼容旧语义：move 后重绑 Lifecycle/对象 回指）
    void SetScene(std::unique_ptr<Core::Scene> s) {
        if (!s) return;
        Scene() = std::move(*s);
        Scene().RebindAfterMove();
        renderableCache_.clear();   // t-perf-scene：内容已换 -> 可见性缓存清空
    }
    const std::vector<std::unique_ptr<Core::Scene>>& Scenes() const { return scenes_; }
    Core::EventBus& Events() { return events_; }
    Core::TimeSingleton& Time() { return time_; }
    Core::Assets::AssetLibrary& Assets() { return Core::Assets::AssetLibrary::Instance(); }
    // t-graph：节点图运行时（引擎每帧求值；绘制节点排进同一条 RndOp 队列——与脚本共用渲染/回放路径）
    GraphRuntime& Graphs() { return graphs_; }
    const GraphRuntime& Graphs() const { return graphs_; }

    Platform::IWindow* Window() const { return window_.get(); }
    Platform::IRenderer* Renderer() const { return parityRenderer_.get(); }   // parity 固定面（1280x800）
    Platform::IRenderer* WindowRenderer() const { return windowRenderer_.get(); }   // G1：窗口呈现面（客户区；窗口模式非空）
    Platform::Viewport WindowViewport() const;   // t1.1：窗口面视图（Letterbox——设计面→客户区；无窗口渲染器=恒等）
    Platform::Input* Input() const { return input_.get(); }
    Platform::Gamepad* Gamepad() const { return gamepad_.get(); }   // t8：手柄（null=无 XInput 降级）
    Platform::IAudioBackend* AudioBackend() const { return audioBackend_.get(); }
    Platform::AudioClock* AudioClock() const { return audioClock_.get(); }
    bool Windowed() const { return windowed_; }
    bool ShouldQuit() const { return shouldQuit_; }

    int Tick(double dt);      // Time→Lifecycle(标准序)→帧钩子→Events Flush→Input EndFrame→AudioClock；记录帧时
    int Render();             // parity 校准场景/用户场景→软渲；窗口→Present；更新 RenderHash
    int Pump();               // 泵窗口消息（无窗口=0）；窗口关闭→1（quit 请求）
    int Resize(int w, int h); // 渲染缓冲重建（窗口配合尺寸回调）

    // —— 呈现所有权：宿主在引擎帧**之上再叠一层**时必须接管 ——
    // 为什么需要：Present() 是直接把帧缓冲 StretchDIBits 到屏幕（**无双重缓冲**），
    //   而 Render() 内部就会 Present 一次。若宿主随后还要在窗口面上补画自己的东西
    //   （例如原生游戏画 HUD/弹幕），就会变成**每帧两次呈现**：第一次把「引擎帧」
    //   （窗口模式下=空场景的校准场景）显示出来，中间再 Clear+重画，最后才 Present
    //   「合成帧」→ 屏幕上每帧闪一下引擎帧（实测现象：深蓝底+橙色色块在闪）。
    // 做法：宿主关掉引擎的呈现，自己在画完后调用 Window()->Present() 一次。
    void SetPresentOnRender(bool on) { presentOnRender_ = on; }
    bool PresentOnRender() const { return presentOnRender_; }

    // —— P1-a：引擎帧钩子（托管每帧服务：Invoke/InvokeRepeating、协程调度、托管 Time 同步）——
    // 每帧 Tick 内按 phase 调两次：phase=0 帧首（Time 已推进、组件回调前）/ phase=1 帧尾（Lifecycle 全序后）。
    // cb=null 解绑；重复注册=替换。存在的理由：托管每帧服务不能只挂在 GameEngine.RunFrame 上——
    //   编辑器 Play 直接驱动 Engine::Tick，那条路径下 RunFrame 永不执行（协程/Invoke 会静默不跑）。
    using FrameHook = void (*)(void* userData, double dt, int phase);
    void SetFrameHook(FrameHook cb, void* userData) { frameHook_ = cb; frameHookUserData_ = userData; }
    FrameHook GetFrameHook() const { return frameHook_; }

    // t2（API 直觉化）：块式主循环（主线程语义；与 C# RunFrame/Run 对称）——内部 Tick(dt)→Pump→Render；
    // maxFrames>0=至多 N 帧（无窗口模式可跑）；maxFrames==0=直到窗口关闭（需窗口——无窗口+0=立即返回 1 防自旋）
    int Run(int maxFrames = 0);

    double LastFrameMs() const { return lastFrameMs_; }
    // t-perf：**惰性**帧哈希（定义在 engine.cpp——那里能看到 DemoFrameHash）。
    // 病史（实测 12~13ms/帧的固定成本）：Render() 原先每帧无条件对整块 parity 帧缓冲算 FNV-1a
    //   （1280×800×4 = **410 万次循环**，每次一个串行依赖的 64 位乘法）→ 约 10ms，
    //   哪怕这一帧什么都没画；Play 时它成了与"画了多少东西"无关的性能天花板。
    // 现在：Render() 只置脏；真正查询时才计算并缓存。语义不变（仍是**最近一帧**的哈希），
    // 只是把成本从"每帧必付"移到"谁问谁付"（黄金帧/一致性测试问一次，代价一次性）。
    uint64_t RenderHash() const;
    // M5：最近一帧用户场景 3D 光栅统计（ms_engine_render3d_stats——剔除/性能观测）
    const Render3D::RenderStats& LastRender3dStats() const { return lastRender3dStats_; }
    // M5：最近一帧**场景收集**到的启用光源数（≤8——`CollectSceneDrawables` 里 `lights.size() < 8` 才收）。
    // 为什么暴露它：文档声称"每帧 ≤8 光源"，但此前**没有任何断言守它**（审计 2026-09-17 实测），
    //   而且上限只在渲染计数里间接可见。给出只读计数即可用纯 C++ 单测钉住契约，不必依赖像素判据。
    // 语义：`Render()` 收集阶段写入；未渲染过=0。
    // ⚠ **收集数 ≠ 生效数**（2026-09-17 实测修正）：着色端 `ShadeCtx` 8 槽恒被 legacy 主光占 1 槽，
    //   故本函数返回 8 时**实际只有 7 盏场景光参与着色**，第 8 盏被静默丢弃。
    //   像素级判据见 `tests/render3d/test_light_accum.cpp`（N=7 与 N=8 逐位相同）。
    size_t LastSceneLightCount() const { return lastSceneLightCount_; }

    // —— t1：GUI 平面命令录制（ms_rnd_* —— Render 时列表非空=双面回放；空=既有路径逐位不变）——
    void RndClearList() { rndOps_.clear(); rndClipDepth_ = 0; } // 清空录制列表（host 帧尾调用——与 Input::EndFrame 同式；裁剪栈同步归零）
    void RndPushOp(RndOp op) { rndOps_.push_back(std::move(op)); } // 追加命令（录制）
    bool RndHasOps() const { return !rndOps_.empty(); }             // 本帧命令非空？
    size_t RndOpCount() const { return rndOps_.size(); }
    // —— t6：裁剪栈录制校验（与回放 renderer 栈同序镜像——录制层判定深度/空栈）——
    int  RndClipDepth() const { return rndClipDepth_; }             // 当前录制层裁剪深度（≤8）
    void RndClipPushOp(double x, double y, double w, double h);     // 记录 ClipPush+深度+1（调用方已校验 <8）
    void RndClipPushRotatedOp(double cx, double cy, double w, double h, double angleRad);   // t-rot-clip
    void RndClipPopOp();                                            // 记录 ClipPop+深度-1（调用方已校验 >0）

    // —— t3：音频 ABI 句柄（引擎拥有——单线程；id=槽位+1（≥10 起——与 MS_ERR_* 正值 1..9 错开防歧义）；槽位闭后保留复用）——
    int64_t AudioOpen(const char* path);            // 成功=id（≥10）；失败=0
    bool    AudioClose(int64_t id);                 // 释放后端（析构默认 Close——幂等安全）
    // ⚠ AudioPlay 是**幂等**的：重复调用不会重头播（同一句柄要重触发请先 AudioSeek(id, 0)）。
    //   唯一的例外是 AudioLoop(id,true) 的句柄——播到尾部后再次 AudioPlay 会回卷重播。
    //   ⇒ 循环要求宿主**每帧调用 AudioPlay**（waveaudio 不认 mci 的 `repeat`，见 IAudioBackend::SetLoop）。
    bool    AudioPlay(int64_t id);
    bool    AudioPause(int64_t id);
    bool    AudioSeek(int64_t id, double ms);
    double  AudioPositionMs(int64_t id);            // 毫秒；无效句柄=-1
    bool    AudioIsOpen(int64_t id);
    bool    AudioVolume(int64_t id, double gain);   // t8：真增益（后端支持=mci 应用；不支持=记录语义降级）
    bool    AudioLoop(int64_t id, bool on);         // t8：循环（宿主驱动——须持续 AudioPlay，见上）
    size_t  AudioHandleCount() const;               // 当前非空句柄数（绑定测试配对断言用）

private:
    struct AudioHandle {
        std::unique_ptr<Platform::IAudioBackend> backend;
        double gain = 1.0;                          // v1：仅记录（无 DSP——P1 真增益）
    };
    Platform::IAudioBackend* AudioSlot(int64_t id) const;

    std::vector<std::unique_ptr<Core::Scene>> scenes_;   // 已加载场景（≥1；[activeScene_] = 活动场景）
    int activeScene_ = 0;
    Core::EventBus events_;
    Core::TimeSingleton time_;
    std::unique_ptr<Platform::IWindow> window_;
    std::unique_ptr<Platform::IRenderer> parityRenderer_;   // 固定 1280x800（parity 哈希面——不随窗口变化）
    std::unique_ptr<Platform::IRenderer> windowRenderer_;     // 窗口呈现面（客户区尺寸——OnResize 跟随）
    std::unique_ptr<Platform::Input> input_;
    std::unique_ptr<Platform::Gamepad> gamepad_;   // t8：手柄（XInput；无=降级）
    std::unique_ptr<Platform::IAudioBackend> audioBackend_;
    std::unique_ptr<Platform::AudioClock> audioClock_;
    std::vector<AudioHandle> audioHandles_;   // t3：音频 ABI 句柄（槽位=backend+gain；id=槽位索引+10）
    bool windowed_ = false;
    bool shouldQuit_ = false;
    bool presentOnRender_ = true;   // 默认保持既有行为；宿主叠层时置 false 并自行 Present
    double lastFrameMs_ = 0;
    mutable uint64_t renderHash_ = 0;     // t-perf：最近一帧的 FNV-1a 哈希（惰性计算——见 RenderHash()）
    mutable bool hashDirty_ = true;       // t-perf：帧已渲但哈希未算（Render 置位；查询时清位）
    GraphRuntime graphs_;                 // t-graph：节点图实例（Start 相位在装载后的首帧触发）
    bool graphStartPending_ = true;
    std::unordered_map<int, bool> graphKeys_;   // t-graph：按键节点的每帧状态（由 Input 填充）
    Render3D::RenderStats lastRender3dStats_{};   // M5：3D 渲染统计（RenderUserScene 更新）
    size_t lastSceneLightCount_ = 0;              // M5：本帧收集到的启用光源数（≤8——见 LastSceneLightCount）
    // t-perf-scene：SceneHasRenderables 缓存（key=Scene*，value=(RenderRevision, has)）——只在可渲染物集合
    // 增删（AddRoot/RemoveRoot/Add- RemoveComponent）时随 revision 失效；Transform/字段变化不重建缓存。
    std::unordered_map<const Core::Scene*, std::pair<uint64_t, bool>> renderableCache_;
    std::vector<RndOp> rndOps_;   // t1：单帧命令列表（录制→Render 回放；空=既有绘制路径）
    int rndClipDepth_ = 0;        // t6：录制层裁剪深度镜像（RndClearList 归零——与 renderer 栈同序）
    bool gpuEnabled_ = false;     // m-gpu：desc.gpuRender（GPU 硬件后端请求）
    std::unique_ptr<HybridEngine::Render3D::GpuRenderer> gpu_;   // m-gpu：窗口面硬件光栅（null=降级）
    std::vector<uint32_t> gpuReadback_;   // m-gpu：GPU 读回缓冲复用（旧=每帧分配 客户区 w*h*4；稳态零分配）
    FrameHook frameHook_ = nullptr;      // P1-a：托管每帧服务钩子（绑定层 ms_engine_set_frame_hook 设置）
    void* frameHookUserData_ = nullptr;  // 钩子上下文（C# 侧 GCHandle 或 null）
};

} // namespace HybridEngine::App