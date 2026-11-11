#include "navigation/NavigationCache.h"
#include "diagnostics/Profiling.h"
#include <algorithm>

namespace Navigation {
using namespace detail;

size_t NavigationCache::EstimateRegionMemory(const NavigationRegion& region) {
    // Include vector capacities and an allowance for hash buckets/nodes.
    size_t bytes = sizeof(NavigationRegion) + 64 + region.nodes.capacity() * sizeof(WalkableNode) +
                   region.entrances.capacity() * sizeof(RegionEntrance) +
                   region.searchTrees.bucket_count() * sizeof(void*);
    for (const auto& [source, tree] : region.searchTrees) {
        (void)source;
        bytes += 64 + sizeof(RegionSearchTree) +
                 (tree->costFromSource.capacity() + tree->parentNodeIndex.capacity()) * sizeof(int);
    }
    return bytes;
}

NavigationCache::RegionCache::iterator
NavigationCache::EraseCachedRegion(PositionMap<std::shared_ptr<NavigationRegion>>::iterator entry) {
    m_CachedRegionBytes -= entry->second->retainedBytes;
    return m_RegionCache.erase(entry);
}

void NavigationCache::EvictRegionsOverBudget() {
    while (m_CachedRegionBytes > Options::Navigation::MAX_CACHED_REGION_BYTES && !m_RegionCache.empty()) {
        const auto oldest =
            std::min_element(m_RegionCache.begin(), m_RegionCache.end(), [](const auto& a, const auto& b) {
                return a.second->lastUsed < b.second->lastUsed;
            });
        EraseCachedRegion(oldest);
    }
}

void NavigationCache::UpdateRegionMemory(NavigationRegion& region) {
    const auto entry = m_RegionCache.find(region.coordinates);
    if (entry == m_RegionCache.end() || entry->second.get() != &region) {
        return;
    }
    m_CachedRegionBytes -= region.retainedBytes;
    region.retainedBytes = EstimateRegionMemory(region);
    m_CachedRegionBytes += region.retainedBytes;
    EvictRegionsOverBudget();
}

std::array<uint64_t, 27> NavigationCache::ReadNeighborRevisions(BlockPosition cluster) const {
    constexpr int clustersPerChunk = Options::World::CHUNK_SIZE / ClusterSide;
    const BlockPosition base{FloorDivide(cluster.x, clustersPerChunk), FloorDivide(cluster.y, clustersPerChunk),
                             FloorDivide(cluster.z, clustersPerChunk)};
    std::array<uint64_t, 27> result{};
    size_t i = 0;
    for (int z = -1; z <= 1; ++z) {
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                result[i++] = m_World.GetNavigationRevision(base + BlockPosition(x, y, z));
            }
        }
    }
    return result;
}

CacheStats NavigationCache::GetCacheStats() {
    CacheStats stats;
    const auto revision = m_World.GetNavigationRevision();
    for (auto entry = m_RegionCache.begin(); entry != m_RegionCache.end();) {
        auto& region = *entry->second;
        if (region.lastValidatedWorldRevision != revision) {
            if (region.neighborRevisions != ReadNeighborRevisions(region.coordinates)) {
                entry = EraseCachedRegion(entry);
                continue;
            }
            region.lastValidatedWorldRevision = revision;
        }
        ++stats.clusters;
        stats.trees += region.searchTrees.size();
        ++entry;
    }
    stats.regionBytes = m_CachedRegionBytes;
    stats.regionByteLimit = Options::Navigation::MAX_CACHED_REGION_BYTES;
    return stats;
}

std::shared_ptr<ChunkSnapshot> NavigationCache::GetOrCreateChunkSnapshot(BlockPosition coordinates,
                                                                         QueryCache& query,
                                                                         SearchBudget& budget) {
    if (const auto pinned = query.pinnedSnapshots.find(coordinates); pinned != query.pinnedSnapshots.end()) {
        return pinned->second;
    }
    const auto chunk = m_World.GetChunks().find(coordinates);
    if (chunk == m_World.GetChunks().end()) {
        m_ChunkSnapshotCache.erase(coordinates);
        return {};
    }
    const auto found = m_ChunkSnapshotCache.find(coordinates);
    if (found != m_ChunkSnapshotCache.end() && found->second->revision == chunk->second.navigationRevision) {
        found->second->lastUsed = ++m_CacheUseCounter;
        return query.KeepSnapshotForQuery(coordinates, found->second, budget.result);
    }
    MC_PROFILE_ZONE("Snapshot navigation chunk");
    auto snapshot = std::make_shared<ChunkSnapshot>();
    snapshot->revision = chunk->second.navigationRevision;
    snapshot->lastUsed = ++m_CacheUseCounter;
    constexpr size_t side = Options::World::CHUNK_SIZE;
    snapshot->voxels.resize(side * side * side);
    chunk->second.octree.CopyVoxels(snapshot->voxels);
    if (m_ChunkSnapshotCache.size() >= Options::Navigation::MAX_CACHED_CHUNK_SNAPSHOTS &&
        !m_ChunkSnapshotCache.contains(coordinates)) {
        const auto oldest = std::min_element(
            m_ChunkSnapshotCache.begin(), m_ChunkSnapshotCache.end(),
            [](const auto& a, const auto& b) { return a.second->lastUsed < b.second->lastUsed; });
        m_ChunkSnapshotCache.erase(oldest);
    }
    m_ChunkSnapshotCache.insert_or_assign(coordinates, snapshot);
    ++budget.result.stats.chunkSnapshotsBuilt;
    return query.KeepSnapshotForQuery(coordinates, snapshot, budget.result);
}

