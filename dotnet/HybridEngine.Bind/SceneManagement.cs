using System;
using System.Collections.Generic;
using System.Text;
using HybridEngine.Engine.Internal;

namespace HybridEngine.Engine.SceneManagement;

// P2/多场景：运行期场景句柄的托管视图（= 引擎场景列表里的一个槽位）。
// 命名空间与 Unity 的 UnityEngine.SceneManagement 对齐；本类**不是** HybridEngine.Engine.Scene
//（后者是「按 IScene 构建场景」的抽象，两者无关，故分属不同命名空间）。
public sealed class Scene
{
    internal IntPtr EnginePtr { get; }
    internal int Index { get; }

    internal Scene(IntPtr engine, int index) { EnginePtr = engine; Index = index; }

    /// <summary>场景名（引擎内名称；.mscene 载入时取容器 name 行）。</summary>
    public string name
    {
        get
        {
            if (EnginePtr == IntPtr.Zero || !IsValid) return "";
            var buf = new byte[256];
            if (Native.ms_scene_name(EnginePtr, Handle, buf, buf.Length) != BindError.OK) return "";
            int n = Array.IndexOf(buf, (byte)0);
            if (n < 0) n = buf.Length;
            return Encoding.UTF8.GetString(buf, 0, n);
        }
    }

    /// <summary>是否仍是引擎场景列表中的有效槽位（卸载后为 false）。</summary>
    public bool IsValid
    {
        get
        {
            if (EnginePtr == IntPtr.Zero) return false;
            if (Native.ms_scenes_get(EnginePtr, Index, out var h) != BindError.OK) return false;
            return h != IntPtr.Zero;
        }
    }

    /// <summary>是否活动场景。</summary>
    public bool IsActive => IsValid && Native.ms_scene_active_index(EnginePtr, out int a) == BindError.OK && a == Index;

    /// <summary>根对象数。</summary>
    public int rootCount
    {
        get
        {
            if (!IsValid) return 0;
            return Native.ms_scene_root_count(EnginePtr, Handle, out int n) == BindError.OK ? n : 0;
        }
    }

    /// <summary>根对象（托管视图；索引越界=抛异常）。</summary>
    public SceneObject GetRoot(int index)
    {
        if (Native.ms_scene_root_get(EnginePtr, Handle, index, out var go) != BindError.OK)
            throw new ArgumentOutOfRangeException(nameof(index));
        long id = Native.ms_go_instance_id(EnginePtr, go);
        return new SceneObject(EnginePtr, go, id, "sceneRoot");
    }

    /// <summary>在本场景新建根对象（多场景下必须按场景建——不是往活动场景加）。</summary>
    public SceneObject AddRoot(string name)
    {
        if (!IsValid) throw new InvalidOperationException("scene 无效（已卸载）");
        int rc = Native.ms_scene_add_root(EnginePtr, Handle, name ?? "GameObject", out long id);
        if (rc != BindError.OK) throw new InvalidOperationException("ms_scene_add_root rc=" + rc);
        if (Native.ms_scene_go_get(EnginePtr, Handle, id, out var go) != BindError.OK)
            throw new InvalidOperationException("ms_scene_go_get failed");
        return new SceneObject(EnginePtr, go, id, name ?? "GameObject");
    }

    public IReadOnlyList<SceneObject> GetRoots()
    {
        var list = new List<SceneObject>();
        int n = rootCount;
        for (int i = 0; i < n; ++i) list.Add(GetRoot(i));
        return list;
    }

    internal IntPtr Handle
    {
        get
        {
            if (EnginePtr == IntPtr.Zero) return IntPtr.Zero;
            return Native.ms_scenes_get(EnginePtr, Index, out var h) == BindError.OK ? h : IntPtr.Zero;
        }
    }

    public override string ToString() => "Scene(" + Index + ", " + name + (IsActive ? ", active" : "") + ")";
}

