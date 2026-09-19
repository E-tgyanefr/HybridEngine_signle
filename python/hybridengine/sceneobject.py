# hybridengine.sceneobject — SceneObject（AddComponent/GetComponent/AddChild/SetActive/Destroy——与 C# 对称）
import ctypes
from . import _interop
from ._interop import check
from .transform import Transform
from .component import _registry, PyComponentBase


class SceneObject:
    def __init__(self, engine_handle, go_handle, obj_id: int, name: str = ""):
        self._e = engine_handle
        self._go = go_handle
        self.id = obj_id
        self._name = name
        self._components = []
        self._children = []
        self.transform = Transform(engine_handle, go_handle)

    @property
    def name(self) -> str:
        buf = ctypes.create_string_buffer(256)
        if _interop.ms_go_name(self._e, self._go, buf, len(buf)) != 0:
            return self._name
        return buf.value.decode("utf-8", "replace")

    @name.setter
    def name(self, v: str):
        self._name = v
        _interop.ms_go_set_name(self._e, self._go, v.encode())

    @property
    def child_count(self) -> int:
        n = ctypes.c_int(0)
        if _interop.ms_go_child_count(self._e, self._go, ctypes.byref(n)) != 0:
            return 0
        return n.value

    def get_child(self, index: int):
        go = ctypes.c_void_p()
        check(_interop.ms_go_child_get(self._e, self._go, index, ctypes.byref(go)), "get_child")
        return SceneObject(self._e, go, _interop.ms_go_instance_id(self._e, go), "")
    def add_component(self, cls):
        inst = cls()
        inst.owner = self
        key = _registry.add(inst, self._e, self._go, self)
        inst._instance_key = key
        self._components.append(inst)
        return inst

    def get_component(self, cls):
        for c in self._components:
            if isinstance(c, cls):
                return c
        return None

    def add_child(self, name: str):
        go = _interop.c_void_p()
        check(_interop.ms_go_add_child(self._e, self._go, name.encode(), ctypes.byref(go)), "add_child")
        child = SceneObject(self._e, go, _interop.ms_go_instance_id(self._e, go), name)
        self._children.append(child)
        return child

    def set_active(self, v: bool):
        check(_interop.ms_go_set_active(self._e, self._go, 1 if v else 0), "set_active")

    def destroy(self):
        check(_interop.ms_go_destroy(self._e, self._go), "destroy")

    def __repr__(self):
        return f"<SceneObject {self.name} #{self.id}>"