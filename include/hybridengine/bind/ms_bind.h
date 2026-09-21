#pragma once
#include <stdint.h>

// M2.5：ms_bind——稳定 C ABI（V3-B：C++ 核 + C# 开发层）
// 线程模型：单线程（创建引擎的同一线程=主线程）；跨线程调用=MS_ERR_THREAD（M25-b 防御）
// 内存所有权：句柄=C++ 对象（ms_engine_destroy 释放；C# 不 free）；GCHandle=C# 拥有；字符串 UTF-8（调用方拥有），出参缓冲=调用方提供

#ifdef MS_BIND_BUILD_DLL
#  ifdef _WIN32
#    define MS_API __declspec(dllexport)
#  else
#    define MS_API __attribute__((visibility("default")))
#  endif
#else
#  define MS_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ms_engine ms_engine;   // HybridEngine::App::Engine*
typedef struct ms_scene ms_scene;     // HybridEngine::Core::Scene*
typedef struct ms_go ms_go;           // HybridEngine::Core::SceneObject*
typedef struct ms_asset ms_asset;     // 资产句柄（绑定层包装）
typedef int64_t ms_id;                // InstanceId（uint64→C# long；设计 long——Windows long=32 修正为 int64_t）
typedef uint64_t ms_hash;             // FNV-1a 64

/* ---- 错误码：0=OK；<0=绑定层（句柄/类型/参数）；>0=引擎（Core/Asset 映射） ---- */
enum {
  MS_BIND_OK = 0,
  MS_ERR_NULL_HANDLE = -1,
  MS_ERR_BAD_TYPE = -2,
  MS_ERR_BAD_ARG = -3,
  MS_ERR_INVALID_OP = 1,
  MS_ERR_NOT_FOUND = 2,
  MS_ERR_IO = 3,
  MS_ERR_HASH_MISMATCH = 4,
  MS_ERR_TRAVERSAL_DENIED = 5,
  MS_ERR_DUPLICATE_NAME = 6,
  MS_ERR_UNKNOWN_FIELD = 7,
  MS_ERR_NOT_SUPPORTED = 8,
  MS_ERR_THREAD = 9
};

/* ---- 引擎生命周期 ---- */
/* P2：宿主接入 —— 把当前线程登记为 ABI 主线程，返回 0=成功。
   为什么需要：编辑器这类宿主在进程内**直接构造**引擎（不经 ms_engine_create），
   此时绑定层的主线程标识尚未设置；不登记则所有 MS_ABI_GUARD 保护的调用都返回 MS_ERR_THREAD
   （实测症状：ms_scripts_build 立即失败且日志为空）。幂等：重复调用无副作用。 */
MS_API int        ms_bind_attach_host(void);
MS_API ms_engine* ms_engine_create(const char* title, int width, int height, int* err);
// t-perf：**带窗口开关的创建**（纯增量，不改 ms_engine_create 语义——它恒等于 windowed=1）。
// 为什么需要：窗口形态下帧率被 DWM/垂直同步封顶（呈现一次就受限），**测不出引擎自身吞吐**；
// 基准/服务器/CI 需要 windowed=0 的无窗口引擎（Tick+Render 全在内存里，不呈现）。
MS_API ms_engine* ms_engine_create_ex(const char* title, int width, int height, int windowed, int* err);
MS_API void       ms_engine_destroy(ms_engine* e);
MS_API int        ms_engine_tick(ms_engine* e, double dt);
MS_API int        ms_engine_render(ms_engine* e, void* rendererCtx);   // 渲染委托（ctx 预留 C# OnRender——内置 parity 校准场景回退）
MS_API int        ms_engine_pump(ms_engine* e);                        // 无窗口=0；窗口关闭=1（quit 请求）
MS_API int        ms_engine_resize(ms_engine* e, int w, int h);
MS_API double     ms_engine_last_frame_ms(ms_engine* e);
MS_API ms_hash    ms_engine_render_hash(ms_engine* e);                 // 当前帧 FNV（parity）

/* ---- 场景 ---- */
MS_API ms_scene*  ms_engine_scene(ms_engine* e);   // 活动场景（单场景=唯一场景——兼容旧调用点）
/* ---- P2/多场景（SceneManager）：引擎持有已加载场景**列表** + 活动场景索引 ----
   兼容承诺：ms_engine_scene / 既有 ms_scene_* 仍以「活动场景」为准，单场景调用点零改动；
   且 ms_scene* 句柄对列表内任意场景都有效（不再限活动场景）。
   生命周期：所有已加载场景都参与 Engine::Tick（各自 Lifecycle）；渲染把全部场景**合成为一趟光栅**
   （逐场景各跑一趟会各自清屏 → 只剩最后一个可见）。
   卸载（ms_scene_unload）：先走 Scene::RemoveRoot 拆除该场景全部对象（OnDisable/OnDestroy →
   托管登记/协程/Invoke 清退）再移除槽位；最后一个场景不可卸载（Scene() 恒有效）。
   **句柄生命周期**：ms_scene* 是场景对象的裸指针——卸载后即悬垂，且按指针成员关系判定对已释放地址
   本质歧义（地址可能被新场景复用），故**不可**依赖「对旧句柄调用会报错」；宿主卸载后应按索引重查。
   **按场景入参**：ms_scene_save / ms_scene_find / ms_scene_go_get / ms_scene_root_count/get 一律作用于
   传入的场景（此前误用活动场景——单场景下不可见，多场景会读错场景）；ms_go_add_child /
   ms_transform_set_parent 作用于对象**所属场景**（不会把子对象建到别的场景）。 */
