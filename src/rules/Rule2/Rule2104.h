#ifndef MISRA_RULES_RULE_2104_H
#define MISRA_RULES_RULE_2104_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 2-10-4
/// "A class, union or enum name (including qualification, if any) shall be a unique identifier."
class Rule2104 : public Rule,
                 public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule2104();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_2104_H

