#include "FreeListAllocator.h"

#include <print>
#include <iterator>

FreeListAllocator::FreeListAllocator(uint32_t capacity, uint32_t alignment)
    : m_Alignment(alignment) {
    m_FreeRanges.emplace(0, capacity);
}

std::optional<FreeListAllocator::Allocation> FreeListAllocator::Allocate(uint32_t count) {
    if (count == 0) {
        return std::nullopt;
    }

    const uint64_t rounded = ((uint64_t{count} + m_Alignment - 1) / m_Alignment) * m_Alignment;
    if (rounded > UINT32_MAX) {
        return std::nullopt;
    }
    count = static_cast<uint32_t>(rounded);

	// First fit; free ranges are sorted by offset, so the lowest offset is returned.
    for (auto it = m_FreeRanges.begin(); it != m_FreeRanges.end(); ++it) {
        if (it->second < count) {
            continue;
        }

		// Found a free range that has a large enough count.
        const Allocation result{it->first, count};
        const uint32_t remaining = it->second - count;
        m_Allocations.emplace(result.offset, count);
        m_FreeRanges.erase(it);
        if (remaining != 0) {
            m_FreeRanges.emplace(result.offset + count, remaining);
        }

        return result;
    }

    return std::nullopt; // Exhausted or fragmented; no live allocation is moved.
}

// Reject invalid/double frees rather than corrupting the free list.
bool FreeListAllocator::Free(Allocation allocation) {
    const auto live = m_Allocations.find(allocation.offset);
    if (live == m_Allocations.end() || live->second != allocation.count) {
        std::println("Double Free in FreeListAllocator!");

        return false;
    }

	// Join adjacent free ranges in both directions.
    auto next = m_FreeRanges.lower_bound(allocation.offset);
    uint32_t offset = allocation.offset;
    uint32_t count = allocation.count;
    if (next != m_FreeRanges.begin()) {
        const auto previous = std::prev(next);
        if (previous->first + previous->second == offset) {
            offset = previous->first;
            count += previous->second;
            m_FreeRanges.erase(previous);
        }
    }
    if (next != m_FreeRanges.end() && offset + count == next->first) {
        count += next->second;
        m_FreeRanges.erase(next);
    }

    m_FreeRanges.emplace(offset, count);
    m_Allocations.erase(live);
    return true;
}
