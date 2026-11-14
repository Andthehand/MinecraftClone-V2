#include "navigation/PathfindingTypes.h"

namespace Navigation::detail {

bool SearchBudget::CanStartPreprocessing() {
    if (HasTimeLimitExpired(true)) {
        return false;
    }

    if (result.stats.expandedNodes < nodeLimit) {
        return true;
    }

    result.budgetLimit = BudgetLimit::Nodes;

    return false;
}

} // namespace Navigation::detail