MS_API int ms_scenes_count(ms_engine* e, int* outCount);                        // 已加载场景数（≥1）
MS_API int ms_scenes_get(ms_engine* e, int index, ms_scene** outScene);         // 按索引取场景句柄
MS_API int ms_scene_active_index(ms_engine* e, int* outIndex);                  // 活动场景索引
MS_API int ms_scene_set_active_index(ms_engine* e, int index);                  // 切换活动场景（越界=BAD_ARG）
MS_API int ms_scene_name(ms_engine* e, ms_scene* s, char* out, int cap);        // 场景名（UTF-8）
MS_API int ms_scene_new(ms_engine* e, const char* name, ms_scene** outScene);   // 附加新建空场景（活动场景不变）
MS_API int ms_scene_load_additive(ms_engine* e, const char* assetPath, ms_scene** outScene);   // 附加加载（含脚本重放；**含脚本组件的场景要求脚本桥+重放回调已注册**，否则返回 MS_ERR_NOT_SUPPORTED(8)——与 ms_scene_load 同一前置条件，不静默丢脚本）
MS_API int ms_scene_unload(ms_engine* e, ms_scene* s);                          // 卸载场景（拆除对象 + 移除槽位）
MS_API int ms_scene_add_root(ms_engine* e, ms_scene* s, const char* name, ms_id* outId);
MS_API int        ms_scene_save(ms_engine* e, ms_scene* s, const char* assetPath);
MS_API int        ms_scene_load(ms_engine* e, ms_scene* s, const char* assetPath);
MS_API int        ms_scene_go_get(ms_engine* e, ms_scene* s, ms_id id, ms_go** outGo);
MS_API int        ms_scene_root_count(ms_engine* e, ms_scene* s, int* outCount);
MS_API int        ms_scene_root_get(ms_engine* e, ms_scene* s, int index, ms_go** outGo);
MS_API int        ms_scene_find(ms_engine* e, ms_scene* s, const char* name, ms_go** outGo);   // 按名 DFS（未找到=MS_ERR_NOT_FOUND）

/* ---- 对象/组件 ---- */
MS_API ms_id      ms_go_instance_id(ms_engine* e, ms_go* g);
MS_API int        ms_go_set_active(ms_engine* e, ms_go* g, int active);
MS_API int        ms_go_destroy(ms_engine* e, ms_go* g);
MS_API int        ms_go_add_child(ms_engine* e, ms_go* g, const char* name, ms_go** outGo);
MS_API int        ms_go_name(ms_engine* e, ms_go* g, char* out, int cap);
MS_API int        ms_go_set_name(ms_engine* e, ms_go* g, const char* name);
MS_API int        ms_go_child_count(ms_engine* e, ms_go* g, int* outCount);
MS_API int        ms_go_child_get(ms_engine* e, ms_go* g, int index, ms_go** outGo);
MS_API int        ms_go_remove_component(ms_engine* e, ms_go* g, const char* scriptType);
typedef void (*ms_cb_void)(void* ctx);
typedef void (*ms_cb_dt)(void* ctx, double dt);
typedef struct ms_component_spec {
    const char* scriptType;    // C# 完全限定名（反射注册键代理）
    ms_cb_void  onAwake;
    ms_cb_void  onEnable;
    ms_cb_void  onStart;
    ms_cb_void  onDisable;
    ms_cb_void  onDestroy;
    ms_cb_dt    onUpdate;
    ms_cb_dt    onFixedUpdate;
    ms_cb_dt    onLateUpdate;
    void*       userData;      // C# 实例 GCHandle（C# 拥有——GameEngine.Dispose 释放）
} ms_component_spec;
MS_API int ms_component_register(ms_engine* e, const ms_component_spec* spec);
MS_API int ms_go_add_component(ms_engine* e, ms_go* g, const char* scriptType);
MS_API int ms_go_get_component(ms_engine* e, ms_go* g, const char* scriptType, void** outReflected);

/* ---- P1-a：组件级 enabled（组件开关——经 Component::SetEnabled → LifecycleDriver::SetActive） ----
   语义：值变化才触发（OnDisable/OnEnable 成对回调；同值=空操作）；enabled=0 的组件被 LifecycleDriver 跳过
        （Start/FixedUpdate/Update/LateUpdate 均不再分派——Unity 组件开关语义）。
   scriptType 查找口径与 ms_go_remove_component 完全一致：脚本组件=per-instance 注册键（FullName#N，
   由 ms_go_add_component 的 spec.scriptType 决定）；C++ 组件=ReflectionRegistry RTTI 类型名。
   未找到=MS_ERR_NOT_FOUND（outOn 置 0）；on 非 0/1=按 !=0 归一。 */
