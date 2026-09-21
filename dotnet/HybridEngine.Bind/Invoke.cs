using System;
using System.Collections.Generic;
using System.Reflection;

namespace HybridEngine.Engine;

// P1-a：Invoke / InvokeRepeating / CancelInvoke / IsInvoking —— Unity 语义的托管实现。
//
// 调度点：ManagedFrame.Tick(phase=1) → InvokeScheduler.Tick(dt)，即引擎每帧 Lifecycle 全序之后。
//   （与协程同一推进点；因此 Invoke 也能在编辑器 Play 直驱 Engine::Tick 的路径下工作。）
//
// 语义约定（Unity 文档对其中若干点未明确规定——此处固定为确定性行为并在 docs 记录）：
//   · 目标方法 = 该组件的**无参实例方法**（public / 非 public，含继承链）——与 Unity 一致（Invoke 不支持传参）。
//   · time <= 0 → 下一帧触发（Unity：负值等同 0）。
//   · Invoke(m,t) 可叠加：同一方法多次 Invoke = 多个待触发项，各自独立计时。
//   · InvokeRepeating(m,t,r)：同 (组件, 方法) 已有重复项时**重置**该项而不叠加（避免无界增长）；
//     r <= 0 退化为单次 Invoke（无法定义重复间隔）。
//   · 组件 Enabled=false：计时**暂停**（不触发、不丢弃），恢复后继续。
//   · 组件销毁 / Object.Destroy(component) → 取消该组件全部 Invoke（ComponentBridge.ThunkDestroy 收口）。
//   · 方法不存在 / 有重载歧义：报错并丢弃该条目（Unity 同为报错且不触发）。
//   · 长帧落后过多时不补跑：重复项顺延一个周期（避免追帧风暴）。
internal sealed class InvokeItem
{
    public ScriptBehaviour Owner = null!;
    // 归属引擎：Reset(engine) 只清本引擎的条目——多引擎并存时全清会误伤其它引擎
    //   （其它引擎的 Invoke 静默失效）。
    public IntPtr Engine;
    public string Method = "";
    public MethodInfo Target = null!;
    public double Remain;      // 距下次触发（秒）
    public double Repeat;      // > 0 = 重复间隔
    public bool Repeating;
    public bool Cancelled;
}

internal static class InvokeScheduler
{
    private static readonly List<InvokeItem> _items = new();
    private static readonly List<InvokeItem> _due = new();
    private static readonly Dictionary<(Type, string), MethodInfo?> _cache = new();

    public static void Invoke(ScriptBehaviour owner, string method, double time)
    {
        if (owner == null || string.IsNullOrEmpty(method)) return;
        var mi = Resolve(owner.GetType(), method);
        if (mi == null) { ReportMissing(owner, method); return; }
        _items.Add(new InvokeItem { Owner = owner, Engine = owner.Owner.EnginePtr, Method = method, Target = mi, Remain = time < 0 ? 0 : time });
    }

    public static void InvokeRepeating(ScriptBehaviour owner, string method, double time, double repeatRate)
    {
        if (owner == null || string.IsNullOrEmpty(method)) return;
        var mi = Resolve(owner.GetType(), method);
        if (mi == null) { ReportMissing(owner, method); return; }
        double delay = time < 0 ? 0 : time;
        if (repeatRate <= 0) { Invoke(owner, method, delay); return; }   // 无有效间隔 → 单次
        for (int i = 0; i < _items.Count; ++i)
        {
            var it = _items[i];
            if (it.Repeating && ReferenceEquals(it.Owner, owner) && it.Method == method)
            {
                it.Remain = delay;      // 重置而非叠加（Unity：重复注册同一方法=重新开始）
                it.Repeat = repeatRate;
                it.Target = mi;
                return;
            }
        }
        _items.Add(new InvokeItem { Owner = owner, Engine = owner.Owner.EnginePtr, Method = method, Target = mi, Remain = delay, Repeat = repeatRate, Repeating = true });
    }

    /// <summary>method==null → 取消该组件全部 Invoke；否则只取消该方法的全部条目（单次+重复）。</summary>
    public static void Cancel(ScriptBehaviour owner, string? method)
    {
        if (owner == null) return;
        for (int i = _items.Count - 1; i >= 0; --i)
        {
            var it = _items[i];
            if (!ReferenceEquals(it.Owner, owner)) continue;
            if (method != null && it.Method != method) continue;
            it.Cancelled = true;
            _items.RemoveAt(i);
        }
    }

    public static void CancelAll(ScriptBehaviour owner) => Cancel(owner, null);

    public static bool IsInvoking(ScriptBehaviour owner, string? method)
    {
        if (owner == null) return false;
        foreach (var it in _items)
        {
            if (!ReferenceEquals(it.Owner, owner)) continue;
            if (method == null || it.Method == method) return true;
        }
        return false;
    }

    public static void Tick(double dt)
    {
        if (_items.Count == 0) return;
        // 先收集到期项再调用：被调方法内可能再 Invoke/CancelInvoke（重入改 _items），
        // 故用 Cancelled 标记 + 副本迭代，避免遍历中修改集合。
        _due.Clear();
        foreach (var it in _items)
        {
            if (it.Cancelled || !it.Owner.Enabled) continue;   // 禁用=暂停计时（不触发也不丢弃）
            it.Remain -= dt;
            if (it.Remain <= 0) _due.Add(it);
        }
        for (int i = 0; i < _due.Count; ++i)
        {
            var it = _due[i];
            if (it.Cancelled || !it.Owner.Enabled) continue;   // 本轮早先的回调可能已取消它
            if (it.Repeating)
            {
                it.Remain += it.Repeat;
                if (it.Remain <= 0) it.Remain = it.Repeat;     // 长帧落后 → 顺延一个周期（不补跑）
            }
            else
            {
                it.Cancelled = true;
                _items.Remove(it);
            }
            try { it.Target.Invoke(it.Owner, null); }
            catch (TargetInvocationException tie) { Debug.LogException(tie.InnerException ?? tie); }
            catch (Exception ex) { Debug.LogException(ex); }
        }
        _due.Clear();
    }

    /// <summary>只清**指定引擎**的 Invoke 条目（ComponentBridge.ReleaseAll(engine) 调用）。
    /// <c>_cache</c> 是 (Type,Method)→MethodInfo 的纯反射缓存，与引擎无关，故保留不清。</summary>
    public static void Reset(IntPtr engine)
    {
        for (int i = _items.Count - 1; i >= 0; --i)
            if (_items[i].Engine == engine) _items.RemoveAt(i);
        _due.Clear();
    }

    public static int PendingCount => _items.Count;

    private static MethodInfo? Resolve(Type type, string method)
    {
        if (_cache.TryGetValue((type, method), out var cached)) return cached;
        MethodInfo? mi;
        try
        {
            // 指定 Type.EmptyTypes：只认无参重载（Invoke 不支持传参），避免与有参重载歧义
            mi = type.GetMethod(method, BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance,
                                null, Type.EmptyTypes, null);
        }
        catch (AmbiguousMatchException) { mi = null; }   // 继承链上多个无参同名 → 歧义=不触发（与 Unity 报错一致）
        _cache[(type, method)] = mi;
        return mi;
    }

    private static void ReportMissing(ScriptBehaviour owner, string method) =>
        Debug.LogError("Invoke: 方法不存在或无参重载 " + owner.GetType().Name + "." + method + "()");
}
