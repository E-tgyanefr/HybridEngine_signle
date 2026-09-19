using System;
using System.Collections.Generic;
using System.Reflection;
using System.Text;
using HybridEngine.Engine.Internal;

namespace HybridEngine.Engine;

// P1-a：场景加载脚本组件重放——把磁盘上的 scriptFields 还原成**托管实例**并补挂到已反序列化的脚本组件上。
//
// 为什么需要：脚本组件的字段活在托管实例上（引擎 C++ 侧无字段副本），场景里只存了
//   「保存时的 per-instance 注册键 + scriptFields JSON」。C++ 反序列化只能造出无回调的占位组件，
//   真正把行为跑起来必须在托管侧重建实例——这就是本类（引擎在 ms_scene_load 内回调 Replay）。
//
// 流程（每个脚本条目一次）：
//   1. 注册键 FullName#N → 托管类型 FullName（跨程序集解析：宿主/项目脚本程序集均可）
//   2. 公共无参构造实例 → 建 Owner/sceneObject 视图（指向刚反序列化的原生对象）
//   3. ComponentBridge.BindExisting：按**原注册键**重注册回调表（C++ 侧同键就地更新 → 已存在的
//      BindComponent 立即取到新回调）、登记托管查询注册表与脚本桥
//   4. 回填 scriptFields；补 Awake→OnEnable（与 AddComponent 挂载同序；Start 由引擎下一帧触发）
internal static class SceneReplay
{
    private static readonly Dictionary<string, Type?> TypeCache = new();

    /// <summary>C++ → C# 重放回调（MsCbScriptReplay）。返回 0=成功，非 0=失败。</summary>
    public static int Replay(IntPtr engine, IntPtr goPtr, string savedKey, string fieldsJson)
    {
        try
        {
            if (engine == IntPtr.Zero || goPtr == IntPtr.Zero || string.IsNullOrEmpty(savedKey)) return -2;

            string typeName = StripInstanceSuffix(savedKey);
            var type = ResolveType(typeName);
            if (type == null)
            {
                Debug.LogError("场景重放：找不到脚本类型 " + typeName + "（注册键 " + savedKey + "）");
                return -1;
            }
            if (Activator.CreateInstance(type) is not ComponentBase comp)
            {
                Debug.LogError("场景重放：类型不可实例化（需要公共无参构造）" + typeName);
                return -1;
            }

            long id = Native.ms_go_instance_id(engine, goPtr);
            comp.Owner = new SceneObject(engine, goPtr, id, "replay");
            comp.RegKey = savedKey;   // 沿用保存键：再次保存时 type 不漂移（场景 round-trip 稳定）
            ComponentBridge.BindExisting(comp, engine, goPtr, savedKey);
            if (!string.IsNullOrEmpty(fieldsJson)) ApplyFieldsJson(comp, fieldsJson);
            comp.Awake();
            if (comp.Enabled) comp.OnEnable();
            return 0;
        }
        catch (Exception ex)
        {
            Debug.LogException(ex);
            return -1;
        }
    }

    /// <summary>"Ns.Type#3" → "Ns.Type"（非脚本键原样返回）。</summary>
    internal static string StripInstanceSuffix(string key)
    {
        int i = key.LastIndexOf('#');
        return i > 0 ? key.Substring(0, i) : key;
    }

    /// <summary>跨已加载程序集解析托管类型（宿主程序集 / 项目脚本程序集均可）。</summary>
    internal static Type? ResolveType(string fullName)
    {
        if (TypeCache.TryGetValue(fullName, out var cached)) return cached;
        Type? found = null;
        foreach (var asm in AppDomain.CurrentDomain.GetAssemblies())
        {
            try { found = asm.GetType(fullName, throwOnError: false); }
            catch { found = null; }
            if (found != null) break;
        }
        TypeCache[fullName] = found;
        return found;
    }

    internal static void ResetCache() => TypeCache.Clear();

    /// <summary>把 scriptFields 对象 JSON 逐字段回填到托管实例（字段集与 CsScriptBridge 的暴露规则一致）。</summary>
    internal static void ApplyFieldsJson(ComponentBase comp, string json)
    {
        foreach (var (name, value) in SplitTopLevel(json))
            CsScriptBridge.ApplyFieldJson(comp, name, value);
    }

    /// <summary>
    /// 顶层对象拆键：{"Speed":3,"Axis":[1,0,0],"Note":"a,b"} → [("Speed","3"),("Axis","[1,0,0]"),("Note","\"a,b\"")]。
    /// 极简实现：值内 {} / [] 按嵌套深度跳过、字符串内的逗号/括号不切分——脚本桥字段值只可能是
    /// 标量 / [x,y,z] / 字符串（与 CsScriptBridge 暴露类型一致），故无需完整 JSON 解析器。
    /// </summary>
    internal static List<(string Key, string Value)> SplitTopLevel(string json)
    {
        var list = new List<(string, string)>();
        if (string.IsNullOrEmpty(json)) return list;
        int i = 0, n = json.Length;
        while (i < n && json[i] != '{') ++i;
        if (i >= n) return list;
        ++i;
        while (i < n)
        {
            while (i < n && (char.IsWhiteSpace(json[i]) || json[i] == ',')) ++i;
            if (i >= n || json[i] != '"') break;      // '}' 或非法输入 → 结束
            ++i;
            var sb = new StringBuilder();
            while (i < n && json[i] != '"')
            {
                if (json[i] == '\\' && i + 1 < n) { ++i; }
                sb.Append(json[i]);
                ++i;
            }
            if (i < n) ++i;                            // 跳过结束引号
            while (i < n && (char.IsWhiteSpace(json[i]) || json[i] == ':')) ++i;
            int vStart = i, depth = 0;
            bool inStr = false;
            while (i < n)
            {
                char ch = json[i];
                if (inStr)
                {
                    if (ch == '\\') { i += 2; continue; }
                    if (ch == '"') inStr = false;
                }
                else if (ch == '"') inStr = true;
                else if (ch == '[' || ch == '{') ++depth;
                else if (ch == ']' || ch == '}')
                {
                    if (depth == 0) break;
                    --depth;
                }
                else if (ch == ',' && depth == 0) break;
                ++i;
            }
            list.Add((sb.ToString(), json.Substring(vStart, i - vStart).Trim()));
        }
        return list;
    }
}
