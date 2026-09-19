using System.Collections;

namespace HybridEngine.Engine;

// 托管风格脚本基类（推荐）：ComponentBase 的惯用面。
// 八回调顺序仍由 C++ LifecycleDriver 保证；本类只提供引擎开发者熟悉的成员。
// P1-b：sceneObject / transform / name 已上提到组件公共基 Component（原生代理同样具备）；
//       组件查询约束放宽到 Component —— GetComponent<MeshVisual>() 等原生查询与 Unity 同形。
public abstract class ScriptBehaviour : ComponentBase
{
    public T? GetComponent<T>() where T : Component => Owner.GetComponent<T>();
    public T[] GetComponents<T>() where T : Component => Owner.GetComponents<T>();
    public bool TryGetComponent<T>(out T? component) where T : Component => Owner.TryGetComponent(out component);
    public T? GetComponentInChildren<T>() where T : Component => Owner.GetComponentInChildren<T>();
    public T[] GetComponentsInChildren<T>() where T : Component => Owner.GetComponentsInChildren<T>();

    // —— 托管风格协程（托管调度——引擎帧钩子每帧驱动）——
    public Routine StartRoutine(IEnumerator routine) => RoutineRunner.Start(this, routine);
    public void StopRoutine(Routine routine) => RoutineRunner.Stop(routine);
    public void StopAllRoutines() => RoutineRunner.StopAll(this);

    // —— P1-a：定时调用（Unity 语义——托管调度，与协程同一推进点；详见 Invoke.cs 语义约定）——
    /// <summary>time 秒后调用无参方法 methodName（time&lt;=0 = 下一帧）。</summary>
    public void Invoke(string methodName, double time) => InvokeScheduler.Invoke(this, methodName, time);
    /// <summary>time 秒后调用 methodName，之后每 repeatRate 秒重复（repeatRate&lt;=0 = 退化为单次）。</summary>
    public void InvokeRepeating(string methodName, double time, double repeatRate) => InvokeScheduler.InvokeRepeating(this, methodName, time, repeatRate);
    /// <summary>取消本组件全部 Invoke/InvokeRepeating。</summary>
    public void CancelInvoke() => InvokeScheduler.Cancel(this, null);
    /// <summary>取消本组件 methodName 的全部 Invoke/InvokeRepeating。</summary>
    public void CancelInvoke(string methodName) => InvokeScheduler.Cancel(this, methodName);
    /// <summary>本组件是否有待触发的 Invoke。</summary>
    public bool IsInvoking() => InvokeScheduler.IsInvoking(this, null);
    /// <summary>methodName 是否有待触发的 Invoke。</summary>
    public bool IsInvoking(string methodName) => InvokeScheduler.IsInvoking(this, methodName);
}