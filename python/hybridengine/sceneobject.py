# hybridengine.sceneobject — SceneObject（AddComponent/GetComponent/AddChild/SetActive/Destroy——与 C# 对称）
import ctypes
from . import _interop
from ._interop import check
from .transform import Transform
from .component import registry_for, PyComponentBase


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
        """按 InstanceId 在所属场景中重新解析出活动指针；已销毁/未指定 → None（调用方跳过原生调用）。"""
        if not self._e or not self._go or not self.id:
            return None
        scene = self._scene
        if not scene:
            scene = _interop.ms_engine_scene(self._e)
        if not scene:
            return None
        live = ctypes.c_void_p()
        if _interop.ms_scene_go_get(self._e, scene, self.id, ctypes.byref(live)) != 0:
            return None
        return live

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
        key = registry_for(self._e).add(inst, self._e, go, self)
        inst._instance_key = key
        self._components.append(inst)
        return inst

    def get_component(self, cls):
        for c in self._components:
            if isinstance(c, cls):
                return c
        return None

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
