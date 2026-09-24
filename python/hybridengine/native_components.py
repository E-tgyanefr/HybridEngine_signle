# hybridengine.native_components — 原生用户场景组件代理（3D 网格/相机/光源 + 2D 精灵）
#
# 与 C# `HybridEngine.Bind/Components.cs` 的 NativeComponent 包装**同形**：方法名/参数序逐一对齐
#   MeshVisual.SetMesh/SetMaterial、SpriteVisual.Set、CameraComponent.Set、LightComponent.Set/Enable
#   → Python: set_mesh/set_material、set、set、set/enable（参数顺序逐一对应，便于双语对照迁移）。
#
# 生命周期（与 transform.py 同一条安全策略，2026-09-21）：
#   代理**只持 owner（SceneObject）**，不缓存任何裸指针。SceneObject 的销毁是延迟的（下个 Tick 才真正 free），
#   旧裸指针是悬垂句柄——解引用即 use-after-free（实测：销毁后从已释放内存读数而不报错）。
#   故每次调用都经 owner._live() 按 InstanceId 重解析活动指针；解析不到 → MS_ERR_NOT_FOUND
#   （与 SceneObject 兄弟方法同码），**不做任何原生调用**。
#
# 代理不持组件句柄：这批 ABI 的寻址口径是 (对象, 组件类型)——如 ms_go_set_mesh 内部走
#   SceneObject::GetComponent<MeshVisual>()（命中**第一个**）。所以同一对象挂两个 MeshVisual 时，
#   两个代理都指向第一个；C# 的 NativeProxyCache 只为 ReferenceEquals 身份，Python 侧无需缓存。
#
# 能力开关（旧 DLL 无这批符号 → None，见 _interop 的 _sig_opt）：
#   调用前一律先判可用性（render3d_abi_available / sprite_abi_available），**绝不对 None 调用**——
#   那是 TypeError，既不是"优雅降级"也不是"明确报错"。
from . import _interop
from ._interop import (check, render3d_abi_available, sprite_abi_available,
                       Render3DNotAvailable, SpriteABINotAvailable)


def require_3d_abi(what: str):
    """3D 用户场景 ABI 门（M5：网格/材质/相机/光源/统计）——不可用则**明确报错**而不是 TypeError。

    `what` = 调用点（进异常消息，便于定位是 host 的哪一行触发的）。
    """
    if not render3d_abi_available():
        raise Render3DNotAvailable(
            "当前 hybridengine.dll 无 3D 用户场景 ABI（ms_go_add_mesh_visual/ms_go_set_mesh/"
            "ms_go_add_camera/ms_camera_set/ms_go_add_light/ms_light_set/ms_light_enable/"
            "ms_go_set_material）——需重建引擎 DLL（含 MMD-3D/M5 批次）；调用点：" + what)


def require_sprite_abi(what: str):
    """2D 精灵 ABI 门（ms_go_add_sprite_visual/ms_sprite_set）——不可用则明确报错。"""
    if not sprite_abi_available():
        raise SpriteABINotAvailable(
            "当前 hybridengine.dll 无 2D 精灵 ABI（ms_go_add_sprite_visual/ms_sprite_set）"
            "——需重建引擎 DLL；调用点：" + what)


def _vec3(values, what: str):
    """3 分量向量 → c_double[3]。

    POINTER(c_double) 形参**不接受 Python list**（ctypes 实测 ArgumentError：expected LP_c_double），
    必须显式转数组。
    ⚠ 长度必须 >= 3：原生侧只认指针、无条件读 3 个 double（少给=越界读），故在包装层先挡（同 renderer._check_blit_len 的理由）。
    """
    seq = list(values)
    if len(seq) < 3:
        raise ValueError("%s 需要 3 个 double（[x, y, z]），实际 %d 个" % (what, len(seq)))
    return (_interop.c_double * 3)(seq[0], seq[1], seq[2])


def _vec3_opt(values, what: str):
    """可空 3 分量向量：None → NULL（原生语义=保持现值），否则同 _vec3。"""
    return None if values is None else _vec3(values, what)


class NativeComponent:
    """原生组件代理基（对齐 C# NativeComponent：轻量视图——生命周期/序列化仍由 C++ 原生组件承担）。

    子类给出 `native_type_name`（C++ 反射注册名——诊断/日志用；ABI 调用本身按类型寻址，不需要它）。
    取用一律经 `SceneObject.add_*`（与 C# 的 Add* 对称）；直接构造只有在要验"宿主未挂该组件"的
    错误路径时才有意义（原生侧会返回 NOT_FOUND/BAD_TYPE——C# 侧对应 GetComponent<T>() 拿不到）。
    """

    native_type_name = ""

    def __init__(self, owner):
        self._owner = owner

    @property
    def owner(self):
        """宿主 SceneObject 视图。"""
        return self._owner

    @property
    def owner_id(self) -> int:
        """宿主对象的 InstanceId（代理失效后仍可用于日志/排查——对齐 C# NativeComponent.OwnerId）。"""
        return self._owner.id or 0

    def _go(self, what: str):
        """解析宿主活动指针；已销毁/解析不到 → MS_ERR_NOT_FOUND（与 SceneObject 兄弟方法同码）。"""
        go = self._owner._live()
        if not go:
            check(_interop.MS_ERR_NOT_FOUND, what)
        return go

    def __repr__(self):
        return "<%s on %r>" % (type(self).__name__, self._owner)


