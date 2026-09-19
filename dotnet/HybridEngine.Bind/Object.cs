using HybridEngine.Engine.Internal;

namespace HybridEngine.Engine;

// 托管静态对象 API。
public static class Object
{
    /// <summary>销毁 SceneObject（委托 C++ 生命周期：帧尾 OnDisable→OnDestroy）。</summary>
    public static void Destroy(SceneObject obj) => obj.Destroy();

    /// <summary>移除托管组件（经 C ABI 组件注册键——OnDisable→OnDestroy 同步；仅托管组件）。</summary>
    public static void Destroy(ComponentBase component)
    {
        if (component?.Owner == null) return;
        int rc = Native.ms_go_remove_component(component.Owner.EnginePtr, component.Owner.GoPtr, component.RegKey);
        if (rc != BindError.OK) throw new System.InvalidOperationException("ms_go_remove_component rc=" + rc);
        component.Owner.DetachComponent(component);
    }

    /// <summary>预制体实例化（.msprefab → 当前场景）。</summary>
    public static SceneObject Instantiate(string prefabAssetPath)
    {
        if (GameEngine.Current == null) throw new System.InvalidOperationException("no current GameEngine");
        return GameEngine.Current.InstantiatePrefab(prefabAssetPath);
    }

    public static T? FindObjectOfType<T>() where T : ComponentBase => ComponentBridge.FindFirst<T>();
    public static T[] FindObjectsOfType<T>() where T : ComponentBase => ComponentBridge.FindAll<T>();

    /// <summary>单场景引擎：无跨场景销毁语义（保持 API 形状）。</summary>
    public static void DontDestroyOnLoad(SceneObject obj) { _ = obj; }
}