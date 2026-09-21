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


class _EngineRegistry:
    """**单个引擎**的注册表。

    为什么必须按引擎分域（2026-09-21 修复）：
      原先是一个模块级全局 `_registry`，而 `Engine.dispose()` 会 `release_all()` 把它整个清空。
      于是**任何一个引擎被释放，其它仍存活的引擎的 thunk 就全被 GC 回收**——
      但 C++ 侧 SpecTable 仍存着那些函数指针、BindComponent 仍持有 spec 指针，
      下一次 ms_engine_tick 就是"调用已释放的代码"：实测
      `OSError: exception: access violation writing 0x...`（进程级崩溃）。
      现在每个引擎一份，release 只清自己那份。
    """
    def __init__(self):
        self._instances = {}      # inst_id -> instance（强引用保活，防 GC）
        self._by_key = {}         # instanceKey -> inst_id
        self._counter = 0
        self._thunks = []         # CFUNCTYPE 对象保活（防 GC）
        self._next_inst_id = 1    # 自己发号，不用 id(inst)：对象回收后 id 会被复用，会串号
        # 具名表序号（1..N，只增不复用）——打包进 ctx 供 thunk 反查所属表
        self.slot = _next_slot()

    def add(self, inst: PyComponentBase, engine_handle, go_handle, go_wrapper=None) -> str:
        self._counter += 1
        key = f"{type(inst).__qualname__}#{self._counter}"
        inst_id = self._next_inst_id
        self._next_inst_id += 1
        self._instances[inst_id] = inst
        self._by_key[key] = inst_id
        inst._instance_key = key
        inst.owner = go_wrapper   # SceneObject 包装回填（对称 C# Owner）
        spec = self._make_spec(key, inst_id)
        check(_interop.ms_component_register(engine_handle, ctypes.byref(spec)), "component_register")
        check(_interop.ms_go_add_component(engine_handle, go_handle, key.encode()), "go_add_component")
        return key

    def _make_spec(self, key: str, inst_id: int) -> ComponentSpec:
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
            key.encode(), t1, t2, t3, t4, t5, t6, t7, t8,
            ctypes.c_void_p(self.pack_ctx(inst_id)),
        )

    def pack_ctx(self, inst_id: int) -> int:
        """把 (本表序号, 实例号) 打包成一个 ctx 整数，thunk 侧可逆解出。

        为什么不直接用实例号：ctx 只有这一个整数，thunk 必须据此判断**哪个引擎**的表。
        用具名表序号（1..N，稳定不复用）而不是 ctx 内部再查表——ctx 在 C++ 侧会被存
        （spec.userData），必须是无状态的纯整数。"""
        return (self.slot << 32) | (inst_id & 0xFFFFFFFF)

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


# 引擎句柄 → 该引擎的注册表。**不做跨引擎共享**（见 _EngineRegistry 的说明）。
_registries: dict = {}
# 具名表序号发号器（只增）——ctx 高 32 位即此序号，thunk 据此找回所属表
_slot_counter = 0


def _next_slot() -> int:
    global _slot_counter
    _slot_counter += 1
    return _slot_counter


# 序号 → 注册表（ctx 反查用）。release 时移除，序号本身永不复用。
_slots: dict = {}


def registry_for(engine_handle) -> _EngineRegistry:
    """取（或建）某引擎的注册表。engine_handle 为 None/0 时退回一个"无名"表，
    以兼容少量历史调用点（不参与 release 分域）。"""
    key = int(engine_handle) if engine_handle else 0
    reg = _registries.get(key)
    if reg is None:
        reg = _EngineRegistry()
        _registries[key] = reg
        _slots[reg.slot] = reg
    return reg


def release_engine(engine_handle) -> bool:
    """只释放**该引擎**的注册表（Engine.dispose 调用）。

    调用时机：必须在 ms_engine_destroy **之后**——先 destroy 再丢 thunk，
    避免销毁过程中（OnDisable/OnDestroy 回调）正好撞上已释放的指针。
    只丢 thunk（会释放 libffi 闭包）；序号不复用，故迟到的回调解出的是空表 → 安全 no-op。"""
    key = int(engine_handle) if engine_handle else 0
    reg = _registries.pop(key, None)
    if reg is None:
        return False
    reg.release_all()
    _slots.pop(reg.slot, None)
    return True


def _resolve_ctx(ctx):
    """把打包 ctx 解回 (注册表, 实例)。表已释放则返回 (None, None)——静默 no-op。"""
    v = int(ctx)
    reg = _slots.get(v >> 32)
    if reg is None:
        return None, None
    return reg, reg.get(v & 0xFFFFFFFF)


def _thunk(fn_name):
    @CALLBACK_VOID
    def thunk(ctx):
        reg, inst = _resolve_ctx(ctx)
        if inst is not None:
            getattr(inst, fn_name)()
    return thunk


def _thunk_dt(fn_name):
    @CALLBACK_DT
    def thunk(ctx, dt):
        reg, inst = _resolve_ctx(ctx)
        if inst is not None:
            getattr(inst, fn_name)(dt)
    return thunk


def _owner_of(go_handle, engine_handle):
    """延迟回填（由 SceneObject 包装处理）——详见 sceneobject.add_component"""
    return None