# hybridengine — Python 开发层（ctypes 经同一 C ABI——与 C# HybridEngine.Bind API 对称；零第三方）
from .engine import GameEngine
from .scene import Scene
from .sceneobject import SceneObject
from .transform import Transform
from .assetstore import AssetStore, Asset
from .component import PyComponentBase
from .renderer import DrawCtx
# 原生用户场景组件（3D 网格/相机/光源 + 2D 精灵——与 C# Components.cs 的包装同形）
from .native_components import (NativeComponent, MeshVisual, SpriteVisual, CameraComponent,
                                LightComponent)
from ._interop import (BindException, GUINotAvailable, Render3DNotAvailable, SpriteABINotAvailable,
                       MS_BIND_OK, MS_ERR_THREAD, error_text)

__all__ = ["GameEngine", "Scene", "SceneObject", "Transform", "AssetStore", "Asset",
           "PyComponentBase", "DrawCtx", "NativeComponent", "MeshVisual", "SpriteVisual",
           "CameraComponent", "LightComponent", "BindException", "GUINotAvailable",
           "Render3DNotAvailable", "SpriteABINotAvailable", "KeyCode"]

import enum


class KeyCode(enum.IntEnum):
    Space = 0x20
    Enter = 0x0D
    Escape = 0x1B
    A = 0x41
    D = 0x44
    F = 0x46
    J = 0x4A
    K = 0x4B
    P = 0x50
    R = 0x52
    S = 0x53
    W = 0x57
    Left = 0x25
    Up = 0x26
    Right = 0x27
    Down = 0x28