MS_API int ms_component_set_enabled(ms_engine* e, ms_go* g, const char* scriptType, int on);
MS_API int ms_component_get_enabled(ms_engine* e, ms_go* g, const char* scriptType, int* outOn);

/* ---- P1-b：组件存在性查询（原生组件 + 脚本组件统一口径——托管 GetComponent<T>() 代理包装用） ----
   查找口径与 ms_component_set_enabled 一致：脚本组件=per-instance 注册键；C++ 组件=RTTI 注册名
   （如 "MeshVisual"/"CameraComponent"/"LightComponent"/"SpriteVisual"）。
   查询语义：缺失**不算错误**——对象句柄有效=MS_BIND_OK 且 outPresent=0/1；句柄/出参非法=MS_ERR_BAD_ARG。
   注：同一对象可挂多个同类型原生组件（C++ AddComponent<T> 不去重）——本函数与 GetComponent<T> 同为「首个命中」。 */
MS_API int ms_go_has_component(ms_engine* e, ms_go* g, const char* typeName, int* outPresent);

/* ---- P1-a：引擎帧钩子（托管每帧服务——Invoke/InvokeRepeating、协程调度、托管 Time 同步） ----
   每帧在 Engine::Tick 内按 phase 调用两次（同一次注册，两个相位）：
     phase=0（帧首）：Time 已推进、组件回调之前——托管侧同步 Time.deltaTime/time/frameCount，
                      保证组件 Update 内读到的是本帧 dt（硬约束：晚于 Lifecycle 会让组件读到上一帧值）。
     phase=1（帧尾）：Lifecycle 全序（Start/FixedUpdate/Update/LateUpdate/销毁队列）之后、Events Flush 之前
                      ——托管侧推进 Invoke 与协程（与托管 RunFrame 既有「Update 后」语义一致）。
   dt=本帧时长。cb=null 解绑；重复注册=替换（后注册者生效）；单线程（创建引擎的线程）。
   存在理由：托管每帧服务若只挂在 GameEngine.RunFrame 上，编辑器直驱 Engine::Tick 的 Play 路径不会执行它。 */
typedef void (*ms_cb_frame)(void* userData, double dt, int phase);
MS_API int ms_engine_set_frame_hook(ms_engine* e, ms_cb_frame cb, void* userData);

/* ---- 3D：用户场景真 3D（MeshVisual/CameraComponent——Engine::Render 用户场景路径走 Render3D 光栅） ----
   用法：ms_scene_add_root → (可选 ms_go_add_child 骨骼层级) → ms_transform_set（世界矩阵组合）
       → ms_go_add_mesh_visual + ms_go_set_mesh（网格=三角形展开）→ ms_go_add_camera + ms_camera_set
       → ms_engine_render（场景含 MeshVisual→3D 光栅：透视/深度/朗伯；无相机=默认街拍位） */
MS_API int ms_go_add_mesh_visual(ms_engine* e, ms_go* g);                                 // 挂 MeshVisual（网格空=不绘制）
MS_API int ms_go_set_mesh(ms_engine* e, ms_go* g, const double* vertsXYZ, int triCount, uint32_t color);   // 填网格（tri×9 doubles 行序；法线缺省=面法线）
MS_API int ms_go_add_camera(ms_engine* e, ms_go* g);                                     // 挂 CameraComponent
MS_API int ms_camera_set(ms_engine* e, ms_go* g, const double* eye, const double* target, double fovDeg, int ortho, double orthoSize);   // 相机看向 target；ortho!=0=正交(orthoSize)
/* ---- 2D：SpriteVisual（纯引擎 2D 场景内容——世界坐标=屏幕像素/原点=屏幕中心；order 大者在上） ---- */
MS_API int ms_go_add_sprite_visual(ms_engine* e, ms_go* g);
MS_API int ms_sprite_set(ms_engine* e, ms_go* g, double cr, double cg, double cb, double ca,
                         double width, double height, const char* textureAsset);
/* ---- M5：3D 光照/材质/统计（多光源 Blinn-Phong + 透明 + 雾 + 视锥剔除——引擎用户场景路径） ----
   光照：ms_go_add_light（挂 LightComponent）→ ms_light_set（参数）→ ms_engine_render（与 RasterOptions 主光叠加）
         ⚠ 场景光**有效上限 7 盏/帧**（实测 2026-09-17 修正；此前注释误写"最多 8"）：
           收集端收 8 个，但着色端 8 槽恒被 legacy 主光占 1 槽 → 第 8 盏被静默丢弃。
           要"每盏都确定生效"请配 ≤6 盏（留 1 盏余量给默认主光）。
           默认主光是否有像素贡献取决于几何：其方向默认 (-0.5,-0.7,-0.5)，背向它的面贡献 0。
   透明：ms_go_set_mesh 的 color 0xAARRGGBB 高 8 位 alpha<FF → 自动进透明队列（End 远→近排序混合，无需额外调用）
   材质：ms_go_set_material（高光/自发光——内存态；.hmat 资产可持久化同名参数） */
