#pragma once

#include "gmock/gmock.h"
#include "infrastructure/joystick/ISDLJoystickWrapper.h"

/**
 * @brief MockSDLJoystickWrapper — SDL3 原生 API 封装的 GMock 桩
 *
 * 用于 SDL3JoystickDriver 的单元测试，不依赖真实 SDL3 硬件。
 * 使用 StrictMock 确保所有 SDL API 调用都被预期。
 */
class MockSDLJoystickWrapper : public ISDLJoystickWrapper {
public:
    MOCK_METHOD(bool, init, (), (override));
    MOCK_METHOD(void, shutdown, (), (override));
    MOCK_METHOD(bool, openGamepad, (SDL_JoystickID), (override));
    MOCK_METHOD(void, closeGamepad, (SDL_JoystickID), (override));
    MOCK_METHOD(int, gamepadCount, (), (const, override));
    MOCK_METHOD(float, getAxis, (SDL_JoystickID, SDL_GamepadAxis), (override));
    MOCK_METHOD(bool, getButton, (SDL_JoystickID, SDL_GamepadButton), (override));
    MOCK_METHOD(void, pumpEvents, (), (override));
    MOCK_METHOD(void, setAxisDeadzone, (SDL_JoystickID, float), (override));
    MOCK_METHOD(void, setConnectionCallback, (GamepadCallback), (override));
};