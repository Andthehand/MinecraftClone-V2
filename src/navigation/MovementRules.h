#pragma once
#include "navigation/PathfindingTypes.h"
#include <array>
#include <cstdlib>
#include <glm/common.hpp>

namespace Navigation::detail {
    inline constexpr std::array<BlockPosition, 4> Directions{{{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}}};

    inline int FloorDivide(int value, int divisor) {
        return value / divisor - (value % divisor < 0 ? 1 : 0);
    }

    inline int ManhattanDistance(BlockPosition a, BlockPosition b) {
        const auto difference = glm::abs(a - b);
        return difference.x + difference.y + difference.z;
    }
} // namespace Navigation::detail

namespace Navigation {
// One movement profile shared by direct voxel queries and sampled region graphs.
class MovementRules {
public:
    explicit MovementRules(Agent profile) noexcept;

    bool IsValid() const noexcept {
        return m_IsValid;
    }

    const Agent& GetAgent() const {
        return m_Agent;
    }

    bool CanStand(const World& world, glm::ivec3 position) const;
    bool CanMove(const World& world, glm::ivec3 from, glm::ivec3 destination) const;

    // Templated readers preserve fast array access for sampled/cached voxels.
    template <class IsAir, class IsSolid>
    bool CanStandUsing(glm::ivec3 position, IsAir&& air, IsSolid&& solid) const {
        if (!m_IsValid) {
            return false;
        }

        if (!solid(position - glm::ivec3(0, 1, 0))) {
            return false;
        }

        for (int offset = 0; offset < m_Agent.height; ++offset) {
            if (!air(position + glm::ivec3(0, offset, 0))) {
                return false;
            }
        }

        return true;
    }

    template <class IsAir>
    bool HasMovementClearance(glm::ivec3 from, glm::ivec3 destination, IsAir&& air) const {
        if (!m_IsValid) {
            return false;
        }

        if (destination.y > from.y) {
            for (int offset = m_Agent.height; offset < m_Agent.height + destination.y - from.y; ++offset) {
                if (!air(from + glm::ivec3(0, offset, 0))) {
                    return false;
                }
            }
        } else {
            for (int y = destination.y; y < from.y + m_Agent.height; ++y) {
                if (!air({destination.x, y, destination.z})) {
                    return false;
                }
            }
        }

        return true;
    }

private:
    static bool IsAir(const World& world, glm::ivec3 position);

private:
    Agent m_Agent;
    bool m_IsValid = false;
};
} // namespace Navigation
