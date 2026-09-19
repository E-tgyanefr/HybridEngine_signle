#pragma once
#include "hybridengine/core/math.hpp"
#include "hybridengine/core/component.hpp"
#include "hybridengine/core/assets/reflection_registry.hpp"
#include "hybridengine/platform/renderer.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace HybridEngine::Render3D {

// M3D：3D 渲染面（右手/Y 上/-Z 前——右手系一致；零第三方；确定性 float64 光栅）
// M5（本版）：视锥 6 面裁剪 + 透视修正插值 + 多光源 Blinn-Phong + 雾 + alpha 混合/透明排序 +
//   网格级视锥剔除 + 渲染统计 + 光照组件 —— 全部为「默认不改变既有像素」的增量
//   （无新特性时走 legacy 快路径，与 M3D/t7 输出逐位一致——黄金断言零回归）。

// —— 矩阵（数学复用+扩展）——
HybridEngine::Core::Mat4 MakePerspective(double fovYDeg, double aspect, double nearZ, double farZ);
HybridEngine::Core::Mat4 MakeOrthographic(double halfW, double halfH, double nearZ, double farZ);
HybridEngine::Core::Mat4 MakeView(const HybridEngine::Core::Vec3& eye, const HybridEngine::Core::Quat& orient);
HybridEngine::Core::Mat4 MakeLookAt(const HybridEngine::Core::Vec3& eye, const HybridEngine::Core::Vec3& target, const HybridEngine::Core::Vec3& up);
HybridEngine::Core::Quat QuatLookAt(const HybridEngine::Core::Vec3& eye, const HybridEngine::Core::Vec3& target, const HybridEngine::Core::Vec3& up);   // MakeLookAt 等价的四元数朝向（编辑器便捷）

// —— 相机（组件）——
struct Camera {
    HybridEngine::Core::Vec3 Position() const { return position_; }
    void SetPosition(const HybridEngine::Core::Vec3& p) { position_ = p; }
    HybridEngine::Core::Quat Orientation() const { return orientation_; }
    void SetOrientation(const HybridEngine::Core::Quat& q) { orientation_ = q; }
    double FovDeg() const { return fovDeg_; }
    void SetFovDeg(double v) { fovDeg_ = v < 35 ? 35 : (v > 120 ? 120 : v); }
    double Near() const { return near_; }
    void SetNear(double v) { near_ = v > 0 ? v : 0.1; }
    double Far() const { return far_; }
    void SetFar(double v) { far_ = v > near_ ? v : 1000; }
    bool Orthographic() const { return ortho_; }
    void SetOrthographic(bool v) { ortho_ = v; }
    double OrthoSize() const { return orthoSize_; }
    void SetOrthoSize(double v) { orthoSize_ = v > 0 ? v : 10; }
private:
    HybridEngine::Core::Vec3 position_{3, 2, 4};
    HybridEngine::Core::Quat orientation_{1, 0, 0, 0};
    double fovDeg_ = 60;
    double near_ = 0.1, far_ = 1000;
    bool ortho_ = false;
    double orthoSize_ = 10;
};

// ============ M5：光照（多光源、Blinn-Phong、确定性 double 着色） ============
// 说明：光源方向口径与 legacy RasterOptions.lightDir 一致（=「指向光源」），方向光/聚光分别见字段注释。
enum class LightType { Directional = 0, Point = 1, Spot = 2 };

struct Light {
    LightType type = LightType::Directional;
    HybridEngine::Core::Vec3 position{0, 3, 0};            // Point/Spot：光源世界位置
    // Directional：指向光源的单位方向（与 RasterOptions.lightDir 同口径——L=N·dir 用）
    // Spot：光锥轴向（光源→照射方向——朝向口径）
    HybridEngine::Core::Vec3 direction{-0.5, -0.7, -0.5};
    HybridEngine::Core::Vec3 color{1, 1, 1};               // 线性 0..1
    double intensity = 1.0;                                // 强度乘子
    double range = 0.0;                                    // Point/Spot：0=无衰减；否则 atten=(1-d/range)^2
    double spotInnerDeg = 25.0;                            // Spot：内角（满强度）
    double spotOuterDeg = 40.0;                            // Spot：外角（0——内外之间线性过渡）
    // 高光系数（M5）：0=该光源不产生高光；1=按材质高光（默认——材质 specular=0 时无高光）
    double specular = 1.0;
};

