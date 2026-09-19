using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using HybridEngine.Engine.Internal;

namespace HybridEngine.Engine;

// 组件桥：per-instance 注册键（FullName#n——多实例安全）+ 委托/句柄保活（GCHandle=C# 拥有——引擎销毁时 ReleaseAll）。
// 托管组件查询：_byGo 维护 (engine,go)→组件列表，任意 SceneObject 代理都可 GetComponent/FindObjectOfType。
internal static class ComponentBridge
{
    private static int _inst;
    private static readonly Dictionary<IntPtr, (GCHandle Handle, object[] Keep)> _table = new();
    private static readonly Dictionary<(IntPtr Engine, IntPtr Go), List<ComponentBase>> _byGo = new();

    public static string NextKey(string fullName) => fullName + "#" + ++_inst;

    public static void Register(ComponentBase comp, IntPtr engine, IntPtr go, string regKey)
    {
        var spec = BuildSpec(comp, regKey, out var handle, out var keep);
        int rc = Native.ms_component_register(engine, ref spec);
        if (rc != BindError.OK) { handle.Free(); throw new InvalidOperationException("ms_component_register rc=" + rc); }
        int rc2 = Native.ms_go_add_component(engine, go, regKey);
        if (rc2 != BindError.OK) { handle.Free(); throw new InvalidOperationException("ms_go_add_component rc=" + rc2); }
        Commit(comp, engine, go, regKey, handle, keep, spec);
    }

    /// <summary>
    /// P1-a 场景重放：原生脚本组件已由 C++ 反序列化创建——此处只**补挂回调表** + 托管登记，
    /// **不再** ms_go_add_component（会重复挂一个组件）。
    /// 同键重注册会就地更新 SpecTable 条目，而已存在的 C++ BindComponent 持有该条目指针 → 立即生效。
    /// </summary>
    public static void BindExisting(ComponentBase comp, IntPtr engine, IntPtr go, string regKey)
    {
        var spec = BuildSpec(comp, regKey, out var handle, out var keep);
        int rc = Native.ms_component_register(engine, ref spec);
        if (rc != BindError.OK) { handle.Free(); throw new InvalidOperationException("ms_component_register(replay) rc=" + rc); }
        Commit(comp, engine, go, regKey, handle, keep, spec);
    }

    private static MsComponentSpec BuildSpec(ComponentBase comp, string regKey, out GCHandle handle, out object[] keep)
    {
        handle = GCHandle.Alloc(comp);
        keep = new object[] {
            new MsCbVoid(ThunkAwake), new MsCbVoid(ThunkEnable), new MsCbVoid(ThunkStart),
            new MsCbVoid(ThunkDisable), new MsCbVoid(ThunkDestroy),
            new MsCbDt(ThunkUpdate), new MsCbDt(ThunkFixed), new MsCbDt(ThunkLate),
        };
        return new MsComponentSpec {
            ScriptType = regKey,
            OnAwake = Marshal.GetFunctionPointerForDelegate((Delegate)keep[0]),
            OnEnable = Marshal.GetFunctionPointerForDelegate((Delegate)keep[1]),
            OnStart = Marshal.GetFunctionPointerForDelegate((Delegate)keep[2]),
            OnDisable = Marshal.GetFunctionPointerForDelegate((Delegate)keep[3]),
            OnDestroy = Marshal.GetFunctionPointerForDelegate((Delegate)keep[4]),
            OnUpdate = Marshal.GetFunctionPointerForDelegate((Delegate)keep[5]),
            OnFixedUpdate = Marshal.GetFunctionPointerForDelegate((Delegate)keep[6]),
            OnLateUpdate = Marshal.GetFunctionPointerForDelegate((Delegate)keep[7]),
            UserData = GCHandle.ToIntPtr(handle),
        };
    }

    private static void Commit(ComponentBase comp, IntPtr engine, IntPtr go, string regKey,
                               GCHandle handle, object[] keep, MsComponentSpec spec)
    {
        _table[spec.UserData] = (handle, keep);
        var key = (engine, go);
        if (!_byGo.TryGetValue(key, out var list)) { list = new List<ComponentBase>(); _byGo[key] = list; }
        list.Add(comp);
        ScriptRegistry.Register(regKey, comp);   // M3.4 脚本桥（types/fields/set）
    }

    // —— 托管组件查询（托管组件注册表；P1-b：约束放宽到 Component 以配合统一查询面）——
    public static T? Find<T>(IntPtr engine, IntPtr go) where T : Component
    {
        if (_byGo.TryGetValue((engine, go), out var list))
            foreach (var c in list) if (c is T t) return t;
        return null;
    }

    public static T[] FindAll<T>(IntPtr engine, IntPtr go) where T : Component
    {
        var result = new List<T>();
        if (_byGo.TryGetValue((engine, go), out var list))
            foreach (var c in list) if (c is T t) result.Add(t);
        return result.ToArray();
    }

    public static T? FindFirst<T>() where T : Component
    {
        foreach (var kv in _byGo)
            foreach (var c in kv.Value) if (c is T t) return t;
        return null;
    }

    public static T[] FindAll<T>() where T : Component
    {
        var result = new List<T>();
        foreach (var kv in _byGo)
            foreach (var c in kv.Value) if (c is T t) result.Add(t);
        return result.ToArray();
    }

    public static void Unregister(ComponentBase comp)
    {
        foreach (var kv in _byGo)
            if (kv.Value.Remove(comp)) return;
    }

    public static void ReleaseAll()
    {
        foreach (var kv in _table) kv.Value.Handle.Free();
        _table.Clear();
        _byGo.Clear();
        ScriptRegistry.UnregisterAll();
        InvokeScheduler.Reset();
        NativeProxyCache.Reset();   // P1-b：原生代理缓存随引擎释放（句柄已失效）
    }

    // —— thunk（静态委托——GCHandle 反解到 C# 实例；同步回调于主线程）——
    private static void ThunkAwake(IntPtr ctx) => ((ComponentBase)GCHandle.FromIntPtr(ctx).Target!).Awake();
    private static void ThunkEnable(IntPtr ctx) => ((ComponentBase)GCHandle.FromIntPtr(ctx).Target!).OnEnable();
    private static void ThunkStart(IntPtr ctx) => ((ComponentBase)GCHandle.FromIntPtr(ctx).Target!).Start();
    private static void ThunkDisable(IntPtr ctx) => ((ComponentBase)GCHandle.FromIntPtr(ctx).Target!).OnDisable();
    private static void ThunkUpdate(IntPtr ctx, double dt) => ((ComponentBase)GCHandle.FromIntPtr(ctx).Target!).Update(dt);
    private static void ThunkFixed(IntPtr ctx, double dt) => ((ComponentBase)GCHandle.FromIntPtr(ctx).Target!).FixedUpdate(dt);
    private static void ThunkLate(IntPtr ctx, double dt) => ((ComponentBase)GCHandle.FromIntPtr(ctx).Target!).LateUpdate(dt);

    private static void ThunkDestroy(IntPtr ctx)
    {
        var comp = (ComponentBase)GCHandle.FromIntPtr(ctx).Target!;
        if (comp is ScriptBehaviour mb)
        {
            RoutineRunner.StopAll(mb);       // 销毁脚本=停掉其协程
            InvokeScheduler.CancelAll(mb);   // P1-a：销毁脚本=取消其全部 Invoke（单次+重复）
        }
        Unregister(comp);
        comp.OnDestroy();
    }
}