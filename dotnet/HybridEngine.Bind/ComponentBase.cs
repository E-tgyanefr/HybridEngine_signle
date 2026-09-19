using System;
using HybridEngine.Engine.Internal;

namespace HybridEngine.Engine;

// M2.5：C# 组件基类——八回调覆写点（标准顺序由 C++ LifecycleDriver 保证）
// P1-b：改为继承 Component（组件公共基）——与原生组件代理共用 GetComponent<T> 查询面。
public abstract class ComponentBase : Component, IDisposable
{
    public virtual void Awake() { }
    public virtual void OnEnable() { }
    public virtual void Start() { }
    public virtual void Update(double dt) { }
    public virtual void FixedUpdate(double dt) { }
    public virtual void LateUpdate(double dt) { }
    public virtual void OnDisable() { }
    public virtual void OnDestroy() { }

    public string RegKey { get; set; } = "";   // 注册键（脚本桥 instanceKey——M3.4）；未挂接实例=空串

    private bool _enabled = true;

    /// <summary>
    /// 组件开关（P1-a）。与 Unity 一致：置 false 后该组件的 Start/FixedUpdate/Update/LateUpdate 不再分派，
    /// 且触发一次 OnDisable；置回 true 触发一次 OnEnable。同值赋值=空操作（不重复回调）。
    /// 走 C ABI（ms_component_set_enabled → Component::SetEnabled → LifecycleDriver::SetActive），
    /// 因此 C++ 侧（编辑器 Inspector / 原生代码）看到的开关状态与托管侧一致。
    /// </summary>
    public bool Enabled
    {
        get
        {
            if (Owner != null && !string.IsNullOrEmpty(RegKey) && _nativeEnabled)
            {
                if (Native.ms_component_get_enabled(Owner.EnginePtr, Owner.GoPtr, RegKey, out int on) == BindError.OK)
                    return on != 0;
            }
            return _enabled;   // 未挂接 / 旧 DLL / 组件已移除
        }
        set
        {
            _enabled = value;
            if (Owner != null && !string.IsNullOrEmpty(RegKey) && _nativeEnabled)
                Native.ms_component_set_enabled(Owner.EnginePtr, Owner.GoPtr, RegKey, value ? 1 : 0);
        }
    }

    // 旧 DLL 兼容：无 ms_component_* 入口时（EntryPointNotFoundException）退化为纯托管开关（不崩）。
    private static bool? _nativeEnabledProbe;
    private static bool _nativeEnabled
    {
        get
        {
            if (_nativeEnabledProbe == null)
            {
                try { Native.ms_component_get_enabled(IntPtr.Zero, IntPtr.Zero, "", out _); _nativeEnabledProbe = true; }
                catch (EntryPointNotFoundException) { _nativeEnabledProbe = false; }
            }
            return _nativeEnabledProbe.Value;
        }
    }

    // 托管释放：仅从 owner 解除（GCHandle/OnDestroy 生命周期=引擎侧 thunk——ComponentBridge.ReleaseAll）
    public void Dispose() => Owner?.DetachComponent(this);
}