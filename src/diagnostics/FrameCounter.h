#pragma once

#include <cstdint>
#include <optional>

class FrameCounter {
public:
    void Reset();
    // Accumulates frame time in seconds and returns a new FPS sample every half second.
    [[nodiscard]]
    std::optional<double> CountFrame(float deltaTime);

private:
    double m_ElapsedTime = 0.0;
    uint64_t m_FrameCount = 0;
};
