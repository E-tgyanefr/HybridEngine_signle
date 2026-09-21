using System;
using System.Runtime.InteropServices;

[assembly: System.Runtime.CompilerServices.InternalsVisibleTo("CsTestRunner")]
// P2：hostfxr 承载入口（编辑器直挂 .cs）——需要 ComponentBridge/SceneObject 内部构造等第一方宿主能力
[assembly: System.Runtime.CompilerServices.InternalsVisibleTo("HybridEngine.ScriptLoader")]

namespace HybridEngine.Engine.Internal;

// M2.5：ms_bind C ABI P/Invoke（纯 BCL——DllImport 仅）
public static class Native
{
    private const string Dll = "hybridengine";
    private const CallingConvention Conv = CallingConvention.Cdecl;

    [DllImport(Dll, CallingConvention = Conv)] public static extern IntPtr ms_engine_create([MarshalAs(UnmanagedType.LPUTF8Str)] string title, int width, int height, out int err);
    [DllImport(Dll, CallingConvention = Conv)] public static extern void ms_engine_destroy(IntPtr e);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_engine_tick(IntPtr e, double dt);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_engine_render(IntPtr e, IntPtr rendererCtx);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_engine_pump(IntPtr e);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_engine_resize(IntPtr e, int w, int h);
    [DllImport(Dll, CallingConvention = Conv)] public static extern double ms_engine_last_frame_ms(IntPtr e);
    [DllImport(Dll, CallingConvention = Conv)] public static extern ulong ms_engine_render_hash(IntPtr e);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_bind_live_engine_count();
    // t2：错误码→人类可读文案（静态 UTF-8）
    [DllImport(Dll, CallingConvention = Conv)] public static extern IntPtr ms_bind_error_text(int code);
    // G2/t137：输入 ABI（vk=KeyCode 值——Win32 VK 直通）
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_input_key_down(IntPtr e, int vk);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_input_key_up(IntPtr e, int vk);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_input_get_key(IntPtr e, int vk);
    [DllImport(Dll, CallingConvention = Conv)] public static extern double ms_input_axis(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_input_get_button(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string action);

    // t1 GUI 平面 ABI：2D 绘制原语（ms_rnd_*——坐标 double、颜色 uint32 0xAARRGGBB（A=alpha 直通；现调用方全 FF=行为不变）；与 C++ IRenderer 同语义）
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_rnd_clear_list(IntPtr e);   // 清空录制列表（非绘制原语——host 每帧帧首调用）
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_rnd_clear(IntPtr e, uint color);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_rnd_clear_rect(IntPtr e, double x, double y, double w, double h, uint color);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_rnd_fill_rect(IntPtr e, double x, double y, double w, double h, uint color);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_rnd_fill_rounded_rect(IntPtr e, double x, double y, double w, double h, double radius, uint color);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_rnd_draw_line(IntPtr e, double x1, double y1, double x2, double y2, uint color, double thickness);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_rnd_fill_circle(IntPtr e, double cx, double cy, double r, uint color);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_rnd_draw_circle(IntPtr e, double cx, double cy, double r, uint color, double thickness);
    // t-perf：批量画圆（弹幕/粒子负载）——1 条命令画 N 个同色圆
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_rnd_fill_circles(IntPtr e, double[] xyR, int count, uint color);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_rnd_draw_circles(IntPtr e, double[] xyR, int count, double thickness, uint color);
    // t-sprite：批量子弹（核心圆 + 外圈）——xy 每颗 2 个 double
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_rnd_bullets(IntPtr e, double[] xy, int count, double r, double thickness, uint coreColor, uint rimColor);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_rnd_fill_triangle(IntPtr e, double x1, double y1, double x2, double y2, double x3, double y3, uint color);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_rnd_fill_quad(IntPtr e, double x1, double y1, double x2, double y2, double x3, double y3, double x4, double y4, uint color);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_rnd_blit_rect(IntPtr e, double x, double y, double w, double h, [In] uint[] srcPixels);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_rnd_blit_alpha(IntPtr e, double x, double y, double w, double h, [In] uint[] srcPixels);   // t1：src=0xAARRGGBB 逐像素合成
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_rnd_clip_push(IntPtr e, double x, double y, double w, double h);   // t6：矩形裁剪入栈（≤8 深；栈顶生效）
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_rnd_clip_push_rotated(IntPtr e, double cx, double cy, double w, double h, double angleRad);   // t-rot-clip：旋转矩形裁剪入栈（中心+角；angle=0≡矩形）
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_rnd_clip_pop(IntPtr e);                                          // t6：弹栈（空栈=MS_ERR_INVALID_OP）

    // t1 GUI 平面 ABI：文本（ms_text_*——UTF-8；录制 Text 命令——回放绘制双面并参与哈希；size=像素 double）
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_text_draw(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string utf8, double x, double y, double size, uint color);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_text_measure(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string utf8, double size, out double outW, out double outH);

    // t1 GUI 平面 ABI：鼠标（ms_input_mouse_*——button 0=左 1=中 2=右；wheel ±1）
    [DllImport(Dll, CallingConvention = Conv)] public static extern double ms_input_mouse_x(IntPtr e);
    [DllImport(Dll, CallingConvention = Conv)] public static extern double ms_input_mouse_y(IntPtr e);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_input_mouse_button(IntPtr e, int button);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_input_mouse_button_down(IntPtr e, int button);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_input_mouse_button_up(IntPtr e, int button);
    [DllImport(Dll, CallingConvention = Conv)] public static extern double ms_input_mouse_wheel_delta(IntPtr e);
    [DllImport(Dll, CallingConvention = Conv)] public static extern double ms_input_mouse_design_x(IntPtr e);
    [DllImport(Dll, CallingConvention = Conv)] public static extern double ms_input_mouse_design_y(IntPtr e);

    // —— t-auto：自动化输入注入（脚本/测试驱动 → 直接写引擎 Input，不依赖窗口消息）——
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_input_inject_key(IntPtr e, int vk, int down);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_input_inject_mouse(IntPtr e, double x, double y, int button, int down);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_input_inject_move(IntPtr e, double x, double y);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_input_inject_wheel(IntPtr e, double x, double y, int delta);

    // t3：音频（WinMM(mci) 桥——引擎级句柄；单线程；句柄归属=引擎（ms_engine_destroy 释放全部）；close 后调用→MS_ERR_NOT_FOUND；
    //   open 返回 ≥10=句柄 id；0=非法；负/1..9=MS_ERR_*；position=毫秒；volume=增益 0..1（v1 仅记录）
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_audio_open(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_audio_close(IntPtr e, long h);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_audio_play(IntPtr e, long h);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_audio_pause(IntPtr e, long h);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_audio_seek(IntPtr e, long h, double ms);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_audio_position(IntPtr e, long h, out double outMs);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_audio_is_open(IntPtr e, long h);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_audio_volume(IntPtr e, long h, double gain);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_audio_loop(IntPtr e, long h, int on);   // t8：循环（Play 时 repeat 生效）
    // t8：手柄（XInput——pad 0..3；button 0..9；axis 0..5；无控制器=防御值）
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_gamepad_count(IntPtr e);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_gamepad_connected(IntPtr e, int pad);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_gamepad_button(IntPtr e, int pad, int button);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_gamepad_button_down(IntPtr e, int pad, int button);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_gamepad_button_up(IntPtr e, int pad, int button);
    [DllImport(Dll, CallingConvention = Conv)] public static extern double ms_gamepad_axis(IntPtr e, int pad, int axis);

    [DllImport(Dll, CallingConvention = Conv)] public static extern IntPtr ms_engine_scene(IntPtr e);
    // P2：多场景（SceneManager）——引擎持有场景列表 + 活动索引
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_scenes_count(IntPtr e, out int outCount);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_scenes_get(IntPtr e, int index, out IntPtr outScene);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_scene_active_index(IntPtr e, out int outIndex);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_scene_set_active_index(IntPtr e, int index);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_scene_name(IntPtr e, IntPtr s, [Out] byte[] outName, int cap);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_scene_new(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string? name, out IntPtr outScene);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_scene_load_additive(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string assetPath, out IntPtr outScene);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_scene_unload(IntPtr e, IntPtr s);

    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_scene_add_root(IntPtr e, IntPtr s, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, out long outId);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_scene_save(IntPtr e, IntPtr s, [MarshalAs(UnmanagedType.LPUTF8Str)] string assetPath);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_scene_load(IntPtr e, IntPtr s, [MarshalAs(UnmanagedType.LPUTF8Str)] string assetPath);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_scene_go_get(IntPtr e, IntPtr s, long id, out IntPtr outGo);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_scene_root_count(IntPtr e, IntPtr s, out int outCount);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_scene_root_get(IntPtr e, IntPtr s, int index, out IntPtr outGo);
[DllImport(Dll, CallingConvention = Conv)] public static extern int ms_scene_find(IntPtr e, IntPtr s, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, out IntPtr outGo);

    [DllImport(Dll, CallingConvention = Conv)] public static extern long ms_go_instance_id(IntPtr e, IntPtr g);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_go_set_active(IntPtr e, IntPtr g, int active);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_go_destroy(IntPtr e, IntPtr g);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_go_add_child(IntPtr e, IntPtr g, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, out IntPtr outGo);
[DllImport(Dll, CallingConvention = Conv)] public static extern int ms_go_name(IntPtr e, IntPtr g, [Out] byte[] outBuf, int cap);
[DllImport(Dll, CallingConvention = Conv)] public static extern int ms_go_set_name(IntPtr e, IntPtr g, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);
[DllImport(Dll, CallingConvention = Conv)] public static extern int ms_go_child_count(IntPtr e, IntPtr g, out int outCount);
[DllImport(Dll, CallingConvention = Conv)] public static extern int ms_go_child_get(IntPtr e, IntPtr g, int index, out IntPtr outGo);
[DllImport(Dll, CallingConvention = Conv)] public static extern int ms_go_remove_component(IntPtr e, IntPtr g, [MarshalAs(UnmanagedType.LPUTF8Str)] string scriptType);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_component_register(IntPtr e, ref MsComponentSpec spec);
    // H2：为指定对象注册**专属** spec（同键双实例各存一份回调表）
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_component_register_for(IntPtr e, IntPtr owner, ref MsComponentSpec spec);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_go_add_component(IntPtr e, IntPtr g, [MarshalAs(UnmanagedType.LPUTF8Str)] string scriptType);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_go_get_component(IntPtr e, IntPtr g, [MarshalAs(UnmanagedType.LPUTF8Str)] string scriptType, out IntPtr outReflected);

    // P1-a：组件级 enabled（经 Component::SetEnabled → LifecycleDriver::SetActive；值变化才 OnDisable/OnEnable）
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_component_set_enabled(IntPtr e, IntPtr g, [MarshalAs(UnmanagedType.LPUTF8Str)] string scriptType, int on);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_component_get_enabled(IntPtr e, IntPtr g, [MarshalAs(UnmanagedType.LPUTF8Str)] string scriptType, out int outOn);
    // P1-a：引擎帧钩子（托管每帧服务——Time 同步/Invoke/协程；phase 0=帧首 1=帧尾）
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_engine_set_frame_hook(IntPtr e, IntPtr cb, IntPtr userData);
    // P1-b：组件存在性查询（原生+脚本统一口径——查询语义，缺失不算错误）
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_go_has_component(IntPtr e, IntPtr g, [MarshalAs(UnmanagedType.LPUTF8Str)] string typeName, out int outPresent);

    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_transform_get(IntPtr e, IntPtr g, [MarshalAs(UnmanagedType.LPUTF8Str)] string field, [Out] double[] outData);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_transform_set(IntPtr e, IntPtr g, [MarshalAs(UnmanagedType.LPUTF8Str)] string field, double[] inData);
    [DllImport(Dll, CallingConvention = Conv)] public static extern long ms_transform_parent(IntPtr e, IntPtr g);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_transform_set_parent(IntPtr e, IntPtr g, long parentId, int keepWorld);

    // 用户场景真 3D（MeshVisual/CameraComponent——Engine::Render 用户场景路径走 Render3D 光栅）
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_go_add_mesh_visual(IntPtr e, IntPtr g);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_go_add_sprite_visual(IntPtr e, IntPtr g);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_sprite_set(IntPtr e, IntPtr g, double cr, double cg, double cb, double ca, double width, double height, [MarshalAs(UnmanagedType.LPUTF8Str)] string? textureAsset);   // 挂 MeshVisual（网格空=不绘制）
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_go_set_mesh(IntPtr e, IntPtr g, [In] double[] vertsXYZ, int triCount, uint color);   // 填网格（tri×9 doubles 行序；法线缺省=面法线）
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_go_add_camera(IntPtr e, IntPtr g);      // 挂 CameraComponent
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_camera_set(IntPtr e, IntPtr g, [In] double[] eye, [In] double[] target, double fovDeg, int ortho, double orthoSize);   // 相机看向 target；ortho!=0=正交(orthoSize)
        // M5：3D 光照/材质/统计（多光源 Blinn-Phong + 高光/自发光 + 透明 0xAARRGGBB alpha + 视锥剔除统计）
        [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_go_add_light(IntPtr e, IntPtr g);   // 挂 LightComponent
        [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_light_set(IntPtr e, IntPtr g, int type, [In] double[]? position, [In] double[]? direction, [In] double[]? color, double intensity, double range, double innerDeg, double outerDeg);   // type 0=方向 1=点 2=聚光
        [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_light_enable(IntPtr e, IntPtr g, int on);
        [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_go_set_material(IntPtr e, IntPtr g, double specular, double shininess, [In] double[]? emissive);   // 高光/自发光
        [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_engine_render3d_stats(IntPtr e, [Out] ulong[] out8, int cap);   // 8×u64 统计

    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_property_get(IntPtr e, IntPtr g, [MarshalAs(UnmanagedType.LPUTF8Str)] string type, [MarshalAs(UnmanagedType.LPUTF8Str)] string field, [Out] byte[] outJson, int cap);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_property_set(IntPtr e, IntPtr g, [MarshalAs(UnmanagedType.LPUTF8Str)] string type, [MarshalAs(UnmanagedType.LPUTF8Str)] string field, [MarshalAs(UnmanagedType.LPUTF8Str)] string jsonIn);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_property_fields(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string type, [Out] byte[] outJson, int cap);

    // M3.4：C# 脚本桥
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_script_bridge_register(IntPtr e, IntPtr userData, IntPtr fieldsFn, IntPtr setFn, IntPtr typesFn);
    // P1-a：脚本组件重放回调注册（场景加载——scriptFields → 托管实例重建）
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_script_replay_register(IntPtr e, IntPtr replayFn, IntPtr userData);
    // t-graph-script：方法调用回调注册（增量 ABI；旧 DLL 无此入口 → 上层用 EntryPointNotFound 降级）
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_script_bridge_register_call(IntPtr e, IntPtr callFn);
    // t-graph-member：成员清单回调注册（增量 ABI）
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_script_bridge_register_members(IntPtr e, IntPtr membersFn);
    // t-graph-script：开启节点图的脚本出口（读写字段/调方法）
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_graph_enable_scripts(IntPtr e);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_script_fields(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string instanceKey, [Out] byte[] outJson, int cap);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_script_field_set(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string instanceKey, [MarshalAs(UnmanagedType.LPUTF8Str)] string field, [MarshalAs(UnmanagedType.LPUTF8Str)] string jsonValue);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_script_types(IntPtr e, [Out] byte[] outJson, int cap);

    [DllImport(Dll, CallingConvention = Conv)] public static extern IntPtr ms_assets_load(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string assetPath, out int err);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_assets_type(IntPtr e, IntPtr a, [Out] byte[] outType, int cap);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_assets_guid(IntPtr e, IntPtr a, [Out] byte[] outGuid, int cap);
    [DllImport(Dll, CallingConvention = Conv)] public static extern void ms_assets_unref(IntPtr e, IntPtr a);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_assets_set_root(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string absRoot);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_assets_save(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string assetPath, byte[] data, int bytes, [MarshalAs(UnmanagedType.LPUTF8Str)] string type);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_assets_save_text(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string assetPath, [MarshalAs(UnmanagedType.LPUTF8Str)] string payload, [MarshalAs(UnmanagedType.LPUTF8Str)] string type);
[DllImport(Dll, CallingConvention = Conv)] public static extern int ms_assets_instantiate_prefab(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string assetPath, out IntPtr outGo);
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_assets_list(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string dir, [Out] byte[] outJson, int cap);   // t7：递归 List→JSON 字符串数组
    [DllImport(Dll, CallingConvention = Conv)] public static extern int ms_assets_texture_pixels(IntPtr e, IntPtr a, out int outW, out int outH, out int outStride, out IntPtr outPixels);   // t7：纹理像素借出指针（句柄存活期间有效）
}

// 回调委托的调用约定必须与 ABI 一致（cdecl）。不标时默认 Winapi=StdCall：
//   x64 只有一种约定、实测无碍，但 x86 下会**栈失衡**（本项目未 pin PlatformTarget，
//   故这里显式标注，避免将来加 x86 目标时踩坑）。
[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate void MsCbVoid(IntPtr ctx);
[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate void MsCbDt(IntPtr ctx, double dt);
// P1-a：引擎帧钩子（phase 0=帧首——Time 同步；1=帧尾——Invoke/协程推进）
[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate void MsCbFrame(IntPtr userData, double dt, int phase);
// P1-a：脚本组件重放（C++ 反序列化完成后回调——托管侧建实例 + 补挂回调 + 回填 scriptFields）
// 显式 LPUTF8Str：脚本注册键/字段 JSON 均为 UTF-8（默认 string 编组是 ANSI——非 ASCII 会乱码）
[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int MsCbScriptReplay(IntPtr userData, IntPtr go, [MarshalAs(UnmanagedType.LPUTF8Str)] string savedKey, [MarshalAs(UnmanagedType.LPUTF8Str)] string fieldsJson);

[StructLayout(LayoutKind.Sequential)]
public struct MsComponentSpec
{
    [MarshalAs(UnmanagedType.LPUTF8Str)] public string? ScriptType;
    public IntPtr OnAwake;
    public IntPtr OnEnable;
    public IntPtr OnStart;
    public IntPtr OnDisable;
    public IntPtr OnDestroy;
    public IntPtr OnUpdate;
    public IntPtr OnFixedUpdate;
    public IntPtr OnLateUpdate;
    public IntPtr UserData;
}

// M2.5 常量（与 ms_bind.h 同步）
public static class BindError
{
    public const int OK = 0;
    public const int ErrNullHandle = -1;
    public const int ErrBadType = -2;
    public const int ErrBadArg = -3;
    public const int ErrInvalidOp = 1;
    public const int ErrNotFound = 2;
    public const int ErrIo = 3;
    public const int ErrHashMismatch = 4;
    public const int ErrTraversalDenied = 5;
    public const int ErrDuplicateName = 6;
    public const int ErrUnknownField = 7;
    public const int ErrNotSupported = 8;
    public const int ErrThread = 9;

    // t2：错误码→人类可读文案便捷（ms_bind_error_text——静态 UTF-8；未知码="未知错误码"）
    public static string ErrorText(int code)
    {
        IntPtr p = Native.ms_bind_error_text(code);
        return p == IntPtr.Zero ? "未知错误码" : Marshal.PtrToStringUTF8(p) ?? "未知错误码";
    }
}