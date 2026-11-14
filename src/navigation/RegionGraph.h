#pragma once
#include "config/Settings.h"
#include "navigation/MovementRules.h"
#include <functional>
#include <memory>

namespace Navigation::detail {
inline constexpr int ClusterSide = Options::Navigation::CLUSTER_SIZE_BLOCKS;
inline constexpr int ClusterVolume = ClusterSide * ClusterSide * ClusterSide;
inline constexpr int EntrancePatchSide = Options::Navigation::ENTRANCE_PATCH_SIZE_BLOCKS;

inline BlockPosition ClusterForPosition(BlockPosition position) {
    return {FloorDivide(position.x, ClusterSide), FloorDivide(position.y, ClusterSide),
            FloorDivide(position.z, ClusterSide)};
}

inline int LocalCellIndex(BlockPosition position, BlockPosition cluster) {
    const auto local = position - cluster * ClusterSide;

    return (local.z * ClusterSide + local.y) * ClusterSide + local.x;
}

inline BlockPosition PositionFromCellIndex(int index, BlockPosition cluster) {
    return cluster * ClusterSide +
           BlockPosition(index % ClusterSide, (index / ClusterSide) % ClusterSide, index / (ClusterSide * ClusterSide));
}

struct MovementEdge {
    BlockPosition target;
    int targetNodeIndex = -1;
    int cost;
};

struct WalkableNode {
    int cellIndex;
    int componentIndex = -1;
    std::array<MovementEdge, 4> edges{};
    int edgeCount = 0;
};

struct RegionEntrance {
    int sourceNodeIndex;
    BlockPosition target;
    int cost;
};

struct RegionSearchTree {
    // Compact arrays indexed by walkable nodes, not the entire voxel volume.
    std::vector<int> costFromSource;
    std::vector<int> parentNodeIndex;
    uint64_t lastUsed = 0;

    explicit RegionSearchTree(size_t count) : costFromSource(count, Infinity), parentNodeIndex(count, -1) {}
};

struct NavigationRegion {
    BlockPosition coordinates;

    // Walkable positions and their cell-to-node lookup.
    std::array<int, ClusterVolume> nodeIndexAtCell;
    std::vector<WalkableNode> nodes;

    // Connections to neighboring regions and cached paths within this region.
    std::vector<RegionEntrance> entrances;
    bool entrancesBuilt = false;
    std::unordered_map<int, std::shared_ptr<RegionSearchTree>> searchTrees;

    // Revisions used to detect changes in the surrounding world.
    std::array<uint64_t, 27> neighborRevisions{};
    uint64_t lastValidatedWorldRevision = 0;

    // Cache usage and memory accounting.
    uint64_t lastUsed = 0;
    size_t retainedBytes = 0;

    explicit NavigationRegion(BlockPosition coordinates) : coordinates(coordinates) { nodeIndexAtCell.fill(-1); }
};

struct ChunkSnapshot {
    uint64_t revision = 0;
    uint64_t lastUsed = 0;
    std::vector<uint32_t> voxels;
};
} // namespace Navigation::detail

namespace Navigation {
// Builds topology and directed entrances; NavigationCache controls their lifetime.
class RegionGraph {
public:
    using Region = detail::NavigationRegion;
    using SearchTree = detail::RegionSearchTree;
    using Budget = detail::SearchBudget;
    using SnapshotLookup = std::function<std::shared_ptr<detail::ChunkSnapshot>(glm::ivec3)>;
    using RegionLookup = std::function<std::shared_ptr<Region>(glm::ivec3)>;

    explicit RegionGraph(const MovementRules& rules) : m_Movement(rules) {}

    bool Build(Region& region, Budget& budget, const SnapshotLookup& findSnapshot) const;
    bool BuildEntrances(Region& region, Budget& budget, const RegionLookup& findRegion) const;
    std::shared_ptr<SearchTree> BuildSearchTree(const Region& region, int sourceNodeIndex, Budget& budget) const;

private:
    struct VoxelSamples;
    bool SampleRegionVoxels(VoxelSamples& samples, Budget& budget, const SnapshotLookup& findSnapshot) const;
    bool CollectWalkableNodes(Region& region, const VoxelSamples& samples, Budget& budget) const;
    bool ConnectWalkableNodes(Region& region, const VoxelSamples& samples, Budget& budget) const;

private:
    const MovementRules& m_Movement;
};
} // namespace Navigation
