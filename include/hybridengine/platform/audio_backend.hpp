#pragma once

namespace HybridEngine::Platform {

// M1：音频后端（采样时钟驱动——v1 WinAudioBackend 语义；mci 端口）
class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;
    // 打开音频文件。wav=**引擎侧确有覆盖**（测试生成 PCM wav 并驱动）；mp3=**能播，但解码完全依赖
    //   Windows 自带的 mci `MPEGVideo` 设备（DirectShow）**——引擎不实现任何解码器。
    //   实测（2026-09-17）：真实 mp3 打开成功、`position` 正常推进；但注册表把 mp3 映射到 MPEGVideo，
    //   该驱动缺失/被裁剪的机器上 mp3 会直接打不开（`MCIERR_CANNOT_LOAD_DRIVER`），而 wav 仍可用。
    //   探测顺序：mpegvideo → waveaudio → 自动。
    //   ⚠ 仓库里**没有一个 .mp3 文件**，也**没有测试打开过 mp3**（只有扩展名→类型映射的断言）——
    //   "mp3 支持"是**借宿主系统的能力**，不是本引擎验证过的能力。
    virtual bool Open(const char* path) = 0;
    virtual void Play() = 0;
    virtual void Pause() = 0;
    virtual void Seek(double ms) = 0;
    virtual double PositionMs() = 0;
    virtual double TimeScale() const = 0;      // 后端不支持变速=1.0（v1 口径）
    virtual bool IsOpen() const = 0;
    virtual void Close() = 0;                  // 释放资源（析构自动）
    // t8：真增益（0..1——后端支持=应用；失败=返回 false——调用方回退记录语义）
    virtual bool SetVolume(double gain) { (void)gain; return false; }
    // t8：循环播放。
    // **契约（2026-09-17 修正）**：循环是**宿主驱动**的——开启后宿主必须**持续调用 Play()**
    //   （例如每帧），由后端在流播到尾部时回卷重播。
    // 为什么不是"设一次就自动循环"：WinMM/mci 的 waveaudio 驱动**不认 `repeat` 关键字**
    //   （实测 0x103 = MCIERR_UNRECOGNIZED_KEYWORD，整条命令被拒），所以设备级循环做不到。
    //   见 src/platform/winmm_audio.cpp 的 Play()/SetLoop() 注释与实测数据。
    // 返回 false = 后端完全不支持循环；true = 已接受（宿主仍需按上述契约持续 Play）。
    virtual bool SetLoop(bool on) { (void)on; return false; }
};

IAudioBackend* CreateWinMmAudiBackend();
// 兼容别名（设计文档签名拼写——WinMmAuid）
inline IAudioBackend* CreateWinMmAuidBackend() { return CreateWinMmAudiBackend(); }

} // namespace HybridEngine::Platform