std::shared_ptr<NavigationRegion>
NavigationCache::GetOrCreateRegion(BlockPosition coordinates, QueryCache& query, SearchBudget& budget) {
    auto& result = budget.result;
    if (budget.HasTimeLimitExpired()) {
        return {};
    }
    if (const auto pinned = query.pinnedRegions.find(coordinates); pinned != query.pinnedRegions.end()) {
        pinned->second->lastUsed = ++m_CacheUseCounter;
        return pinned->second;
    }
    auto found = m_RegionCache.find(coordinates);
    const auto revision = m_World.GetNavigationRevision();
    std::array<uint64_t, 27> neighborRevisions{};
    if (found != m_RegionCache.end()) {
        auto& region = *found->second;
        region.lastUsed = ++m_CacheUseCounter;
        if (region.lastValidatedWorldRevision == revision) {
            return query.KeepRegionForQuery(coordinates, found->second, budget.result);
        }
        MC_PROFILE_ZONE("Validate navigation cache");
        neighborRevisions = ReadNeighborRevisions(coordinates);
        if (region.neighborRevisions == neighborRevisions) {
            region.lastValidatedWorldRevision = revision;
            return query.KeepRegionForQuery(coordinates, found->second, budget.result);
        }
        EraseCachedRegion(found);
    } else {
        neighborRevisions = ReadNeighborRevisions(coordinates);
    }
    if (!budget.CanStartPreprocessing()) {
        return {};
    }
    MC_PROFILE_ZONE("Build navigation movement graph");
    auto region = std::make_shared<NavigationRegion>(coordinates);
    region->neighborRevisions = neighborRevisions;
    region->lastValidatedWorldRevision = revision;
    region->lastUsed = ++m_CacheUseCounter;
    const auto findSnapshot = [&](BlockPosition chunk) {
        return GetOrCreateChunkSnapshot(chunk, query, budget);
    };
    if (!m_RegionGraph.Build(*region, budget, findSnapshot)) {
        return {};
    }
    m_RegionCache.emplace(coordinates, region);
    UpdateRegionMemory(*region);
    ++result.stats.clustersBuilt;
    return query.KeepRegionForQuery(coordinates, region, budget.result);
}

bool NavigationCache::EnsureEntrances(NavigationRegion& region, QueryCache& query, SearchBudget& budget) {
    if (region.entrancesBuilt) {
        return true;
    }
    const auto findRegion = [&](BlockPosition coordinates) {
        return GetOrCreateRegion(coordinates, query, budget);
    };
    if (!m_RegionGraph.BuildEntrances(region, budget, findRegion)) {
        return false;
    }
    UpdateRegionMemory(region);
    return true;
}

std::shared_ptr<RegionSearchTree>
NavigationCache::GetOrCreateSearchTree(NavigationRegion& region, int sourceNodeIndex, SearchBudget& budget) {
    auto& result = budget.result;
    if (const auto entry = region.searchTrees.find(sourceNodeIndex); entry != region.searchTrees.end()) {
        entry->second->lastUsed = ++m_CacheUseCounter;
        ++result.stats.treeCacheHits;
        return entry->second;
    }
    if (!budget.CanStartPreprocessing()) {
        return {};
    }
    auto tree = m_RegionGraph.BuildSearchTree(region, sourceNodeIndex, budget);
    if (!tree) {
        return {};
    }
    tree->lastUsed = ++m_CacheUseCounter;
    if (region.searchTrees.size() >= Options::Navigation::MAX_CACHED_SEARCH_TREES_PER_REGION) {
        const auto oldest = std::min_element(
            region.searchTrees.begin(), region.searchTrees.end(),
            [](const auto& a, const auto& b) { return a.second->lastUsed < b.second->lastUsed; });
        region.searchTrees.erase(oldest);
    }
    region.searchTrees.emplace(sourceNodeIndex, tree);
    UpdateRegionMemory(region);
    ++result.stats.treesBuilt;
    return tree;
}

void NavigationCache::Clear() {
    m_RegionCache.clear();
    m_CachedRegionBytes = 0;
    m_ChunkSnapshotCache.clear();
}

} // namespace Navigation
