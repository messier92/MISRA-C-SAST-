#ifndef MISRA_RULES_RULE_665_H
#define MISRA_RULES_RULE_665_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 6-6-5
/// "A function shall have a single point of exit at the end of the function."
class Rule665 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule665();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_665_H

