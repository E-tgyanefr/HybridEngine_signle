using System;

namespace HybridEngine.Engine;

// 场景接口（设计 §2.2）：Build 构建（引擎 Root 下建对象）、Update 每帧（未接入引擎循环——P1 周期钩子，MS 与引擎循环解耦）
public interface IScene
{
    void Build(GameEngine engine);
    void Update(double dt);
}

public sealed class Scene : IScene
{
    public void Build(GameEngine engine) { }
    public void Update(double dt) { }
}
