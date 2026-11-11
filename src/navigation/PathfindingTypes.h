#pragma once

#include "config/Settings.h"
#include "world/World.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace Navigation {
    struct Agent {
        // The upright body occupies one horizontal block and 1-8 vertical blocks.
        int height = 2;
        int maxStepUp = 1; // 0 or 1; movement must execute the selected step/jump.
        int maxDrop = 3;   // 0-16; a valid drop does not imply a valid reverse climb.
    };

    enum class PathStatus {
        Found,
        NoPath,
        InvalidStart,
        InvalidGoal,
        BudgetExceeded,
        InvalidAgent,
        InvalidTimeBudget,
        InvalidState
    };
    enum class BudgetLimit { None, Nodes, Time };

    // Work performed by one query, rather than totals retained in the cache.
    struct PathSearchStats {
        size_t expandedNodes = 0; // Also counts region and search-tree construction.
        size_t abstractExpandedNodes = 0;
        size_t clustersBuilt = 0;
        size_t treesBuilt = 0;
        size_t treeCacheHits = 0;
        bool usedHierarchy = false;
        size_t clustersTouched = 0; // Unique graphs used, including neighbor graphs.
        size_t chunkSnapshotsBuilt = 0;
        size_t chunkSnapshotsTouched = 0;
    };

    struct PathResult {
        PathStatus status = PathStatus::NoPath;

        // Integer feet positions, including both endpoints. Center X/Z for movement.
        std::vector<glm::ivec3> cells;
        uint64_t worldRevision = 0;
        BudgetLimit budgetLimit = BudgetLimit::None;

        PathSearchStats stats{};
    };

    struct CacheStats {
        size_t clusters = 0;    // Valid retained region graphs, including empty ones.
        size_t trees = 0;       // Valid retained search trees.
        size_t regionBytes = 0; // Estimated retained graph/tree allocations.
        size_t regionByteLimit = 0;
    };

    namespace detail {
        using BlockPosition = glm::ivec3;
        using SearchClock = std::chrono::steady_clock;
        template <class T> using PositionMap = std::unordered_map<BlockPosition, T, ChunkCoordHash>;

        inline constexpr int Infinity = std::numeric_limits<int>::max();

        // Shared by all stages so preprocessing and search charge the same query.
        struct SearchBudget {
            PathResult& result;
            size_t nodeLimit;
            SearchClock::time_point deadline;
            uint32_t timeChecks = 0;

            bool HasTimeLimitExpired(bool force = false) {
                if (deadline == SearchClock::time_point{}) {
                    return false;
                }
                // Checking periodically keeps clock reads out of most inner iterations.
                if (!force && (++timeChecks & (Options::Navigation::TIME_LIMIT_CHECK_INTERVAL_CALLS - 1)) != 0) {
                    return false;
                }
                if (SearchClock::now() < deadline) {
                    return false;
                }
                result.budgetLimit = BudgetLimit::Time;

                return true;
            }

            bool CanStartPreprocessing();

            bool TrySpendNode() {
                if (result.stats.expandedNodes >= nodeLimit) {
                    result.budgetLimit = BudgetLimit::Nodes;
                    return false;
                }
                result.stats.expandedNodes++;

                return true;
            }
        };
    } // namespace detail
} // namespace Navigation
