#include "navigation/MovementRules.h"
#include "world/Block.h"

namespace Navigation {
using namespace detail;

MovementRules::MovementRules(Agent profile) noexcept : m_Agent(profile) {
    m_IsValid = profile.height >= 1 && profile.height <= 8 && profile.maxStepUp >= 0 && profile.maxStepUp <= 1 &&
                profile.maxDrop >= 0 && profile.maxDrop <= 16;
}

bool MovementRules::IsAir(const World& world, BlockPosition position) {
    const auto block = world.GetBlock(position);

    return block && *block == Blocks::Air;
}

bool MovementRules::CanStand(const World& world, BlockPosition position) const {
    return CanStandUsing(
        position, [&](BlockPosition samplePosition) { return IsAir(world, samplePosition); },
        [&](BlockPosition samplePosition) {
            const auto block = world.GetBlock(samplePosition);

            return block && *block != Blocks::Air;
        });
}

bool MovementRules::CanMove(const World& world, BlockPosition from, BlockPosition destination) const {
    if (!m_IsValid) {
        return false;
    }

    const auto difference = destination - from;

    return std::abs(difference.x) + std::abs(difference.z) == 1 && difference.y <= m_Agent.maxStepUp &&
           difference.y >= -m_Agent.maxDrop && CanStand(world, from) && CanStand(world, destination) &&
           HasMovementClearance(from, destination, [&](BlockPosition position) { return IsAir(world, position); });
}

} // namespace Navigation
