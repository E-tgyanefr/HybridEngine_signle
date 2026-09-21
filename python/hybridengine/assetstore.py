# hybridengine.assetstore — AssetStore（Load/TypeOf/GuidOf/Unref/SaveText/SaveBinary/List/TexturePixels——与 C# AssetStore 对称）
import ctypes
import json

from . import _interop
from ._interop import check, BindException, MS_ERR_BAD_ARG


class Asset:
    def __init__(self, engine_handle, handle):
        self._e = engine_handle
        self._h = handle
        # 幂等标志：C 侧 ms_assets_unref 会 `delete h`，**无法自检句柄是否已释放**，
        #   故重复 unref 会二次 free → 实测进程级堆损坏（STATUS_HEAP_CORRUPTION）。
        self._released = False

    @property
    def released(self) -> bool:
        return self._released

    @property
    def type(self):
        buf = ctypes.create_string_buffer(32)
        check(_interop.ms_assets_type(self._e, self._h, buf, 32), "assets_type")
        return buf.value.decode()

    @property
    def guid(self):
        buf = ctypes.create_string_buffer(32)
        check(_interop.ms_assets_guid(self._e, self._h, buf, 32), "assets_guid")
        return buf.value.decode()

    # t7：纹理像素（借出指针快照——句柄存活期间有效；零拷贝直读后拷贝）→ (w, h, stride, [0xFFRRGGBB...])
    def texture_pixels(self):
        w = _interop.c_int(0); h = _interop.c_int(0); st = _interop.c_int(0)
        pp = ctypes.c_void_p(0)
        check(_interop.ms_assets_texture_pixels(self._e, self._h, ctypes.byref(w), ctypes.byref(h), ctypes.byref(st),
                                                ctypes.byref(pp)), "texture_pixels")
        n = w.value * h.value
        ptr = ctypes.cast(pp.value, ctypes.POINTER(ctypes.c_uint32))
        return (w.value, h.value, st.value, [ptr[i] for i in range(n)])

    def texture_size(self):
        w = _interop.c_int(0); h = _interop.c_int(0)
        st = _interop.c_int(0); pp = ctypes.c_void_p(0)
        check(_interop.ms_assets_texture_pixels(self._e, self._h, ctypes.byref(w), ctypes.byref(h), ctypes.byref(st),
                                                ctypes.byref(pp)), "texture_size")
        return (w.value, h.value)

    def unref(self):
        """释放引用。**幂等**——二次调用直接返回（否则 C 侧二次 delete = 堆损坏）。"""
        if self._released:
            return False
        self._released = True
        _interop.ms_assets_unref(self._e, self._h)
        return True

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.unref()
        return False


class AssetStore:
    def __init__(self, engine_handle):
        self._e = engine_handle

    def set_root(self, abs_root: str) -> bool:
        rc = _interop.ms_assets_set_root(self._e, abs_root.encode())
        if rc != 0:
            raise BindException(rc, "set_root")
        return True

    def load(self, asset_path: str) -> Asset:
        err = _interop.c_int(0)
        h = _interop.ms_assets_load(self._e, asset_path.encode(), ctypes.byref(err))
        if not h:
            raise BindException(err.value, f"assets_load {asset_path}")
        return Asset(self._e, h)

    def save_binary(self, asset_path: str, data: bytes, type_: str) -> bool:
        buf = ctypes.create_string_buffer(data, len(data))
        rc = _interop.ms_assets_save(self._e, asset_path.encode(), buf, len(data), type_.encode())
        if rc != 0:
            raise BindException(rc, f"save_binary {asset_path}")
        return True

    def save_text(self, asset_path: str, payload: str, type_: str) -> bool:
        rc = _interop.ms_assets_save_text(self._e, asset_path.encode(), payload.encode(), type_.encode())
        if rc != 0:
            raise BindException(rc, f"save_text {asset_path}")
        return True

    # t7：递归资产列表（类型表过滤——JSON 字符串数组；cap 不足自动扩容重试）→ [str,...]
    def list(self, asset_dir: str = "Assets/"):
        cap = 4096
        for _ in range(8):
            buf = ctypes.create_string_buffer(cap)
            rc = _interop.ms_assets_list(self._e, asset_dir.encode(), buf, cap)
            if rc == 0:
                return json.loads(buf.value.decode())
            if rc != MS_ERR_BAD_ARG:
                raise BindException(rc, f"assets_list {asset_dir}")
            cap *= 2
        raise BindException(MS_ERR_BAD_ARG, "assets_list too large")
