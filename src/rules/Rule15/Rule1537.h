#ifndef MISRA_RULE_15_3_7_H
#define MISRA_RULE_15_3_7_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 15-3-7
/// Where multiple handlers are provided in a single try-catch statement or
/// function-try-block, any ellipsis (catch-all) handler shall occur last.
class Rule1537 : public Rule,
                 public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule1537();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULE_15_3_7_H

