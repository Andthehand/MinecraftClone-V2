#pragma once

#include "config/Settings.h"
#include "navigation/PathfindingTypes.h"
#include "navigation/MovementRules.h"
#include "navigation/NavigationCache.h"
#include "navigation/HierarchicalPathfinder.h"

namespace Navigation {
// Synchronous CPU navigation over loaded terrain. Use on the world's owning
// thread. One instance per world/agent profile; the world must outlive it.
class Pathfinder {
public:
    explicit Pathfinder(const World& world, Agent agent = {});
    // Unsupported agent profiles are reported as InvalidAgent by searches.
    ~Pathfinder();
    Pathfinder(const Pathfinder&) = delete;
    Pathfinder& operator=(const Pathfinder&) = delete;

    [[nodiscard]] bool CanStand(glm::ivec3 feet) const;
    [[nodiscard]] bool CanMove(glm::ivec3 from, glm::ivec3 to) const;
    // Sparse HPA*: entrance sampling trades exact optimality for speed.
    // maxTime=0 disables the cooperative deadline; cleanup may overshoot a limit.
    // A negative maxTime returns InvalidTimeBudget.
    [[nodiscard]] PathResult
    FindPath(glm::ivec3 start, glm::ivec3 goal,
             size_t maxExpandedNodes = Options::Navigation::DEFAULT_HIERARCHICAL_MAX_EXPANDED_NODES,
             std::chrono::milliseconds maxTime = std::chrono::milliseconds{
                 Options::Navigation::DEFAULT_HIERARCHICAL_TIME_LIMIT_MS});

    void ClearCache();
    [[nodiscard]] CacheStats GetCacheStats() const;

private:
    const World& m_World;

    // Pathfinding Variables
    MovementRules m_Movement;
    mutable NavigationCache m_Cache;
    HierarchicalPathfinder m_Hierarchy;
};
} // namespace Navigation
