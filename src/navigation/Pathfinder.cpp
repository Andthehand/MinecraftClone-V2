#include "navigation/Pathfinder.h"
#include "diagnostics/Profiling.h"

namespace Navigation {
using namespace detail;

Pathfinder::Pathfinder(const World& world, Agent agent)
    : m_World(world), m_Movement(agent), m_Cache(world, m_Movement),
      m_Hierarchy(world, m_Movement, m_Cache) {}

Pathfinder::~Pathfinder() = default;

bool Pathfinder::CanStand(BlockPosition feet) const {
    return m_Movement.CanStand(m_World, feet);
}

bool Pathfinder::CanMove(BlockPosition from, BlockPosition destination) const {
    return m_Movement.CanMove(m_World, from, destination);
}

PathResult Pathfinder::FindPath(BlockPosition start, BlockPosition goal, size_t nodeBudget,
                                std::chrono::milliseconds maxTime) {
    MC_PROFILE_ZONE("Navigation HPA*");
    if (maxTime.count() < 0) {
        return {
            .status = PathStatus::InvalidTimeBudget,
            .worldRevision = m_World.GetNavigationRevision(),
        };
    }
    const auto deadline = maxTime.count() == 0 ? SearchClock::time_point{} : SearchClock::now() + maxTime;

    return m_Hierarchy.FindPath(start, goal, nodeBudget, deadline);
}

void Pathfinder::ClearCache() {
    MC_PROFILE_ZONE("Clear navigation cache");
    m_Cache.Clear();
}

CacheStats Pathfinder::GetCacheStats() const {
    return m_Cache.GetCacheStats();
}

} // namespace Navigation
