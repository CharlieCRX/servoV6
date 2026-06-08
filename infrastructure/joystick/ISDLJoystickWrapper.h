#pragma once
#include <SDL3/SDL.h>
#include <functional>
#include <memory>
#include <string>

/**
 * @brief SDL3 原生 API 的 C++ RAII 封装
 *
 * 职责：
 *   - SDL_Init(SDL_INIT_GAMEPAD) / SDL_Quit() 生命周期管理
 *   - 手柄打开/关闭的 RAII 封装
 *   - SDL 事件泵（内部循环或由外部驱动）
 */
class ISDLJoystickWrapper {
public:
    virtual ~ISDLJoystickWrapper() = default;

    // ── 初始化/关闭 ──
    virtual bool init()     = 0;
    virtual void shutdown() = 0;

    // ── 手柄管理 ──
    virtual bool openGamepad(SDL_JoystickID instanceId)    = 0;
    virtual void closeGamepad(SDL_JoystickID instanceId)   = 0;
    virtual int  gamepadCount() const                      = 0;

    // ── 轴值读取（归一化到 [-1.0, 1.0]）──
    virtual float getAxis(SDL_JoystickID instanceId, SDL_GamepadAxis axis) = 0;

    // ── 按钮状态 ──
    virtual bool  getButton(SDL_JoystickID instanceId, SDL_GamepadButton button) = 0;

    // ── 事件轮询（驱动 SDL 内部状态机）──
    virtual void  pumpEvents() = 0;

    // ── 死区设置 ──
    virtual void  setAxisDeadzone(SDL_JoystickID instanceId, float deadzone) = 0;

    // ── 连接/断开回调 ──
    using GamepadCallback = std::function<void(SDL_JoystickID, bool connected)>;
    virtual void setConnectionCallback(GamepadCallback cb) = 0;
};

/// @brief 工厂函数：创建 SDL3 原生 API 封装实例
std::unique_ptr<ISDLJoystickWrapper> CreateSDL3JoystickWrapper();