MS_API int ms_go_add_light(ms_engine* e, ms_go* g);                                  // 挂 LightComponent（默认方向光）
// 光源参数：type 0=方向光 1=点光 2=聚光；position/direction/color=各 3 double（color 0..1）；
//   direction：方向光=指向光源方向；聚光=光锥轴向（光源→照射方向）；range=0 无距离衰减；innerDeg/outerDeg=聚光内外角
MS_API int ms_light_set(ms_engine* e, ms_go* g, int type, const double* position, const double* direction,
                        const double* color, double intensity, double range, double innerDeg, double outerDeg);
MS_API int ms_light_enable(ms_engine* e, ms_go* g, int on);                          // 光源开关（enabled）
// 材质参数（MeshVisual.mesh）：specular 高光强度（0=关——legacy 观感）/shininess 指数（>=1，钳制）/emissive 0..1（可空）
MS_API int ms_go_set_material(ms_engine* e, ms_go* g, double specular, double shininess, const double* emissive);
// 最近一帧用户场景 3D 统计（8×uint64 顺序：submitted/drawn/culledMeshes/culledTriangles/clipped/shaded/blended/sortedDraws）
// 返回写入数（cap 截断；无 3D 帧=0）——剔除/性能观测用
MS_API int ms_engine_render3d_stats(ms_engine* e, uint64_t* out, int cap);

/* ---- plug：插件 ABI（运行时插件管理——插件经此检索/装配场景） ----
   ms_plugin_count/find：检索已注册插件（引擎创建时 LoadAll 已 init）
   ms_plugin_scene：调用插件 SceneBuilder（preset=插件自定义预设名）——返回 0=成功 */
MS_API int  ms_plugin_count(ms_engine* e);
MS_API int  ms_plugin_find(ms_engine* e, const char* name);          // 1=存在 0=未注册（BAD_ARG=null）
MS_API int  ms_plugin_version(ms_engine* e, const char* name, char* outVer, int cap);
MS_API int  ms_plugin_scene(ms_engine* e, const char* name, const char* preset);   // SceneBuilder（0=成功）
MS_API int  ms_plugin_load(ms_engine* e, const char* dllPath);      // 动态插件（Dll——<name>_plugin_desc 入口）；0=成功 负=错误

/* ---- 输入（G2/t137：C++ Input 全口径经 ABI；vk=KeyCode 值（Win32 VK 直通）） ---- */
MS_API int    ms_input_key_down(ms_engine* e, int vk);                    // 本帧按下沿（EndFrame 清）
MS_API int    ms_input_key_up(ms_engine* e, int vk);                      // 本帧抬起沿
MS_API int    ms_input_get_key(ms_engine* e, int vk);                     // 当前按住
MS_API double ms_input_axis(ms_engine* e, const char* name);              // Horizontal/Vertical（WASD+方向键）
MS_API int    ms_input_get_button(ms_engine* e, const char* action);      // Fire1/Jump/Submit/Cancel

/* ---- 变换 ---- */
MS_API int  ms_transform_get(ms_engine* e, ms_go* g, const char* field, double* out);  // pos(3)/rot(4 wxyz quat)/scale(3)
MS_API int  ms_transform_set(ms_engine* e, ms_go* g, const char* field, const double* in);
MS_API ms_id ms_transform_parent(ms_engine* e, ms_go* g);
MS_API int  ms_transform_set_parent(ms_engine* e, ms_go* g, ms_id parentId, int keepWorld);

/* ---- C# 脚本桥（M3.4：types/fields/set 三回调——编辑器脚本字段经绑定） ---- */
typedef int (*ms_cb_script_fields)(void* userData, const char* instanceKey, char* outJson, int cap);
typedef int (*ms_cb_script_field_set)(void* userData, const char* instanceKey, const char* field, const char* jsonValue);
typedef int (*ms_cb_script_types)(void* userData, char* outJson, int cap);
MS_API int ms_script_bridge_register(ms_engine* e, void* userData, ms_cb_script_fields fieldsFn, ms_cb_script_field_set setFn, ms_cb_script_types typesFn);
// t-graph-script：**方法调用回调**（托管侧反射 Invoke）。单独注册=纯增量，不改既有 4 参注册的签名。
// outResult 收返回值（void → 0）；返回 MS_BIND_OK=调用成功。
typedef int (*ms_cb_script_call)(void* userData, const char* instanceKey, const char* method, double arg, double* outResult);
MS_API int ms_script_bridge_register_call(ms_engine* e, ms_cb_script_call callFn);
// t-graph-member：**脚本成员清单回调**（类型 → 可调方法 / 可读写字段，JSON）。同样是纯增量注册。
typedef int (*ms_cb_script_members)(void* userData, char* outJson, int cap);
MS_API int ms_script_bridge_register_members(ms_engine* e, ms_cb_script_members membersFn);
MS_API int ms_script_fields(ms_engine* e, const char* instanceKey, char* outJson, int cap);
MS_API int ms_script_field_set(ms_engine* e, const char* instanceKey, const char* field, const char* jsonValue);
MS_API int ms_script_types(ms_engine* e, char* outJson, int cap);

