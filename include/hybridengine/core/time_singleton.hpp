#pragma once
namespace HybridEngine::Core {

// M0：TimeSingleton（引擎命名）
class TimeSingleton {
public:
    double DeltaTime() const { return deltaTime_; }
    double UnscaledDeltaTime() const { return unscaledDeltaTime_; }
    void SetTimeScale(double s) { timeScale_ = s; }
    double TimeScale() const { return timeScale_; }
    double TimeSinceStart() const { return timeSinceStart_; }
    void SetPaused(bool p) { paused_ = p; }
    bool Paused() const { return paused_; }
    void Tick(double realDt);

private:
    double deltaTime_ = 0;
    double unscaledDeltaTime_ = 0;
    double timeScale_ = 1.0;
    double timeSinceStart_ = 0;
    bool paused_ = false;
};
} // namespace HybridEngine::Core
