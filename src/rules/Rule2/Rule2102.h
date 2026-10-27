#ifndef MISRA_RULES_RULE_2102_H
#define MISRA_RULES_RULE_2102_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 2-10-2
/// "Identifiers declared in an inner scope shall not hide an
///  identifier declared in an outer scope."
class Rule2102 : public Rule,
                 public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule2102();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_2102_H

