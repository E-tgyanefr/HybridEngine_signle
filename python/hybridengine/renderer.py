# hybridengine.renderer — DrawCtx：GUI 平面 ctypes 绘制代理（与 C# IRenderer 对称；同一 C ABI）
# 10 原语 + draw_text/measure_text + blit_alpha；颜色 = int（0xAARRGGBB——A=alpha 直通：填充原语 straight-alpha 合成；现示例全 FF=行为不变）
import ctypes
import math

from . import _interop
from ._interop import GUINotAvailable, gui_abi_available


def _require_gui():
    if not gui_abi_available():
        raise GUINotAvailable(
            "当前 hybridengine.dll 无 GUI ABI（ms_rnd_*/ms_text_*）——需先重建引擎 DLL（含 t1 GUI 平面）")


def _check_blit_len(w: float, h: float, src_pixels):
    """blit 源像素长度校验——**必须在调用前做**。

    C 侧 `op.src.assign(srcPixels, srcPixels + (size_t)iw * (size_t)ih)` 完全信任 w*h，
    源序列长度不足时按 w*h 读越界内存：实测长度 2 配 1000x1000 → access violation；
    小幅不足（如 4x4 只给 1 像素）**不报错**，变成静默堆越界读（可能泄漏内存内容）。
    ABI 层无法自检，只能在包装层挡。
    """
    need = int(math.ceil(w)) * int(math.ceil(h))
    if need < 0:
        need = 0
    if len(src_pixels) < need:
        raise ValueError(
            "blit 源像素不足：w=%r h=%r 需要 %d 个像素，实际只有 %d 个"
            % (w, h, need, len(src_pixels)))


class DrawCtx:
    """绘制上下文：GameEngine.on_render(draw) 回调参数；方法=1 次 ctypes 调用（命令录制——ms_engine_render 统一回放）。

    帧清理：run_frame 每帧帧首 ms_rnd_clear_list（命令跨帧不残留）；回放先清黑→按序（本帧命令同帧可见）。
    确定性：同参数序列 → 同帧像素 → 同 render_hash（黄金帧断言基础）。
    """

    def __init__(self, engine_handle):
        self._e = engine_handle

    # —— 10 绘制原语（ms_rnd_*）——
    def clear(self, color: int):
        """整帧清色（0xAARRGGBB）"""
        _require_gui()
        _interop.ms_rnd_clear(self._e, color)

    def clear_rect(self, x: float, y: float, w: float, h: float, color: int):
        _require_gui()
        _interop.ms_rnd_clear_rect(self._e, x, y, w, h, color)

    def fill_rect(self, x: float, y: float, w: float, h: float, color: int):
        _require_gui()
        _interop.ms_rnd_fill_rect(self._e, x, y, w, h, color)

    def fill_rounded_rect(self, x: float, y: float, w: float, h: float, radius: float, color: int):
        _require_gui()
        _interop.ms_rnd_fill_rounded_rect(self._e, x, y, w, h, radius, color)

    def draw_line(self, x1: float, y1: float, x2: float, y2: float, color: int, thickness: float = 1.0):
        _require_gui()
        _interop.ms_rnd_draw_line(self._e, x1, y1, x2, y2, color, thickness)

    def fill_circle(self, cx: float, cy: float, r: float, color: int):
        _require_gui()
        _interop.ms_rnd_fill_circle(self._e, cx, cy, r, color)

    def draw_circle(self, cx: float, cy: float, r: float, color: int, thickness: float = 1.0):
        _require_gui()
        _interop.ms_rnd_draw_circle(self._e, cx, cy, r, color, thickness)

    def fill_triangle(self, x1: float, y1: float, x2: float, y2: float, x3: float, y3: float, color: int):
        _require_gui()
        _interop.ms_rnd_fill_triangle(self._e, x1, y1, x2, y2, x3, y3, color)

    def fill_quad(self, x1: float, y1: float, x2: float, y2: float, x3: float, y3: float,
                  x4: float, y4: float, color: int):
        _require_gui()
        _interop.ms_rnd_fill_quad(self._e, x1, y1, x2, y2, x3, y3, x4, y4, color)

    def blit_rect(self, x: float, y: float, w: float, h: float, src_pixels):
        """图像 blit（src_pixels：0xAARRGGBB 整数序列——不透明拷贝=背靠背语义）

        ⚠ src_pixels 长度必须 >= ceil(w)*ceil(h)：C 侧按 w*h 无条件拷贝源像素，
        长度不足会**越界读**（实测 access violation；小幅不足则不报错而是静默堆越界读）。
        """
        _require_gui()
        _check_blit_len(w, h, src_pixels)
        arr = (ctypes.c_uint32 * len(src_pixels))(*src_pixels)
        _interop.ms_rnd_blit_rect(self._e, x, y, w, h, ctypes.cast(arr, ctypes.POINTER(ctypes.c_uint32)))

    def blit_alpha(self, x: float, y: float, w: float, h: float, src_pixels):
        """t1：逐像素 straight-alpha 合成 blit（src_pixels：0xAARRGGBB 整数序列——A=alpha 生效；与 blit_rect 仅合成差异）

        ⚠ 同 blit_rect：长度必须 >= ceil(w)*ceil(h)，否则越界读。
        """
        _require_gui()
        _check_blit_len(w, h, src_pixels)
        arr = (ctypes.c_uint32 * len(src_pixels))(*src_pixels)
        _interop.ms_rnd_blit_alpha(self._e, x, y, w, h, ctypes.cast(arr, ctypes.POINTER(ctypes.c_uint32)))

    # —— 矩形裁剪（t6：Clip/Scissor——越界内容不画出；与 draw 原语成对使用）——
    def clip_push(self, x: float, y: float, w: float, h: float):
        """矩形裁剪入栈（≤8 深；栈顶=当前裁剪——嵌套仅栈顶生效=保存/恢复）"""
        _require_gui()
        _interop.ms_rnd_clip_push(self._e, x, y, w, h)

    def clip_push_rotated(self, cx: float, cy: float, w: float, h: float, angle_rad: float):
        """旋转矩形裁剪入栈（中心+尺寸+角度弧度——斜劈/分离位移精确切分；angle=0≡clip_push；同栈 pop 恢复）"""
        _require_gui()
        _interop.ms_rnd_clip_push_rotated(self._e, cx, cy, w, h, angle_rad)

    def clip_pop(self):
        """弹栈恢复上一级（空栈=MS_ERR_INVALID_OP——调用方成对使用）"""
        _require_gui()
        _interop.ms_rnd_clip_pop(self._e)

    # —— 文本（ms_text_*——UTF-8；录制 Text 命令——回放绘制双面，参与哈希）——
    def draw_text(self, text: str, x: float, y: float, size: float, color: int):
        _require_gui()
        _interop.ms_text_draw(self._e, text.encode("utf-8"), x, y, size, color)

    def measure_text(self, text: str, size: float):
        """→ (width, height)（像素 double）"""
        _require_gui()
        w = _interop.c_double(0.0)
        h = _interop.c_double(0.0)
        _interop.ms_text_measure(self._e, text.encode("utf-8"), size, ctypes.byref(w), ctypes.byref(h))
        return (w.value, h.value)
