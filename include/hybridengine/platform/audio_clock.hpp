#pragma once

namespace HybridEngine::Platform {
class IAudioBackend;

// M1：采样时钟（v1 AudioClock 语义：有后端=PositionMs 为源；无后端=EngineTime 回退）
class AudioClock {
public:
    void Bind(IAudioBackend* backend);
    bool HasBackend() const { return backend_ != nullptr; }
    double SongTimeMs() const;      // 采样位置（无后端/未打开→引擎时间回退）
    void Tick(double realDt);       // 每帧驱动（回退累计 realDt*TimeScale）
    void SetTimeScale(double s);
    double TimeScale() const { return timeScale_; }
private:
    IAudioBackend* backend_ = nullptr;
    double timeScale_ = 1.0;
    double engineTimeMs_ = 0;
};

} // namespace HybridEngine::Platform
