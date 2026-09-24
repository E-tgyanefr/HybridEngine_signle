# hybridengine._interop — ctypes 绑定（ms_bind.h 稳定 C ABI——与 C# HybridEngine.Bind 同一 ABI 层；零第三方）
# 线程模型：单线程主线程（回调同步回 Python——调用方持 GIL）；跨线程调用→BindException(MS_ERR_THREAD)
import ctypes
import os

__all__ = ["lib", "BindException", "MS_ERR_*", "ComponentSpec", "CALLBACK_VOID", "CALLBACK_DT"]


def _find_dll() -> str:
    env = os.environ.get("HYBRIDENGINE_DLL")
    if env and os.path.exists(env):
        return env
    here = os.path.dirname(os.path.abspath(__file__))
    for name in ("hybridengine.dll", "libhybridengine.dll", "hybridengine.so"):
        p = os.path.join(here, name)
        if os.path.exists(p):
            return p
    return "hybridengine.dll"


_abs_dll = _find_dll()

# Win10+ 安全加载（LOAD_LIBRARY_SEARCH_*）：DLL 依赖须经 add_dll_directory 登记（PATH 不再兜底——ctypes 现代语义）
for _d in (os.path.dirname(os.path.abspath(_abs_dll)), os.environ.get("HYBRIDENGINE_MINGW", "")):
    if _d and os.path.isdir(_d):
        # Windows 安全加载目录（Linux/macOS 无此 API，跳过）
        if hasattr(os, "add_dll_directory"):
            try:
                os.add_dll_directory(_d)
            except (OSError, ValueError):
                pass

lib = ctypes.CDLL(_abs_dll)

# 便捷别名（各模块引用）
c_void_p = ctypes.c_void_p
c_char_p = ctypes.c_char_p
c_int = ctypes.c_int
c_int64 = ctypes.c_int64
c_uint64 = ctypes.c_uint64
c_double = ctypes.c_double

# ---- 错误码（ms_bind.h 同步） ----
MS_BIND_OK = 0
MS_ERR_NULL_HANDLE = -1
MS_ERR_BAD_TYPE = -2
MS_ERR_BAD_ARG = -3
MS_ERR_INVALID_OP = 1
MS_ERR_NOT_FOUND = 2
MS_ERR_IO = 3
MS_ERR_HASH_MISMATCH = 4
MS_ERR_TRAVERSAL_DENIED = 5
MS_ERR_DUPLICATE_NAME = 6
MS_ERR_UNKNOWN_FIELD = 7
MS_ERR_NOT_SUPPORTED = 8
MS_ERR_THREAD = 9

_NAMES = {
    MS_BIND_OK: "MS_BIND_OK", MS_ERR_NULL_HANDLE: "MS_ERR_NULL_HANDLE",
    MS_ERR_BAD_TYPE: "MS_ERR_BAD_TYPE", MS_ERR_BAD_ARG: "MS_ERR_BAD_ARG",
    MS_ERR_INVALID_OP: "MS_ERR_INVALID_OP", MS_ERR_NOT_FOUND: "MS_ERR_NOT_FOUND",
    MS_ERR_IO: "MS_ERR_IO", MS_ERR_HASH_MISMATCH: "MS_ERR_HASH_MISMATCH",
    MS_ERR_TRAVERSAL_DENIED: "MS_ERR_TRAVERSAL_DENIED", MS_ERR_DUPLICATE_NAME: "MS_ERR_DUPLICATE_NAME",
    MS_ERR_UNKNOWN_FIELD: "MS_ERR_UNKNOWN_FIELD", MS_ERR_NOT_SUPPORTED: "MS_ERR_NOT_SUPPORTED",
    MS_ERR_THREAD: "MS_ERR_THREAD",
}


class BindException(RuntimeError):
    def __init__(self, code: int, context: str = ""):
        self.code = code
        name = _NAMES.get(code, f"code{code}")
        super().__init__(f"{name} ({code})" + (f" @ {context}" if context else ""))


def check(rc: int, context: str = "") -> int:
    if rc != MS_BIND_OK:
        raise BindException(rc, context)
    return rc


