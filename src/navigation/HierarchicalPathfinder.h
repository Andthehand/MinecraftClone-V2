#pragma once
#include "navigation/MovementRules.h"

namespace Navigation {
class NavigationCache;

// Entrance-level A*. Borrows persistent data and owns its frontier/query pins.
class HierarchicalPathfinder {
public:
    HierarchicalPathfinder(const World& source, const MovementRules& rules, NavigationCache& sharedCache)
        : m_World(source), m_Movement(rules), m_Cache(sharedCache) {}

    PathResult FindPath(glm::ivec3 start, glm::ivec3 goal, size_t nodeBudget,
                        detail::SearchClock::time_point deadline = {});

private:
    const World& m_World;
    const MovementRules& m_Movement;

    NavigationCache& m_Cache;
};
} // namespace Navigation
