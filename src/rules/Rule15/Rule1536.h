#ifndef MISRA_RULE_15_3_6_H
#define MISRA_RULE_15_3_6_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 15-3-6
/// Where multiple handlers are provided in a single try-catch statement or
/// function-try-block for a derived class and some or all of its bases, the
/// handlers shall be ordered most-derived to base class.
class Rule1536 : public Rule,
                 public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule1536();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULE_15_3_6_H

