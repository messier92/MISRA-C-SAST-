#ifndef MISRA_RULES_RULE_531_H
#define MISRA_RULES_RULE_531_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 5-3-1
/// "Each operand of the ! operator, the logical && or the logical ||
///  operators shall have type bool."
class Rule531 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule531();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_531_H

