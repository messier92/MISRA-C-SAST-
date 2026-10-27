#ifndef MISRA_RULES_RULE_2106_H
#define MISRA_RULES_RULE_2106_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 2-10-6
/// "If an identifier refers to a type, it shall not also refer to an
///  object or a function in the same scope."
class Rule2106 : public Rule,
                 public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule2106();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_2106_H