def error_text(code: int) -> str:
    """t2 便捷：错误码→人类可读文案（ms_bind_error_text——静态 UTF-8；未知码→空串；旧 DLL 无该符号→空串）"""
    fn = getattr(lib, "ms_bind_error_text", None)
    if fn is None:
        return ""
    fn.restype = ctypes.c_char_p
    fn.argtypes = [ctypes.c_int]
    s = fn(code)
    return s.decode("utf-8", "replace") if s else ""


# ---- 回调类型（ctypes CFUNCTYPE——八回调） ----
CALLBACK_VOID = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
CALLBACK_DT = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_double)


class ComponentSpec(ctypes.Structure):
    _fields_ = [
        ("scriptType", ctypes.c_char_p),
        ("onAwake", CALLBACK_VOID),
        ("onEnable", CALLBACK_VOID),
        ("onStart", CALLBACK_VOID),
        ("onDisable", CALLBACK_VOID),
        ("onDestroy", CALLBACK_VOID),
        ("onUpdate", CALLBACK_DT),
        ("onFixedUpdate", CALLBACK_DT),
        ("onLateUpdate", CALLBACK_DT),
        ("userData", ctypes.c_void_p),
    ]


# ---- 函数签名（43+4 脚本桥——ms_ 前缀保留=稳定 ABI） ----
def _sig(name, restype, argtypes):
    fn = getattr(lib, name)
    fn.restype = restype
    fn.argtypes = argtypes
    return fn


# t1 GUI 平面 ABI（可选能力：旧 DLL 无该批符号——导入不炸；调用时 GUINotAvailable）
def _sig_opt(name, restype, argtypes):
    fn = getattr(lib, name, None)
    if fn is None:
        return None
    fn.restype = restype
    fn.argtypes = argtypes
    return fn


class GUINotAvailable(RuntimeError):
    """当前 hybridengine.dll 无 GUI ABI（ms_rnd_*/ms_text_*/ms_input_mouse_*）——需 t1 后重建引擎 DLL。"""


_GUI_ABI_CACHE = None


def gui_abi_available() -> bool:
    """GUI ABI 符号是否齐全（包装层按此决定能力开关）。

    结果**缓存一次**（M10）：它只取决于已加载的 DLL，进程内不会变；
    而绘制原语每个都会调用本函数（一次 23 发 hasattr），逐调用探测纯属浪费。
    """
    global _GUI_ABI_CACHE
    if _GUI_ABI_CACHE is None:
        _GUI_ABI_CACHE = all(
            hasattr(lib, n)
            for n in ("ms_rnd_clear_list", "ms_rnd_clear", "ms_rnd_clear_rect", "ms_rnd_fill_rect",
                      "ms_rnd_fill_rounded_rect", "ms_rnd_draw_line", "ms_rnd_fill_circle", "ms_rnd_draw_circle",
                      "ms_rnd_fill_triangle", "ms_rnd_fill_quad", "ms_rnd_blit_rect", "ms_rnd_blit_alpha",
                      "ms_rnd_clip_push", "ms_rnd_clip_push_rotated", "ms_rnd_clip_pop",
                      "ms_text_draw", "ms_text_measure",
                      "ms_input_mouse_x", "ms_input_mouse_y", "ms_input_mouse_button",
                      "ms_input_mouse_button_down", "ms_input_mouse_button_up", "ms_input_mouse_wheel_delta")
        )
    return _GUI_ABI_CACHE


P = ctypes.c_void_p
PC = ctypes.c_char_p
D = ctypes.c_double
I = ctypes.c_int
L = ctypes.c_int64  # ms_id
UL = ctypes.c_uint64

