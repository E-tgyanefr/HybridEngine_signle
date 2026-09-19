#pragma once
#include "hybridengine/core/scene.hpp"
#include "hybridengine/core/assets/asset_library.hpp"
#include "hybridengine/platform/renderer.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace HybridEngine::Editor::Ai {

// t130/t131：编辑器 AI 助手（A=离线规则 12 条零服务（默认/恒开）；B=本地 OpenAI 兼容档（127.0.0.1:8080 默认关）
// 线性：B 错误/超时/断连=回退规则结果（永不阻塞编辑器——5s 超时+后台线程）；WinINet（系统库）HTTP——零第三方红线内

// —— 设置 ——
struct AiSettings {
    bool enabledLocal = false;                    // B 档默认关（A 恒开——AI-3 决策）
    std::string endpoint = "http://127.0.0.1:8080";
    int timeoutSec = 5;
    std::string persistencePath = "Assets/EditorAiConfig.mcfg";   // 设置 JSON（M2 侧车）
};

// —— 状态（六区共享 + 持久化） ——
enum class AiMode { OfflineRules, LocalAi };
class AiState {
public:
    static AiMode Mode();
    static void SetMode(AiMode m);
    static AiSettings& Settings();
    static void Persist();                        // 写设置 JSON（mcfg）
    static void Load();                           // 读设置 JSON（幂等）
};

// —— A：离线规则 ——
enum class Severity { Info, Warn, Error };
struct RuleResult {
    std::string ruleId;
    Severity sev = Severity::Info;
    std::string message;
    bool isObject = false;
    long objectId = 0;
};

class RuleChecker {
public:
    // 全场景（A 同步 <10ms——确定性；顺序稳定）
    static std::vector<RuleResult> Run(const HybridEngine::Core::Scene& scene);
    // 选中对象
    static std::vector<RuleResult> RunObject(const HybridEngine::Core::Scene& scene, long goId);
    // 12 条规则元数据（id/label/severity——测试+UI 徽章）
    struct RuleMeta { const char* id; const char* label; Severity sev; };
    static const std::vector<RuleMeta>& RuleMetas();
};

// —— B：本地服务客户端 ——
struct AiResponse {
    int code = 0;                  // HTTP 状态（0=未发起/超时）
    std::string text;
    bool fallback = false;         // true=规则回退（无服务/超时/断连）
    std::string fallbackMessage;   // 回退原因
};

class LocalAiClient {
public:
    LocalAiClient();
    ~LocalAiClient();
    LocalAiClient(const LocalAiClient&) = delete;
    LocalAiClient& operator=(const LocalAiClient&) = delete;

    bool Available();                                  // ping（30s 间隔缓存）
    AiResponse Check(const std::string& sceneJson);    // POST /v1/chat/completions（OpenAI 兼容）
    AiResponse Summary(const std::string& objectJson);
    AiResponse Assist(const std::string& scriptSnippet);
    // 异步封装（B=异步——后台线程+回调；⚠ 主线程回派=调用方责任：done 回调必须经 EventBus/主线程队列
    // 回派（防跨线程触碰 UI——t131 评审偏离 ③ 登记：P1 接线时落实——当前无 UI 消费方不阻塞）
    void CheckAsync(const std::string& sceneJson, std::function<void(const AiResponse&)> done);
    void SummaryAsync(const std::string& objectJson, std::function<void(const AiResponse&)> done);
    void AssistAsync(const std::string& scriptSnippet, std::function<void(const AiResponse&)> done);
    // 测试注入（mock 传输——B 关闭零调用断言/响应解析/回退）
    struct Transport {
        std::function<bool(const std::string& endpoint, const std::string& path,
                           const std::string& body, int timeoutSec, int& code, std::string& resp)> post;
    };
    static void SetTestTransport(Transport t);         // 设置后网络调用经注入实现
    static void ClearTestTransport();
    static int TestCallCount();                        // B 关闭零调用断言（验收 #3）

private:
    AiResponse Request(const std::string& path, const std::string& body);
    static bool Post(const std::string& endpoint, const std::string& path,
                     const std::string& body, int timeoutSec, int& code, std::string& resp);
    std::atomic<bool> pingOk_{false};
    std::atomic<long long> pingAt_{0};
};

} // namespace HybridEngine::Editor::Ai
