using System;
using System.Text;
using System.Collections.Generic;
using HybridEngine.Engine.Internal;

namespace HybridEngine.Engine;

// M2.5：SceneObject 句柄代理（ms_go_* ——引擎拥有对象；代理=轻量 C# 视图）
public sealed class SceneObject
{
    // 构造时记下的原生裸指针。**不要直接用**——对象销毁后它是悬垂句柄。
    private IntPtr _goPtr;
    // 本视图所属场景（原生 ms_scene* 裸指针）。**不能假设"活动场景"**：
    //   ms_scene_go_get 是**按传入场景**查找的（ms_bind.h:76：多场景下用活动场景会读错场景），
    //   故必须记住创建本对象时所在的场景。多场景（SceneManager additive load）下这是必需的。
    //   该句柄在「场景被卸载」后才悬垂——那属于 ABI 已声明的宿主责任（ms_bind.h:74），
    //   故此处只做 NULL 校验、不额外加固。
    private readonly IntPtr _scene;
    /// <summary>
    /// 交给原生调用的对象指针：**按 InstanceId 在所属场景中重新解析**后的活动指针。
    ///
    /// 为什么不能直接返回构造时存下的裸指针（2026-09-21 修复）：
    ///   SceneObject::Destroy 是**延迟**销毁（LifecycleDriver 下个 Tick 才 roots_.erase 真正 free），
    ///   之后任何用旧指针的调用都是 use-after-free。实测（C#）：
    ///     · comp.Enabled      → ms_component_get_enabled 内 AV（0xC0000005）
    ///     · go.Transform.Position → ms_transform_get 内 AV
    ///     · go.Name           → 读出垃圾字符串；赋值 → ms_go_set_name 内 AV
    ///     · go.Destroy() 二次 → libstdc++ 断言中止（0xC0000409）
    ///   本类已有 ResolveLivePtr 做这件事，但只有两处在用。
    ///   改为**属性即解析**后，全部调用点一次性受益，且不会漏（新增调用点默认安全）。
    ///
    /// 场景归属：ms_scene_go_get 是**按传入场景**查找的（ms_bind.h:76——用活动场景在多场景下会读错场景），
    ///   故构造时尽量记下所属场景（_scene）；未提供时回落到活动场景（单场景等价）。
    ///
    /// 代价：每次访问多一次 ms_scene_go_get（O(所属场景对象数)）。这些是检查器/编辑期路径，
    ///   崩溃远比这点开销严重。**若将来有热路径需要裸指针，请显式用 _goPtr 并自行保证生命周期。**
    ///
    /// 返回 IntPtr.Zero = 对象已销毁/引擎已释放——原生侧对 Zero 一律返回 BAD_ARG，不崩。
    /// </summary>
    internal IntPtr GoPtr => ResolveLivePtr();
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
        return new SceneObject(EnginePtr, go, id, "child", _scene);
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
        : this(engine, go, id, name, IntPtr.Zero) { }

    /// <summary>带**所属场景**的构造。多场景（SceneManager additive）必须传它——
    /// ms_scene_go_get 按传入场景查找，用活动场景会查到别的场景去（ms_bind.h:76）。
    /// 传 IntPtr.Zero 表示"未指定"：解析时回落到活动场景（单场景下等价）。</summary>
    internal SceneObject(IntPtr engine, IntPtr go, long id, string name, IntPtr scene)
    {
        EnginePtr = engine;
        _goPtr = go; Id = id; _name = name; _scene = scene;
        // Transform 持有 owner（不是裸指针快照）——它每次访问都经 owner.GoPtr 重解析，
        // 对象销毁后自然失效而不是 use-after-free。
        Transform = new Transform(this);
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
        // 子对象与父对象在同一场景——必须把 _scene 传下去（此前漏传 → _scene=Zero → 解析回落
        // 活动场景：多场景下会查错场景，拿不到（静默失败）或查到同 id 的**另一个**对象）。
        var child = new SceneObject(EnginePtr, go, id, name, _scene);
        _children.Add(child);
        return child;
    }

    public void SetActive(bool v) => Native.ms_go_set_active(EnginePtr, GoPtr, v ? 1 : 0);

    /// <summary>销毁对象。**幂等**：销毁后清零裸指针（Id 保留——用于日志/诊断），
    /// 二次调用直接返回。此前二次 Destroy 会重入删除路径 → libstdc++ 断言中止
    /// （unique_ptr&lt;LifecycleDriver&gt;::operator*，退出码 0xC0000409）。</summary>
    public void Destroy()
    {
        if (_goPtr == IntPtr.Zero) return;   // 已销毁（或本就无效）
        Native.ms_go_destroy(EnginePtr, _goPtr);
        _goPtr = IntPtr.Zero;                // 关键：不留下可被再次使用的悬垂指针
    }

    /// <summary>
    /// 把本视图解析为**活动场景中的当前指针**（P1-b）。
    /// 对象已销毁 / 场景已整体替换 / 引擎已释放 → IntPtr.Zero。
    /// 为什么需要：_goPtr 是原生对象裸指针——对象销毁后它成为悬垂句柄，
    /// 任何直接解引用（如 ms_go_has_component）都是 use-after-free。此处经 InstanceId 在活动场景树中重查，
    /// 全程不触碰旧指针，因此可安全用于「这个视图还有效吗」判定。
    /// 代价：一次 ms_engine_scene + 按 id 的 DFS（O(场景对象数)）。
    /// 注：属性 GoPtr 也走这里——故**所有**原生调用都自动获得这层保护。
    /// </summary>
    internal IntPtr ResolveLivePtr()
    {
        // 注意：这里必须读 _goPtr 字段，不能读 GoPtr 属性（那会无限自递归）
        if (EnginePtr == IntPtr.Zero || _goPtr == IntPtr.Zero || Id == 0) return IntPtr.Zero;

        // ① 构造时明确给了所属场景 → **只认它**。
        //    不能"找不到就换个场景再找"：那会在多场景下读到另一个场景里同 id 的对象。
        if (_scene != IntPtr.Zero)
            return Native.ms_scene_go_get(EnginePtr, _scene, Id, out var own) == BindError.OK ? own : IntPtr.Zero;

        // ② 未指定场景 → 先活动场景（单场景下即唯一路径，与旧行为逐字一致）。
        var active = Native.ms_engine_scene(EnginePtr);
        if (active != IntPtr.Zero && Native.ms_scene_go_get(EnginePtr, active, Id, out var live) == BindError.OK)
            return live;

        // ③ 活动场景里没有 → 扫其余已加载场景。
        //    走这里的调用点：附加加载（additive）下由**宿主/重放**建的视图——它们只拿到 go 指针
        //    （SceneReplay.Replay / Scripts.AddComponent 的原生入口就没有场景参数）。
        //    这样"猜"是安全的：InstanceId 由**进程级**单调计数器发号（core/instance.hpp：从 1 起、
        //    永不复用；反序列化也重新发号，不还原盘上 id）→ 全进程唯一，不会撞到别的场景的对象。
        if (Native.ms_scenes_count(EnginePtr, out int n) != BindError.OK) return IntPtr.Zero;
        for (int i = 0; i < n; ++i)
        {
            if (Native.ms_scenes_get(EnginePtr, i, out var s) != BindError.OK) continue;
            if (s == IntPtr.Zero || s == active) continue;
            if (Native.ms_scene_go_get(EnginePtr, s, Id, out var found) == BindError.OK) return found;
        }
        return IntPtr.Zero;
    }

    internal void DetachComponent(ComponentBase comp) => _components.Remove(comp);
}