ms_engine_create = _sig("ms_engine_create", P, [PC, I, I, ctypes.POINTER(I)])
ms_engine_destroy = _sig("ms_engine_destroy", None, [P])
ms_engine_tick = _sig("ms_engine_tick", I, [P, D])
ms_engine_render = _sig("ms_engine_render", I, [P, P])
ms_engine_pump = _sig("ms_engine_pump", I, [P])
ms_engine_resize = _sig("ms_engine_resize", I, [P, I, I])
ms_engine_last_frame_ms = _sig("ms_engine_last_frame_ms", D, [P])
ms_engine_render_hash = _sig("ms_engine_render_hash", UL, [P])
ms_bind_live_engine_count = _sig("ms_bind_live_engine_count", I, [])

# G2/t137：输入 ABI（vk=KeyCode 值（Win32 VK 直通））
ms_input_key_down = _sig("ms_input_key_down", I, [P, I])
ms_input_key_up = _sig("ms_input_key_up", I, [P, I])
ms_input_get_key = _sig("ms_input_get_key", I, [P, I])
ms_input_axis = _sig("ms_input_axis", D, [P, PC])
ms_input_get_button = _sig("ms_input_get_button", I, [P, PC])

# t1 GUI 平面 ABI：2D 绘制原语（坐标 double、颜色 uint32 0xAARRGGBB——A=alpha 直通（填充原语 straight-alpha 合成；现调用方全 FF=行为不变））
# 旧 DLL 无此批符号 → None（能力开关）；绘制/鼠标调用侧以 gui_abi_available() 判断
ms_rnd_clear_list = _sig_opt("ms_rnd_clear_list", I, [P])
ms_rnd_clear = _sig_opt("ms_rnd_clear", I, [P, ctypes.c_uint32])
ms_rnd_clear_rect = _sig_opt("ms_rnd_clear_rect", I, [P, D, D, D, D, ctypes.c_uint32])
ms_rnd_fill_rect = _sig_opt("ms_rnd_fill_rect", I, [P, D, D, D, D, ctypes.c_uint32])
ms_rnd_fill_rounded_rect = _sig_opt("ms_rnd_fill_rounded_rect", I, [P, D, D, D, D, D, ctypes.c_uint32])
ms_rnd_draw_line = _sig_opt("ms_rnd_draw_line", I, [P, D, D, D, D, ctypes.c_uint32, D])
ms_rnd_fill_circle = _sig_opt("ms_rnd_fill_circle", I, [P, D, D, D, ctypes.c_uint32])
ms_rnd_draw_circle = _sig_opt("ms_rnd_draw_circle", I, [P, D, D, D, ctypes.c_uint32, D])
ms_rnd_fill_triangle = _sig_opt("ms_rnd_fill_triangle", I, [P, D, D, D, D, D, D, ctypes.c_uint32])
ms_rnd_fill_quad = _sig_opt("ms_rnd_fill_quad", I, [P, D, D, D, D, D, D, D, D, ctypes.c_uint32])
ms_rnd_blit_rect = _sig_opt("ms_rnd_blit_rect", I, [P, D, D, D, D, ctypes.POINTER(ctypes.c_uint32)])
ms_rnd_blit_alpha = _sig_opt("ms_rnd_blit_alpha", I, [P, D, D, D, D, ctypes.POINTER(ctypes.c_uint32)])   # t1：src=0xAARRGGBB 逐像素合成
ms_rnd_clip_push = _sig_opt("ms_rnd_clip_push", I, [P, D, D, D, D])   # t6：矩形裁剪入栈（≤8 深；栈顶生效）
ms_rnd_clip_push_rotated = _sig_opt("ms_rnd_clip_push_rotated", I, [P, D, D, D, D, D])   # t-rot-clip：旋转矩形裁剪入栈（中心+角；angle=0≡矩形）
ms_rnd_clip_pop = _sig_opt("ms_rnd_clip_pop", I, [P])                 # t6：弹栈（空栈=MS_ERR_INVALID_OP）

# t1 GUI 平面 ABI：文本（UTF-8；录制 Text 命令——回放绘制双面并参与哈希；size=像素 double）
ms_text_draw = _sig_opt("ms_text_draw", I, [P, PC, D, D, D, ctypes.c_uint32])
ms_text_measure = _sig_opt("ms_text_measure", I, [P, PC, D, ctypes.POINTER(D), ctypes.POINTER(D)])