class MeshVisual(NativeComponent):
    """MeshVisual（3D 网格 + 材质——对齐 C# MeshVisual）。

    取用：`go.add_mesh_visual()`（← ms_go_add_mesh_visual）。
    """

    native_type_name = "MeshVisual"

    def set_mesh(self, verts_xyz, tri_count: int, color: int):
        """填网格：verts_xyz = 三角形展开（每三角 3 顶点 × [x,y,z] = 9 doubles，行序），tri_count = 三角数。

        color = 0xAARRGGBB（组件 tint——覆盖网格 baseColor；高 8 位 alpha<FF 自动进透明队列）。
        ⚠ verts_xyz 长度必须 >= tri_count*9：原生 ms_go_set_mesh 只认 tri_count、按 (tri_count*9) 无条件读
          （少给=越界读，实测可 access violation）——故在包装层先校验，不做原生调用。
        """
        require_3d_abi("MeshVisual.set_mesh")
        if not 0 <= color <= 0xFFFFFFFF:
            raise ValueError("color 必须是 0xAARRGGBB（0..0xFFFFFFFF），实际 %r" % (color,))
        need = tri_count * 9 if tri_count > 0 else 0
        seq = list(verts_xyz)
        if len(seq) < need:
            raise ValueError("set_mesh 顶点不足：tri_count=%d 需要 %d 个 double（三角形×9），实际 %d 个"
                             % (tri_count, need, len(seq)))
        arr = (_interop.c_double * need)(*seq[:need]) if need else None
        check(_interop.ms_go_set_mesh(self._owner._e, self._go("ms_go_set_mesh"), arr, tri_count, color),
              "ms_go_set_mesh")

    def set_material(self, specular: float, shininess: float, emissive=None):
        """材质参数（内存态；.hmat 资产持久化同名参数）。

        specular >= 0（0=关高光=legacy 观感）；shininess >= 1（原生把 <1 钳到 1）；
        emissive = 自发光 0..1 的 3 分量，None = 保持现值（对齐 C# SetMaterial 的可空缺省）。
        """
        require_3d_abi("MeshVisual.set_material")
        check(_interop.ms_go_set_material(self._owner._e, self._go("ms_go_set_material"),
                                          specular, shininess, _vec3_opt(emissive, "emissive")),
              "ms_go_set_material")


class SpriteVisual(NativeComponent):
    """SpriteVisual（2D 精灵——对齐 C# SpriteVisual）。

    取用：`go.add_sprite_visual()`（← ms_go_add_sprite_visual）。
    世界坐标=屏幕像素、原点=屏幕中心；位置由 Transform 决定（ms_bind.h:166）。
    """

    native_type_name = "SpriteVisual"

    def set(self, r: float, g: float, b: float, a: float, width: float, height: float,
            texture_asset=None):
        """颜色(0..1)/alpha/像素尺寸/纹理资产路径（None 或 "" = 纯色）——参数序与 C# SpriteVisual.Set 一致。"""
        require_sprite_abi("SpriteVisual.set")
        check(_interop.ms_sprite_set(self._owner._e, self._go("ms_sprite_set"), r, g, b, a,
                                     width, height,
                                     texture_asset.encode("utf-8") if texture_asset else None),
              "ms_sprite_set")


class CameraComponent(NativeComponent):
    """CameraComponent（相机——对齐 C# CameraComponent）。

    取用：`go.add_camera_component()`（← ms_go_add_camera）。
    注：相机参数由**组件**持有（不是 Transform）；Transform 可独立用于 3D 网格等。
    """

    native_type_name = "CameraComponent"

    def set(self, eye, target, fov_deg: float, orthographic: bool = False, ortho_size: float = 10.0):
        """相机位 eye 看向 target（各 = 3 分量世界坐标）；fov_deg 视野角（35..120）；
        orthographic=True 走正交投影（用 ortho_size）。参数序与 C# CameraComponent.Set 一致。"""
        require_3d_abi("CameraComponent.set")
        check(_interop.ms_camera_set(self._owner._e, self._go("ms_camera_set"),
                                     _vec3(eye, "eye"), _vec3(target, "target"), fov_deg,
                                     1 if orthographic else 0, ortho_size),
              "ms_camera_set")


class LightComponent(NativeComponent):
    """LightComponent（光源——对齐 C# LightComponent）。

    取用：`go.add_light_component()`（← ms_go_add_light）。
    ⚠ 场景光有效上限 7 盏/帧（ms_bind.h:172）：着色端 8 槽恒被 legacy 主光占 1 槽 →
      第 8 盏被静默丢弃；要"每盏都确定生效"请配 <= 6 盏。
    """

    native_type_name = "LightComponent"

    def enable(self, on: bool):
        """光源开关（enabled 字段）——参数序与 C# LightComponent.Enable 一致。"""
        require_3d_abi("LightComponent.enable")
        check(_interop.ms_light_enable(self._owner._e, self._go("ms_light_enable"), 1 if on else 0),
              "ms_light_enable")

    def set(self, type, position, direction, color, intensity: float, range: float,
            inner_deg: float, outer_deg: float):   # 名字逐字对齐 C#（type/range 与内建同名——见 docstring）
        """光源参数：type 0=方向光 1=点光 2=聚光；position/direction/color = 3 分量（None = 保持现值）；
        intensity 强度；range 距离衰减半径（0=无）；inner_deg/outer_deg 聚光内外角（度）。

        参数名 `type`/`range` 与内建同名——C# 亦如此（`Set(int type, ..., double range, ...)`），
        这里保持逐字对齐以便双语对照（故不改成 kind/radius）。
        """
        require_3d_abi("LightComponent.set")
        check(_interop.ms_light_set(self._owner._e, self._go("ms_light_set"), type,
                                    _vec3_opt(position, "position"), _vec3_opt(direction, "direction"),
                                    _vec3_opt(color, "color"), intensity, range, inner_deg, outer_deg),
              "ms_light_set")