// —— M5：材质参数（内存覆盖 / .hmat 解析——不改 MeshData 资产格式）——
struct MaterialParams {
    HybridEngine::Platform::Rgba color{255, 255, 255, 255};   // a<255=半透明（BlendMode::Alpha 生效）
    double specular = 0.0;                                    // 0=关闭（legacy 输出不变）
    double shininess = 32.0;
    HybridEngine::Core::Vec3 emissive{0, 0, 0};               // 自发光（0..1 线性——叠加在光照之上）
};

// —— M5：雾（默认关闭=legacy 输出逐位不变）——
struct FogOptions {
    enum class Mode { Linear = 0, Exp2 = 1 };
    bool enabled = false;
    HybridEngine::Platform::Rgba color{8, 10, 18, 255};
    Mode mode = Mode::Linear;
    double start = 10.0, end = 60.0;   // Linear：起止距离（相机空间）
    double density = 0.02;             // Exp2：exp(-(density*d)^2)
};

// —— M5：绘制标志/混合模式 ——
enum DrawFlags : uint32_t {
    DrawNoCull       = 1u << 0,   // 关闭背面剔除（双面/透明面片）——同时关闭该网格的视锥剔除
    DrawNoDepthWrite = 1u << 1,   // 深度测试但不写（透明队列默认）
    DrawNoDepthTest  = 1u << 2,   // 不做深度测试（UI/叠层）
    DrawUnlit        = 1u << 3,   // 不受光照（纯色/贴图直出——天空盒/自发光面板）
};
enum class BlendMode { Opaque = 0, Alpha = 1, Additive = 2 };

struct DrawParams {
    uint32_t flags = 0;
    BlendMode blend = BlendMode::Opaque;
    const MaterialParams* material = nullptr;   // 非空=覆盖 mesh 自带材质（生命期须覆盖到本次绘制/End）
};

// —— Mesh ——
struct MeshVertex { HybridEngine::Core::Vec3 pos; HybridEngine::Core::Vec3 normal; double u = 0.0, v = 0.0; };   // t7：UV（纹理采样——缺省 0,0；无纹理路径不受影响）
struct MeshData {
    std::string name;
    std::vector<MeshVertex> vertices;   // 三角形展开
    HybridEngine::Platform::Rgba baseColor{255, 255, 255, 255};
    // M5：内存材质参数（不进 .mstri 格式；DrawParams::material 可覆盖）
    double specular = 0.0;
    double shininess = 32.0;
    HybridEngine::Core::Vec3 emissive{0, 0, 0};
    bool HasNormals() const;
    void ComputeNormals();   // 缺省=面法线（叉积）
    void ComputeSmoothNormals();   // 平滑法线（按位置焊接累加面法线；角色网格用，缺省路径不受影响）
    void Bounds(HybridEngine::Core::Vec3& outMin, HybridEngine::Core::Vec3& outMax) const;   // M5：局部 AABB（空网格=原点）
};
MeshData MakeCube(double size);
MeshData MakePyramid(double base, double height);

// —— t7：纹理引用（借出指针——渲染期有效；调用方保证生命周期）——
// 像素格式=0xFFRRGGBB（与 TextureAsset/软渲同格式——零转换）；空=无纹理=既有纯色路径（逐位不变）。
struct TextureRef {
    const uint32_t* pixels = nullptr;
    int width = 0, height = 0, stride = 0;   // stride=行字节数（TextureAsset 同口径）
    bool Valid() const { return pixels != nullptr && width > 0 && height > 0; }
};

