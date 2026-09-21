# hybridengine.transform — Transform（pos/rot quat/scale/parent——经 ms_transform_* ABI；与 C# Transform 对称）
from . import _interop
from ._interop import check


class Transform:
    """变换代理。

    ⚠ 存 **owner 而不是 go 裸指针**（2026-09-21 修复）：
      原先构造时把 go 句柄快照下来，对象销毁后每个属性访问都是 use-after-free
      （实测 Python：`go.transform.position` 从已释放内存读出 [0,0,0] 而不报错）。
      owner._live() 每次访问都按 InstanceId 重解析，销毁后为 None → 跳过原生调用。
    """

    def __init__(self, owner):
        self._owner = owner

    def _go(self):
        return self._owner._live()

    def _require(self, what: str):
        go = self._go()
        if not go:
            check(_interop.MS_ERR_NOT_FOUND, what)   # 已销毁 → 与原生同码报错
        return go

    @property
    def position(self):
        out = (_interop.c_double * 3)()
        check(_interop.ms_transform_get(self._owner._e, self._require("transform_get.pos"), b"pos", out),
              "transform_get.pos")
        return [out[0], out[1], out[2]]

    @position.setter
    def position(self, v):
        arr = (_interop.c_double * 3)(v[0], v[1], v[2])
        check(_interop.ms_transform_set(self._owner._e, self._require("transform_set.pos"), b"pos", arr),
              "transform_set.pos")

    @property
    def rotation(self):
        out = (_interop.c_double * 4)()
        check(_interop.ms_transform_get(self._owner._e, self._require("transform_get.rot"), b"rot", out),
              "transform_get.rot")
        return [out[0], out[1], out[2], out[3]]   # wxyz 四元数

    @rotation.setter
    def rotation(self, v):
        arr = (_interop.c_double * 4)(v[0], v[1], v[2], v[3])
        check(_interop.ms_transform_set(self._owner._e, self._require("transform_set.rot"), b"rot", arr),
              "transform_set.rot")

    @property
    def scale(self):
        out = (_interop.c_double * 3)()
        check(_interop.ms_transform_get(self._owner._e, self._require("transform_get.scale"), b"scale", out),
              "transform_get.scale")
        return [out[0], out[1], out[2]]

    @scale.setter
    def scale(self, v):
        arr = (_interop.c_double * 3)(v[0], v[1], v[2])
        check(_interop.ms_transform_set(self._owner._e, self._require("transform_set.scale"), b"scale", arr),
              "transform_set.scale")

    @property
    def parent(self):
        return _interop.ms_transform_parent(self._owner._e, self._require("transform_parent"))

    def set_parent(self, parent_id: int, keep_world: bool = True):
        check(_interop.ms_transform_set_parent(self._owner._e, self._require("set_parent"),
                                               parent_id, 1 if keep_world else 0), "set_parent")

    def detach(self):
        check(_interop.ms_transform_set_parent(self._owner._e, self._require("detach"), 0, 0), "detach")
