# hybridengine.script_bridge — Python 侧的**脚本承载桥**
#
# 为什么要它（H4）：场景里含脚本组件时，ms_scene_load 只走"脚本重放"路径的条件是
#   `Script::HostingActive()` —— 即**桥（fields/set/types）+ 重放回调都就绪**。
#   此前 Python 只包了组件生命周期（PyComponentBase 八回调），没注册桥，
#   于是用 Python 存的含 Python 组件的场景**加载不回来**（报 MS_ERR_NOT_SUPPORTED）。
#   本模块补齐这条通路，与 C# 的 CsScriptBridge + SceneReplay 对称。
#
# 与 C# 对齐的**字段 schema**（决定场景文件里 scriptFields 的内容）：
#   可序列化字段 = public 且非可调用属性，类型限于 {bool, int, float, str, 三元数值序列}；
#   序列化形式：int -> 123、float -> G 格式、bool -> true/false、str -> "..."（JSON 转义）、
#              三元数值序列 -> [x,y,z]（对齐 C# Vector3）。
#   注：用 json.dumps 生成字符串而非手工拼引号——C# 那边手写引号不转义引号字符（潜在缺陷），
#   Python 侧走标准转义更稳，两边语义一致（都是 JSON 字符串）。
#
# ⚠ 回调对象（CFUNCTYPE）**必须被强引用保活**：一旦被 GC 回收，
#   C++ 侧存的就是已释放的 libffi 闭包 → 崩溃。故统一挂在 Engine 实例上（见 register）。
import ctypes
import json
import math

from . import _interop
from ._interop import I, P, PC, D, check, MS_ERR_BAD_ARG

# 回调签名（与 ms_bind.h 的 ms_cb_script_* 一致；cdecl）
_CB_FIELDS = ctypes.CFUNCTYPE(ctypes.c_int, P, PC, P, ctypes.c_int)
_CB_SET = ctypes.CFUNCTYPE(ctypes.c_int, P, PC, PC, PC)
_CB_TYPES = ctypes.CFUNCTYPE(ctypes.c_int, P, P, ctypes.c_int)
_CB_REPLAY = ctypes.CFUNCTYPE(ctypes.c_int, P, P, PC, PC)

# 可序列化标量类型（bool 必须在 int 之前判定——Python 里 bool 是 int 的子类）
_SCALARS = (bool, int, float, str)

# 桥内部字段（不属于脚本的可编辑字段）：字段**暴露**与**回填**必须用同一份名单，
#   否则会出现"检查器能改、但重放回填被跳过"（或反之）的不对称。故提为模块常量。
_SKIP_FIELDS = frozenset({"owner", "enabled", "_instance_key"})


def _is_vec3(v) -> bool:
    if isinstance(v, (list, tuple)) and len(v) == 3:
        return all(isinstance(x, (int, float)) and not isinstance(x, bool) for x in v)
    return False


def _serializable(v) -> bool:
    return isinstance(v, _SCALARS) or _is_vec3(v)


def _to_json(v) -> str:
    """组件字段 → JSON 值文本（与 C# ToJson 同 schema）。"""
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    if isinstance(v, float):
        if not math.isfinite(v):
            return "null"          # inf/nan 不是合法 JSON——与 C# 的 G 格式不同，这里显式降级
        return repr(v) if v != int(v) else str(int(v))
    if isinstance(v, str):
        return json.dumps(v, ensure_ascii=False)
    if _is_vec3(v):
        return "[" + ",".join(_to_json(x) for x in v) + "]"
    return "null"


def _from_json(text: str, current):
    """JSON 值文本 → Python 值（按现有字段类型定型；与 C# FromJson 同 schema）。"""
    t = json.loads(text)
    if isinstance(current, bool):
        return bool(t)
    if isinstance(current, int) and not isinstance(current, bool):
        return int(round(float(t)))
    if isinstance(current, float):
        return float(t)
    if isinstance(current, str):
        return t if isinstance(t, str) else str(t)
    if _is_vec3(current):
        return [float(x) for x in t]
    if t is None:
        return None
    return t