/* ---- P1-a：脚本组件重放（场景加载——scriptFields → 托管实例重建） ----
   背景：脚本组件在场景里以「保存时的 per-instance 注册键」(FullName#N) 落盘，ReflectionRegistry 对它没有工厂，
   因此加载需要托管侧重建实例。流程（ms_scene_load 内部，桥 + 本回调均就绪时）：
     1) 反序列化时按注册键形状（*#数字）认领脚本条目 → 建占位 BindComponent（回调全空=静默）；
     2) 旧场景经 Scene::RemoveRoot 拆除（触发 OnDisable/OnDestroy → 托管登记清退）；
     3) 装载新场景，对每个脚本条目调用 replayFn(go, savedKey, fieldsJson)：
        托管侧建实例、按 savedKey 重新 ms_component_register（同键就地更新 → 已存在的 BindComponent
        立即取到新回调）、补挂 Owner/sceneObject 视图、回填 scriptFields。
   返回 0=成功；非 0=该条目重放失败（ms_scene_load 透传该码）。
   savedKey=保存时的注册键（托管侧沿用，保证再次保存无键漂移）；fieldsJson=scriptFields 原文（可空串）。 */
typedef int (*ms_cb_script_replay)(void* userData, ms_go* go, const char* savedKey, const char* fieldsJson);
MS_API int ms_script_replay_register(ms_engine* e, ms_cb_script_replay replayFn, void* userData);

/* ---- P2：项目内 .cs 直挂（编辑器 Add Component / Play 直接使用项目脚本） ----
   方案：外部 `dotnet build`（编译）+ 进程内 hostfxr 承载（执行）。**零第三方依赖**——
   hostfxr/coreclr 属 .NET 运行时自带组件（系统库性质，同 D3D11/XInput）；不使用内嵌 Roslyn。
   managedDir：HybridEngine.Bind.dll + HybridEngine.ScriptLoader.dll(+runtimeconfig/deps) 所在目录；
               空/null = 用宿主 exe 所在目录。
   无 dotnet 运行时的机器：ms_dotnet_status 返回 0、编译/加载返回负值——编辑器据此优雅降级
   （脚本目录不可用），不影响既有功能。

   单引擎同一性：编辑器自带引擎（静态链接 bind）。若托管侧按常规 P/Invoke "hybridengine.dll"
   会再加载一份 DLL → 第二套 Bridge/SpecTable/引擎注册表，脚本将操作不到编辑器的场景树。
   故加载时把**宿主模块路径**传给托管入口，由 ScriptLoader 装 DllImportResolver 指回同一模块。 */
MS_API int ms_dotnet_status(char* outInfo, int cap);   // 1=可用 0=不可用（文案写入 outInfo）
// 编译项目脚本（生成 <root>/Library/ScriptAssemblies/*.csproj + dotnet build）→ 0=成功
MS_API int ms_scripts_build(ms_engine* e, const char* projectRoot, const char* managedDir, char* outLog, int cap);
// 加载脚本程序集 + 注册脚本桥 + 枚举可挂类型 → outJson = 类型全名 JSON 数组
MS_API int ms_scripts_load(ms_engine* e, const char* projectRoot, const char* managedDir, const char* hostModule, char* outJson, int cap);
// 按托管类型名给对象挂项目脚本组件（须先 ms_scripts_load）
MS_API int ms_scripts_add_component(ms_engine* e, ms_go* g, const char* typeName);
// 卸载项目脚本程序集（可收集 ALC——支持「改脚本→重编译→重载」）
MS_API int ms_scripts_unload(ms_engine* e);

/* ---- 反射属性（M2 ReflectionRegistry 经 ABI——M25-e JSON 传输） ---- */
MS_API int ms_property_get(ms_engine* e, ms_go* g, const char* type, const char* field, char* outJson, int cap);
MS_API int ms_property_set(ms_engine* e, ms_go* g, const char* type, const char* field, const char* jsonIn);
MS_API int ms_property_fields(ms_engine* e, const char* type, char* outJson, int cap);

/* ---- 资产（句柄） ---- */
MS_API ms_asset* ms_assets_load(ms_engine* e, const char* assetPath, int* err);
MS_API int    ms_assets_type(ms_engine* e, ms_asset* a, char* outType, int cap);
MS_API int    ms_assets_guid(ms_engine* e, ms_asset* a, char* outGuid, int cap);
MS_API void   ms_assets_unref(ms_engine* e, ms_asset* a);
MS_API int    ms_assets_set_root(ms_engine* e, const char* absRoot);   // 项目根（C# 侧 Save/Load 前置）
MS_API int    ms_assets_save(ms_engine* e, const char* assetPath, const void* data, int bytes, const char* type);  // 二进制安全（save_text 的超集）
MS_API int    ms_assets_save_text(ms_engine* e, const char* assetPath, const char* payload, const char* type);
MS_API int    ms_assets_instantiate_prefab(ms_engine* e, const char* assetPath, ms_go** outGo);   // 预制体实例化→场景

