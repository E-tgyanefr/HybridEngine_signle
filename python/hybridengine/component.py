# hybridengine.component — PyComponentBase（八回调基类）+ 全局 registry（与 C# ComponentBridge 对称）
import ctypes
from . import _interop
from ._interop import CALLBACK_VOID, CALLBACK_DT, ComponentSpec, check


class PyComponentBase:
    """八回调基类（标准顺序由 C++ LifecycleDriver 保证——与 C# ComponentBase 对称）。
    用户子类：
        class Scorer(PyComponentBase):
            def update(self, dt): ...
    """
    def __init__(self):
        self.owner = None          # SceneObject（注册时回填）
        self.enabled = True
        self._instance_key = ""

    def awake(self): ...
    def on_enable(self): ...
    def start(self): ...
    def update(self, dt): ...
    def fixed_update(self, dt): ...
    def late_update(self, dt): ...
    def on_disable(self): ...
    def on_destroy(self): ...


class _ComponentRegistry:
    """全局注册表：userData=id(inst)（Python 对象指针）——强引用保活（防 GC 回收= C# GCHandle 对称）。
    另维护 instanceKey=FullName#n（与 C# ComponentBridge 同款多实例安全）。"""
    def __init__(self):
        self._instances = {}      # id -> instance
        self._by_key = {}         # instanceKey -> id
        self._counter = 0
        self._thunks = []         # CFUNCTYPE 对象保活（防 GC）

    def add(self, inst: PyComponentBase, engine_handle, go_handle, go_wrapper=None) -> str:
        self._counter += 1
        key = f"{type(inst).__qualname__}#{self._counter}"
        self._instances[id(inst)] = inst
        self._by_key[key] = id(inst)
        inst._instance_key = key
        inst.owner = go_wrapper   # SceneObject 包装回填（对称 C# Owner）
        spec = self._make_spec(key, ctypes.c_void_p(id(inst)))
        check(_interop.ms_component_register(engine_handle, ctypes.byref(spec)), "component_register")
        check(_interop.ms_go_add_component(engine_handle, go_handle, key.encode()), "go_add_component")
        return key

    def _make_spec(self, key: str, user_data) -> ComponentSpec:
        inst = self._instances[user_data.value]
        t1 = CALLBACK_VOID(_thunk("awake"))
        t2 = CALLBACK_VOID(_thunk("on_enable"))
        t3 = CALLBACK_VOID(_thunk("start"))
        t4 = CALLBACK_VOID(_thunk("on_disable"))
        t5 = CALLBACK_VOID(_thunk("on_destroy"))
        t6 = CALLBACK_DT(_thunk_dt("update"))
        t7 = CALLBACK_DT(_thunk_dt("fixed_update"))
        t8 = CALLBACK_DT(_thunk_dt("late_update"))
        self._thunks.extend([t1, t2, t3, t4, t5, t6, t7, t8])
        return ComponentSpec(
            key.encode(), t1, t2, t3, t4, t5, t6, t7, t8, user_data,
        )

    def get(self, inst_id: int):
        return self._instances.get(inst_id)

    def find_by_key(self, key: str):
        i = self._by_key.get(key)
        return self._instances.get(i) if i else None

    def release_all(self):
        self._instances.clear()
        self._by_key.clear()
        self._thunks.clear()

    def __len__(self):
        return len(self._instances)


_registry = _ComponentRegistry()


def _thunk(fn_name):
    @CALLBACK_VOID
    def thunk(ctx):
        inst = _registry.get(int(ctx))
        if inst is not None:
            getattr(inst, fn_name)()
    return thunk


def _thunk_dt(fn_name):
    @CALLBACK_DT
    def thunk(ctx, dt):
        inst = _registry.get(int(ctx))
        if inst is not None:
            getattr(inst, fn_name)(dt)
    return thunk


def _owner_of(go_handle, engine_handle):
    """延迟回填（由 SceneObject 包装处理）——详见 sceneobject.add_component"""
    return None