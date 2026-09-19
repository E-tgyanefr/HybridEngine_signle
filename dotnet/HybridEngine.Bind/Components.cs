using System;
using System.Collections.Generic;
using System.Linq;
using HybridEngine.Engine.Internal;

namespace HybridEngine.Engine;

// P1-b：原生组件托管代理基。
//
// 代理 = 轻量视图（持有 SceneObject 视图）——生命周期/序列化/渲染仍由 C++ 原生组件承担：
//   · 代理**不参与**八回调、不进脚本桥（字段编辑走 ms_property_* 反射路径）
//   · 身份稳定：同一 (对象, 类型) 的由 NativeProxyCache 缓存复用 → ReferenceEquals 成立（与 Unity 组件引用语义一致）
//   · Exists：探测原生组件当前是否还在（对象销毁/组件移除后代理即失效，不再当有效引用用）
public abstract class NativeComponent : Component
{
    /// <summary>原生类型名（C++ 反射注册名——ABI 查找口径，如 "MeshVisual"）。</summary>
    public abstract string NativeTypeName { get; }

    public sealed override bool IsNative => true;

    /// <summary>该原生组件当前是否仍存在于宿主对象上（对象/组件销毁后为 false）。</summary>
    /// <remarks>
    /// 安全性：先经 <see cref="SceneObject.ResolveLivePtr"/> 按 InstanceId 重查活动场景，
    /// 对象已销毁时直接返回 false —— **不会**解引用已失效的裸指针（否则是 use-after-free）。
    /// </remarks>
    public bool Exists
    {
        get
        {
            if (Owner == null) return false;
            var live = Owner.ResolveLivePtr();
            if (live == IntPtr.Zero) return false;
            return Native.ms_go_has_component(Owner.EnginePtr, live, NativeTypeName, out int present) == BindError.OK
                   && present != 0;
        }
    }

    /// <summary>宿主对象的 InstanceId（代理失效后仍可用于日志/排查）。</summary>
    public long OwnerId => Owner?.Id ?? 0;
}

// P1-b：原生代理工厂 + 缓存。
// 工厂表是「类型 → 原生名 + 构造」的唯一登记点——新增原生包装时在此登记（NativeProxyCacheTests 会断言无遗漏）。
internal static class NativeProxyCache
{
    internal readonly struct Entry
    {
        public readonly string TypeName;
        public readonly Func<SceneObject, NativeComponent> Create;
        public Entry(string typeName, Func<SceneObject, NativeComponent> create) { TypeName = typeName; Create = create; }
    }

    private static readonly Dictionary<Type, Entry> Factories = new()
    {
        [typeof(MeshVisual)] = new Entry("MeshVisual", o => new MeshVisual(o)),
        [typeof(SpriteVisual)] = new Entry("SpriteVisual", o => new SpriteVisual(o)),
        [typeof(CameraComponent)] = new Entry("CameraComponent", o => new CameraComponent(o)),
        [typeof(LightComponent)] = new Entry("LightComponent", o => new LightComponent(o)),
    };

    private static readonly Dictionary<(IntPtr Engine, IntPtr Go, Type T), NativeComponent> Cache = new();

    /// <summary>该托管类型是否是原生包装的具体类型（工厂表精确命中）。</summary>
    public static bool IsNativeWrapper(Type t) => t != null && Factories.ContainsKey(t);

    /// <summary>该托管类型是否应走原生查询路径（具体包装，或其基类/接口——支持 GetComponent&lt;NativeComponent&gt;() 式基类查询）。</summary>
    public static bool IsNativeQuery(Type t) => t != null && typeof(NativeComponent).IsAssignableFrom(t);

    /// <summary>已登记的原生包装类型（测试用——与 NativeComponent 子类集合比对）。</summary>
    public static IEnumerable<Type> RegisteredTypes => Factories.Keys;

    /// <summary>
    /// 查询原生组件代理：宿主未挂该原生组件 → null（与 Unity GetComponent 未命中同义）。
    /// 命中时复用缓存实例（身份稳定）；若缓存实例对应的原生组件已被移除，则失效重建。
    /// 非泛型入口——供 SceneObject 在「运行时才能判定 T 是否原生包装」处调用。
    /// 支持基类查询：t 为抽象基类/接口时，按类型名序返回首个「已登记且已挂载」的具体包装（确定性）。
    /// </summary>
    public static NativeComponent? Get(Type t, SceneObject owner)
    {
        if (owner == null || t == null) return null;
        if (Factories.TryGetValue(t, out var entry)) return GetExact(t, entry, owner);

        // 基类/接口查询（如 GetComponent<NativeComponent>() / GetComponent<Component>() 的原生支路）
        foreach (var concrete in Factories.Keys.Where(k => t.IsAssignableFrom(k)).OrderBy(k => k.Name, StringComparer.Ordinal))
            if (Factories.TryGetValue(concrete, out var e2) && Present(owner, e2.TypeName))
                return GetExact(concrete, e2, owner);
        return null;
    }

