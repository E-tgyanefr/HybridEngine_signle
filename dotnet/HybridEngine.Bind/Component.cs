namespace HybridEngine.Engine;

// P1-b：组件公共基——对应 Unity 的 Component。
//
// 与 Unity 同形的约束关系：托管脚本组件（ComponentBase）与原生组件托管代理（NativeComponent）共同继承本类，
// 使 GetComponent<T>() / GetComponents<T>() 能用单一泛型方法同时查询两类组件
// ——C# 无法仅靠泛型约束重载同名方法（`where T : ComponentBase` 与 `where T : NativeComponent`
//   签名相同=编译错误），故必须以公共基类收敛约束（Unity 也正是这个形状）。
//
// 本基类只承载「身份 + 宿主视图」：
//   · 生命周期/八回调/字段桥 → ComponentBase（托管脚本）
//   · 原生行为（网格/材质/相机/光照）→ 各原生包装（NativeComponent 子类）
public abstract class Component
{
    /// <summary>该组件挂载的 SceneObject。</summary>
    public SceneObject Owner { get; internal set; } = null!;

    /// <summary>该组件挂载的 SceneObject（等价于 this.sceneObject）。</summary>
    public SceneObject sceneObject => Owner;

    /// <summary>该对象的 Transform（等价于 this.transform）。</summary>
    public Transform transform => Owner.Transform;

    /// <summary>对象名（托管视图）。</summary>
    public string name { get => Owner.Name; set => Owner.Name = value; }

    /// <summary>是否原生组件代理（托管脚本组件=false）。</summary>
    public virtual bool IsNative => false;
}
