# hybridengine.transform — Transform（pos/rot quat/scale/parent——经 ms_transform_* ABI；与 C# Transform 对称）
from . import _interop
from ._interop import check


class Transform:
    def __init__(self, engine_handle, go_handle):
        self._e = engine_handle
        self._go = go_handle

    @property
    def position(self):
        out = (_interop.c_double * 3)()
        check(_interop.ms_transform_get(self._e, self._go, b"pos", out), "transform_get.pos")
        return [out[0], out[1], out[2]]

    @position.setter
    def position(self, v):
        arr = (_interop.c_double * 3)(v[0], v[1], v[2])
        check(_interop.ms_transform_set(self._e, self._go, b"pos", arr), "transform_set.pos")

    @property
    def rotation(self):
        out = (_interop.c_double * 4)()
        check(_interop.ms_transform_get(self._e, self._go, b"rot", out), "transform_get.rot")
        return [out[0], out[1], out[2], out[3]]   # wxyz 四元数

    @rotation.setter
    def rotation(self, v):
        arr = (_interop.c_double * 4)(v[0], v[1], v[2], v[3])
        check(_interop.ms_transform_set(self._e, self._go, b"rot", arr), "transform_set.rot")

    @property
    def scale(self):
        out = (_interop.c_double * 3)()
        check(_interop.ms_transform_get(self._e, self._go, b"scale", out), "transform_get.scale")
        return [out[0], out[1], out[2]]

    @scale.setter
    def scale(self, v):
        arr = (_interop.c_double * 3)(v[0], v[1], v[2])
        check(_interop.ms_transform_set(self._e, self._go, b"scale", arr), "transform_set.scale")

    @property
    def parent(self):
        return _interop.ms_transform_parent(self._e, self._go)

    def set_parent(self, parent_id: int, keep_world: bool = True):
        check(_interop.ms_transform_set_parent(self._e, self._go, parent_id, 1 if keep_world else 0), "set_parent")

    def detach(self):
        check(_interop.ms_transform_set_parent(self._e, self._go, 0, 0), "detach")
