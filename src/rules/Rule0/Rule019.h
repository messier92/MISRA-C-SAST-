#ifndef MISRA_RULES_RULE_019_H
#define MISRA_RULES_RULE_019_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 0-1-9
/// "There shall be no dead code."
class Rule019 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule019();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;

    // MatchCallback
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_019_H

