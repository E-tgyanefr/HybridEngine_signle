using System;

namespace HybridEngine.Engine;

// 日志门面（托管控制台；编辑器控制台接入见下一阶段）。
public static class Debug
{
    public static void Log(object? message) => Console.WriteLine("[HE] " + message);
    public static void LogWarning(object? message) => Console.WriteLine("[HE][WARN] " + message);
    public static void LogError(object? message) => Console.Error.WriteLine("[HE][ERROR] " + message);
    public static void LogException(Exception ex) => Console.Error.WriteLine("[HE][EXCEPTION] " + ex);
}