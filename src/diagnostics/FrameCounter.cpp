#include "diagnostics/FrameCounter.h"

void FrameCounter::Reset() {
    m_ElapsedTime = 0.0;
    m_FrameCount = 0;
}

std::optional<double> FrameCounter::CountFrame(float deltaTime) {
    ++m_FrameCount;
    m_ElapsedTime += deltaTime;

    if (m_ElapsedTime < 0.5) {
        return std::nullopt;
    }

    const double fps = static_cast<double>(m_FrameCount) / m_ElapsedTime;
    Reset();

    return fps;
}