/* ---- 资产列表 & 纹理像素（t7：X5 管线闭环——曲库/封面/图标「资产→渲染」） ----
   ms_assets_list：dir 递归 List（类型表过滤——复用 AssetLibrary::List）→ JSON 字符串数组（["Assets/a.bmp",...]）；
   out 缓冲不足（cap 溢出）=MS_ERR_BAD_ARG（文档化）；root 未设=空数组。
   ms_assets_texture_pixels：Texture 像素**借出指针**（句柄（ms_assets_unref/引擎销毁）存活期间有效——零拷贝直读；
   句柄失效后禁用（未定义——不代表可访问）；w/h/stride 出参——**stride = 输出像素缓冲的行 pitch（恒为 w*4 字节）**，
   即按 `pixels[y * (stride/4) + x]` 索引；**不是**源文件行字节数（BMP 源行含 4 字节对齐填充、
   PNG 源扫描行是 w*通道数，两者与输出 pitch 都不同——按源侧值当 pitch 走行会读错行）；非 Texture=MS_ERR_BAD_TYPE。 */
MS_API int    ms_assets_list(ms_engine* e, const char* dir, char* outJson, int cap);
MS_API int    ms_assets_texture_pixels(ms_engine* e, ms_asset* a, int* outW, int* outH, int* outStride, uint32_t** outPixels);

/* ---- 错误文案（t2：错误码→人类可读 UTF-8；仅新增——不改任何既有函数/ABI） ---- */
MS_API const char* ms_bind_error_text(int code);   // 静态 UTF-8 文案（调用方只读不释放）；未知码="未知错误码"

/* ---- 测试辅助（句柄计数断言——验收 #2） ---- */
MS_API int    ms_bind_live_engine_count(void);

/* ---- GUI 平面命令录制（t1：ms_rnd_* —— 与 IRenderer 原语 1:1；命令跨帧保持——host 帧尾清） ----
   ms_engine_render：列表空=既有路径（黄金帧契约 4634E387E024BE90 逐位不变）；非空=双面（parity+窗口）先清黑→按序回放。
   命名注：ms_rnd_clear(颜色) = 全屏 Clear 原语（IRenderer::Clear 语义——C# Interop 已声明同形）；清空录制列表=ms_rnd_clear_list。
   t1 alpha：ms_rnd_* 颜色参数 = 0xAARRGGBB（A=alpha——填充原语 straight-alpha 合成 out=(src*a+dst*(255-a)+127)/255；
   A=FF=直写快路径（既有输出逐位不变）；A=0=跳过；现调用方全 FF=行为不变；Clear=整帧覆盖（alpha 忽略）；blit_rect=不透明拷贝）。 */
MS_API int ms_rnd_clear_list(ms_engine* e);
// t-perf：本帧已排入的绘制命令条数（诊断/基准用——渲染耗时随条数线性变化，
// 排查"为什么这帧慢"第一步就是看它）。纯增量，不改任何既有函数。
MS_API int ms_rnd_op_count(ms_engine* e, int* outCount);
// t-perf：**批量画圆**（弹幕/粒子负载）——一次调用画 N 个同色圆，只产生 1 条命令。
// xy_r = 每圆 3 个 double（x,y,r），count 个圆。理由：逐颗一条命令时"入队+析构+分发"的开销
// 与像素工作量同量级（实测 897 条/帧 ≈ 1.3ms）；同色批量后 800 条 → 8 条。
MS_API int ms_rnd_fill_circles(ms_engine* e, const double* xy_r, int count, uint32_t color);
MS_API int ms_rnd_draw_circles(ms_engine* e, const double* xy_r, int count, double thickness, uint32_t color);
// t-sprite：**批量子弹**（核心圆 + 外圈，整批同款）——xy 每颗 2 个 double；一次调用画 N 颗。
// 语义与「先 ms_rnd_fill_circles 再 ms_rnd_draw_circles」逐位等价；软件渲染器走烘焙精灵（每行一次 memcpy）。
MS_API int ms_rnd_bullets(ms_engine* e, const double* xy, int count, double r, double thickness,
                          uint32_t coreColor, uint32_t rimColor);