// t7：双线性采样（零第三方自实现；u/v clamp 到 [0,1]——与 GPU sampler（SAMPLER_CLAMP）同口径；
// 输出恒 a=255）
HybridEngine::Platform::Rgba SampleBilinear(const TextureRef& tex, double u, double v);

// —— 光栅选项/渲染器 ——
struct RasterOptions {
    HybridEngine::Platform::Rgba clear{8, 10, 18, 255};
    HybridEngine::Platform::Rgba clearBottom{0, 0, 0, 255};   // M5：clearGradient=true 时的底部色
    bool clearGradient = false;                                // M5：垂直渐变背景（天空感——默认关）
    HybridEngine::Platform::Rgba ambient{30, 30, 36, 255};
    HybridEngine::Core::Vec3 lightDir{-0.5, -0.7, -0.5};
    HybridEngine::Core::Vec3 lightColor{1, 1, 1};
    double lambert = 0.85;
    bool cullBackface = true;
    // M5：附加光源（与 legacy 主光叠加；空=单光 legacy 路径——既有输出逐位不变）
    // ⚠ 有效上限 **7 盏**（实测 2026-09-17 修正，此前注释误写 8）：`ShadeCtx` 的 8 个槽
    //   恒被 legacy 主光占 1 个（`render3d.cpp` 的 pushLight 先推主光），故第 8 盏场景光
    //   在到达着色前被静默丢弃。判据：`tests/render3d/test_light_accum.cpp`
    //   （N=1..7 逐盏线性 +26；N=7 与 N=8/9/12 逐位相同）。
    std::vector<Light> lights;
    FogOptions fog;                    // M5：雾（默认关）
    bool cullMeshes = true;            // M5：整体 AABB 视锥剔除（只影响工作量/Stats，不改变可见像素）
    // t-perf-simd：oracle 开关——true=强制标量参考路径（legacy / 纹理 / modern 多光源全部）。测试用它把
    // SIMD 路径与标量路径做**逐位**对比；生产默认 false（与标量路径输出必须完全一致，否则测试红）。
    bool forceScalarRaster = false;
};

// —— M5：渲染统计（每帧 Begin 清零；性能/剔除观测用）——
struct RenderStats {
    uint64_t submittedTriangles = 0;   // 提交（剔除前）
    uint64_t drawnTriangles = 0;       // 实际光栅化（裁剪后扇形三角）
    uint64_t culledMeshes = 0;         // 视锥剔除的网格数
    uint64_t culledTriangles = 0;      // 视锥剔除网格连带三角数
    uint64_t clippedTriangles = 0;     // 触发裁剪的三角数
    uint64_t shadedPixels = 0;         // 写入像素（含透明混合）
    uint64_t blendedPixels = 0;        // 透明混合像素
    uint64_t sortedDraws = 0;          // 透明队列排序绘制数
    uint64_t simdPairs = 0;            // t-perf-simd：实际走 SIMD 路径处理的像素数
                                       // （SSE2 2-lane legacy / AVX2 4-lane legacy+纹理+modern；oracle/性能诊断）
};

