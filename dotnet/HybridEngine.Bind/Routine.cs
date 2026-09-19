using System;
using System.Collections;
using System.Collections.Generic;

namespace HybridEngine.Engine;

// 托管风格协程（托管调度——GameEngine.RunFrame 驱动；不触碰 C ABI）。
public abstract class YieldInstruction
{
    internal abstract bool IsDone(double dt);
}

public sealed class WaitSeconds : YieldInstruction
{
    private double _remain;
    public WaitSeconds(double seconds) => _remain = seconds < 0 ? 0 : seconds;
    internal override bool IsDone(double dt) { _remain -= dt; return _remain <= 0; }
}

public sealed class WaitFrameEnd : YieldInstruction
{
    internal override bool IsDone(double dt) => true;   // 下一帧继续
}

public sealed class WaitFixedStep : YieldInstruction
{
    internal override bool IsDone(double dt) => true;   // P1：细粒度固定步调度
}

public sealed class Routine
{
    internal ScriptBehaviour? Owner;
    internal IEnumerator Body = null!;
    internal YieldInstruction? Waiting;
    internal Routine? Nested;
    internal bool Done;
    public bool IsDone => Done;
}

internal static class RoutineRunner
{
    private static readonly List<Routine> _running = new();

    public static Routine Start(ScriptBehaviour owner, IEnumerator routine)
    {
        var c = new Routine { Owner = owner, Body = routine };
        _running.Add(c);
        Advance(c, 0.0);   // 托管调度：立即执行到首个 yield
        return c;
    }

    public static void Stop(Routine c)
    {
        c.Done = true;
        _running.Remove(c);
    }

    public static void StopAll(ScriptBehaviour owner)
    {
        for (int i = _running.Count - 1; i >= 0; --i)
            if (ReferenceEquals(_running[i].Owner, owner)) _running.RemoveAt(i);
    }

    public static void Tick(double dt)
    {
        for (int i = _running.Count - 1; i >= 0; --i)
        {
            var c = _running[i];
            if (c.Done) { _running.RemoveAt(i); continue; }
            Advance(c, dt);
        }
    }

    private static void Advance(Routine c, double dt)
    {
        try
        {
            if (c.Nested != null)
            {
                if (!c.Nested.Done) return;
                c.Nested = null;
                return;   // 嵌套完成——宿主下一帧继续
            }
            if (c.Waiting != null)
            {
                if (!c.Waiting.IsDone(dt)) return;
                c.Waiting = null;
            }
            if (!c.Body.MoveNext()) { c.Done = true; return; }
            switch (c.Body.Current)
            {
                case YieldInstruction yi: c.Waiting = yi; break;
                case Routine nested: c.Nested = nested; break;
            }
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("[HybridEngine][ERROR] coroutine exception: " + ex);
            c.Done = true;
        }
    }
}