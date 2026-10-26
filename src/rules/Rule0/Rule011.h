#ifndef MISRA_RULES_RULE_011_H
#define MISRA_RULES_RULE_011_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 0-1-1
/// "A project shall not contain unreachable code."
///
/// Detects statements that appear after return, break, continue,
/// throw, or goto within the same compound statement.
class Rule011 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule011();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;

    // MatchCallback
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_011_H
