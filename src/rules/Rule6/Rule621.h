#ifndef MISRA_RULES_RULE_621_H
#define MISRA_RULES_RULE_621_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 6-2-1
/// "Assignment operators shall not be used in sub-expressions."
class Rule621 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule621();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_621_H