# t1 GUI 平面 ABI：鼠标（button 0=左 1=中 2=右；wheel delta 本帧累计 ±1）
ms_input_mouse_x = _sig_opt("ms_input_mouse_x", D, [P])
ms_input_mouse_y = _sig_opt("ms_input_mouse_y", D, [P])
ms_input_mouse_button = _sig_opt("ms_input_mouse_button", I, [P, I])
ms_input_mouse_button_down = _sig_opt("ms_input_mouse_button_down", I, [P, I])
ms_input_mouse_button_up = _sig_opt("ms_input_mouse_button_up", I, [P, I])
ms_input_mouse_wheel_delta = _sig_opt("ms_input_mouse_wheel_delta", D, [P])

ms_engine_scene = _sig("ms_engine_scene", P, [P])
ms_scenes_count = _sig("ms_scenes_count", I, [P, ctypes.POINTER(I)])          # 已加载场景数（≥1）
ms_scenes_get = _sig("ms_scenes_get", I, [P, I, ctypes.POINTER(P)])          # 按索引取场景句柄
ms_scene_load_additive = _sig("ms_scene_load_additive", I, [P, PC, ctypes.POINTER(P)])   # 附加加载（活动场景不变）
ms_scene_add_root = _sig("ms_scene_add_root", I, [P, P, PC, ctypes.POINTER(L)])
ms_scene_save = _sig("ms_scene_save", I, [P, P, PC])
ms_scene_load = _sig("ms_scene_load", I, [P, P, PC])
ms_scene_go_get = _sig("ms_scene_go_get", I, [P, P, L, ctypes.POINTER(P)])
ms_scene_root_count = _sig("ms_scene_root_count", I, [P, P, ctypes.POINTER(I)])
ms_scene_root_get = _sig("ms_scene_root_get", I, [P, P, I, ctypes.POINTER(P)])

ms_go_instance_id = _sig("ms_go_instance_id", L, [P, P])
ms_go_set_active = _sig("ms_go_set_active", I, [P, P, I])
ms_go_destroy = _sig("ms_go_destroy", I, [P, P])
ms_go_add_child = _sig("ms_go_add_child", I, [P, P, PC, ctypes.POINTER(P)])
ms_component_register = _sig("ms_component_register", I, [P, ctypes.POINTER(ComponentSpec)])
# H2：为指定对象注册**专属** spec（同键双实例各存一份回调表——带 go 句柄）
ms_component_register_for = _sig_opt("ms_component_register_for", I, [P, P, ctypes.POINTER(ComponentSpec)])
ms_go_add_component = _sig("ms_go_add_component", I, [P, P, PC])
ms_go_get_component = _sig("ms_go_get_component", I, [P, P, PC, ctypes.POINTER(P)])

ms_transform_get = _sig("ms_transform_get", I, [P, P, PC, ctypes.POINTER(D)])
ms_transform_set = _sig("ms_transform_set", I, [P, P, PC, ctypes.POINTER(D)])
ms_transform_parent = _sig("ms_transform_parent", L, [P, P])
ms_transform_set_parent = _sig("ms_transform_set_parent", I, [P, P, L, I])
# MMD-3D / M5：用户场景 3D（MeshVisual/CameraComponent/LightComponent）——可选能力（旧 DLL 无该批符号=返回 None）
ms_go_add_mesh_visual = _sig_opt("ms_go_add_mesh_visual", I, [P, P])
ms_go_set_mesh = _sig_opt("ms_go_set_mesh", I, [P, P, ctypes.POINTER(D), I, ctypes.c_uint32])   # verts=tri×9 doubles；color=0xAARRGGBB
ms_go_add_camera = _sig_opt("ms_go_add_camera", I, [P, P])
ms_camera_set = _sig_opt("ms_camera_set", I, [P, P, ctypes.POINTER(D), ctypes.POINTER(D), D, I, D])
ms_go_add_light = _sig_opt("ms_go_add_light", I, [P, P])
ms_light_set = _sig_opt("ms_light_set", I, [P, P, I, ctypes.POINTER(D), ctypes.POINTER(D), ctypes.POINTER(D), D, D, D, D])
ms_light_enable = _sig_opt("ms_light_enable", I, [P, P, I])
ms_go_set_material = _sig_opt("ms_go_set_material", I, [P, P, D, D, ctypes.POINTER(D)])
ms_engine_render3d_stats = _sig_opt("ms_engine_render3d_stats", I, [P, ctypes.POINTER(UL), I])

