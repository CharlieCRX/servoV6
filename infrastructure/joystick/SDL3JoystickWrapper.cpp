#include "ISDLJoystickWrapper.h"
#include <algorithm>
#include <map>
#include <memory>

// ============================================================================
// SDL3JoystickWrapper — SDL3 C API 的 C++ RAII 封装实现
// ============================================================================

class SDL3JoystickWrapper : public ISDLJoystickWrapper {
public:
    SDL3JoystickWrapper() = default;
    ~SDL3JoystickWrapper() override { shutdown(); }

    // ── 初始化/关闭 ──
    bool init() override
    {
        if (SDL_Init(SDL_INIT_GAMEPAD) != 0) {
            return false;
        }
        m_initialized = true;
        return true;
    }

    void shutdown() override
    {
        // 关闭所有已打开的手柄
        for (auto& [id, gamepad] : m_gamepads) {
            if (gamepad) {
                SDL_CloseGamepad(gamepad);
            }
        }
        m_gamepads.clear();

        if (m_initialized) {
            SDL_Quit();
            m_initialized = false;
        }
    }

    // ── 手柄管理 ──
    bool openGamepad(SDL_JoystickID instanceId) override
    {
        if (!m_initialized) return false;

        SDL_Gamepad* gamepad = SDL_OpenGamepad(instanceId);
        if (!gamepad) return false;

        m_gamepads[instanceId] = gamepad;
        return true;
    }

    void closeGamepad(SDL_JoystickID instanceId) override
    {
        auto it = m_gamepads.find(instanceId);
        if (it != m_gamepads.end()) {
            if (it->second) {
                SDL_CloseGamepad(it->second);
            }
            m_gamepads.erase(it);
        }
    }

    int gamepadCount() const override
    {
        // 使用 SDL3 官方 API 获取已连接的游戏手柄数量
        int count = 0;
        SDL_JoystickID* joysticks = SDL_GetGamepads(&count);
        if (joysticks) {
            SDL_free(joysticks);
        }
        return count;
    }

    // ── 轴值读取（归一化到 [-1.0, 1.0]）──
    float getAxis(SDL_JoystickID instanceId, SDL_GamepadAxis axis) override
    {
        auto it = m_gamepads.find(instanceId);
        if (it == m_gamepads.end() || !it->second) {
            return 0.0f;
        }

        // SDL_GetGamepadAxis 返回 [-32768, 32767] 范围的 Sint16 值
        Sint16 raw = SDL_GetGamepadAxis(it->second, axis);
        return static_cast<float>(raw) / 32767.0f;
    }

    // ── 按钮状态 ──
    bool getButton(SDL_JoystickID instanceId, SDL_GamepadButton button) override
    {
        auto it = m_gamepads.find(instanceId);
        if (it == m_gamepads.end() || !it->second) {
            return false;
        }
        return SDL_GetGamepadButton(it->second, button) != 0;
    }

    // ── 事件轮询 ──
    void pumpEvents() override
    {
        if (!m_initialized) return;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_GAMEPAD_ADDED:
                    if (m_connectionCallback) {
                        m_connectionCallback(event.gdevice.which, true);
                    }
                    break;

                case SDL_EVENT_GAMEPAD_REMOVED:
                    if (m_connectionCallback) {
                        m_connectionCallback(event.gdevice.which, false);
                    }
                    break;

                // 其他事件类型由 SDL 内部处理（轴值/按钮通过 poll 读取）
                default:
                    break;
            }
        }
    }

    // ── 死区设置 ──
    void setAxisDeadzone(SDL_JoystickID /*instanceId*/, float /*deadzone*/) override
    {
        // SDL3 暂未提供逐手柄逐轴的死区 API
        // 死区判定在 SDL3JoystickDriver::computeDirection() 中软件实现
    }

    // ── 连接/断开回调 ──
    void setConnectionCallback(GamepadCallback cb) override
    {
        m_connectionCallback = std::move(cb);
    }

private:
    bool m_initialized = false;
    std::map<SDL_JoystickID, SDL_Gamepad*> m_gamepads;
    GamepadCallback m_connectionCallback;
};

// ── 工厂函数 ──
std::unique_ptr<ISDLJoystickWrapper> CreateSDL3JoystickWrapper()
{
    return std::make_unique<SDL3JoystickWrapper>();
}