def _public_fields(inst):
    """可序列化字段：public（不以 _ 开头）属性，跳过可调用/私有/内部桥字段。

    与 C# 的 `GetFields(Public|Instance)` + `GetProperties` 口径对齐；
    Python 没有"公开/私有"的强制边界，故用**前导下划线**作约定，
    并显式排除本模块与组件基类用的内部名（owner/_instance_key/...）。
    """
    skip = _SKIP_FIELDS
    out = {}
    for name in dir(type(inst)):
        if name.startswith("_") or name in skip:
            continue
        try:
            v = getattr(inst, name)
        except Exception:
            continue           # 取值抛异常=跳过这一项（不让它把整份字段 JSON 带崩）
        if callable(v):
            continue
        if _serializable(v):
            out[name] = v
    return out


class _Bridge:
    """一个引擎的桥实例：持有回调对象（保活）+ 提供实际实现。"""

    def __init__(self, engine):
        self._engine = engine
        self._e = engine._e
        # 回调对象（保活——挂在 self 上，self 又被 Engine 强引用）
        self._cb_fields = _CB_FIELDS(self._on_fields)
        self._cb_set = _CB_SET(self._on_set)
        self._cb_types = _CB_TYPES(self._on_types)
        self._cb_replay = _CB_REPLAY(self._on_replay)

    # —— 工具 ——
    def _write(self, out, cap: int, text: str) -> int:
        """按 UTF-8 写入 out/cap。

        返回码契约（见 ms_bind.h 的 ms_cb_script_fields）：
          · 容量不足 → `MS_ERR_BAD_ARG`（**必须可区分**：引擎据此换大缓冲重试一次；
            旧实现返回 -1，与"NULL 句柄"同码，引擎无法识别 → 字段超限时静默丢弃）；
          · 成功 → 0。
        **不许截断**：写出半个 JSON 比失败更难查。
        """
        b = text.encode("utf-8")
        if cap <= 0 or len(b) + 1 > cap:
            return MS_ERR_BAD_ARG
        ctypes.memmove(out, b, len(b))
        ctypes.memset(out + len(b), 0, 1)
        return 0

    def _find(self, instance_key: str):
        from .component import registry_for
        return registry_for(self._e).find_by_key(instance_key)

    # —— 桥回调 ①：读字段（编辑器检查器 + 存档 scriptFields 共用）——
    def _on_fields(self, user_data, instance_key, out, cap):
        try:
            key = instance_key.decode("utf-8")
        except Exception:
            return -1
        comp = self._find(key)
        if comp is None:
            return -1
        obj = {name: v for name, v in _public_fields(comp).items()}
        text = "{" + ",".join(
            json.dumps(k, ensure_ascii=False) + ":" + _to_json(v) for k, v in obj.items()
        ) + "}"
        return self._write(out, cap, text)

    # —— 桥回调 ②：写字段（检查器编辑 + 重放回填）——
    def _on_set(self, user_data, instance_key, field, json_value):
        try:
            key = instance_key.decode("utf-8")
            name = field.decode("utf-8")
            text = json_value.decode("utf-8")
        except Exception:
            return -1
        comp = self._find(key)
        if comp is None:
            return -1
        if name.startswith("_") or name in _SKIP_FIELDS:
            return -1
        cls_attr = getattr(type(comp), name, None)
        if cls_attr is None or callable(cls_attr):
            return -1
        try:
            current = getattr(comp, name)
        except Exception:
            return -1
        if not _serializable(current) and current is not None:
            return -1
        try:
            setattr(comp, name, _from_json(text, current))
        except Exception:
            return -1
        return 0

    # —— 桥回调 ③：类型清单（节点图调色板用）——
    def _on_types(self, user_data, out, cap):
        from .component import registry_for
        names = sorted({type(c).__qualname__ for c in registry_for(self._e).all_instances()})
        return self._write(out, cap, json.dumps(names, ensure_ascii=False))

    # —— 重放回调：场景加载时按保存键重建 Python 组件 ——
    #   与 C# SceneReplay.Replay 同流程：解析类型 → 实例化 → 建 Owner 视图 →
    #   BindExisting（按 (键, 对象) 补挂回调表）→ 回填字段 → Awake/OnEnable。
    def _on_replay(self, user_data, go, saved_key, fields_json):
        try:
            from .sceneobject import SceneObject
            from .component import registry_for
            key = saved_key.decode("utf-8")
            fields = fields_json.decode("utf-8") if fields_json else ""

            type_name = key.rsplit("#", 1)[0] if "#" in key else key
            cls = _resolve_type(type_name)
            if cls is None:
                return -1                       # 类型不在本进程 → 明确失败（引擎会报错，不静默）
            inst = cls()
            obj_id = _interop.ms_go_instance_id(self._e, go)
            inst.owner = SceneObject(self._e, go, obj_id, "replay")
            reg = registry_for(self._e)
            reg.bind_existing(inst, self._e, go, key, inst.owner)
            # 回填字段（与 C# ApplyFieldsJson 同序：先字段后 Awake/OnEnable）
            if fields:
                try:
                    data = json.loads(fields)
                except Exception:
                    data = {}
                for name, v in data.items():
                    # 与 `_on_set` / `_public_fields` 同一份跳过名单：否则手写或跨语言写的
                    #   scriptFields 里出现 "owner" 会**覆盖刚建好的 Owner 视图**（enabled 同理会
                    #   被采纳）——暴露与回填必须对称，见 _SKIP_FIELDS 的注释。
                    if name.startswith("_") or name in _SKIP_FIELDS:
                        continue
                    try:
                        setattr(inst, name, v)
                    except Exception:
                        pass
            inst.awake()
            if getattr(inst, "enabled", True):
                inst.on_enable()
            return 0
        except Exception:
            return -1

    # —— 注册到引擎 ——
    def register(self) -> None:
        check(_interop.ms_script_bridge_register(
            self._e, None, self._cb_fields, self._cb_set, self._cb_types), "script_bridge_register")
        check(_interop.ms_script_replay_register(
            self._e, self._cb_replay, None), "script_replay_register")


