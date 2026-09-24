using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using HybridEngine.Engine.Internal;

namespace HybridEngine.Engine;

// M3.4：CsScriptBridge——C# 反射桥（types/fields/set 三回调经绑定注册；编辑器 Inspector 脚本字段）
public static class CsScriptBridge
{
    private static IntPtr _engine;
    // 委托保活：GetFunctionPointerForDelegate 不持有引用——必须由静态字段持有，否则被 GC 回收即崩
    private static FieldsDel? _fieldsDel;
    private static SetDel? _setDel;
    private static TypesDel? _typesDel;
    private static CallDel? _callDel;   // t-graph-script：节点图调用脚本方法（反射 Invoke）
    private static MembersDel? _membersDel;   // t-graph-member：脚本成员清单（节点图调色板下拉用）
    private static MsCbScriptReplay? _replayDel;

    public static void Register(IntPtr engineHandle)
    {
        _engine = engineHandle;
        _fieldsDel = FieldsGet;
        _setDel = FieldSet;
        _typesDel = TypeList;
        int rc = Internal.Native.ms_script_bridge_register(engineHandle, IntPtr.Zero,
            Marshal.GetFunctionPointerForDelegate(_fieldsDel),
            Marshal.GetFunctionPointerForDelegate(_setDel),
            Marshal.GetFunctionPointerForDelegate(_typesDel));
        if (rc != 0) throw new InvalidOperationException("ms_script_bridge_register rc=" + rc);

        // P1-a：脚本组件重放回调（场景加载——scriptFields → 托管实例重建）。
        // 旧 DLL 无此入口：桥仍可用，仅场景重放不可用（ms_scene_load 回退「无脚本实例」旧路径）。
        _replayDel = SceneReplay.Replay;
        try
        {
            int rc2 = Internal.Native.ms_script_replay_register(engineHandle, Marshal.GetFunctionPointerForDelegate(_replayDel), engineHandle);
            if (rc2 != 0) throw new InvalidOperationException("ms_script_replay_register rc=" + rc2);
        }
        catch (EntryPointNotFoundException) { _replayDel = null; }

        // t-graph-script：节点图 → 调用脚本方法。老 DLL 没这个入口时降级（脚本节点会明确报错，不静默）
        _callDel = ScriptCall;
        try
        {
            int rc3 = Internal.Native.ms_script_bridge_register_call(engineHandle, Marshal.GetFunctionPointerForDelegate(_callDel));
            if (rc3 != 0) throw new InvalidOperationException("ms_script_bridge_register_call rc=" + rc3);
        }
        catch (EntryPointNotFoundException) { _callDel = null; }

        // t-graph-member：脚本成员清单（类型 → 可调方法 / 可读写字段）。老 DLL 无入口时降级为空清单。
        _membersDel = MembersJson;
        try
        {
            int rc4 = Internal.Native.ms_script_bridge_register_members(engineHandle, Marshal.GetFunctionPointerForDelegate(_membersDel));
            if (rc4 != 0) throw new InvalidOperationException("ms_script_bridge_register_members rc=" + rc4);
        }
        catch (EntryPointNotFoundException) { _membersDel = null; }
    }

    // 反向 P/Invoke：出参=IntPtr+cap（byte[] 不可用于回调出参——长度未知）
    //
    // ⚠ 每个 string 参数都必须显式 [MarshalAs(UnmanagedType.LPUTF8Str)]：
    //   反向 P/Invoke 的默认 string 编组是 **CharSet.Ansi = 系统 ACP**，而 ABI 契约是 UTF-8
    //   （ms_bind.h 头注释）。漏标会让非 ASCII 实例键/字段值被按 ACP 解码 →
    //   静默乱码（实测 ACP=936 时 '中文类型#1' → '涓枃绫诲瀷#1'），
    //   进而 instanceKey 查不到组件 → 检查器编辑/脚本调用静默失效。
    //   同一文件里 Interop.cs 的 MsCbScriptReplay 已正确标注，可对照。
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate int FieldsDel(IntPtr userData, [MarshalAs(UnmanagedType.LPUTF8Str)] string instanceKey, IntPtr outJson, int cap);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate int SetDel(IntPtr userData, [MarshalAs(UnmanagedType.LPUTF8Str)] string instanceKey,
                               [MarshalAs(UnmanagedType.LPUTF8Str)] string field,
                               [MarshalAs(UnmanagedType.LPUTF8Str)] string jsonValue);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate int TypesDel(IntPtr userData, IntPtr outJson, int cap);
    // t-graph-script：调用脚本的一个公开方法（无参或单个 double 参数）。outResult 收返回值（void → 0）。
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate int CallDel(IntPtr userData, [MarshalAs(UnmanagedType.LPUTF8Str)] string instanceKey,
                                [MarshalAs(UnmanagedType.LPUTF8Str)] string method, double arg, out double outResult);
    // t-graph-member：成员清单（JSON 出参）——节点图调色板的下拉数据
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate int MembersDel(IntPtr userData, IntPtr outJson, int cap);

