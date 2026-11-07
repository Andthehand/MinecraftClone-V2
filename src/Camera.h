#pragma once

#include <array>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

class Camera {
public:
	Camera();

	void SetPosition(glm::vec3 position);
	void MoveLocal(float forward, float right, float up);
	void MoveWorld(glm::vec3 delta);
	void AddYawPitch(float yawDeltaRadians, float pitchDeltaRadians);
	void SetAspectRatio(float aspectRatio);

	[[nodiscard]] glm::vec3 GetPosition() const;
	[[nodiscard]] glm::vec3 GetForward() const;
	[[nodiscard]] glm::vec3 GetRight() const;
	[[nodiscard]] glm::vec3 GetUp() const;
	[[nodiscard]] float GetFovYRadians() const;
	[[nodiscard]] glm::mat4 GetViewMatrix() const;
	[[nodiscard]] glm::mat4 GetProjectionMatrix() const;
	[[nodiscard]] const glm::mat4& GetViewProjectionMatrix() const;

	[[nodiscard]] std::array<glm::vec4, 6> GetFrustumPlanes() const;
	[[nodiscard]] static bool IsBoxVisible(const std::array<glm::vec4, 6>& planes,
		const glm::vec3& minimum, const glm::vec3& maximum);

private:
	glm::vec3 m_Position;
	float m_YawRadians;
	float m_PitchRadians;
	float m_FovYRadians;
	float m_AspectRatio;

	mutable glm::mat4 m_ViewProjection{1.0f};
	mutable bool m_ViewProjectionDirty = true;
};
