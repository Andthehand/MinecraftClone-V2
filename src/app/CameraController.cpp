#include "app/CameraController.h"
#include "rendering/Camera.h"
#include "config/Settings.h"
#include <SDL3/SDL.h>

void UpdateCameraFromKeyboard(Camera& camera, float deltaTime) {
    const bool* keyboard = SDL_GetKeyboardState(nullptr);
    const float moveForward = (keyboard[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (keyboard[SDL_SCANCODE_S] ? 1.0f : 0.0f);
    const float moveRight = (keyboard[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (keyboard[SDL_SCANCODE_A] ? 1.0f : 0.0f);
    const float moveUp = (keyboard[SDL_SCANCODE_SPACE] ? 1.0f : 0.0f) - (keyboard[SDL_SCANCODE_LCTRL] ? 1.0f : 0.0f);
    const float yawInput = (keyboard[SDL_SCANCODE_RIGHT] ? 1.0f : 0.0f) - (keyboard[SDL_SCANCODE_LEFT] ? 1.0f : 0.0f);
    const float pitchInput = (keyboard[SDL_SCANCODE_UP] ? 1.0f : 0.0f) - (keyboard[SDL_SCANCODE_DOWN] ? 1.0f : 0.0f);

    constexpr float MOVE_SPEED_UNITS_PER_SECOND = Options::Camera::MOVE_SPEED_UNITS_PER_SECOND;
    camera.MoveLocal(moveForward * MOVE_SPEED_UNITS_PER_SECOND * deltaTime, moveRight * MOVE_SPEED_UNITS_PER_SECOND * deltaTime, moveUp * MOVE_SPEED_UNITS_PER_SECOND * deltaTime);
    camera.AddYawPitch(yawInput * Options::Camera::TURN_SPEED_RADIANS_PER_SECOND * deltaTime, pitchInput * Options::Camera::TURN_SPEED_RADIANS_PER_SECOND * deltaTime);
}