    // t-graph-script：反射调用。只认「公开实例方法 + 无参/单 double 参数」——覆盖面够用（游戏逻辑里的
    //   Spawn*/Reset*/Set* 基本都是这个形状），且**明确拒绝**其它签名（返回非 0，让图里能看到失败）。
    private static int ScriptCall(IntPtr userData, string instanceKey, string method, double arg, out double outResult)
    {
        outResult = 0.0;
        var comp = ScriptRegistry.Find(_engine, instanceKey);
        if (comp == null) return -1;                       // 实例不在（未挂载/已销毁）
        var t = comp.GetType();
        var mi = t.GetMethod(method, BindingFlags.Public | BindingFlags.Instance, null, Type.EmptyTypes, null);
        bool withArg = false;
        if (mi == null)
        {
            mi = t.GetMethod(method, BindingFlags.Public | BindingFlags.Instance, null, new[] { typeof(double) }, null);
            withArg = mi != null;
        }
        if (mi == null) return -2;                         // 没有这个方法/签名不支持
        try
        {
            object? r = withArg ? mi.Invoke(comp, new object[] { arg }) : mi.Invoke(comp, null);
            if (r is IConvertible cv) outResult = cv.ToDouble(System.Globalization.CultureInfo.InvariantCulture);
            return 0;
        }
        catch (Exception) { return -3; }                   // 方法内部抛异常（不让它穿到 C++ 侧）
    }

    // t-graph-member：枚举**已挂载脚本实例**的类型 → 其可调用方法与可读写字段。
    //   "可调用" = 与 ScriptCall 支持的形状一致（公开实例方法 + 无参/单 double）——
    //   清单与执行端**同一判据**，避免下拉里列出调不了的方法。
    private static int MembersJson(IntPtr userData, IntPtr outJson, int cap)
    {
        var sb = new StringBuilder();
        sb.Append("[");
        var seen = new HashSet<Type>();
        bool firstType = true;
        foreach (var comp in ScriptRegistry.All(_engine))
        {
            var t = comp.GetType();
            if (!seen.Add(t)) continue;
            if (!firstType) sb.Append(",");
            firstType = false;
            sb.Append("{\"type\":\"").Append(t.FullName ?? t.Name).Append("\",\"methods\":[");
            bool firstM = true;
            foreach (var m in t.GetMethods(BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly))
            {
                if (m.IsSpecialName) continue;                                  // 属性访问器不算
                var ps = m.GetParameters();
                bool callable = ps.Length == 0 ||
                                (ps.Length == 1 && ps[0].ParameterType == typeof(double));
                if (!callable) continue;
                if (!firstM) sb.Append(",");
                firstM = false;
                sb.Append("{\"name\":\"").Append(m.Name).Append("\",\"arg\":").Append(ps.Length == 1 ? "true" : "false")
                  .Append(",\"ret\":\"").Append(m.ReturnType == typeof(void) ? "void" : "num").Append("\"}");
            }
            sb.Append("],\"fields\":[");
            bool firstF = true;
            foreach (var f in t.GetFields(BindingFlags.Public | BindingFlags.Instance))
            {
                if (!IsSerializable(f.FieldType)) continue;
                if (!firstF) sb.Append(",");
                firstF = false;
                sb.Append("{\"name\":\"").Append(f.Name).Append("\",\"rw\":true}");
            }
            sb.Append("],\"props\":[");
            bool firstP = true;
            foreach (var p in t.GetProperties(BindingFlags.Public | BindingFlags.Instance))
            {
                if (!p.CanRead || p.GetIndexParameters().Length != 0) continue;
                if (!IsSerializable(p.PropertyType)) continue;
                if (!firstP) sb.Append(",");
                firstP = false;
                sb.Append("{\"name\":\"").Append(p.Name).Append("\",\"rw\":").Append(p.CanWrite ? "true" : "false").Append("}");
            }
            sb.Append("]}");
        }
        sb.Append("]");
        return WriteUtf8(sb.ToString(), outJson, cap);
    }

