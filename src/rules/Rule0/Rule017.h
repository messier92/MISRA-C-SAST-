#ifndef MISRA_RULES_RULE_017_H
#define MISRA_RULES_RULE_017_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 0-1-7
/// "The value returned by a function having a non-void return type that is
///  not an overloaded operator shall always be used."
class Rule017 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule017();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;

    // MatchCallback
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_017_H