// ===== t-graph：节点图（连线式编程）=====
// 定位：**引擎内运行时解释**——不生成代码；引擎每帧求值（Start 一次 / Update 每帧 / 按键节点看 down）。
// 图资产 = `.hgraph`（JSON，放 Assets/ 下）；绘制节点排进与脚本**同一条** RndOp 队列。
// 文字节点需要 GDI 掩码，宿主先调 ms_graph_enable_text（编辑器/播放器启动时一次即可）。
MS_API int ms_graph_load(ms_engine* e, const char* assetPath);
MS_API int ms_graph_unload(ms_engine* e, const char* assetPath);
MS_API int ms_graph_count(ms_engine* e, int* outCount);
MS_API int ms_graph_enable_text(ms_engine* e);
// t-graph-script：给节点图装上**脚本出口**（读写脚本字段 / 调用脚本方法）——启动时调一次
MS_API int ms_graph_enable_scripts(ms_engine* e);
// 调用某脚本实例的一个公开方法（无参或单个 double 参数）；outResult 收返回值（void → 0）
MS_API int ms_script_call(ms_engine* e, const char* instanceKey, const char* method, double arg, double* outResult);
// 工程脚本成员清单（类型 → 公开方法/字段）JSON——节点图调色板用
MS_API int ms_scripts_members(char* outJson, int cap);
MS_API int ms_graph_set_var(ms_engine* e, const char* name, double v);
MS_API int ms_graph_get_var(ms_engine* e, const char* name, double* outV);
MS_API int ms_graph_node_registry(char* outJson, int cap);
MS_API int ms_graph_save(ms_engine* e, const char* assetPath);                                                          // 清空录制列表（非绘制原语）
MS_API int ms_rnd_clear(ms_engine* e, uint32_t color);                                                // 全屏 Clear（color=0xAARRGGBB；alpha 忽略=整帧覆盖）
MS_API int ms_rnd_clear_rect(ms_engine* e, double x, double y, double w, double h, uint32_t color);   // ClearRect（=FillRect 同语义）
MS_API int ms_rnd_fill_rect(ms_engine* e, double x, double y, double w, double h, uint32_t color);
MS_API int ms_rnd_fill_rounded_rect(ms_engine* e, double x, double y, double w, double h, double radius, uint32_t color);
MS_API int ms_rnd_draw_line(ms_engine* e, double x1, double y1, double x2, double y2, uint32_t color, double thickness);
MS_API int ms_rnd_fill_circle(ms_engine* e, double cx, double cy, double r, uint32_t color);
MS_API int ms_rnd_draw_circle(ms_engine* e, double cx, double cy, double r, uint32_t color, double thickness);
MS_API int ms_rnd_fill_triangle(ms_engine* e, double x1, double y1, double x2, double y2, double x3, double y3, uint32_t color);
MS_API int ms_rnd_fill_quad(ms_engine* e, double x1, double y1, double x2, double y2, double x3, double y3, double x4, double y4, uint32_t color);
MS_API int ms_rnd_blit_rect(ms_engine* e, double x, double y, double w, double h, const uint32_t* srcPixels);   // 像素立即拷入 Op（无悬垂）；不透明拷贝（背靠背语义）
MS_API int ms_rnd_blit_alpha(ms_engine* e, double x, double y, double w, double h, const uint32_t* srcPixels);  // t1：src=0xAARRGGBB 逐像素 straight-alpha 合成（与 blit_rect 仅合成差异；像素立即拷入 Op——无悬垂）
/* t6：矩形裁剪（Clip/Scissor）——push=入栈（≤8 深；栈顶=当前裁剪——嵌套仅栈顶生效=保存/恢复语义；坐标=设计坐标系）；
   所有绘制原语（含 Blit 与文本 span）回放时按栈顶矩形裁剪（越界内容不画出=展示面"内容裁剪"）；pop=恢复上一级。
   空栈 pop=MS_ERR_INVALID_OP；超深 push（第 9 个）=MS_ERR_BAD_ARG（不记录——回放栈与录制镜像一致）。 */
MS_API int ms_rnd_clip_push(ms_engine* e, double x, double y, double w, double h);
MS_API int ms_rnd_clip_pop(ms_engine* e);
/* t-rot-clip：旋转矩形裁剪（cx,cy,w,h,angleRad——中心旋转；精确斜劈/分离位移；栈顶生效≤8 深；同矩形栈语义与错误码） */
MS_API int ms_rnd_clip_push_rotated(ms_engine* e, double cx, double cy, double w, double h, double angleRad);

/* ---- 文本（t1：GDI 白掩码光栅——RuntimeFont LRU 512；UTF-8；size=像素字号；CJK 直支持） ----
   ms_text_draw=录制 Text 命令（录制时 GDI 光栅化一次——t10 掩码共享缓存+引用计数（pinned）：Op 持有共享引用零拷贝；
   回放零系统调用=严格确定性）；随 ms_engine_render 双面回放。 */
MS_API int ms_text_draw(ms_engine* e, const char* utf8, double x, double y, double size, uint32_t color);
MS_API int ms_text_measure(ms_engine* e, const char* utf8, double size, double* outW, double* outH);          // 像素宽/高