    private static int FieldsGet(IntPtr userData, string instanceKey, IntPtr outJson, int cap)
    {
        var comp = ScriptRegistry.Find(_engine, instanceKey);
        if (comp == null) return -1;
        return WriteUtf8(BuildFieldsJson(comp), outJson, cap);
    }

    private static int FieldSet(IntPtr userData, string instanceKey, string field, string jsonValue)
    {
        var comp = ScriptRegistry.Find(_engine, instanceKey);
        if (comp == null) return -1;
        return SetFieldValue(comp, field, jsonValue) ? 0 : -2;
    }

    private static int TypeList(IntPtr userData, IntPtr outJson, int cap)
    {
        var sb = new StringBuilder();
        sb.Append("[");
        bool first = true;
        foreach (var kv in ScriptRegistry.Types(_engine))
        {
            if (!first) sb.Append(",");
            first = false;
            sb.Append("\"").Append(kv).Append("\"");
        }
        sb.Append("]");
        return WriteUtf8(sb.ToString(), outJson, cap);
    }

    /// <summary>桥内部/宿主簿记属性——**不算脚本字段**，读（BuildFieldsJson）与写（SetFieldValue）
    /// 两侧都必须挡（2026-09-24 补齐）。
    /// 为什么需要：BuildFieldsJson 为支持 `public int Score => _score;` 这类只读属性，会把 public
    /// 属性一并扫进来，于是**基类的簿记属性被顺带带出**：`name`（=Owner.Name，回填会改对象名）、
    /// `IsNative`、`RegKey`（检查器里可写——改了它组件就再也查不到）、`Enabled`。
    /// 症状：每个组件的 scriptFields 都多出这几项，重放回填又写回去（读档时悄悄改对象名/组件开关）。
    /// 注意 MembersJson（节点图调色板）只扫**字段**，不含属性，故本来就没这个泄漏——只此一处要挡。
    /// Python 侧早有同一份名单（见 python/hybridengine/script_bridge.py 的 _SKIP_FIELDS），两侧口径对齐。</summary>
    private static readonly HashSet<string> SkipFields = new() { "name", "IsNative", "RegKey", "Enabled" };

    private static string BuildFieldsJson(ComponentBase comp)
    {
        var sb = new StringBuilder();
        sb.Append("{");
        bool first = true;
        foreach (var f in comp.GetType().GetFields(BindingFlags.Public | BindingFlags.Instance))
        {
            if (SkipFields.Contains(f.Name)) continue;
            if (!IsSerializable(f.FieldType)) continue;
            if (!first) sb.Append(",");
            first = false;
            sb.Append("\"").Append(f.Name).Append("\":").Append(ToJson(f.GetValue(comp)));
        }
        // t-graph-member：**公开属性也读出来**——引擎侧脚本常用 `public int Score => _score;` 这种只读属性
        //   暴露状态（STG 脚本就是），只认字段的话图里会看到空清单。
        foreach (var p in comp.GetType().GetProperties(BindingFlags.Public | BindingFlags.Instance))
        {
            if (SkipFields.Contains(p.Name)) continue;      // 基类簿记属性不是脚本字段（见 SkipFields）
            if (!p.CanRead || p.GetIndexParameters().Length != 0) continue;
            if (!IsSerializable(p.PropertyType)) continue;
            try
            {
                if (!first) sb.Append(",");
                first = false;
                sb.Append("\"").Append(p.Name).Append("\":").Append(ToJson(p.GetValue(comp)));
            }
            catch { /* 取值抛异常=跳过这一项（不让它把整份字段 JSON 带崩） */ }
        }
        sb.Append("}");
        return sb.ToString();
    }

    private static bool SetFieldValue(ComponentBase comp, string field, string json)
    {
        foreach (var f in comp.GetType().GetFields(BindingFlags.Public | BindingFlags.Instance))
        {
            if (SkipFields.Contains(f.Name)) continue;
            if (f.Name != field || !IsSerializable(f.FieldType)) continue;
            f.SetValue(comp, FromJson(json, f.FieldType));
            return true;
        }
        // t-graph-member：字段找不到 → 试**可写属性**（`public double Speed { get; set; }` 这类）
        foreach (var p in comp.GetType().GetProperties(BindingFlags.Public | BindingFlags.Instance))
        {
            if (SkipFields.Contains(p.Name)) continue;      // 不许经检查器/重放改 RegKey 等簿记项
            if (p.Name != field || !p.CanWrite || p.GetIndexParameters().Length != 0) continue;
            if (!IsSerializable(p.PropertyType)) continue;
            p.SetValue(comp, FromJson(json, p.PropertyType));
            return true;
        }
        return false;
    }