def _resolve_type(type_name: str):
    """按 `模块.类名` / `类名` 解析 Python 组件类型（只认 PyComponentBase 子类）。"""
    from .component import PyComponentBase
    if "." in type_name:
        mod_name, _, cls_name = type_name.rpartition(".")
        try:
            import importlib
            mod = importlib.import_module(mod_name)
            cls = getattr(mod, cls_name, None)
            if isinstance(cls, type) and issubclass(cls, PyComponentBase):
                return cls
        except Exception:
            pass
    # 退化：在 PyComponentBase 的已加载子类里按 qualname / name 找
    for sub in _all_subclasses(PyComponentBase):
        if type_name in (sub.__qualname__, sub.__name__):
            return sub
    return None


def _all_subclasses(cls):
    out = []
    for sub in cls.__subclasses__():
        out.append(sub)
        out.extend(_all_subclasses(sub))
    return out


def register(engine) -> bool:
    """为某引擎注册脚本桥 + 重放回调（幂等）。

    返回 True=已注册；False=当前 DLL 无这些入口（旧 DLL）——此时场景重放不可用，
    但**不影响**组件生命周期（与 C# 的 EntryPointNotFoundException 降级一致）。
    """
    if getattr(engine, "_script_bridge", None) is not None:
        return True
    if _interop.ms_script_bridge_register is None or _interop.ms_script_replay_register is None:
        return False
    bridge = _Bridge(engine)
    bridge.register()
    engine._script_bridge = bridge      # 强引用保活（否则回调被 GC → C++ 侧悬垂）
    return True
