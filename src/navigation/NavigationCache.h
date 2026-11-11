#pragma once

#include "navigation/RegionGraph.h"

namespace Navigation {

// The active search keeps these pins even if persistent cache entries are evicted.
struct QueryCache {
    detail::PositionMap<std::shared_ptr<detail::NavigationRegion>> pinnedRegions;
    detail::PositionMap<std::shared_ptr<detail::ChunkSnapshot>> pinnedSnapshots;

    std::shared_ptr<detail::NavigationRegion> KeepRegionForQuery(
        glm::ivec3 coordinates,
        std::shared_ptr<detail::NavigationRegion> region,
        PathResult& result) {
        pinnedRegions.emplace(coordinates, region);
        result.stats.clustersTouched = pinnedRegions.size();

        return region;
    }

    std::shared_ptr<detail::ChunkSnapshot> KeepSnapshotForQuery(
        glm::ivec3 coordinates,
        std::shared_ptr<detail::ChunkSnapshot> snapshot,
        PathResult& result) {
        pinnedSnapshots.emplace(coordinates, snapshot);
        result.stats.chunkSnapshotsTouched = pinnedSnapshots.size();

        return snapshot;
    }
};

// Persistent graphs, source trees and terrain snapshots for one world/profile.
class NavigationCache {
public:
    using Region = detail::NavigationRegion;
    using SearchTree = detail::RegionSearchTree;
    using Snapshot = detail::ChunkSnapshot;
    using Budget = detail::SearchBudget;

    NavigationCache(const World& source, const MovementRules& movement)
        : m_World(source), m_RegionGraph(movement) {}

    NavigationCache(const NavigationCache&) = delete;
    NavigationCache& operator=(const NavigationCache&) = delete;

    std::shared_ptr<Region> GetOrCreateRegion(
        glm::ivec3 coordinates,
        QueryCache& query,
        Budget& budget
    );

    bool EnsureEntrances(
        Region& region,
        QueryCache& query,
        Budget& budget
    );

    std::shared_ptr<SearchTree> GetOrCreateSearchTree(
        Region& region,
        int sourceNodeIndex,
        Budget& budget
    );

    void Clear();

    CacheStats GetCacheStats();

private:
    using RegionCache = detail::PositionMap<std::shared_ptr<Region>>;

    std::shared_ptr<Snapshot> GetOrCreateChunkSnapshot(
        glm::ivec3 coordinates,
        QueryCache& query,
        Budget& budget
    );

    std::array<uint64_t, 27> ReadNeighborRevisions(glm::ivec3 coordinates) const;
    static size_t EstimateRegionMemory(const Region& region);
    RegionCache::iterator EraseCachedRegion(RegionCache::iterator entry);
    void EvictRegionsOverBudget();
    void UpdateRegionMemory(Region& region);

private:
    const World& m_World;

    RegionGraph m_RegionGraph;
    RegionCache m_RegionCache;
    detail::PositionMap<std::shared_ptr<Snapshot>> m_ChunkSnapshotCache;

    uint64_t m_CacheUseCounter = 0;
    size_t m_CachedRegionBytes = 0;
};

} // namespace Navigation