    /// <summary>P1-a 场景重放：按字段名回填单个字段（JSON 值文本——与 fields 桥同 schema）。</summary>
    internal static bool ApplyFieldJson(ComponentBase comp, string field, string json) => SetFieldValue(comp, field, json);

    private static bool IsSerializable(Type t) => t == typeof(int) || t == typeof(double) || t == typeof(bool) || t == typeof(string) || t == typeof(Vector3);

    private static string ToJson(object? v)
    {
        if (v is int i) return i.ToString();
        if (v is double d) return d.ToString("G");
        if (v is bool b) return b ? "true" : "false";
        if (v is string s) return "\"" + s + "\"";
        if (v is Vector3 vec) return "[" + vec.X.ToString("G") + "," + vec.Y.ToString("G") + "," + vec.Z.ToString("G") + "]";
        return "null";
    }

    private static object? FromJson(string json, Type t)
    {
        if (t == typeof(int)) return (int)(Math.Round(ParseNum(json)));
        if (t == typeof(double)) return ParseNum(json);
        if (t == typeof(bool)) return json == "true";
        if (t == typeof(string)) return json.Trim('"');
        if (t == typeof(Vector3))
        {
            var parts = json.Trim('[', ']').Split(',');
            return new Vector3(double.Parse(parts[0]), double.Parse(parts[1]), double.Parse(parts[2]));
        }
        return null;
    }

    private static double ParseNum(string json) => double.Parse(json.Contains('.') ? json : json + ".0");

    private static int WriteUtf8(string s, IntPtr buf, int cap)
    {
        byte[] b = Encoding.UTF8.GetBytes(s);
        // -3 (ErrBadArg) 是**约定**的"容量不足"码：引擎据此换大缓冲重试一次（见 ms_bind.h 的
        //   ms_cb_script_fields 返回码契约）。别改成 -1——那与"实例未找到"同码，引擎无法区分
        //   "装不下"和"真失败"，字段就会被静默丢弃。
        if (b.Length + 1 > cap) return -3;
        Marshal.Copy(b, 0, buf, b.Length);
        Marshal.WriteByte(buf, b.Length, 0);
        return 0;
    }
}

// 脚本组件实例/类型登记（ComponentBridge 注册时联动）
internal static class ScriptRegistry
{
    // 按**引擎**分域：多引擎并存时全局字典会让 A 引擎的 instanceKey 命中 B 引擎的组件
    //   （键形如 Type#n，两个引擎的计数器各自从 1 开始 → 必然撞键）。
    // 另用有序键列表保证 Types()/All() 的枚举顺序**跨进程稳定**
    //   （M9：原先直接用 Dictionary.Values，随字符串哈希随机化而变）。
    private static readonly Dictionary<(IntPtr Engine, string Key), ComponentBase> Map = new();
    private static readonly Dictionary<(IntPtr Engine, string Key), string> TypeNames = new();
    private static readonly List<(IntPtr Engine, string Key)> Order = new();

    public static void Register(IntPtr engine, string key, ComponentBase comp)
    {
        var k = (engine, key);
        if (!Map.ContainsKey(k)) Order.Add(k);
        Map[k] = comp;
        TypeNames[k] = comp.GetType().Name;
    }

    public static void UnregisterAll(IntPtr engine)
    {
        Order.RemoveAll(k => k.Engine == engine);
        foreach (var k in new List<(IntPtr, string)>(Map.Keys)) if (k.Item1 == engine) Map.Remove(k);
        foreach (var k in new List<(IntPtr, string)>(TypeNames.Keys)) if (k.Item1 == engine) TypeNames.Remove(k);
    }

    public static ComponentBase? Find(IntPtr engine, string key) =>
        Map.TryGetValue((engine, key), out var c) ? c : null;

    /// <summary>本引擎已挂载实例的类型名（**按注册顺序**——跨进程稳定）。</summary>
    public static IEnumerable<string> Types(IntPtr engine)
    {
        foreach (var k in Order)
            if (k.Engine == engine && TypeNames.TryGetValue(k, out var tn)) yield return tn;
    }

    // t-graph-member：所有已挂载实例（成员清单按它枚举类型）——同样按注册顺序
    public static IEnumerable<ComponentBase> All(IntPtr engine)
    {
        foreach (var k in Order)
            if (k.Engine == engine && Map.TryGetValue(k, out var c)) yield return c;
    }
}