    /// <summary>泛型入口（调用方已静态确定 T : NativeComponent）。</summary>
    public static T? Get<T>(SceneObject owner) where T : NativeComponent => (T?)Get(typeof(T), owner);

    private static NativeComponent? GetExact(Type t, Entry entry, SceneObject owner)
    {
        if (owner.GoPtr == IntPtr.Zero) return null;
        var key = (owner.EnginePtr, owner.GoPtr, t);
        if (Cache.TryGetValue(key, out var cached))
        {
            if (Present(owner, entry.TypeName)) return cached;   // 仍在 → 复用（身份稳定）
            Cache.Remove(key);                                  // 已被移除/宿主已销毁 → 失效
            return null;
        }
        if (!Present(owner, entry.TypeName)) return null;
        var made = entry.Create(owner);
        Cache[key] = made;
        return made;
    }

    /// <summary>Add* 之后登记（让 Add 与后续 GetComponent 返回同一代理实例）。</summary>
    public static T Remember<T>(SceneObject owner, T proxy) where T : NativeComponent
    {
        if (owner != null && owner.GoPtr != IntPtr.Zero) Cache[(owner.EnginePtr, owner.GoPtr, typeof(T))] = proxy;
        return proxy;
    }

    public static void Reset() => Cache.Clear();

    /// <summary>原生组件是否存在——先安全解析活动指针（对象销毁 → false），不解引用失效句柄。</summary>
    private static bool Present(SceneObject owner, string nativeTypeName)
    {
        var live = owner.ResolveLivePtr();
        if (live == IntPtr.Zero) return false;
        return Native.ms_go_has_component(owner.EnginePtr, live, nativeTypeName, out int present) == BindError.OK
               && present != 0;
    }
}

// 原生组件托管包装（轻量方法面——生命周期/序列化仍由原生组件承担）。
public sealed class MeshVisual : NativeComponent
{
    internal MeshVisual(SceneObject owner) { Owner = owner; }
    public override string NativeTypeName => "MeshVisual";

    public void SetMesh(double[] vertsXYZ, int triCount, uint color)
    {
        int rc = Native.ms_go_set_mesh(Owner.EnginePtr, Owner.GoPtr, vertsXYZ, triCount, color);
        if (rc != BindError.OK) throw new InvalidOperationException("ms_go_set_mesh rc=" + rc);
    }
    public void SetMaterial(double specular, double shininess, double[]? emissive = null)
    {
        int rc = Native.ms_go_set_material(Owner.EnginePtr, Owner.GoPtr, specular, shininess, emissive);
        if (rc != BindError.OK) throw new InvalidOperationException("ms_go_set_material rc=" + rc);
    }
}

public sealed class SpriteVisual : NativeComponent
{
    internal SpriteVisual(SceneObject owner) { Owner = owner; }
    public override string NativeTypeName => "SpriteVisual";

    public void Set(double r, double g, double b, double a, double width, double height, string? textureAsset = null)
    {
        int rc = Native.ms_sprite_set(Owner.EnginePtr, Owner.GoPtr, r, g, b, a, width, height, textureAsset);
        if (rc != BindError.OK) throw new InvalidOperationException("ms_sprite_set rc=" + rc);
    }
}

public sealed class CameraComponent : NativeComponent
{
    internal CameraComponent(SceneObject owner) { Owner = owner; }
    public override string NativeTypeName => "CameraComponent";

    public void Set(double[] eye, double[] target, double fovDeg, bool orthographic = false, double orthoSize = 10.0)
    {
        int rc = Native.ms_camera_set(Owner.EnginePtr, Owner.GoPtr, eye, target, fovDeg, orthographic ? 1 : 0, orthoSize);
        if (rc != BindError.OK) throw new InvalidOperationException("ms_camera_set rc=" + rc);
    }
}

public sealed class LightComponent : NativeComponent
{
    internal LightComponent(SceneObject owner) { Owner = owner; }
    public override string NativeTypeName => "LightComponent";

    public void Enable(bool on)
    {
        int rc = Native.ms_light_enable(Owner.EnginePtr, Owner.GoPtr, on ? 1 : 0);
        if (rc != BindError.OK) throw new InvalidOperationException("ms_light_enable rc=" + rc);
    }
    public void Set(int type, double[] position, double[] direction, double[] color,
                    double intensity, double range, double innerDeg, double outerDeg)
    {
        int rc = Native.ms_light_set(Owner.EnginePtr, Owner.GoPtr, type, position, direction, color,
                                     intensity, range, innerDeg, outerDeg);
        if (rc != BindError.OK) throw new InvalidOperationException("ms_light_set rc=" + rc);
    }
}
