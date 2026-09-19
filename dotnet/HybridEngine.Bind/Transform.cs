using System;
using HybridEngine.Engine.Internal;

namespace HybridEngine.Engine;

// M2.5：数学最小集（对齐 C++ Vec3/Quat——double）
public readonly struct Vector3
{
    public readonly double X; public readonly double Y; public readonly double Z;
    public Vector3(double x, double y, double z) { X = x; Y = y; Z = z; }
    public static Vector3 Zero => new Vector3(0, 0, 0);
}

public readonly struct Quaternion
{
    public readonly double W; public readonly double X; public readonly double Y; public readonly double Z;
    public Quaternion(double w, double x, double y, double z) { W = w; X = x; Y = y; Z = z; }
    public static Quaternion Identity => new Quaternion(1, 0, 0, 0);
}

// 变换代理（ABI ms_transform_*——pos(3)/rot(4 wxyz)/scale(3)；parent 经 ms_id。class=属性链可写）
public sealed class Transform
{
    private readonly IntPtr _engine;
    private readonly IntPtr _go;
    internal Transform(IntPtr engine, IntPtr go) { _engine = engine; _go = go; }

    public Vector3 Position
    {
        get { var v = new double[3]; Native.ms_transform_get(_engine, _go, "pos", v); return new Vector3(v[0], v[1], v[2]); }
        set { Native.ms_transform_set(_engine, _go, "pos", new[] { value.X, value.Y, value.Z }); }
    }
    public Quaternion Rotation
    {
        get { var v = new double[4]; Native.ms_transform_get(_engine, _go, "rot", v); return new Quaternion(v[0], v[1], v[2], v[3]); }
        set { Native.ms_transform_set(_engine, _go, "rot", new[] { value.W, value.X, value.Y, value.Z }); }
    }
    public Vector3 Scale
    {
        get { var v = new double[3]; Native.ms_transform_get(_engine, _go, "scale", v); return new Vector3(v[0], v[1], v[2]); }
        set { Native.ms_transform_set(_engine, _go, "scale", new[] { value.X, value.Y, value.Z }); }
    }
    public long ParentId => Native.ms_transform_parent(_engine, _go);
    public void SetParent(long parentId, bool keepWorld = true) => Native.ms_transform_set_parent(_engine, _go, parentId, keepWorld ? 1 : 0);
    public void DetachParent() => Native.ms_transform_set_parent(_engine, _go, 0, 0);
}
