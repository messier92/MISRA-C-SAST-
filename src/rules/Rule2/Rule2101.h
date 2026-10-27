#ifndef MISRA_RULES_RULE_2101_H
#define MISRA_RULES_RULE_2101_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 2-10-1
/// "Different identifiers shall be typographically unambiguous."
class Rule2101 : public Rule,
                 public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule2101();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_2101_H

