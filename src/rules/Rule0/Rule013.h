#ifndef MISRA_RULES_RULE_013_H
#define MISRA_RULES_RULE_013_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 0-1-3
/// "A project shall not contain unused variables."
class Rule013 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule013();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;

    // MatchCallback
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_013_H

