#ifndef MISRA_RULES_RULE_645_H
#define MISRA_RULES_RULE_645_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 6-4-5
/// "An unconditional throw or break statement shall terminate every
///  non-empty switch-clause."
class Rule645 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule645();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_645_H