/* ---- 输入鼠标（t1：边沿=本帧（EndFrame 清——与 ms_input_key_down 同式）；wheel=本帧累计） ---- */
MS_API double ms_input_mouse_x(ms_engine* e);                    // 当前客户区 x
MS_API double ms_input_mouse_y(ms_engine* e);                    // 当前客户区 y
MS_API int    ms_input_mouse_button(ms_engine* e, int button);   // 当前按住（0=左 1=中 2=右；越界=假）
MS_API int    ms_input_mouse_button_down(ms_engine* e, int button);   // 本帧按下沿
MS_API int    ms_input_mouse_button_up(ms_engine* e, int button);     // 本帧抬起沿
MS_API double ms_input_mouse_wheel_delta(ms_engine* e);          // 本帧累计滚轮格数（±1/格）
MS_API double ms_input_mouse_design_x(ms_engine* e);    // t1.1：设计面鼠标 x（ViewportPolicy ToVirtual——Letterbox/DPI 自动换算）
MS_API double ms_input_mouse_design_y(ms_engine* e);
// 自动化输入注入（脚本/测试驱动用：直接写 engine->Input()，与窗口回调同一对象、语义一致）
MS_API int    ms_input_inject_key(ms_engine* e, int vk, int down);
MS_API int    ms_input_inject_mouse(ms_engine* e, double x, double y, int button, int down);
MS_API int    ms_input_inject_move(ms_engine* e, double x, double y);
MS_API int    ms_input_inject_wheel(ms_engine* e, double x, double y, int delta);    // t1.1：设计面鼠标 y

/* ---- 音频（t3：WinMM(mci) 桥——引擎级句柄；单线程（跨线程=MS_ERR_THREAD）；时基=毫秒；状态机=closed/open/playing/paused） ----
   ms_audio_open 返回：≥10=句柄 id（槽位句柄——与 MS_ERR_* 正值 1..9 错开防歧义；0=不合法）；失败=MS_ERR_*（文件不存在=MS_ERR_NOT_FOUND；后端打开失败=MS_ERR_IO）。
   句柄归属=引擎（ms_engine_destroy 释放全部——不强求显式 close）；句柄时序=close 后全部调用→MS_ERR_NOT_FOUND。
   ms_audio_volume：真增益（0..1——mci `setaudio <alias> volume to n`；**已实测生效**）。
        返回值：MS_BIND_OK=增益已下发到设备；MS_ERR_NOT_FOUND=句柄无效**或**设备不接受音量命令
        （此时增益**仍被记录**；要区分两种情况用 ms_audio_is_open 确认句柄）。
        ⚠ 2026-09-17 修正：此前用 `set <alias> audio volume to n`，实测被设备拒绝
        （290 = MCIERR_BAD_CONSTANT），且实现无条件返回 OK → **"看起来成功但音量从未生效"**。两处已修。
   ms_audio_loop：t8 循环播放（1/0）；**宿主驱动**——须持续 ms_audio_play，详见 audio_backend.hpp。
        ⚠ 旧注释写"Play 时 `play repeat`"是错的：waveaudio 不认 repeat（0x103），设备级循环做不到。 */
MS_API int  ms_audio_open(ms_engine* e, const char* path);         // 打开（wav/mp3——mci 类型探测）→ 句柄 id（≥10）
MS_API int  ms_audio_close(ms_engine* e, int64_t h);               // 关闭（释放后端资源）
MS_API int  ms_audio_play(ms_engine* e, int64_t h);                // 播放（open 后；状态→playing）
MS_API int  ms_audio_pause(ms_engine* e, int64_t h);               // 暂停（状态→paused）
MS_API int  ms_audio_seek(ms_engine* e, int64_t h, double ms);     // 定位（毫秒；ms>=0）
MS_API int  ms_audio_position(ms_engine* e, int64_t h, double* outMs);  // 当前播放位置（毫秒）
MS_API int  ms_audio_is_open(ms_engine* e, int64_t h);             // 1=open 0=非 open（含无效句柄=0）
MS_API int  ms_audio_volume(ms_engine* e, int64_t h, double gain); // 增益（0..1；OK=已下发，NOT_FOUND=句柄无效或设备不支持）
MS_API int  ms_audio_loop(ms_engine* e, int64_t h, int on);        // t8：循环（1/0；无效句柄=MS_ERR_NOT_FOUND）

/* ---- 手柄（t8：XInput——xinput1_4.dll 系统组件；4 槽位；边沿=本帧 EndFrame 清） ----
   按钮=0..9（A/B/X/Y/LB/RB/Back/Start/LeftStick/RightStick）；轴=0..5（LX/LY/RX/RY -1..1
   （deadzone 归零+重归一）；LT/RT 0..1）；pad=0..3（越界=MS_ERR_BAD_ARG）；
   无控制器/无 XInput：connected=0、按钮/轴=0（防御——不报错）。 */
MS_API int    ms_gamepad_count(ms_engine* e);                      // 槽位数（XInput=4；无 XInput=0）
MS_API int    ms_gamepad_connected(ms_engine* e, int pad);         // 1=已连接 0=未连接
MS_API int    ms_gamepad_button(ms_engine* e, int pad, int button);      // 当前按住
MS_API int    ms_gamepad_button_down(ms_engine* e, int pad, int button); // 本帧按下沿
MS_API int    ms_gamepad_button_up(ms_engine* e, int pad, int button);   // 本帧抬起沿
MS_API double ms_gamepad_axis(ms_engine* e, int pad, int axis);    // 模拟量（-1..1/0..1）

#ifdef __cplusplus
}
#endif
