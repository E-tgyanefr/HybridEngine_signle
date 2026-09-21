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
    // ⚠ 必须按**引擎**分域（2026-09-21 修复）：
    //   原先 _table 只以 UserData 为键、ReleaseAll() 无参全清。于是
    //   GameEngine.Dispose()（或任何一处释放）会把**其它仍存活引擎**的
    //   GCHandle/delegate 根一起丢掉，而 C++ SpecTable 仍存着那些函数指针 →
    //   下一次 ms_engine_tick 就是"回调已回收的委托"：
    //   实测 `Process terminated. A callback was made on a garbage collected delegate`，
    //   退出码 -2146232797（fail-fast，进程级）。
    private static readonly Dictionary<(IntPtr Engine, IntPtr UserData), (GCHandle Handle, object[] Keep)> _table = new();
    private static readonly Dictionary<(IntPtr Engine, IntPtr Go), List<ComponentBase>> _byGo = new();

    public static string NextKey(string fullName) => fullName + "#" + ++_inst;

    /// <summary>把计数器推到不低于已知键的序号——场景重放会带进别的进程/会话生成的键（如 <c>Foo#3</c>），
    /// 若本进程计数器还停在 1，就会生成同一个键，导致 SpecTable 条目被就地覆盖、两个实例的派发串线
    /// （实测：存活对象丢掉自己的 Update，重放实例拿到两次派发）。</summary>
    public static void ReserveKey(string regKey)
    {
        int hash = regKey.LastIndexOf('#');
        if (hash < 0 || hash == regKey.Length - 1) return;
        if (int.TryParse(regKey.AsSpan(hash + 1), out int n) && n > _inst) _inst = n;
    }

    public static void Register(ComponentBase comp, IntPtr engine, IntPtr go, string regKey)
    {
        var spec = BuildSpec(comp, regKey, out var handle, out var keep);
        // H2：按 (注册键, 宿主对象) 注册**专属**条目——同键双实例各自有自己的回调表，
        //   否则另一对象挂着同名键时会互相覆盖（连 Update 都会丢）。
        int rc = Native.ms_component_register_for(engine, go, ref spec);
        if (rc != BindError.OK) { handle.Free(); throw new InvalidOperationException("ms_component_register_for rc=" + rc); }
        int rc2 = Native.ms_go_add_component(engine, go, regKey);
        if (rc2 != BindError.OK) { handle.Free(); throw new InvalidOperationException("ms_go_add_component rc=" + rc2); }
        Commit(comp, engine, go, regKey, handle, keep, spec);
    }

    /// <summary>
    /// P1-a 场景重放：原生脚本组件已由 C++ 反序列化创建——此处只**补挂回调表** + 托管登记，
    /// **不再** ms_go_add_component（会重复挂一个组件）。
    /// H2：按 (注册键, **本对象**) 注册专属条目，C++ 侧经 RebindToOwnEntry 取回自己的那份
    /// （若只按类型键注册，同键双实例时会命中对方的条目 → 派发串线）。
    /// </summary>
    public static void BindExisting(ComponentBase comp, IntPtr engine, IntPtr go, string regKey)
    {
        // 重放键来自场景文件（可能是别的进程/会话生成的）——先把计数器推过它，
        //   否则本进程之后生成的键会与它撞号。
        ReserveKey(regKey);
        var spec = BuildSpec(comp, regKey, out var handle, out var keep);
        int rc = Native.ms_component_register_for(engine, go, ref spec);
        if (rc != BindError.OK) { handle.Free(); throw new InvalidOperationException("ms_component_register_for(replay) rc=" + rc); }
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
        _table[(engine, spec.UserData)] = (handle, keep);
        var key = (engine, go);
        if (!_byGo.TryGetValue(key, out var list)) { list = new List<ComponentBase>(); _byGo[key] = list; }
        list.Add(comp);
        ScriptRegistry.Register(engine, regKey, comp);   // M3.4 脚本桥（types/fields/set）
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

    /// <summary>
    /// 只释放**指定引擎**的桥接状态。必须按引擎分域——无参全清会让其它存活引擎的
    /// 组件回调变悬垂（C++ 仍持旧函数指针），下一次 tick 直接崩。
    /// 调用时机在 ms_engine_destroy **之前**（销毁过程还要走 OnDisable/OnDestroy 回调）。
    /// </summary>
    public static void ReleaseAll(IntPtr engine)
    {
        var dead = new List<(IntPtr Engine, IntPtr UserData)>();
        foreach (var kv in _table) if (kv.Key.Engine == engine) dead.Add(kv.Key);
        foreach (var k in dead) { _table[k].Handle.Free(); _table.Remove(k); }

        var deadGo = new List<(IntPtr Engine, IntPtr Go)>();
        foreach (var kv in _byGo) if (kv.Key.Engine == engine) deadGo.Add(kv.Key);
        foreach (var k in deadGo) _byGo.Remove(k);

        ScriptRegistry.UnregisterAll(engine);
        InvokeScheduler.Reset(engine);
        RoutineRunner.Reset(engine);
        NativeProxyCache.Reset(engine);
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