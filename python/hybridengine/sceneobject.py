# hybridengine.sceneobject — SceneObject（AddComponent/GetComponent/AddChild/SetActive/Destroy——与 C# 对称）
import ctypes
from . import _interop
from ._interop import check
from .transform import Transform
from .component import registry_for, PyComponentBase
from .native_components import (MeshVisual, SpriteVisual, CameraComponent, LightComponent,
                                require_3d_abi, require_sprite_abi)


class SceneObject:
    """SceneObject 句柄代理。

    ⚠ 关于 `_go` 的所有权（2026-09-21 修复）：
      原生 SceneObject 的销毁是**延迟**的（下个 Tick 才真正 free 根节点），
      销毁后旧裸指针即为悬垂句柄，任何解引用都是 use-after-free。
      实测（Python）：对象销毁后 `go.name` 仍"成功"返回旧值、`go.transform.position`
      返回 [0,0,0]——不是崩溃而是**从已释放内存读数**（不确定行为，换台机器可能就是崩）。
      故对外一律经 `_live()` 重新按 InstanceId 解析；解析不到（Zero）就由原生侧
      返回 BAD_ARG，不会崩。
    """

    def __init__(self, engine_handle, go_handle, obj_id: int, name: str = "", scene_handle=None):
        self._e = engine_handle
        self._go = go_handle
        # 所属场景：ms_scene_go_get 按**传入场景**查找（多场景下用活动场景会读错场景）。
        # 未提供时回落活动场景（单场景等价）。
        self._scene = scene_handle
        self.id = obj_id
        self._name = name
        self._components = []
        self._children = []
        self.transform = Transform(self)

    def _live(self):
        """按 InstanceId 在所属场景中重新解析出活动指针；已销毁/未指定 → None（调用方跳过原生调用）。

        场景归属（与 C# SceneObject.ResolveLivePtr 同策略；ms_scene_go_get 是**按传入场景**查找的）：
          ① 构造时给了 scene_handle → **只认它**。不能"找不到就换个场景再找"——
             那会在多场景下解析到另一个场景里同 id 的对象。
          ② 没给 → 先活动场景（单场景下即唯一路径，与旧行为一致）。
          ③ 活动场景里没有 → 扫其余已加载场景（附加加载时由重放/宿主建的视图只拿得到 go 指针）。
        这样"猜"是安全的：InstanceId 由**进程级**单调计数器发号（core/instance.hpp：从 1 起、
        永不复用；反序列化也重新发号，不还原盘上 id）→ 全进程唯一，不会撞到别的场景的对象。
        """
        if not self._e or not self._go or not self.id:
            return None
        live = ctypes.c_void_p()
        if self._scene:
            if _interop.ms_scene_go_get(self._e, self._scene, self.id, ctypes.byref(live)) == 0:
                return live
            return None
        candidates = []
        active = _interop.ms_engine_scene(self._e)
        if active:
            candidates.append(active)
        n = ctypes.c_int(0)
        if _interop.ms_scenes_count(self._e, ctypes.byref(n)) == 0:
            for i in range(n.value):
                s = ctypes.c_void_p()
                if _interop.ms_scenes_get(self._e, i, ctypes.byref(s)) != 0 or not s.value:
                    continue
                if s.value in candidates:
                    continue
                candidates.append(s.value)
        for s in candidates:
            if _interop.ms_scene_go_get(self._e, s, self.id, ctypes.byref(live)) == 0:
                return live
        return None

    @property
    def alive(self) -> bool:
        """该代理对应的原生对象是否仍存在（销毁后为 False）。"""
        return bool(self._live())

    @property
    def name(self) -> str:
        go = self._live()
        if not go:
            return self._name            # 已销毁：退回缓存名，不触碰悬垂指针
        buf = ctypes.create_string_buffer(256)
        if _interop.ms_go_name(self._e, go, buf, len(buf)) != 0:
            return self._name
        return buf.value.decode("utf-8", "replace")

    @name.setter
    def name(self, v: str):
        self._name = v
        go = self._live()
        if go:
            _interop.ms_go_set_name(self._e, go, v.encode())

    @property
    def child_count(self) -> int:
        go = self._live()
        if not go:
            return 0
        n = ctypes.c_int(0)
        if _interop.ms_go_child_count(self._e, go, ctypes.byref(n)) != 0:
            return 0
        return n.value

    def get_child(self, index: int):
        go = self._live()
        if not go:
            check(_interop.MS_ERR_NOT_FOUND, "get_child")   # 已销毁 → 与原生同码报错
        child = ctypes.c_void_p()
        check(_interop.ms_go_child_get(self._e, go, index, ctypes.byref(child)), "get_child")
        return SceneObject(self._e, child, _interop.ms_go_instance_id(self._e, child), "",
                           scene_handle=self._scene)

    def add_component(self, cls):
        inst = cls()
        inst.owner = self
        go = self._live()
        if not go:
            check(_interop.MS_ERR_NOT_FOUND, "add_component")
        # 按**本对象所属引擎**取注册表（此前是全局单例 → 任一引擎释放会清掉其它引擎的 thunk）
        # _instance_key/owner 由 _register_inner 统一写入——此处不再重复赋一遍。
        key = registry_for(self._e).add(inst, self._e, go, self)
        self._components.append(inst)
        return inst

    def get_component(self, cls):
        for c in self._components:
            if isinstance(c, cls):
                return c
        return None

    # —— 原生用户场景组件（与 C# SceneObject.AddMeshVisual/AddSpriteVisual/AddCameraComponent/AddLightComponent 对称）——
    # 原生组件（C++ 侧）生命周期/序列化由引擎承担，返回的是**轻量代理**（只持本对象——见 native_components）。
    # 三个 3D 入口先过 render3d_abi_available 门、精灵入口过 sprite_abi_available 门：
    # 符号缺失时抛明确异常，而不是对着 None 调用抛 TypeError。
    # 代理也登记进 _components：get_component(MeshVisual) 与 C# GetComponent<MeshVisual>() 同形
    #   （原生侧寻址是 (对象, 类型)，GetComponent 命中的是**第一个**同类组件）。
    def _add_native(self, cls, sym, what: str, gate):
        gate(what)
        go = self._live()
        if not go:
            check(_interop.MS_ERR_NOT_FOUND, what)   # 已销毁 → 与原生同码报错（不解引用悬垂指针）
        check(sym(self._e, go), what)
        comp = cls(self)
        self._components.append(comp)
        return comp

    def add_mesh_visual(self):
        """挂 MeshVisual（3D 网格——网格数据经返回代理的 set_mesh 填充；空网格=不绘制）。

        ⚠ 原生 Add* **不去重**（ms_bind.h:144）：同一对象可挂多个同类组件，
        而 ms_go_set_mesh 走 GetComponent<T>() 只命中**第一个**——要改"刚加的这个"请只用最近一次的返回值。
        """
        return self._add_native(MeshVisual, _interop.ms_go_add_mesh_visual,
                                "SceneObject.add_mesh_visual", require_3d_abi)

    def add_sprite_visual(self):
        """挂 SpriteVisual（2D 精灵——颜色/尺寸经返回代理的 set 填充）。"""
        return self._add_native(SpriteVisual, _interop.ms_go_add_sprite_visual,
                                "SceneObject.add_sprite_visual", require_sprite_abi)

    def add_camera_component(self):
        """挂 CameraComponent（相机——参数经返回代理的 set 填充）。"""
        return self._add_native(CameraComponent, _interop.ms_go_add_camera,
                                "SceneObject.add_camera_component", require_3d_abi)

    def add_light_component(self):
        """挂 LightComponent（光源——参数经返回代理的 set/enable 填充；默认方向光）。"""
        return self._add_native(LightComponent, _interop.ms_go_add_light,
                                "SceneObject.add_light_component", require_3d_abi)

    def add_child(self, name: str):
        go = self._live()
        if not go:
            check(_interop.MS_ERR_NOT_FOUND, "add_child")
        child = ctypes.c_void_p()
        check(_interop.ms_go_add_child(self._e, go, name.encode(), ctypes.byref(child)), "add_child")
        obj = SceneObject(self._e, child, _interop.ms_go_instance_id(self._e, child), name,
                          scene_handle=self._scene)
        self._children.append(obj)
        return obj

    def set_active(self, v: bool):
        go = self._live()
        if not go:
            check(_interop.MS_ERR_NOT_FOUND, "set_active")
        check(_interop.ms_go_set_active(self._e, go, 1 if v else 0), "set_active")

    def destroy(self):
        """销毁对象。**幂等**：销毁后清零裸指针，二次调用直接返回。"""
        if not self._go:
            return
        go = self._live()
        if go:
            _interop.ms_go_destroy(self._e, go)
        self._go = None                  # 关键：不留可被再次使用的悬垂指针

    def __repr__(self):
        return f"<SceneObject {self.name} #{self.id}>"
