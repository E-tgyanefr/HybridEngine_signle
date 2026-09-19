using System;
using System.Text;
using System.Collections.Generic;
using HybridEngine.Engine.Internal;

namespace HybridEngine.Engine;

// M2.5：SceneObject 句柄代理（ms_go_* ——引擎拥有对象；代理=轻量 C# 视图）
public sealed class SceneObject
{
    internal IntPtr GoPtr { get; private set; }
    internal IntPtr EnginePtr { get; }
    public long Id { get; private set; }
    private string _name = "";
    public string Name
    {
        get
        {
            var buf = new byte[256];
            if (Native.ms_go_name(EnginePtr, GoPtr, buf, buf.Length) != BindError.OK) return _name;
            int n = Array.IndexOf(buf, (byte)0);
            if (n < 0) n = buf.Length;
            return Encoding.UTF8.GetString(buf, 0, n);
        }
        set
        {
            _name = value;
            if (GoPtr != IntPtr.Zero) Native.ms_go_set_name(EnginePtr, GoPtr, value);
        }
    }
    public Transform Transform { get; internal set; }
    public IReadOnlyList<SceneObject> Children
    {
        get
        {
            var list = new List<SceneObject>();
            int n = ChildCount;
            for (int i = 0; i < n; ++i) list.Add(GetChild(i));
            return list;
        }
    }
    public int ChildCount { get { return Native.ms_go_child_count(EnginePtr, GoPtr, out int n) == BindError.OK ? n : 0; } }
    public SceneObject GetChild(int index)
    {
        if (Native.ms_go_child_get(EnginePtr, GoPtr, index, out var go) != BindError.OK)
            throw new ArgumentOutOfRangeException(nameof(index));
        long id = Native.ms_go_instance_id(EnginePtr, go);
        return new SceneObject(EnginePtr, go, id, "child");
    }
    public static SceneObject? Find(string name) => GameEngine.Current?.FindSceneObject(name);
    // P1-b：Add* 经 NativeProxyCache 登记 → Add 返回的代理与后续 GetComponent<T>() 同一实例（身份稳定）
    public MeshVisual AddMeshVisual() { if (Native.ms_go_add_mesh_visual(EnginePtr, GoPtr) != BindError.OK) throw new InvalidOperationException("ms_go_add_mesh_visual"); return NativeProxyCache.Remember(this, new MeshVisual(this)); }
    public SpriteVisual AddSpriteVisual() { if (Native.ms_go_add_sprite_visual(EnginePtr, GoPtr) != BindError.OK) throw new InvalidOperationException("ms_go_add_sprite_visual"); return NativeProxyCache.Remember(this, new SpriteVisual(this)); }
    public CameraComponent AddCameraComponent() { if (Native.ms_go_add_camera(EnginePtr, GoPtr) != BindError.OK) throw new InvalidOperationException("ms_go_add_camera"); return NativeProxyCache.Remember(this, new CameraComponent(this)); }
    public LightComponent AddLightComponent() { if (Native.ms_go_add_light(EnginePtr, GoPtr) != BindError.OK) throw new InvalidOperationException("ms_go_add_light"); return NativeProxyCache.Remember(this, new LightComponent(this)); }
    private readonly List<SceneObject> _children = new();
    private readonly List<ComponentBase> _components = new();

    internal SceneObject(IntPtr engine, IntPtr go, long id, string name)
    {
        EnginePtr = engine;
        GoPtr = go; Id = id; _name = name;   // 构造不回写原生名（GetChild 等代理不改变场景）
        Transform = new Transform(EnginePtr, GoPtr);
    }

    public T AddComponent<T>() where T : ComponentBase, new()
    {
        var comp = new T();
        comp.Owner = this;
        comp.RegKey = ComponentBridge.NextKey(typeof(T).FullName ?? typeof(T).Name);
        ComponentBridge.Register(comp, EnginePtr, GoPtr, comp.RegKey);
        _components.Add(comp);
        return comp;
    }

    // —— 组件查询（P1-b：单一泛型面同时覆盖托管脚本组件与原生组件代理——与 Unity 同形约束 where T : Component）——
    public T? GetComponent<T>() where T : Component
    {
        var script = ComponentBridge.Find<T>(EnginePtr, GoPtr);
        if (script != null) return script;
        foreach (var c in _components) if (c is T t) return t;   // 兜底（注册表未及时更新）
        // 原生组件 → 托管代理（未挂该原生组件=null；未登记的原生包装类型也=null）
        if (NativeProxyCache.IsNativeQuery(typeof(T))) return NativeProxyCache.Get(typeof(T), this) as T;
        return null;
    }

    public T[] GetComponents<T>() where T : Component
    {
        var scripts = ComponentBridge.FindAll<T>(EnginePtr, GoPtr);
        if (scripts.Length > 0) return scripts;
        if (NativeProxyCache.IsNativeQuery(typeof(T)) && NativeProxyCache.Get(typeof(T), this) is T proxy)
            return new[] { proxy };
        return scripts;
    }

    /// <summary>Unity 同形：命中=写入 out 并返回 true（未命中=out 为 null）。</summary>
    public bool TryGetComponent<T>(out T? component) where T : Component
    {
        component = GetComponent<T>();
        return component != null;
    }

    public T? GetComponentInChildren<T>() where T : Component
    {
        var self = GetComponent<T>();
        if (self != null) return self;
        foreach (var child in Children)
        {
            var r = child.GetComponentInChildren<T>();
            if (r != null) return r;
        }
        return null;
    }

    public T[] GetComponentsInChildren<T>() where T : Component
    {
        var list = new List<T>();
        if (GetComponent<T>() is { } self) list.Add(self);
        foreach (var child in Children) list.AddRange(child.GetComponentsInChildren<T>());
        return list.ToArray();
    }

    public SceneObject AddChild(string name)
    {
        if (Native.ms_go_add_child(EnginePtr, GoPtr, name, out var go) != BindError.OK)
            throw new InvalidOperationException("ms_go_add_child failed");
        long id = Native.ms_go_instance_id(EnginePtr, go);
        var child = new SceneObject(EnginePtr, go, id, name);
        _children.Add(child);
        return child;
    }

    public void SetActive(bool v) => Native.ms_go_set_active(EnginePtr, GoPtr, v ? 1 : 0);
    public void Destroy() => Native.ms_go_destroy(EnginePtr, GoPtr);

    /// <summary>
    /// 把本视图的 GoPtr 解析为**活动场景中的当前指针**（P1-b）。
    /// 对象已销毁 / 场景已整体替换 / 引擎已释放 → IntPtr.Zero。
    /// 为什么需要：GoPtr 是原生对象裸指针——对象销毁后它成为悬垂句柄，
    /// 任何直接解引用（如 ms_go_has_component）都是 use-after-free。此处经 InstanceId 在活动场景树中重查，
    /// 全程不触碰旧指针，因此可安全用于「这个视图还有效吗」判定。
    /// 代价：一次 ms_engine_scene + 按 id 的 DFS（O(场景对象数)）——仅用于有效性/存在性判定路径。
    /// </summary>
    internal IntPtr ResolveLivePtr()
    {
        if (EnginePtr == IntPtr.Zero || GoPtr == IntPtr.Zero || Id == 0) return IntPtr.Zero;
        var scene = Native.ms_engine_scene(EnginePtr);
        if (scene == IntPtr.Zero) return IntPtr.Zero;
        return Native.ms_scene_go_get(EnginePtr, scene, Id, out var live) == BindError.OK ? live : IntPtr.Zero;
    }

    internal void DetachComponent(ComponentBase comp) => _components.Remove(comp);
}
