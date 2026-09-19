# hybridengine.engine — GameEngine 门面（与 C# GameEngine.cs 对称；单线程主线程驱动）
import ctypes
import inspect

from . import _interop
from ._interop import check, BindException, GUINotAvailable, gui_abi_available
from .sceneobject import SceneObject
from .assetstore import AssetStore
from .renderer import DrawCtx


class GameEngine:
    def __init__(self, title: str = "HybridEngine v3", width: int = 1280, height: int = 720, scene=None):
        self.title = title
        self.width = width
        self.height = height
        err = _interop.c_int(0)
        self._e = _interop.ms_engine_create(title.encode(), width, height, ctypes.byref(err))
        if not self._e:
            raise BindException(err.value, "engine_create")
        self._scene = _interop.ms_engine_scene(self._e)
        self.assets = AssetStore(self._e)
        self.root = self._add_root("Root")
        self.should_quit = False
        # t1：GUI 平面——绘制回调（可空）+ DrawCtx 代理（与 C# OnRender/IRenderer 对称）
        self.on_render = None
        self.draw = DrawCtx(self._e)
        if scene is not None:
            scene.build(self)

    def _add_root(self, name: str) -> SceneObject:
        obj_id = _interop.c_int64(0)
        check(_interop.ms_scene_add_root(self._e, self._scene, name.encode(), ctypes.byref(obj_id)), "add_root")
        go = _interop.c_void_p()
        check(_interop.ms_scene_go_get(self._e, self._scene, obj_id.value, ctypes.byref(go)), "go_get")
        return SceneObject(self._e, go, obj_id.value, name)

    def run_frame(self, dt: float = 0.016) -> int:
        rc = check(_interop.ms_engine_tick(self._e, dt), "tick")
        pump = _interop.ms_engine_pump(self._e)
        # 录播式帧序（t1 GUI 平面定稿）：帧首清列表 → on_frame/on_render 录制本帧命令（原语+文字=Text 命令）
        # → ms_engine_render 先清黑→按序回放+哈希（同帧可见；空命令=黄金路径逐位不变=零回归）
        if gui_abi_available():
            _interop.ms_rnd_clear_list(self._e)
        self._invoke_on_frame(dt)
        self._invoke_on_render()
        check(_interop.ms_engine_render(self._e, None), "render")
        if pump != 0:
            self.should_quit = True
        return rc

    def _invoke_on_frame(self, dt: float):
        cb = getattr(self, "on_frame", None)
        if cb is None:
            return
        n = self._cb_arity(cb)
        if n == 0:
            cb()
        else:
            cb(dt)

    def _invoke_on_render(self):
        cb = getattr(self, "on_render", None)
        if cb is None:
            return
        # 向后兼容：回调参数可空（旧签名=无参）；新签名 = on_render(draw)
        if self._cb_arity(cb) == 0:
            cb()
        else:
            cb(self.draw)

    @staticmethod
    def _cb_arity(cb) -> int:
        try:
            return len(inspect.signature(cb).parameters)
        except (TypeError, ValueError):
            return 1

    def run(self, frames: int = -1, dt: float = 0.016) -> int:
        i = 0
        while not self.should_quit and (frames < 0 or i < frames):
            self.run_frame(dt)
            i += 1
        return 0

    def save_scene(self, asset_path: str):
        check(_interop.ms_scene_save(self._e, self._scene, asset_path.encode()), "save_scene")

    def load_scene(self, asset_path: str):
        check(_interop.ms_scene_load(self._e, self._scene, asset_path.encode()), "load_scene")

    @property
    def root_count(self) -> int:
        n = _interop.c_int(0)
        check(_interop.ms_scene_root_count(self._e, self._scene, ctypes.byref(n)), "root_count")
        return n.value

    @property
    def render_hash(self) -> int:
        return _interop.ms_engine_render_hash(self._e)

    @property
    def last_frame_ms(self) -> float:
        return _interop.ms_engine_last_frame_ms(self._e)

    @staticmethod
    def live_engine_count() -> int:
        return _interop.ms_bind_live_engine_count()

    def resize(self, w: int, h: int):
        check(_interop.ms_engine_resize(self._e, w, h), "resize")

    # —— G2/t137：输入 ABI（vk=KeyCode 值——Win32 VK 直通；引擎窗口键泵由 run_frame/Pump 驱动） ——
    def key_down(self, vk: int) -> bool:
        """本帧按下沿（EndFrame 清）"""
        return bool(_interop.ms_input_key_down(self._e, vk))

    def key_up(self, vk: int) -> bool:
        """本帧抬起沿"""
        return bool(_interop.ms_input_key_up(self._e, vk))

    def get_key(self, vk: int) -> bool:
        """当前按住"""
        return bool(_interop.ms_input_get_key(self._e, vk))

    def axis(self, name: str) -> float:
        """Horizontal/Vertical（WASD+方向键）"""
        return _interop.ms_input_axis(self._e, name.encode())

    def get_button(self, action: str) -> bool:
        """Fire1/Jump/Submit/Cancel"""
        return bool(_interop.ms_input_get_button(self._e, action.encode()))

    # —— t1 GUI：鼠标（ms_input_mouse_*——button 0=左 1=中 2=右；wheel delta 本帧累计 ±1）——
    @property
    def mouse_x(self) -> float:
        if not gui_abi_available():
            raise GUINotAvailable("当前 DLL 无 GUI ABI（鼠标）——需重建引擎 DLL")
        return _interop.ms_input_mouse_x(self._e)

    @property
    def mouse_y(self) -> float:
        if not gui_abi_available():
            raise GUINotAvailable("当前 DLL 无 GUI ABI（鼠标）——需重建引擎 DLL")
        return _interop.ms_input_mouse_y(self._e)

    @property
    def mouse_wheel_delta(self) -> float:
        if not gui_abi_available():
            raise GUINotAvailable("当前 DLL 无 GUI ABI（鼠标）——需重建引擎 DLL")
        return _interop.ms_input_mouse_wheel_delta(self._e)

    def mouse_button(self, button: int) -> bool:
        """当前按住（0=左 1=中 2=右）"""
        if not gui_abi_available():
            raise GUINotAvailable("当前 DLL 无 GUI ABI（鼠标）——需重建引擎 DLL")
        return bool(_interop.ms_input_mouse_button(self._e, button))

    def mouse_button_down(self, button: int) -> bool:
        """本帧按下沿（帧尾清）"""
        if not gui_abi_available():
            raise GUINotAvailable("当前 DLL 无 GUI ABI（鼠标）——需重建引擎 DLL")
        return bool(_interop.ms_input_mouse_button_down(self._e, button))

    def mouse_button_up(self, button: int) -> bool:
        """本帧抬起沿（帧尾清）"""
        if not gui_abi_available():
            raise GUINotAvailable("当前 DLL 无 GUI ABI（鼠标）——需重建引擎 DLL")
        return bool(_interop.ms_input_mouse_button_up(self._e, button))

    def find(self, name: str):
        """按名 DFS 查找场景对象；未找到= None。"""
        go = _interop.c_void_p()
        rc = _interop.ms_scene_find(self._e, self._scene, name.encode(), ctypes.byref(go))
        if rc != 0 or not go:
            return None
        return SceneObject(self._e, go, _interop.ms_go_instance_id(self._e, go), name)

    def instantiate_prefab(self, asset_path: str):
        """预制体实例化到当前场景（.msprefab）。"""
        go = _interop.c_void_p()
        check(_interop.ms_assets_instantiate_prefab(self._e, asset_path.encode(), ctypes.byref(go)), "instantiate_prefab")
        return SceneObject(self._e, go, _interop.ms_go_instance_id(self._e, go), "prefab")
    def dispose(self):
        if self._e:
            from .component import _registry
            _registry.release_all()
            _interop.ms_engine_destroy(self._e)
            self._e = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.dispose()
        return False