class Render3D {
public:
    bool Begin(int w, int h);
    void SetCamera(const Camera& cam, double aspect, double deviceScale = 1.0);
    // 提交（即时绘制——既有语义；tint.a==0=不覆盖=用 mesh.baseColor）
    void Submit(const MeshData& mesh, const HybridEngine::Core::Mat4& world, HybridEngine::Platform::Rgba tint,
                const TextureRef& tex = {});
    // M5：显式绘制参数（flags/blend/材质覆盖——同 Submit 语义扩展）
    void Submit(const MeshData& mesh, const HybridEngine::Core::Mat4& world, HybridEngine::Platform::Rgba tint,
                const TextureRef& tex, const DrawParams& params);
    // M5：透明队列（End/FlushTransparent 时按相机距离远→近排序绘制——正确 alpha 合成）
    void SubmitTransparent(const MeshData& mesh, const HybridEngine::Core::Mat4& world, HybridEngine::Platform::Rgba tint,
                           const TextureRef& tex = {});
    void SubmitTransparent(const MeshData& mesh, const HybridEngine::Core::Mat4& world, HybridEngine::Platform::Rgba tint,
                           const TextureRef& tex, const DrawParams& params);
    void FlushTransparent();
    void End() { FlushTransparent(); }
    void DrawMesh(const MeshData& mesh, const HybridEngine::Core::Mat4& world, HybridEngine::Platform::Rgba tint,
                  const TextureRef& tex = {}) { Submit(mesh, world, tint, tex); }   // t7 便捷别名（既有调用零改动）
    const HybridEngine::Platform::Rgba* Color() const { return color_.data(); }
    const float* Depth() const { return depth_.data(); }
    void Composite(HybridEngine::Platform::IRenderer& r, int dx, int dy);
    // t3d-edit：叠加合成（编辑器 ScenePanel 网格通道——透明底=不清屏）
    void SetOverlay(bool v) { overlay_ = v; }
    void CompositeOverlay(HybridEngine::Platform::IRenderer& r, int dx, int dy);
    void ProjectPoint(const HybridEngine::Core::Vec3& world, double& sx, double& sy, double& sz) const;
    int Width() const { return w_; } int Height() const { return h_; }
    void SetOptions(const RasterOptions& o) { opts_ = o; }
    const RasterOptions& Options() const { return opts_; }
    const RenderStats& Stats() const { return stats_; }
    void ResetStats() { stats_ = RenderStats{}; }

private:
    struct ClipVertex {
        double cx = 0, cy = 0, cz = 0, cw = 1;   // clip 空间
        HybridEngine::Core::Vec3 world;          // 世界位置（点光/雾/高光）
        HybridEngine::Core::Vec3 nrm;            // 世界法线
        double u = 0, v = 0;
    };
    struct ShadeLight {
        int type = 0;
        HybridEngine::Core::Vec3 pos, dir, color;
        double intensity = 1.0, range = 0.0, cosInner = -1.0, cosOuter = -1.0, specular = 0.0, shininess = 32.0;
    };
    struct ShadeCtx {
        bool modern = false;          // 现代着色路径（多光/雾/高光/自发光/Unlit/混合之一）
        bool unlit = false;
        bool hasTex = false;
        bool alphaBlend = false;
        int blendMode = 0;            // 0=opaque 1=alpha 2=additive
        bool fog = false;
        int fogMode = 0;
        double fogStart = 0, fogEnd = 1, fogDensity = 0;
        HybridEngine::Core::Vec3 fogColor{0, 0, 0};
        HybridEngine::Core::Vec3 ambient{0, 0, 0};      // 0..1
        HybridEngine::Platform::Rgba base{255, 255, 255, 255};
        HybridEngine::Core::Vec3 emissive{0, 0, 0};
        TextureRef tex;
        HybridEngine::Core::Vec3 camPos;
        bool normalFlip = false;                         // 关剔除时法线朝视点修正
        int lightCount = 0;
        ShadeLight lights[8];
    };