// P2/多场景：SceneManager（Unity 同形 API）。
//
// 语义要点：
//   · 引擎恒有 ≥1 个场景（Scene() 始终有效）；最后一个场景不可卸载。
//   · 所有已加载场景都参与引擎帧（各自 Lifecycle 全序）；渲染把全部场景合成为**一趟**光栅。
//   · 卸载先拆除该场景全部对象（OnDisable/OnDestroy → 脚本实例/协程/Invoke 随之清退），再移除槽位。
//   · 附加加载含脚本组件重放（scriptFields → 托管实例重建）。
public static class SceneManager
{
    private static IntPtr E => GameEngine.Current?.Handle ?? IntPtr.Zero;

    /// <summary>已加载场景数（无引擎=0）。</summary>
    public static int sceneCount
    {
        get
        {
            if (E == IntPtr.Zero) return 0;
            return Native.ms_scenes_count(E, out int n) == BindError.OK ? n : 0;
        }
    }

    public static Scene GetSceneAt(int index)
    {
        if (index < 0 || index >= sceneCount) throw new ArgumentOutOfRangeException(nameof(index));
        return new Scene(E, index);
    }

    public static Scene GetActiveScene()
    {
        if (E == IntPtr.Zero) throw new InvalidOperationException("no current GameEngine");
        if (Native.ms_scene_active_index(E, out int a) != BindError.OK) throw new InvalidOperationException("ms_scene_active_index failed");
        return new Scene(E, a);
    }

    public static bool SetActiveScene(Scene scene)
    {
        if (scene == null || E == IntPtr.Zero) return false;
        return Native.ms_scene_set_active_index(E, scene.Index) == BindError.OK;
    }

    /// <summary>附加新建空场景（活动场景不变）。</summary>
    public static Scene CreateScene(string name)
    {
        if (E == IntPtr.Zero) throw new InvalidOperationException("no current GameEngine");
        int rc = Native.ms_scene_new(E, name ?? "Scene", out var handle);
        if (rc != BindError.OK) throw new InvalidOperationException("ms_scene_new rc=" + rc);
        return FindByHandle(handle) ?? throw new InvalidOperationException("ms_scene_new: scene not found");
    }

    /// <summary>
    /// 载入场景。additive=false（默认）=替换当前**活动场景**内容（= GameEngine.LoadScene 语义）；
    /// additive=true=附加到场景列表（活动场景不变）。
    /// </summary>
    public static Scene LoadScene(string assetPath, bool additive = false)
    {
        if (E == IntPtr.Zero) throw new InvalidOperationException("no current GameEngine");
        if (string.IsNullOrEmpty(assetPath)) throw new ArgumentException("assetPath 为空", nameof(assetPath));
        if (!additive)
        {
            int rc0 = Native.ms_scene_load(E, Native.ms_engine_scene(E), assetPath);
            if (rc0 != BindError.OK) throw new InvalidOperationException("ms_scene_load rc=" + rc0);
            return GetActiveScene();
        }
        int rc = Native.ms_scene_load_additive(E, assetPath, out var handle);
        if (rc != BindError.OK) throw new InvalidOperationException("ms_scene_load_additive rc=" + rc);
        return FindByHandle(handle) ?? throw new InvalidOperationException("ms_scene_load_additive: scene not found");
    }

    /// <summary>卸载场景（含对象拆除与脚本实例清退）。最后一个场景不可卸载 → 返回 false。</summary>
    public static bool UnloadScene(Scene scene)
    {
        if (scene == null || E == IntPtr.Zero || !scene.IsValid) return false;
        return Native.ms_scene_unload(E, scene.Handle) == BindError.OK;
    }

    // 场景句柄 → 索引（引擎新增槽位只追加，故按句柄比对）
    private static Scene? FindByHandle(IntPtr handle)
    {
        if (handle == IntPtr.Zero) return null;
        int n = sceneCount;
        for (int i = 0; i < n; ++i)
            if (Native.ms_scenes_get(E, i, out var h) == BindError.OK && h == handle) return new Scene(E, i);
        return null;
    }
}