# 2D：SpriteVisual（ms_bind.h:166 归 2D 段——世界坐标=屏幕像素/原点=屏幕中心）
# 故**不并进**上面的 3D 批次：render3d_abi_available() 不含这两个符号，另有 sprite_abi_available()。
# 两者都用 _sig_opt（旧 DLL → None）；调用侧经 native_components.require_sprite_abi 先判可用性，
# **绝不对 None 调用**（那是 TypeError，既非优雅降级也非明确报错）。
ms_go_add_sprite_visual = _sig_opt("ms_go_add_sprite_visual", I, [P, P])
ms_sprite_set = _sig_opt("ms_sprite_set", I, [P, P, D, D, D, D, D, D, PC])   # cr,cg,cb,ca,width,height,textureAsset（UTF-8；NULL=纯色）

# H4：脚本承载桥 + 场景重放（旧 DLL 无这些入口 → None，见 script_bridge.register 的降级）
#   ⚠ 这 5 个符号**只在此处声明一次**，且必须用 _sig_opt：若在本文件别处再用 _sig 声明一遍，
#     后声明会覆盖这里，旧 DLL 上 `import hybridengine` 直接 AttributeError，
#     register() 里的 `is None` 降级分支永远走不到（本文件曾有过这样一份重复声明，已删）。
ms_script_bridge_register = _sig_opt("ms_script_bridge_register", I, [P, P, P, P, P])
ms_script_replay_register = _sig_opt("ms_script_replay_register", I, [P, P, P])
ms_script_fields = _sig_opt("ms_script_fields", I, [P, PC, P, I])
ms_script_field_set = _sig_opt("ms_script_field_set", I, [P, PC, PC, PC])
ms_script_types = _sig_opt("ms_script_types", I, [P, P, I])


class Render3DNotAvailable(RuntimeError):
    """当前 hybridengine.dll 无 3D 用户场景 ABI（ms_go_add_mesh_visual/ms_go_set_mesh/ms_camera_*/
    ms_go_add_light/ms_light_*/ms_go_set_material）——需重建引擎 DLL（含 MMD-3D/M5 批次）。"""


def render3d_abi_available() -> bool:
    """3D 用户场景 ABI 是否齐全（M5：网格/材质/相机/光源/统计——旧 DLL=无）。

    **调用点**：`native_components.require_3d_abi()`（MeshVisual/CameraComponent/LightComponent 的
    全部入口 + `SceneObject.add_mesh_visual/add_camera_component/add_light_component`）。
    这些符号都用 `_sig_opt` 取（旧 DLL → None），故调用前必须先经本函数判定，
    否则会对着 None 调用而抛 TypeError（既不是"优雅降级"也不是"明确报错"）——
    ABI 缺失时由 require_3d_abi 抛 Render3DNotAvailable。

    结果**不缓存**（与 gui_abi_available 的缓存不同）：本函数只在能力入口调用（每帧热路径不经过它），
    而测试要能 monkeypatch 符号后立刻看到判定翻转。
    """
    return all(
        globals().get(n) is not None
        for n in ("ms_go_add_mesh_visual", "ms_go_set_mesh", "ms_go_add_camera", "ms_camera_set",
                  "ms_go_add_light", "ms_light_set", "ms_light_enable", "ms_go_set_material",
                  "ms_engine_render3d_stats")
    )


class SpriteABINotAvailable(RuntimeError):
    """当前 hybridengine.dll 无 2D 精灵 ABI（ms_go_add_sprite_visual/ms_sprite_set）——需重建引擎 DLL。"""