    void SubmitInternal(const MeshData& mesh, const HybridEngine::Core::Mat4& world, HybridEngine::Platform::Rgba tint,
                        const TextureRef& tex, const DrawParams& params, bool transparentQueue);
    void DrawMeshNow(const MeshData& mesh, const HybridEngine::Core::Mat4& world, HybridEngine::Platform::Rgba base,
                     const TextureRef& tex, const DrawParams& params);
    void RasterizeTriangle(const ClipVertex& a, const ClipVertex& b, const ClipVertex& c, const ShadeCtx& ctx, uint32_t flags);
    int w_ = 0, h_ = 0;
    bool overlay_ = false;
    std::vector<HybridEngine::Platform::Rgba> color_;
    std::vector<float> depth_;
    HybridEngine::Core::Mat4 mvp_{};
    HybridEngine::Core::Mat4 model_{};
    HybridEngine::Core::Mat4 view_{};
    HybridEngine::Core::Mat4 viewProj_{};
    RasterOptions opts_{};
    RenderStats stats_{};
    HybridEngine::Core::Vec3 camPos_{3, 2, 4};
    // 复用暂存（避免每帧分配——软渲热路径）
    std::vector<ClipVertex> worldVerts_;
    std::vector<ClipVertex> polyA_, polyB_;
    struct PendingDraw {
        const MeshData* mesh = nullptr;
        HybridEngine::Core::Mat4 world;
        HybridEngine::Platform::Rgba base{255, 255, 255, 255};
        TextureRef tex;
        DrawParams params;
        double distSq = 0.0;
    };
    std::vector<PendingDraw> transparent_;
};

// —— 组件（场景集成——HYBRIDENGINE_REFLECT 字段）——
class MeshVisual : public HybridEngine::Core::Component {
public:
    MeshData mesh;
    HybridEngine::Platform::Rgba color{255, 255, 255, 255};
    bool MeshEnabled = true;
    // t2：材质资产关联（.hmat 路径 "Assets/..."；空=无）——渲染时经 AssetLibrary 解析
    std::string materialAsset;
    HYBRIDENGINE_REFLECT(MeshVisual, MeshEnabled, materialAsset)
};

class CameraComponent : public HybridEngine::Core::Component {
public:
    Camera cam;
    int priority = 0;
    bool active = true;
    HYBRIDENGINE_REFLECT(CameraComponent, priority, active)
};

// M5：光源组件（场景光照——编辑器 Add Component / .mscene 序列化均可用）
// type：0=Directional 1=Point 2=Spot（Enum 语义——检查器可编辑；序列化为数字）
class LightComponent : public HybridEngine::Core::Component {
public:
    bool enabled = true;
    int type = 0;
    HybridEngine::Core::Vec3 position{0, 3, 0};
    HybridEngine::Core::Vec3 direction{-0.5, -0.7, -0.5};
    HybridEngine::Core::Vec3 color{1, 1, 1};
    double intensity = 1.0;
    double range = 0.0;
    double spotInnerDeg = 25.0;
    double spotOuterDeg = 40.0;
    double specular = 1.0;   // M5：该光源高光系数（材质 specular 决定强度）
    Light ToLight() const {
        Light l;
        l.type = type == 1 ? LightType::Point : (type == 2 ? LightType::Spot : LightType::Directional);
        l.position = position;
        l.direction = direction;
        l.color = color;
        l.intensity = intensity;
        l.range = range;
        l.spotInnerDeg = spotInnerDeg;
        l.spotOuterDeg = spotOuterDeg;
        l.specular = specular;
        return l;
    }
    HYBRIDENGINE_REFLECT(LightComponent, enabled, type, position, direction, color, intensity, range,
                         spotInnerDeg, spotOuterDeg, specular)
};


// ============ 2D：SpriteVisual（纯引擎 2D 场景内容） ============
// 约定：世界坐标 = 屏幕像素，原点=屏幕中心，X 右 / Y 上；渲染时按 order 升序绘制（后绘制在上）。
// 纯色=color/alpha；textureAsset 非空=尝试加载 TextureAsset（.bmp/.png）并按 width/height 裁剪 blit。
class SpriteVisual : public HybridEngine::Core::Component {
public:
    bool enabled = true;
    int order = 0;                                      // 绘制序（大者在上）
    HybridEngine::Core::Vec3 color{1, 1, 1};            // 0..1
    double alpha = 1.0;
    double width = 64.0, height = 64.0;                 // 像素
    std::string textureAsset;                           // "Assets/xx.bmp|png"，空=纯色
    HYBRIDENGINE_REFLECT(SpriteVisual, enabled, order, color, alpha, width, height, textureAsset)
};
} // namespace HybridEngine::Render3D