def sprite_abi_available() -> bool:
    """2D 精灵 ABI 符号是否齐全（旧 DLL=无）。

    与 render3d_abi_available 分开：ms_bind.h 把 SpriteVisual 归 2D 段，两个符号**不在** 3D 批次里，
    两者可独立缺失（故各自判各自的）。调用点：`native_components.require_sprite_abi()`。
    """
    return all(globals().get(n) is not None for n in ("ms_go_add_sprite_visual", "ms_sprite_set"))

ms_property_get = _sig("ms_property_get", I, [P, P, PC, PC, ctypes.c_char_p, I])
ms_property_set = _sig("ms_property_set", I, [P, P, PC, PC, PC])
ms_property_fields = _sig("ms_property_fields", I, [P, PC, ctypes.c_char_p, I])

ms_assets_load = _sig("ms_assets_load", P, [P, PC, ctypes.POINTER(I)])
ms_assets_type = _sig("ms_assets_type", I, [P, P, ctypes.c_char_p, I])
ms_assets_guid = _sig("ms_assets_guid", I, [P, P, ctypes.c_char_p, I])
ms_assets_unref = _sig("ms_assets_unref", None, [P, P])
ms_assets_set_root = _sig("ms_assets_set_root", I, [P, PC])
ms_assets_save = _sig("ms_assets_save", I, [P, PC, P, I, PC])
ms_assets_save_text = _sig("ms_assets_save_text", I, [P, PC, PC, PC])
ms_assets_list = _sig("ms_assets_list", I, [P, PC, ctypes.c_char_p, I])   # t7：递归 List→JSON 数组（cap 溢出=BAD_ARG）
ms_assets_texture_pixels = _sig("ms_assets_texture_pixels", I, [P, P, ctypes.POINTER(I), ctypes.POINTER(I), ctypes.POINTER(I), ctypes.POINTER(P)])   # t7：纹理像素借出指针

# t8：音频句柄面（与 C ABI 一一对应——open 返回 ≥10=句柄 id；失败=错误码）与手柄（XInput）
ms_audio_open = _sig("ms_audio_open", I, [P, PC])
ms_audio_close = _sig("ms_audio_close", I, [P, L])
ms_audio_play = _sig("ms_audio_play", I, [P, L])
ms_audio_pause = _sig("ms_audio_pause", I, [P, L])
ms_audio_seek = _sig("ms_audio_seek", I, [P, L, D])
ms_audio_position = _sig("ms_audio_position", I, [P, L, ctypes.POINTER(D)])
ms_audio_is_open = _sig("ms_audio_is_open", I, [P, L])
ms_audio_volume = _sig("ms_audio_volume", I, [P, L, D])   # t8：真增益（0..1；后端不支持=记录语义）
ms_audio_loop = _sig("ms_audio_loop", I, [P, L, I])        # t8：循环（1/0）
ms_gamepad_count = _sig("ms_gamepad_count", I, [P])
ms_gamepad_connected = _sig("ms_gamepad_connected", I, [P, I])
ms_gamepad_button = _sig("ms_gamepad_button", I, [P, I, I])
ms_gamepad_button_down = _sig("ms_gamepad_button_down", I, [P, I, I])
ms_gamepad_button_up = _sig("ms_gamepad_button_up", I, [P, I, I])
ms_gamepad_axis = _sig("ms_gamepad_axis", D, [P, I, I])
# P0：场景查询/命名/子级/组件移除/预制体实例化
ms_scene_find = _sig("ms_scene_find", I, [P, P, PC, ctypes.POINTER(P)])
ms_go_name = _sig("ms_go_name", I, [P, P, c_void_p, I])
ms_go_set_name = _sig("ms_go_set_name", I, [P, P, PC])
ms_go_child_count = _sig("ms_go_child_count", I, [P, P, ctypes.POINTER(I)])
ms_go_child_get = _sig("ms_go_child_get", I, [P, P, I, ctypes.POINTER(P)])
ms_go_remove_component = _sig("ms_go_remove_component", I, [P, P, PC])
ms_assets_instantiate_prefab = _sig("ms_assets_instantiate_prefab", I, [P, PC, ctypes.POINTER(P)])